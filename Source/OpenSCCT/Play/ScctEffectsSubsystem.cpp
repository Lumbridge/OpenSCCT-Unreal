#include "Play/ScctEffectsSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Parse.h"
#include "Hud/ScctVisionSubsystem.h"
#include "Misc/ScopeExit.h"
#include "OpenSCCT.h"
#include "Play/ScctEquipmentSubsystem.h"
#include "Play/ScctExposure.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	// The light on the bodies of the local player's side (UpdateBodyFill): its strength in a moderately lit room, in the
	// map lights' units, how far it reaches from the camera and how far above the camera it sits.
	// scct.BodyFill changes the strength while the game runs (0 switches the light off).
	constexpr float BodyFillIntensity = 2.4f;
	constexpr float BodyFillRadius = 700.0f;
	constexpr float BodyFillRaise = 25.0f;

	TAutoConsoleVariable<float> CVarBodyFill(TEXT("scct.BodyFill"), BodyFillIntensity,
		TEXT("Strength of the soft light from the camera on the bodies of the local player's own side (0: off)."), ECVF_Default);

	constexpr int32 MaxLights = 16;
	constexpr int32 MaxMeshes = 96;
	constexpr int32 MaxTracers = 48;
	constexpr int32 MaxImpactSlots = 64;

	// A tracer is drawn as the last stretch of its flight.
	constexpr float TracerLength = 260.0f;
	constexpr float TracerWidth = 3.5f;

	// The simulation's light brightness (0..255) in candelas. Level lights are imported at 0.45 per unit
	// (Tools/Import/import_map.py); an effect's light is a flash that has to read against them.
	constexpr float CandelasPerBrightness = 2.0f;

	// Half the width of a bullet mark, cm, by what was hit (the mark's own DrawScale is laid over it): the hole
	// is the middle third of that and the chipped ring around it the rest (the original's mark was about 12 cm
	// across; smaller than these it cannot be made out from two metres). A rifle round leaves a crater the
	// size of a coin in concrete, a clean hole with a bright lip in sheet metal, a wider star in glass.
	constexpr float DecalHalfSizeConcrete = 5.5f;
	constexpr float DecalHalfSizeMetal = 3.6f;
	constexpr float DecalHalfSizeWood = 4.4f;
	constexpr float DecalHalfSizeGlass = 6.5f;
	constexpr float DecalHalfSizeSoft = 4.6f;

	const TCHAR* const GlowMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxGlow.M_ScctFxGlow");
	const TCHAR* const SmokeMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxSmoke.M_ScctFxSmoke");
	const TCHAR* const DecalMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxDecal.M_ScctFxDecal");
	const TCHAR* const SolidMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxSolid.M_ScctFxSolid");
	const TCHAR* const ImportedMeshFolder = TEXT("/Game/Imported/Fx");

	/** A name reduced to its letters and digits: how imported assets are matched to the game's object paths. */
	FString NameKey(const FString& Name)
	{
		FString Key;
		for (const TCHAR Character : Name)
		{
			if (FChar::IsAlnum(Character))
			{
				Key.AppendChar(FChar::ToLower(Character));
			}
		}
		return Key;
	}

	/** The simulation's light colour bytes: hue around the wheel, and saturation where 255 is white. */
	FLinearColor LightColor(float Hue, float Saturation)
	{
		return FLinearColor(Hue / 255.0f * 360.0f, 1.0f - FMath::Clamp(Saturation / 255.0f, 0.0f, 1.0f), 1.0f).HSVToLinearRGB();
	}
}

bool UScctEffectsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctEffectsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	if (Session == nullptr)
	{
		return;
	}

	Session->OnSessionChanged.AddUObject(this, &UScctEffectsSubsystem::HandleSessionChanged);
	Session->OnMatchStarted.AddUObject(this, &UScctEffectsSubsystem::HandleMatchStarted);
	Session->OnBeforeTicks.AddUObject(this, &UScctEffectsSubsystem::HandleBeforeTicks);
	Session->OnFrame.AddUObject(this, &UScctEffectsSubsystem::HandleFrame);

	// Flashes are scaled by the picture's exposure (Play/ScctExposure.h).
	FScctExposure::Start();

	bLog = FParse::Param(FCommandLine::Get(), TEXT("ScctFxLog"));
	bAutoFire = FParse::Param(FCommandLine::Get(), TEXT("ScctAutoFire"));
	FParse::Value(FCommandLine::Get(), TEXT("ScctFxTimeScale="), TimeScale);
	TimeScale = FMath::Clamp(TimeScale, 0.01f, 4.0f);

	Random.Initialize(0x5CC7);
	ShotBuffer.SetNumUninitialized(128);
	ActorBuffer.SetNumUninitialized(320);
	MarkBuffer.SetNumUninitialized(MaxImpactSlots);
	Tracers.SetNum(MaxTracers);
	ImpactSerials.Init(0, MaxImpactSlots);
	ImpactSeenFrame.Init(0, MaxImpactSlots);
	Actors.Reserve(320);
}

void UScctEffectsSubsystem::Deinitialize()
{
	if (Session != nullptr)
	{
		Session->OnSessionChanged.RemoveAll(this);
		Session->OnMatchStarted.RemoveAll(this);
		Session->OnBeforeTicks.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------------------------------------------

void UScctEffectsSubsystem::HandleSessionChanged(int32 OldId, int32 NewId)
{
	if (OldId != 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects summary: %d muzzle flashes, %d tracers, %d impacts, %d effect actors (%d drawn as stand-in shapes), at most %d sprites at once"),
			NumFlashes, NumTracers, NumImpacts, NumActors, NumStandIns, PeakSprites);
	}
	// Actor ids are reused by the next match: nothing drawn may carry over.
	ResetMatch();
}

void UScctEffectsSubsystem::ResetMatch()
{
	for (TPair<int32, FActorFx>& Pair : Actors)
	{
		EndActor(Pair.Value, false);
	}
	Actors.Reset();
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		ReleaseLight(Index);
	}
	for (UDecalComponent* Decal : Decals)
	{
		if (Decal != nullptr)
		{
			Decal->SetVisibility(false);
		}
	}
	for (FTracer& Tracer : Tracers)
	{
		Tracer.bLive = false;
	}
	for (int32& Serial : ImpactSerials)
	{
		Serial = 0;
	}
	Sprites.Clear();
	MatchSeconds = 0.0f;
	bFireHeld = bAltFireHeld = false;
	NumFlashes = NumFlashesLeftToViewModel = NumTracers = NumImpacts = NumActors = NumStandIns = PeakSprites = 0;
}

void UScctEffectsSubsystem::HandleMatchStarted()
{
	FString Error;
	Events = ScctFeature<FScctEventsApi>(&Error);
	if (Events == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Effects: none this match, the bridge's events are missing: %s"), *Error);
		return;
	}
	Events->Begin();
	if (EnsureScene())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: ready (%s, %d imported effect meshes, time scale %.2f%s)"),
			Sprites.IsDrawable() ? TEXT("sprite materials found") : TEXT("sprite materials MISSING: run Tools/Effects/build_effect_assets.py"),
			MeshAssets.Num(), TimeScale, bAutoFire ? TEXT(", auto fire on") : TEXT(""));
	}
}

bool UScctEffectsSubsystem::EnsureScene()
{
	if (bSceneReady)
	{
		return true;
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	Host = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	if (Host == nullptr)
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	Host->SetRootComponent(Root);
	Root->RegisterComponent();

	UStaticMesh* Quad = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	Sprites.Init(Host, Quad, LoadObject<UMaterialInterface>(nullptr, GlowMaterialPath), LoadObject<UMaterialInterface>(nullptr, SmokeMaterialPath));

	DecalMaterial = LoadObject<UMaterialInterface>(nullptr, DecalMaterialPath);
	StandInMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (UMaterialInterface* Solid = LoadObject<UMaterialInterface>(nullptr, SolidMaterialPath))
	{
		StandInMaterial = UMaterialInstanceDynamic::Create(Solid, this);
		StandInMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.02f, 0.02f, 0.02f));
		StandInMaterial->SetVectorParameterValue(TEXT("Glow"), FLinearColor(3.0f, 0.25f, 0.1f));
	}

	Lights.SetNum(MaxLights);
	for (int32 Index = 0; Index < MaxLights; ++Index)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Host);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetVisibility(false);
		Light->SetupAttachment(Root);
		Light->RegisterComponent();
		LightComponents.Add(Light);
	}

	BodyFill = NewObject<UPointLightComponent>(Host);
	BodyFill->SetMobility(EComponentMobility::Movable);
	BodyFill->bUseInverseSquaredFalloff = false; // the same units as the map's lights (Tools/Import/import_map.py)
	BodyFill->SetLightFalloffExponent(1.0f);
	BodyFill->SetAttenuationRadius(BodyFillRadius);
	BodyFill->SetCastShadows(false);
	BodyFill->SetSpecularScale(0.6f);
	BodyFill->SetLightingChannels(false, true, false);
	// Lighting channels do not hold for the air: without this the light, which sits at the camera, lights the haze
	// right in front of the lens and a foggy yard turns into a flat grey wash.
	BodyFill->SetVolumetricScatteringIntensity(0.0f);
	BodyFill->SetVisibility(false);
	BodyFill->SetupAttachment(Root);
	BodyFill->RegisterComponent();

	MeshUsed.Init(false, MaxMeshes);
	MeshPool.SetNum(MaxMeshes);

	Decals.SetNum(MaxImpactSlots);
	DecalTints.SetNum(MaxImpactSlots);

	// The effect meshes are original content imported on the player's machine, so they are found by looking, not
	// by fixed references.
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({ ImportedMeshFolder }, false);
	TArray<FAssetData> Found;
	Registry.GetAssetsByPath(ImportedMeshFolder, Found, true);
	for (const FAssetData& Asset : Found)
	{
		if (Asset.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName())
		{
			MeshAssets.Add(NameKey(Asset.AssetName.ToString()), Asset.GetSoftObjectPath());
		}
	}

	bSceneReady = true;
	return true;
}

void UScctEffectsSubsystem::HandleBeforeTicks(float DeltaSeconds)
{
	if (!bAutoFire || !Session->IsPlaying())
	{
		return;
	}

	// -ScctAutoFire: the fire keys go down and come up on the player controller as under a finger, so the shots
	// take the path real ones take (Unreal's key state, the bridge's bindings, the simulation).
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player == nullptr)
	{
		return;
	}
	const bool bFire = MatchSeconds > 3.0f && FMath::Fmod(MatchSeconds, 1.0f) < 0.5f;
	const bool bAltFire = MatchSeconds > 3.0f && FMath::Fmod(MatchSeconds, 6.0f) > 5.6f;
	if (bFire != bFireHeld)
	{
		Player->HoldScriptedAction(TEXT("fire"), bFire);
	}
	if (bAltFire != bAltFireHeld)
	{
		Player->HoldScriptedAction(TEXT("altfire"), bAltFire);
	}
	bFireHeld = bFire;
	bAltFireHeld = bAltFire;
}

void UScctEffectsSubsystem::HandleFrame(float DeltaSeconds)
{
	MatchSeconds += DeltaSeconds;
	if (Events == nullptr || !bSceneReady || !IsValid(Host))
	{
		return;
	}

	++Frame;
	const float FxSeconds = DeltaSeconds * TimeScale;
	DrainShots();
	UpdateTracers(FxSeconds);
	UpdateImpactMarks();
	UpdateEffectActors(FxSeconds);
	UpdateLights(FxSeconds);
	UpdateBodyFill();

	const FScctBridgeView& View = Session->GetView();
	Sprites.Tick(FxSeconds, FVector(View.X, View.Y, View.Z), FRotator(View.Pitch, View.Yaw, View.Roll).Quaternion());
	PeakSprites = FMath::Max(PeakSprites, Sprites.GetNumLive());
}

void UScctEffectsSubsystem::UpdateBodyFill()
{
	if (BodyFill == nullptr)
	{
		return;
	}
	const AScctPawnProxy* Local = Session->FindLocalProxy();
	if (Local == nullptr)
	{
		if (BodyFill->IsVisible())
		{
			BodyFill->SetVisibility(false);
		}
		return;
	}

	// The fill's channel on the bodies of the local player's side, off every other body (sides can change
	// between rounds while the proxies stay).
	for (const FScctBridgePawn& Pawn : Session->GetPawns())
	{
		const AScctPawnProxy* Proxy = Session->FindProxy(Pawn.ActorId);
		UPrimitiveComponent* Body = Proxy != nullptr ? Proxy->GetBodyMesh() : nullptr;
		if (Body == nullptr)
		{
			continue;
		}
		const bool bOwnSide = Proxy->GetTeam() == Local->GetTeam();
		if (static_cast<bool>(Body->LightingChannels.bChannel1) != bOwnSide)
		{
			Body->SetLightingChannels(true, bOwnSide, false);
		}
	}

	const FScctBridgeView& View = Session->GetView();
	BodyFill->SetWorldLocation(FVector(View.X, View.Y, View.Z + BodyFillRaise));
	// Over the whole exposure range, not a flash's narrower one: it lights nothing but these bodies, and with the
	// flash's upper bound a spy in a brightly lit room was a flat black cut-out again.
	BodyFill->SetIntensity(FMath::Max(CVarBodyFill.GetValueOnGameThread(), 0.0f) * FScctExposure::PictureScale(0.03f, 16.0f));
	if (!BodyFill->IsVisible())
	{
		BodyFill->SetVisibility(true);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Shots
// ---------------------------------------------------------------------------------------------------------------

FVector UScctEffectsSubsystem::FindMuzzle(const FScctShot& Shot) const
{
	const FVector Estimate(Shot.X, Shot.Y, Shot.Z);
	if (Shot.Kind == EScctShotKind::FirstPersonMuzzle)
	{
		// The local player's own gun: its view model is not drawn by this subsystem; the simulation's estimate
		// (in front of the camera, low and to the right) is where that gun's muzzle is.
		return Estimate;
	}

	const AScctPawnProxy* Proxy = Session->FindProxy(Shot.ShooterId);
	if (Proxy == nullptr || !Proxy->HasModel() || !Proxy->IsShown() || Proxy->IsFirstPerson())
	{
		return Estimate;
	}
	// The gun drawn on the body knows where its barrel ends (a replacement model's muzzle socket included).
	FTransform Barrel;
	const UScctEquipmentSubsystem* Equipment = GetWorld()->GetSubsystem<UScctEquipmentSubsystem>();
	if (Equipment != nullptr && Equipment->FindMuzzle(Shot.ShooterId, Barrel))
	{
		return Barrel.GetLocation();
	}
	const FName Hand = Proxy->FindMeshBoneName(TEXT("B R Hand"));
	if (Hand.IsNone())
	{
		return Estimate;
	}
	// The gun's barrel ends about this far in front of the hand that holds it.
	return Proxy->GetBodyMesh()->GetSocketLocation(Hand) + FVector(Shot.DirX, Shot.DirY, Shot.DirZ) * 24.0;
}

void UScctEffectsSubsystem::DrainShots()
{
	for (;;)
	{
		const int32 Count = Events->DrainShots(ShotBuffer.GetData(), ShotBuffer.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FScctShot& Shot = ShotBuffer[Index];
			const FVector Direction = FVector(Shot.DirX, Shot.DirY, Shot.DirZ).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
			if (Shot.Kind == EScctShotKind::Tracer)
			{
				++NumTracers;
				const FVector Start(Shot.X, Shot.Y, Shot.Z);
				const FVector Impact(Shot.ImpactX, Shot.ImpactY, Shot.ImpactZ);
				// The original hides a tracer for the last stretch before its impact; a short shot shows none.
				const float Distance = static_cast<float>(FVector::Dist(Start, Impact)) - Shot.HideDistance;
				if (bLog)
				{
					UE_LOG(LogOpenSCCT, Log, TEXT("Effects: tracer of pawn %d from (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f), visible for %.0f cm at %.0f cm/s"),
						Shot.ShooterId, Start.X, Start.Y, Start.Z, Impact.X, Impact.Y, Impact.Z, FMath::Max(Distance, 0.0f), Shot.Speed);
				}
				if (Distance <= 0.0f)
				{
					continue;
				}
				for (FTracer& Tracer : Tracers)
				{
					if (!Tracer.bLive)
					{
						Tracer.Start = Start;
						Tracer.Direction = (Impact - Start).GetSafeNormal(UE_SMALL_NUMBER, Direction);
						Tracer.Distance = Distance;
						Tracer.Travelled = 0.0f;
						Tracer.Speed = Shot.Speed > 0.0f ? Shot.Speed : 7000.0f;
						Tracer.bLive = true;
						break;
					}
				}
				continue;
			}

			const bool bSticky = Shot.Kind == EScctShotKind::MuzzleSticky;
			const bool bFirstPerson = Shot.Kind == EScctShotKind::FirstPersonMuzzle;
			if (bFirstPerson)
			{
				// One owner for the local player's own flash: while the equipment draws a view model the flash
				// sits on that model's muzzle and is drawn there; a second one here would double it.
				const UScctEquipmentSubsystem* Equipment = GetWorld()->GetSubsystem<UScctEquipmentSubsystem>();
				if (Equipment != nullptr && Equipment->DrawsLocalMuzzleFlash())
				{
					++NumFlashesLeftToViewModel;
					if (bLog)
					{
						UE_LOG(LogOpenSCCT, Log, TEXT("Effects: the local first-person flash of pawn %d is the view model's"), Shot.ShooterId);
					}
					continue;
				}
			}
			++NumFlashes;
			const FVector Muzzle = FindMuzzle(Shot);
			const float Scale = (Shot.Flags & EScctShotFlags::Pistol) != 0 ? 0.6f : 1.0f;
			if (bLog)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Effects: muzzle flash kind %d of pawn %d at (%.0f, %.0f, %.0f) (the simulation's estimate (%.0f, %.0f, %.0f)), flags %d"),
					Shot.Kind, Shot.ShooterId, Muzzle.X, Muzzle.Y, Muzzle.Z, Shot.X, Shot.Y, Shot.Z, Shot.Flags);
			}
			// Seen down the sights a flash fills far more of the picture than from outside: it is kept small.
			SpawnMuzzleFlash(Muzzle, Direction, bFirstPerson ? Scale * 0.35f : Scale, bSticky,
				(Shot.Flags & EScctShotFlags::Suppressed) != 0, (Shot.Flags & EScctShotFlags::Shotgun) != 0);
		}
		if (Count < ShotBuffer.Num())
		{
			return;
		}
	}
}

void UScctEffectsSubsystem::UpdateTracers(float DeltaSeconds)
{
	for (FTracer& Tracer : Tracers)
	{
		if (!Tracer.bLive)
		{
			continue;
		}
		Tracer.Travelled += Tracer.Speed * DeltaSeconds;
		if (Tracer.Travelled - TracerLength >= Tracer.Distance)
		{
			Tracer.bLive = false;
			continue;
		}
		const float Head = FMath::Min(Tracer.Travelled, Tracer.Distance);
		const float Tail = FMath::Max(Tracer.Travelled - TracerLength, 0.0f);
		Sprites.DrawStreak(Tracer.Start + Tracer.Direction * Tail, Tracer.Start + Tracer.Direction * Head, TracerWidth, FLinearColor(28.0f, 17.0f, 6.0f, 1.0f));
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Impact marks
// ---------------------------------------------------------------------------------------------------------------

void UScctEffectsSubsystem::UpdateImpactMarks()
{
	const int32 Count = Events->GetImpactMarks(MarkBuffer.GetData(), MarkBuffer.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctImpactMark& Mark = MarkBuffer[Index];
		if (Mark.Slot < 0 || Mark.Slot >= MaxImpactSlots)
		{
			continue;
		}
		ImpactSeenFrame[Mark.Slot] = Frame;

		const FVector At(Mark.X, Mark.Y, Mark.Z);
		const FVector Normal = FRotator(Mark.Pitch, Mark.Yaw, Mark.Roll).Vector();
		const bool bNew = ImpactSerials[Mark.Slot] != Mark.Serial;
		const bool bShown = (Mark.Flags & EScctImpactFlags::DecalShown) != 0 && (Mark.Flags & EScctImpactFlags::Shocker) == 0;
		if (bNew)
		{
			ImpactSerials[Mark.Slot] = Mark.Serial;
			++NumImpacts;
			const FString& SparkClass = GetName(Mark.SparkClassId);
			const FString& TypeClass = GetName(Mark.TypeId);
			if (bLog)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Effects: impact slot %d '%s' spark '%s' at (%.0f, %.0f, %.0f) facing (%.2f, %.2f, %.2f), decal %s"),
					Mark.Slot, *TypeClass, *SparkClass, At.X, At.Y, At.Z, Normal.X, Normal.Y, Normal.Z, bShown ? TEXT("shown") : TEXT("none"));
			}
			SpawnImpact(At, Normal, SparkClass, TypeClass);
		}

		TObjectPtr<UDecalComponent>& Decal = Decals[Mark.Slot];
		if (bShown && DecalMaterial != nullptr)
		{
			if (Decal == nullptr)
			{
				Decal = NewObject<UDecalComponent>(Host);
				Decal->SetDecalMaterial(DecalMaterial);
				Decal->SetupAttachment(Host->GetRootComponent());
				Decal->RegisterComponent();
				Decal->SetFadeScreenSize(0.0005f);
				// Each slot's decal has its own colours, set for the surface that was hit.
				DecalTints[Mark.Slot] = Decal->CreateDynamicMaterialInstance();
			}
			if (bNew || !Decal->IsVisible())
			{
				const float HalfSize = TintDecal(DecalTints[Mark.Slot], GetName(Mark.TypeId), GetName(Mark.SparkClassId));
				// A decal projects along its own X axis, into the surface; its turn about that axis is random so
				// a burst does not stamp one shape, and so is its size a little.
				const float Size = HalfSize * (Mark.DrawScale > 0.0f ? FMath::Clamp(Mark.DrawScale, 0.6f, 2.0f) : 1.0f) * Random.FRandRange(0.85f, 1.2f);
				FRotator Facing = (-Normal).Rotation();
				Facing.Roll = Random.FRandRange(0.0f, 360.0f);
				// Shallow, so a mark on a thin wall or a door edge is not projected onto what stands behind it.
				Decal->DecalSize = FVector(4.0, Size, Size);
				Decal->SetWorldLocationAndRotation(At, Facing);
				Decal->SetVisibility(true);
			}
		}
		else if (Decal != nullptr && Decal->IsVisible())
		{
			Decal->SetVisibility(false);
		}
	}

	// A slot that is no longer listed has expired.
	for (int32 Slot = 0; Slot < MaxImpactSlots; ++Slot)
	{
		if (ImpactSeenFrame[Slot] != Frame && Decals[Slot] != nullptr && Decals[Slot]->IsVisible())
		{
			Decals[Slot]->SetVisibility(false);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Effect actors
// ---------------------------------------------------------------------------------------------------------------

void UScctEffectsSubsystem::UpdateEffectActors(float DeltaSeconds)
{
	const int32 Count = Events->GetEffectActors(ActorBuffer.GetData(), ActorBuffer.Num());
	const UScctVisionSubsystem* Vision = UScctVisionSubsystem::Find(GetWorld());
	const bool bShowVisionOnly = Vision != nullptr && Vision->ShowsVisionOnlyEffects();
	int32 NumVisionOnly = 0;
	ON_SCOPE_EXIT
	{
		const int32 Shown = bShowVisionOnly ? NumVisionOnly : 0;
		if (NumVisionOnly != LoggedVisionOnly || Shown != LoggedVisionOnlyShown)
		{
			LoggedVisionOnly = NumVisionOnly;
			LoggedVisionOnlyShown = Shown;
			UE_LOG(LogOpenSCCT, Log, TEXT("Effects: %d vision-only effects listed, %d shown (mode %d)"), NumVisionOnly, Shown, Vision != nullptr ? Vision->GetVisionMode() : 0);
		}
	};
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctEffectActor& Record = ActorBuffer[Index];
		FActorFx* Fx = Actors.Find(Record.ActorId);
		if ((Record.Flags & EScctEffectFlags::Deleted) != 0)
		{
			if (Fx != nullptr)
			{
				EndActor(*Fx, (Record.Flags & EScctEffectFlags::Killed) != 0);
				Actors.Remove(Record.ActorId);
			}
			continue;
		}
		if (Fx == nullptr)
		{
			Fx = &Actors.Add(Record.ActorId);
			StartActor(*Fx, Record);
		}
		Fx->SeenFrame = Frame;
		Fx->Age += DeltaSeconds;

		// Where it is: on a drawn pawn's bone, or where the simulation has it.
		const FVector Scale(Record.ScaleX != 0.0f ? Record.ScaleX : 1.0f, Record.ScaleY != 0.0f ? Record.ScaleY : 1.0f, Record.ScaleZ != 0.0f ? Record.ScaleZ : 1.0f);
		FTransform Place(FRotator(Record.Pitch, Record.Yaw, Record.Roll), FVector(Record.X, Record.Y, Record.Z));
		// bHideInNormalVision: shown only while the goggles show night or thermal vision.
		const bool bVisionOnly = (Record.Flags & EScctEffectFlags::VisionOnly) != 0;
		NumVisionOnly += bVisionOnly ? 1 : 0;
		bool bHidden = (Record.Flags & EScctEffectFlags::Hidden) != 0 || (bVisionOnly && !bShowVisionOnly);
		if ((Record.Flags & EScctEffectFlags::OnBone) != 0)
		{
			const AScctPawnProxy* Proxy = Session->FindProxy(Record.BaseActorId);
			const FName Bone = Proxy != nullptr && Proxy->HasModel() ? FindDrawnBone(*Proxy, Record.BoneId) : NAME_None;
			if (Bone.IsNone())
			{
				// Its wearer is not drawn (yet): nothing may show at the actor's own place in mid-air.
				bHidden = true;
			}
			else
			{
				const FTransform BoneWorld = Proxy->GetBodyMesh()->GetSocketTransform(Bone);
				const FTransform Relative(FRotator(Record.RelPitch, Record.RelYaw, Record.RelRoll), FVector(Record.RelX, Record.RelY, Record.RelZ));
				Place = Relative * FTransform(BoneWorld.GetRotation(), BoneWorld.GetLocation());
				// What a player wears is not in front of their own eyes.
				bHidden |= !Proxy->IsShown() || Proxy->IsFirstPerson();
			}
		}
		const FVector Before = Fx->Location;
		Fx->Location = Place.GetLocation();

		if (Fx->Mesh != INDEX_NONE)
		{
			UStaticMeshComponent* Component = MeshPool[Fx->Mesh];
			// A beam whose simulated start hangs in mid-air belongs to whoever draws the hand or device it comes from.
			const bool bMeshHidden = bHidden || (Record.Kind == EScctEffectKind::Beam && (Record.Flags & EScctEffectFlags::OwnView) != 0);
			if (Component->IsVisible() == bMeshHidden)
			{
				Component->SetVisibility(!bMeshHidden);
			}
			if (!bMeshHidden)
			{
				// The original draws a mesh moved by -PrePivot in its own, scaled frame. The stand-in shape is not
				// the game's mesh, so it keeps its own small size.
				const FVector MeshScale = Fx->bStandIn ? FVector(0.09) : Scale;
				const FVector Pivot = Fx->bStandIn ? FVector::ZeroVector : Scale * -FVector(Record.PrePivotX, Record.PrePivotY, Record.PrePivotZ);
				Component->SetWorldTransform(FTransform(Place.GetRotation(), Place.TransformPosition(Pivot), MeshScale));
			}
		}

		if (!bHidden && Fx->Style != EStyle::None && Record.Kind == EScctEffectKind::Emitter)
		{
			Emit(Fx->Style, Fx->Location, FVector(Record.VelX, Record.VelY, Record.VelZ), DeltaSeconds, Fx->EmitDebt);
		}
		if (!bHidden && Fx->TrailStyle != EStyle::None && !Before.Equals(Fx->Location, 0.5))
		{
			Emit(Fx->TrailStyle, Fx->Location, FVector::ZeroVector, DeltaSeconds, Fx->EmitDebt);
		}

		// Its light: the brightness changes every frame (the grenade's flash fades), and a hidden light still lights.
		const bool bLit = (Record.Kind == EScctEffectKind::Light || (Record.Flags & EScctEffectFlags::DynamicLight) != 0) && Record.LightBrightness > 0.5f;
		if (bLit)
		{
			const FLinearColor Color = LightColor(Record.LightHue, Record.LightSaturation);
			const float Candelas = Record.LightBrightness * CandelasPerBrightness;
			const float Radius = FMath::Max(Record.LightRadius, 100.0f);
			if (Fx->Light == INDEX_NONE)
			{
				Fx->Light = AcquireLight(Fx->Location, Color, Candelas, Radius, 0.0f, true);
			}
			if (Fx->Light != INDEX_NONE)
			{
				UPointLightComponent* Light = LightComponents[Fx->Light];
				Light->SetWorldLocation(Fx->Location);
				Light->SetIntensity(Candelas);
				Light->SetLightColor(Color);
				Light->SetAttenuationRadius(Radius);
			}
		}
		else if (Fx->Light != INDEX_NONE)
		{
			ReleaseLight(Fx->Light);
			Fx->Light = INDEX_NONE;
		}
	}

	// An actor that is simply no longer listed has ended too.
	for (auto It = Actors.CreateIterator(); It; ++It)
	{
		if (It.Value().SeenFrame != Frame)
		{
			EndActor(It.Value(), false);
			It.RemoveCurrent();
		}
	}
}

void UScctEffectsSubsystem::StartActor(FActorFx& Fx, const FScctEffectActor& Record)
{
	++NumActors;
	Fx.ClassId = Record.ClassId;
	Fx.Kind = Record.Kind;
	Fx.Location = FVector(Record.X, Record.Y, Record.Z);
	Fx.Style = Record.Kind == EScctEffectKind::Emitter ? FindStyle(Record.ClassId) : EStyle::None;
	Fx.TrailStyle = Record.TrailClassId != 0 ? FindStyle(Record.TrailClassId) : EStyle::None;

	const bool bDrawsMesh = Record.Kind == EScctEffectKind::Projectile || Record.Kind == EScctEffectKind::Mine || Record.Kind == EScctEffectKind::Gadget
		|| Record.Kind == EScctEffectKind::Beam || Record.Kind == EScctEffectKind::Decal;
	if (bDrawsMesh)
	{
		UStaticMesh* Mesh = FindMesh(Record.MeshId);
		// A beam or a puddle without its mesh is better left out than drawn as a ball.
		Fx.bStandIn = Mesh == nullptr && Record.Kind != EScctEffectKind::Beam && Record.Kind != EScctEffectKind::Decal;
		if (Mesh != nullptr || Fx.bStandIn)
		{
			for (int32 Index = 0; Index < MaxMeshes; ++Index)
			{
				if (MeshUsed[Index])
				{
					continue;
				}
				TObjectPtr<UStaticMeshComponent>& Component = MeshPool[Index];
				if (Component == nullptr)
				{
					Component = NewObject<UStaticMeshComponent>(Host);
					Component->SetMobility(EComponentMobility::Movable);
					Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Component->SetupAttachment(Host->GetRootComponent());
					Component->RegisterComponent();
				}
				Component->SetStaticMesh(Fx.bStandIn ? StandInMesh.Get() : Mesh);
				Component->EmptyOverrideMaterials();
				if (Fx.bStandIn && StandInMaterial != nullptr)
				{
					Component->SetMaterial(0, StandInMaterial);
				}
				// Thin glowing beams and flat puddles throw no shadow.
				Component->SetCastShadow(Record.Kind != EScctEffectKind::Beam && Record.Kind != EScctEffectKind::Decal);
				Component->SetVisibility(false);
				MeshUsed[Index] = true;
				Fx.Mesh = Index;
				NumStandIns += Fx.bStandIn ? 1 : 0;
				break;
			}
		}
	}

	if (bLog)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: actor %d '%s' kind %d flags %d at (%.0f, %.0f, %.0f): %s%s"), Record.ActorId, *GetName(Record.ClassId), Record.Kind,
			Record.Flags, Record.X, Record.Y, Record.Z,
			Fx.Mesh != INDEX_NONE ? (Fx.bStandIn ? TEXT("stand-in shape for mesh ") : TEXT("mesh ")) : TEXT("particles"),
			Fx.Mesh != INDEX_NONE ? *GetName(Record.MeshId) : TEXT(""));
	}

	// What happens once, as it appears.
	if ((Record.Flags & EScctEffectFlags::Hidden) != 0)
	{
		return;
	}
	switch (Fx.Style)
	{
	case EStyle::Explosion:
		SpawnExplosion(Fx.Location);
		break;
	case EStyle::FlashBang:
		SpawnFlashBang(Fx.Location);
		break;
	case EStyle::Chaff:
		for (int32 Index = 0; Index < 140; ++Index)
		{
			FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Glow);
			Particle.Position = Fx.Location;
			Particle.Velocity = Random.VRand() * Random.FRandRange(150.0f, 900.0f) + FVector(0.0, 0.0, 300.0);
			Particle.Color = FLinearColor(3.0f, 3.2f, 3.6f, Random.FRandRange(0.4f, 1.0f));
			Particle.Life = Random.FRandRange(3.0f, 9.0f);
			Particle.SizeStart = Particle.SizeEnd = Random.FRandRange(2.0f, 5.0f);
			Particle.Drag = 3.5f;
			Particle.Gravity = 35.0f;
		}
		break;
	case EStyle::Sparks:
		SpawnSparks(Fx.Location, FVector::UpVector, 16, FLinearColor(24.0f, 14.0f, 4.0f), 600.0f);
		break;
	case EStyle::Puff:
		SpawnDust(Fx.Location, FVector::UpVector, 5, FLinearColor(0.35f, 0.33f, 0.3f, 0.35f), 30.0f);
		break;
	default:
		break;
	}
}

void UScctEffectsSubsystem::EndActor(FActorFx& Fx, bool bKilled)
{
	if (Fx.Mesh != INDEX_NONE)
	{
		MeshPool[Fx.Mesh]->SetVisibility(false);
		MeshUsed[Fx.Mesh] = false;
		Fx.Mesh = INDEX_NONE;
	}
	if (Fx.Light != INDEX_NONE)
	{
		ReleaseLight(Fx.Light);
		Fx.Light = INDEX_NONE;
	}
	// Its particles are in the shared pool and die out where they are, killed or not.
}

void UScctEffectsSubsystem::Emit(EStyle Style, const FVector& At, const FVector& Velocity, float DeltaSeconds, float& Debt)
{
	float Rate = 0.0f;
	switch (Style)
	{
	case EStyle::Smoke: Rate = 16.0f; break;
	case EStyle::Gas: Rate = 12.0f; break;
	case EStyle::Wisp: Rate = 26.0f; break;
	case EStyle::Sparks: Rate = 14.0f; break;
	case EStyle::Electric: Rate = 40.0f; break;
	case EStyle::Flare: Rate = 40.0f; break;
	case EStyle::Water: Rate = 30.0f; break;
	default: return;
	}

	Debt += Rate * DeltaSeconds;
	for (; Debt >= 1.0f; Debt -= 1.0f)
	{
		switch (Style)
		{
		case EStyle::Smoke:
		case EStyle::Gas:
		{
			// A cloud that fills a room: big soft puffs that swell, drift and thin out over many seconds.
			const bool bGas = Style == EStyle::Gas;
			FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Smoke);
			Particle.Position = At + Random.VRand() * 15.0;
			Particle.Velocity = Random.VRand() * Random.FRandRange(40.0f, 160.0f) + FVector(0.0, 0.0, 45.0);
			Particle.Color = bGas ? FLinearColor(0.42f, 0.5f, 0.2f, 0.4f) : FLinearColor(0.55f, 0.55f, 0.57f, 0.6f);
			Particle.Life = Random.FRandRange(5.0f, 9.0f);
			Particle.SizeStart = 50.0f;
			Particle.SizeEnd = Random.FRandRange(260.0f, 420.0f);
			Particle.Drag = 1.1f;
			Particle.Gravity = -6.0f;
			Particle.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
			Particle.Spin = Random.FRandRange(-0.3f, 0.3f);
			Particle.FadeIn = 0.15f;
			break;
		}
		case EStyle::Wisp:
		{
			FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Smoke);
			Particle.Position = At;
			Particle.Velocity = Velocity * 0.1 + Random.VRand() * 8.0 + FVector(0.0, 0.0, 22.0);
			Particle.Color = FLinearColor(0.6f, 0.6f, 0.62f, 0.22f);
			Particle.Life = Random.FRandRange(0.7f, 1.4f);
			Particle.SizeStart = 3.0f;
			Particle.SizeEnd = Random.FRandRange(16.0f, 30.0f);
			Particle.Drag = 1.5f;
			Particle.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
			Particle.Spin = Random.FRandRange(-1.0f, 1.0f);
			Particle.FadeIn = 0.2f;
			break;
		}
		case EStyle::Sparks:
			SpawnSparks(At, FVector::UpVector, 1, FLinearColor(24.0f, 14.0f, 4.0f), 450.0f);
			break;
		case EStyle::Electric:
		{
			// Arcs: short blue-white dashes that jump about the source.
			FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Glow);
			Particle.Position = At + Random.VRand() * 6.0;
			Particle.Velocity = Random.VRand() * Random.FRandRange(150.0f, 520.0f);
			Particle.Color = FLinearColor(8.0f, 14.0f, 30.0f, 1.0f);
			Particle.Life = Random.FRandRange(0.05f, 0.16f);
			Particle.SizeStart = 2.5f;
			Particle.SizeEnd = 1.0f;
			Particle.Stretch = 0.04f;
			break;
		}
		case EStyle::Flare:
		{
			// A burning core with sparks falling from it.
			FScctFxParticle& Core = Sprites.Spawn(EScctFxLayer::Glow);
			Core.Position = At;
			Core.Color = FLinearColor(40.0f, 12.0f, 6.0f, 1.0f);
			Core.Life = 0.12f;
			Core.SizeStart = Random.FRandRange(22.0f, 34.0f);
			Core.SizeEnd = 10.0f;
			FScctFxParticle& Spark = Sprites.Spawn(EScctFxLayer::Glow);
			Spark.Position = At;
			Spark.Velocity = Random.VRand() * Random.FRandRange(60.0f, 260.0f);
			Spark.Color = FLinearColor(20.0f, 6.0f, 2.0f, 1.0f);
			Spark.Life = Random.FRandRange(0.3f, 0.8f);
			Spark.SizeStart = 2.0f;
			Spark.SizeEnd = 0.5f;
			Spark.Gravity = 500.0f;
			Spark.Stretch = 0.03f;
			break;
		}
		case EStyle::Water:
		{
			FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Smoke);
			Particle.Position = At + Random.VRand() * 4.0;
			Particle.Velocity = Random.VRand() * Random.FRandRange(20.0f, 90.0f) + FVector(0.0, 0.0, 40.0);
			Particle.Color = FLinearColor(0.75f, 0.82f, 0.9f, 0.35f);
			Particle.Life = Random.FRandRange(0.25f, 0.5f);
			Particle.SizeStart = 1.5f;
			Particle.SizeEnd = 4.0f;
			Particle.Gravity = 700.0f;
			Particle.Stretch = 0.03f;
			break;
		}
		default:
			break;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// The looks
// ---------------------------------------------------------------------------------------------------------------

FVector UScctEffectsSubsystem::RandomCone(const FVector& Axis, float HalfAngleDegrees)
{
	return Random.VRandCone(Axis, FMath::DegreesToRadians(HalfAngleDegrees));
}

void UScctEffectsSubsystem::SpawnMuzzleFlash(const FVector& At, const FVector& Direction, float Scale, bool bSticky, bool bSuppressed, bool bShotgun)
{
	// The spy's pistol fires a charged dart: a cold blue flash. A merc's gun burns powder.
	const FLinearColor Hot = bSticky ? FLinearColor(3.0f, 7.0f, 20.0f) : FLinearColor(18.0f, 10.0f, 3.2f);
	const float Size = Scale * (bShotgun ? 1.5f : 1.0f) * (bSuppressed ? 0.45f : 1.0f);
	const float Level = bSuppressed ? 0.35f : 1.0f;

	FScctFxParticle& Core = Sprites.Spawn(EScctFxLayer::Glow);
	Core.Position = At + Direction * 6.0 * Size;
	Core.Color = FLinearColor(Hot.R, Hot.G, Hot.B, Level);
	Core.Life = 0.07f;
	Core.SizeStart = 18.0f * Size;
	Core.SizeEnd = 30.0f * Size;
	Core.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
	Core.FadeIn = 0.01f;

	if (!bSuppressed)
	{
		// The jet out of the barrel and the gas that escapes sideways: a star, different every shot.
		FScctFxParticle& Jet = Sprites.Spawn(EScctFxLayer::Glow);
		Jet.Position = At;
		Jet.Velocity = Direction * 1500.0 * Size;
		Jet.Color = FLinearColor(Hot.R, Hot.G, Hot.B, 1.0f);
		Jet.Life = 0.06f;
		Jet.SizeStart = 13.0f * Size;
		Jet.SizeEnd = 6.0f * Size;
		Jet.Stretch = 0.035f;
		Jet.FadeIn = 0.01f;
		const int32 Spikes = bShotgun ? 7 : 4;
		for (int32 Index = 0; Index < Spikes; ++Index)
		{
			FScctFxParticle& Spike = Sprites.Spawn(EScctFxLayer::Glow);
			Spike.Position = At;
			Spike.Velocity = RandomCone(Direction, 70.0f) * Random.FRandRange(500.0f, 1000.0f) * Size;
			Spike.Color = FLinearColor(Hot.R, Hot.G, Hot.B, 0.8f);
			Spike.Life = Random.FRandRange(0.04f, 0.07f);
			Spike.SizeStart = 8.0f * Size;
			Spike.SizeEnd = 3.0f * Size;
			Spike.Stretch = 0.03f;
			Spike.FadeIn = 0.01f;
		}
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		FScctFxParticle& Smoke = Sprites.Spawn(EScctFxLayer::Smoke);
		Smoke.Position = At + Direction * 8.0;
		Smoke.Velocity = Direction * Random.FRandRange(40.0f, 120.0f) + FVector(0.0, 0.0, 18.0) + Random.VRand() * 12.0;
		Smoke.Color = FLinearColor(0.6f, 0.6f, 0.6f, 0.16f);
		Smoke.Life = Random.FRandRange(0.5f, 0.9f);
		Smoke.SizeStart = 6.0f * Size;
		Smoke.SizeEnd = 38.0f * Size;
		Smoke.Drag = 2.5f;
		Smoke.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
		Smoke.FadeIn = 0.15f;
	}

	const FLinearColor LightTint = bSticky ? FLinearColor(0.3f, 0.55f, 1.0f) : FLinearColor(1.0f, 0.68f, 0.36f);
	// The light sits a little way down the line of fire, so it lights the room and not just the shooter's hands.
	AcquireLight(At + Direction * 35.0, LightTint, 45.0f * Level, 900.0f, 0.09f, true);
}

void UScctEffectsSubsystem::SpawnSparks(const FVector& At, const FVector& Normal, int32 Count, const FLinearColor& Color, float Speed)
{
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FScctFxParticle& Spark = Sprites.Spawn(EScctFxLayer::Glow);
		Spark.Position = At + Normal * 1.5;
		Spark.Velocity = RandomCone(Normal, 65.0f) * Random.FRandRange(0.25f, 1.0f) * Speed;
		Spark.Color = FLinearColor(Color.R, Color.G, Color.B, 1.0f);
		Spark.Life = Random.FRandRange(0.18f, 0.6f);
		Spark.SizeStart = Random.FRandRange(1.4f, 2.6f);
		Spark.SizeEnd = 0.6f;
		Spark.Stretch = 0.025f;
		Spark.Gravity = 900.0f;
		Spark.Drag = 1.2f;
		Spark.FadeIn = 0.01f;
	}
}

void UScctEffectsSubsystem::SpawnDust(const FVector& At, const FVector& Normal, int32 Count, const FLinearColor& Color, float Size)
{
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FScctFxParticle& Dust = Sprites.Spawn(EScctFxLayer::Smoke);
		Dust.Position = At + Normal * 2.0;
		Dust.Velocity = RandomCone(Normal, 50.0f) * Random.FRandRange(30.0f, 170.0f);
		Dust.Color = Color;
		Dust.Life = Random.FRandRange(0.5f, 1.3f);
		Dust.SizeStart = Size * 0.2f;
		Dust.SizeEnd = Size * Random.FRandRange(0.8f, 1.4f);
		Dust.Drag = 3.0f;
		Dust.Gravity = 25.0f;
		Dust.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
		Dust.Spin = Random.FRandRange(-1.0f, 1.0f);
		Dust.FadeIn = 0.1f;
	}
}

void UScctEffectsSubsystem::SpawnImpact(const FVector& At, const FVector& Normal, const FString& SparkClass, const FString& TypeClass)
{
	// The surface is in the impact's class and its spark class ("SImpactEffectMetal", "SBase.SSparkWood").
	const auto Is = [&SparkClass, &TypeClass](const TCHAR* Word)
	{
		return SparkClass.Contains(Word) || TypeClass.Contains(Word);
	};

	if (Is(TEXT("Shocker")))
	{
		SpawnSparks(At, Normal, 12, FLinearColor(8.0f, 14.0f, 34.0f), 380.0f);
		AcquireLight(At + Normal * 8.0, FLinearColor(0.35f, 0.55f, 1.0f), 160.0f, 500.0f, 0.12f, false);
	}
	else if (Is(TEXT("Blood")))
	{
		SpawnDust(At, Normal, 6, FLinearColor(0.22f, 0.005f, 0.005f, 0.7f), 22.0f);
	}
	else if (Is(TEXT("Water")))
	{
		SpawnDust(At, Normal, 6, FLinearColor(0.8f, 0.86f, 0.92f, 0.4f), 26.0f);
		float Debt = 8.0f;
		Emit(EStyle::Water, At, FVector::ZeroVector, 0.0f, Debt);
	}
	else if (Is(TEXT("Metal")) || Is(TEXT("Fence")) || Is(TEXT("Armor")))
	{
		SpawnSparks(At, Normal, 16, FLinearColor(30.0f, 20.0f, 8.0f), 750.0f);
		FScctFxParticle& Hit = Sprites.Spawn(EScctFxLayer::Glow);
		Hit.Position = At + Normal * 2.0;
		Hit.Color = FLinearColor(30.0f, 22.0f, 10.0f, 1.0f);
		Hit.Life = 0.06f;
		Hit.SizeStart = 14.0f;
		Hit.SizeEnd = 4.0f;
		SpawnDust(At, Normal, 1, FLinearColor(0.5f, 0.5f, 0.5f, 0.2f), 16.0f);
	}
	else if (Is(TEXT("Wood")))
	{
		SpawnDust(At, Normal, 5, FLinearColor(0.34f, 0.24f, 0.13f, 0.45f), 24.0f);
		SpawnSparks(At, Normal, 3, FLinearColor(4.0f, 2.2f, 0.8f), 300.0f);
	}
	else if (Is(TEXT("Glass")))
	{
		SpawnSparks(At, Normal, 10, FLinearColor(5.0f, 6.0f, 7.0f), 420.0f);
		SpawnDust(At, Normal, 2, FLinearColor(0.8f, 0.85f, 0.9f, 0.2f), 18.0f);
	}
	else if (Is(TEXT("Sand")) || Is(TEXT("Soft")))
	{
		SpawnDust(At, Normal, 6, FLinearColor(0.5f, 0.43f, 0.3f, 0.45f), 30.0f);
	}
	else
	{
		// Concrete and anything unnamed: a puff of grey dust and a few chips that glow for an instant.
		SpawnDust(At, Normal, 5, FLinearColor(0.5f, 0.49f, 0.47f, 0.4f), 26.0f);
		SpawnSparks(At, Normal, 4, FLinearColor(16.0f, 10.0f, 4.0f), 420.0f);
	}
}

float UScctEffectsSubsystem::TintDecal(UMaterialInstanceDynamic* Tint, const FString& TypeClass, const FString& SparkClass)
{
	const auto Is = [&SparkClass, &TypeClass](const TCHAR* Word)
	{
		return SparkClass.Contains(Word) || TypeClass.Contains(Word);
	};

	// The hole itself and the ring of broken surface around it: fresh splinters on wood, bare bright metal,
	// crushed white on glass, chipped grey on concrete. Strength is how much of the surface the mark covers.
	// Rim is how much of the chipped ring shows (1: a crater of broken surface, low: little more than the hole).
	FLinearColor Hole(0.012f, 0.011f, 0.01f);
	FLinearColor Rim(0.3f, 0.29f, 0.27f);
	float Strength = 0.9f;
	float RimStrength = 0.55f;
	float HalfSize = DecalHalfSizeConcrete;
	if (Is(TEXT("Wood")))
	{
		Hole = FLinearColor(0.02f, 0.012f, 0.006f);
		Rim = FLinearColor(0.46f, 0.34f, 0.2f); // fresh splinters, paler than the board
		RimStrength = 0.75f;
		HalfSize = DecalHalfSizeWood;
	}
	else if (Is(TEXT("Metal")) || Is(TEXT("Fence")) || Is(TEXT("Armor")))
	{
		Hole = FLinearColor(0.006f, 0.006f, 0.007f);
		Rim = FLinearColor(0.42f, 0.42f, 0.45f);
		RimStrength = 0.7f;
		HalfSize = DecalHalfSizeMetal;
	}
	else if (Is(TEXT("Glass")))
	{
		Hole = FLinearColor(0.05f, 0.055f, 0.06f);
		Rim = FLinearColor(0.7f, 0.74f, 0.78f);
		Strength = 0.6f;
		RimStrength = 0.45f;
		HalfSize = DecalHalfSizeGlass;
	}
	else if (Is(TEXT("Sand")) || Is(TEXT("Soft")) || Is(TEXT("Blood")))
	{
		Hole = FLinearColor(0.02f, 0.016f, 0.012f);
		Rim = FLinearColor(0.1f, 0.085f, 0.065f);
		Strength = 0.75f;
		RimStrength = 0.35f;
		HalfSize = DecalHalfSizeSoft;
	}
	if (Tint != nullptr)
	{
		Tint->SetVectorParameterValue(TEXT("Color"), Hole);
		Tint->SetVectorParameterValue(TEXT("RimColor"), Rim);
		Tint->SetScalarParameterValue(TEXT("Strength"), Strength);
		Tint->SetScalarParameterValue(TEXT("RimStrength"), RimStrength);
	}
	return HalfSize;
}

void UScctEffectsSubsystem::SpawnExplosion(const FVector& At)
{
	// The instant of the blast: one white-hot ball.
	FScctFxParticle& Flash = Sprites.Spawn(EScctFxLayer::Glow);
	Flash.Position = At + FVector(0.0, 0.0, 20.0);
	Flash.Color = FLinearColor(7.0f, 5.0f, 2.8f, 1.0f);
	Flash.Life = 0.14f;
	Flash.SizeStart = 140.0f;
	Flash.SizeEnd = 260.0f;
	Flash.FadeIn = 0.02f;

	// The fireball rolling outwards and burning out.
	for (int32 Index = 0; Index < 18; ++Index)
	{
		FScctFxParticle& Fire = Sprites.Spawn(EScctFxLayer::Glow);
		const FVector Out = RandomCone(FVector::UpVector, 100.0f);
		Fire.Position = At + Out * 20.0;
		Fire.Velocity = Out * Random.FRandRange(250.0f, 700.0f);
		Fire.Color = FLinearColor(9.0f, 3.2f, 0.6f, 0.9f);
		Fire.Life = Random.FRandRange(0.3f, 0.7f);
		Fire.SizeStart = Random.FRandRange(70.0f, 110.0f);
		Fire.SizeEnd = Random.FRandRange(180.0f, 260.0f);
		Fire.Drag = 4.0f;
		Fire.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
		Fire.Spin = Random.FRandRange(-2.0f, 2.0f);
		Fire.FadeIn = 0.08f;
	}

	// Burning fragments thrown far and falling.
	for (int32 Index = 0; Index < 60; ++Index)
	{
		FScctFxParticle& Spark = Sprites.Spawn(EScctFxLayer::Glow);
		Spark.Position = At + FVector(0.0, 0.0, 10.0);
		Spark.Velocity = RandomCone(FVector::UpVector, 95.0f) * Random.FRandRange(600.0f, 2400.0f);
		Spark.Color = FLinearColor(16.0f, 8.5f, 2.4f, 1.0f);
		Spark.Life = Random.FRandRange(0.5f, 1.5f);
		Spark.SizeStart = Random.FRandRange(2.5f, 4.5f);
		Spark.SizeEnd = 1.0f;
		Spark.Stretch = 0.02f;
		Spark.Gravity = 980.0f;
		Spark.Drag = 0.9f;
		Spark.FadeIn = 0.01f;
	}

	// The smoke that is left: dark at the heart, rising and thinning for seconds.
	for (int32 Index = 0; Index < 26; ++Index)
	{
		FScctFxParticle& Smoke = Sprites.Spawn(EScctFxLayer::Smoke);
		const FVector Out = RandomCone(FVector::UpVector, 100.0f);
		Smoke.Position = At + Out * 30.0;
		Smoke.Velocity = Out * Random.FRandRange(120.0f, 480.0f) + FVector(0.0, 0.0, 70.0);
		const float Grey = Random.FRandRange(0.05f, 0.2f);
		Smoke.Color = FLinearColor(Grey, Grey, Grey, 0.75f);
		Smoke.Life = Random.FRandRange(2.5f, 5.0f);
		Smoke.SizeStart = Random.FRandRange(80.0f, 130.0f);
		Smoke.SizeEnd = Random.FRandRange(300.0f, 460.0f);
		Smoke.Drag = 2.2f;
		Smoke.Gravity = -22.0f;
		Smoke.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
		Smoke.Spin = Random.FRandRange(-0.5f, 0.5f);
		Smoke.FadeIn = 0.06f;
	}

	// Dust blown along the ground.
	for (int32 Index = 0; Index < 12; ++Index)
	{
		FScctFxParticle& Dust = Sprites.Spawn(EScctFxLayer::Smoke);
		const float Angle = Random.FRandRange(0.0f, UE_TWO_PI);
		Dust.Position = At;
		Dust.Velocity = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.05) * Random.FRandRange(500.0f, 1000.0f);
		Dust.Color = FLinearColor(0.42f, 0.4f, 0.36f, 0.35f);
		Dust.Life = Random.FRandRange(1.2f, 2.4f);
		Dust.SizeStart = 60.0f;
		Dust.SizeEnd = Random.FRandRange(200.0f, 320.0f);
		Dust.Drag = 3.5f;
		Dust.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
		Dust.FadeIn = 0.1f;
	}

	// The simulation lights the blast too (SFlashLight_FragGrenade, an effect actor of its own); this one is the
	// fireball's glow on the room, kept low so the two together do not white out the picture at a few metres.
	AcquireLight(At + FVector(0.0, 0.0, 40.0), FLinearColor(1.0f, 0.6f, 0.28f), 220.0f, 1600.0f, 0.45f, true);
}

void UScctEffectsSubsystem::SpawnFlashBang(const FVector& At)
{
	FScctFxParticle& Flash = Sprites.Spawn(EScctFxLayer::Glow);
	Flash.Position = At;
	Flash.Color = FLinearColor(60.0f, 60.0f, 66.0f, 1.0f);
	Flash.Life = 0.3f;
	Flash.SizeStart = 500.0f;
	Flash.SizeEnd = 900.0f;
	Flash.FadeIn = 0.02f;
	SpawnSparks(At, FVector::UpVector, 30, FLinearColor(40.0f, 40.0f, 46.0f), 1400.0f);
	SpawnDust(At, FVector::UpVector, 8, FLinearColor(0.7f, 0.7f, 0.7f, 0.3f), 160.0f);
	AcquireLight(At + FVector(0.0, 0.0, 30.0), FLinearColor(1.0f, 1.0f, 1.0f), 5000.0f, 3000.0f, 0.3f, true);
}

// ---------------------------------------------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------------------------------------------

int32 UScctEffectsSubsystem::AcquireLight(const FVector& At, const FLinearColor& Color, float Candelas, float Radius, float Life, bool bShadows)
{
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		if (Lights[Index].bUsed)
		{
			continue;
		}
		// A timed flash is as bright in the picture in a dark cellar as in a lit hall: without this the eye,
		// opened up for the dark, sees a muzzle flash a metre from a wall as a white screen. A light an effect
		// actor holds (a flare, a burning thing) is a lamp like any other and keeps its strength.
		const float Shown = Life > 0.0f ? Candelas * FScctExposure::FlashScale() : Candelas;
		Lights[Index] = { Life, 0.0f, Shown, true };
		UPointLightComponent* Light = LightComponents[Index];
		Light->SetWorldLocation(At);
		Light->SetLightColor(Color);
		Light->SetIntensity(Shown);
		Light->SetAttenuationRadius(Radius);
		Light->SetCastShadows(bShadows);
		Light->SetVisibility(true);
		return Index;
	}
	return INDEX_NONE;
}

void UScctEffectsSubsystem::ReleaseLight(int32 Index)
{
	if (Lights.IsValidIndex(Index) && Lights[Index].bUsed)
	{
		Lights[Index].bUsed = false;
		LightComponents[Index]->SetVisibility(false);
	}
}

void UScctEffectsSubsystem::UpdateLights(float DeltaSeconds)
{
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		FLight& Light = Lights[Index];
		if (!Light.bUsed || Light.Life <= 0.0f)
		{
			continue;
		}
		Light.Age += DeltaSeconds;
		if (Light.Age >= Light.Life)
		{
			ReleaseLight(Index);
			continue;
		}
		// A flash is brightest at once and dies away quickly.
		LightComponents[Index]->SetIntensity(Light.Candelas * FMath::Square(1.0f - Light.Age / Light.Life));
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------------------------------------------

const FString& UScctEffectsSubsystem::GetName(int32 Id)
{
	if (const FString* Known = Names.Find(Id))
	{
		return *Known;
	}
	TCHAR Buffer[256];
	const int32 Length = Id != 0 && Events != nullptr ? Events->GetName(Id, Buffer, UE_ARRAY_COUNT(Buffer)) : 0;
	return Names.Add(Id, Length > 0 ? FString(Buffer) : FString());
}

FName UScctEffectsSubsystem::FindDrawnBone(const AScctPawnProxy& Proxy, int32 BoneId)
{
	// The spy's and the merc's models are different meshes, so the answer is kept per kind.
	const int64 Key = (static_cast<int64>(Proxy.GetPawn().Kind) << 32) | static_cast<uint32>(BoneId);
	if (const FName* Known = DrawnBones.Find(Key))
	{
		return *Known;
	}
	return DrawnBones.Add(Key, Proxy.FindMeshBoneName(GetName(BoneId)));
}

UScctEffectsSubsystem::EStyle UScctEffectsSubsystem::FindStyle(int32 ClassId)
{
	if (const EStyle* Known = Styles.Find(ClassId))
	{
		return *Known;
	}

	// The class names are the original's (French and English mixed): the first word that fits decides.
	static const TPair<const TCHAR*, EStyle> Words[] = {
		{ TEXT("Douille"), EStyle::None },        // shell casings: drawn with the gun, not here
		{ TEXT("FootPrint"), EStyle::None },
		{ TEXT("FlashBang"), EStyle::FlashBang },
		{ TEXT("Explosion"), EStyle::Explosion },
		{ TEXT("MineProxEmitter"), EStyle::Explosion },
		{ TEXT("Chaff"), EStyle::Chaff },
		{ TEXT("Flare"), EStyle::Flare },
		{ TEXT("SmokeCanon"), EStyle::Wisp },
		{ TEXT("Launch"), EStyle::Wisp },
		{ TEXT("Poison"), EStyle::Gas },
		{ TEXT("StickyCam"), EStyle::Gas },
		{ TEXT("Smoke"), EStyle::Smoke },
		{ TEXT("Phospho"), EStyle::Smoke },
		{ TEXT("Extincteur"), EStyle::Smoke },
		{ TEXT("Steam"), EStyle::Smoke },
		{ TEXT("Shock"), EStyle::Electric },
		{ TEXT("Electric"), EStyle::Electric },
		{ TEXT("Malfunction"), EStyle::Electric },
		{ TEXT("Tazer"), EStyle::Electric },
		{ TEXT("Spark"), EStyle::Sparks },
		{ TEXT("Water"), EStyle::Water },
		{ TEXT("Sprinkler"), EStyle::Water },
		{ TEXT("FlicFloc"), EStyle::Water },
	};
	const FString& Name = GetName(ClassId);
	EStyle Style = Name.IsEmpty() ? EStyle::None : EStyle::Puff;
	for (const TPair<const TCHAR*, EStyle>& Word : Words)
	{
		if (Name.Contains(Word.Key))
		{
			Style = Word.Value;
			break;
		}
	}
	return Styles.Add(ClassId, Style);
}

UStaticMesh* UScctEffectsSubsystem::FindMesh(int32 MeshId)
{
	if (MeshId == 0)
	{
		return nullptr;
	}
	if (const TObjectPtr<UStaticMesh>* Known = Meshes.Find(MeshId))
	{
		return *Known;
	}

	// "SGameplayObjects.Gadgets.frag_grenade" was imported as the mesh "SGameplayObjects_frag_grenade".
	const FString& Path = GetName(MeshId);
	int32 FirstDot = INDEX_NONE, LastDot = INDEX_NONE;
	UStaticMesh* Mesh = nullptr;
	if (Path.FindChar(TEXT('.'), FirstDot) && Path.FindLastChar(TEXT('.'), LastDot))
	{
		if (const FSoftObjectPath* Asset = MeshAssets.Find(NameKey(Path.Left(FirstDot) + Path.Mid(LastDot + 1))))
		{
			Mesh = Cast<UStaticMesh>(Asset->TryLoad());
		}
	}
	if (Mesh == nullptr)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: no imported mesh for '%s' (Tools/Effects/build_effect_assets.py imports them); a stand-in shape is drawn"), *Path);
	}
	return Meshes.Add(MeshId, Mesh);
}

// ---------------------------------------------------------------------------------------------------------------
// For other features
// ---------------------------------------------------------------------------------------------------------------

void UScctEffectsSubsystem::SpawnSparkBurst(const FVector& At, const FVector& Normal, int32 Count)
{
	if (!bSceneReady || !IsValid(Host))
	{
		return;
	}
	const FVector Out = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::DownVector);
	SpawnSparks(At, Out, Count, FLinearColor(16.0f, 20.0f, 34.0f), 420.0f);
	AcquireLight(At + Out * 10.0, FLinearColor(0.55f, 0.7f, 1.0f), 30.0f, 350.0f, 0.1f, false);
}

void UScctEffectsSubsystem::SpawnGlassBurst(const FVector& At, const FVector& Direction)
{
	if (!bSceneReady || !IsValid(Host))
	{
		return;
	}
	// Shards read as cold glints that fall fast; the fine dust hangs a moment where the pane was.
	const FVector Out = Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::DownVector);
	SpawnSparks(At, Out, 36, FLinearColor(5.0f, 6.0f, 6.5f), 520.0f);
	SpawnDust(At, Out, 8, FLinearColor(0.82f, 0.87f, 0.9f, 0.3f), 55.0f);
}
