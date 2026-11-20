#include "Play/ScctEquipmentSubsystem.h"

#include "Play/ScctEchelonLightFilter.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureLightProfile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Hud/ScctVisionSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctEquipmentData.h"
#include "Play/ScctExposure.h"
#include "Play/ScctFirstPersonView.h"
#include "Play/ScctLaserBeam.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Play/ScctViewPawn.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctWeaponAttachmentsApi.h"

namespace
{
	constexpr int32 MaxEquipment = 64;
	constexpr int32 MaxAttachments = 192;
	constexpr int32 MaxAttachmentBones = 160;

	// How far first-person primitives are pulled towards the camera. The muzzle is up to 76 cm from the eye and
	// a wall can be as near as the pawn's collision radius; at this scale the whole model stays nearer than that.
	constexpr float FirstPersonScale = 0.42f;

	// The torch. The original's brightness (304) is a number for its baked, unshadowed projector; as a physical
	// spot light under Lumen this many candelas per unit gives a beam that owns a dark room without washing out
	// a lit one. Tuned by eye on AquaE and MuseE.
	constexpr float TorchCandelasPerBrightness = 0.8f;
	constexpr float TorchInnerCone = 0.2f;          // of the outer half angle: a hot centre that falls away to the rim
	constexpr float TorchFogScattering = 12.0f;     // the beam hangs in the level's volumetric fog
	constexpr float StockTorchBrightness = 304.0f;
	const FLinearColor LaserColor(1.0f, 0.03f, 0.02f);
	const FLinearColor MountedLaserColor(0.055f, 1.0f, 0.025f);
	constexpr double LaserRibbonWidth = 3.6;        // cm, including the faint outer glow
	constexpr float LaserBeamWidth = 0.7f;          // cm
	constexpr float LaserDotWidth = 2.2f;           // cm
	constexpr float LaserBeamIntensity = 8.0f;
	constexpr float LaserBeamSharpness = 1.7f;
	constexpr float LaserDotIntensity = 160.0f;
	constexpr float MountedBeamWidthRatio = 0.33f;
	constexpr float MountedBeamGain = 0.33f;
	constexpr float MountedDotWidth = 0.75f;
	constexpr float MountedDotIntensity = 24.0f;

	// Trace the posed outlet on its own axis. In first person this transform already includes the
	// renderer's scale and FOV. Only the short obstruction check starts at the holder's eye or hand.
	int32 TraceMountedLaser(int32 HolderId, const FVector& Holder, const FTransform& Emitter, FVector& End)
	{
		const FVector Origin = Emitter.GetLocation();
		const FVector Direction = Emitter.GetUnitAxis(EAxis::X);
		const auto* Api = ScctFeature<FScctLaserTraceApi>();
		float Distance = 0.0f;
		const FScctMountedLaserRay Ray{float(Holder.X), float(Holder.Y), float(Holder.Z),
			float(Origin.X), float(Origin.Y), float(Origin.Z), float(Direction.X), float(Direction.Y), float(Direction.Z)};
		const int32 Result = Api ? Api->Trace(HolderId, &Ray, &Distance) : -1;
		End = Origin + Direction * Distance;
		return Result;
	}

	const TCHAR* const CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const SpherePath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	// For tuning with the game running; the defaults are the values above.
	TAutoConsoleVariable<float> CVarTorchCandelas(TEXT("scct.Torch.CandelasPerBrightness"), TorchCandelasPerBrightness,
		TEXT("Candelas of a merc's torch per unit of the original's light brightness (304 for the torch)."));
	TAutoConsoleVariable<float> CVarTorchFog(TEXT("scct.Torch.FogScattering"), TorchFogScattering,
		TEXT("How strongly a merc's torch beam shows in the level's volumetric fog."));
	TAutoConsoleVariable<float> CVarMercTorchPeak(TEXT("scct.Merc.TorchPeak"), 7.0f,
		TEXT("Maximum direct intensity of the mercenary's first-person torch, in Unreal's unitless falloff mode. Follows the gadget's brightness."));
	TAutoConsoleVariable<float> CVarFirstPersonScale(TEXT("scct.FirstPerson.Scale"), FirstPersonScale,
		TEXT("How far the first-person view model is pulled towards the camera (0.05 to 1). Lower keeps it out of walls; too low and the near plane cuts the hands."));

	struct FFlagName
	{
		int32 Flag;
		const TCHAR* Name;
	};

	// The flags worth a log line when they change: what is drawn differently.
	const FFlagName LoggedFlags[] = {
		{ EScctEquipmentFlags::TorchOn, TEXT("TorchOn") },
		{ EScctEquipmentFlags::TorchInCamera, TEXT("TorchInCamera") },
		{ EScctEquipmentFlags::LaserOn, TEXT("LaserOn") },
		{ EScctEquipmentFlags::LaserBeam, TEXT("LaserBeam") },
		{ EScctEquipmentFlags::LaserDot, TEXT("LaserDot") },
		{ EScctEquipmentFlags::GogglesGlow, TEXT("GogglesGlow") },
		{ EScctEquipmentFlags::GogglesDown, TEXT("GogglesDown") },
		{ EScctEquipmentFlags::WeaponInHand, TEXT("WeaponInHand") },
		{ EScctEquipmentFlags::FirstPerson, TEXT("FirstPerson") },
		{ EScctEquipmentFlags::InScope, TEXT("InScope") },
		{ EScctEquipmentFlags::Reloading, TEXT("Reloading") },
	};

	const TCHAR* SlotName(int32 Slot)
	{
		static const TCHAR* const Names[] = { TEXT("weapon"), TEXT("carried"), TEXT("goggle glow") };
		return Slot >= 0 && Slot < UE_ARRAY_COUNT(Names) ? Names[Slot] : TEXT("?");
	}

	void SetIntCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByGameSetting);
		}
	}
}

bool UScctEquipmentSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctEquipmentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UScctSessionSubsystem* Match = Collection.InitializeDependency<UScctSessionSubsystem>();
	if (Match == nullptr)
	{
		return;
	}
	Session = Match;
	StartedHandle = Match->OnMatchStarted.AddUObject(this, &UScctEquipmentSubsystem::OnMatchStarted);
	StoppedHandle = Match->OnMatchStopped.AddUObject(this, &UScctEquipmentSubsystem::OnMatchStopped);
	ChangedHandle = Match->OnSessionChanged.AddUObject(this, &UScctEquipmentSubsystem::OnSessionChanged);
	FrameHandle = Match->OnFrame.AddUObject(this, &UScctEquipmentSubsystem::OnFrame);

	if (FParse::Value(FCommandLine::Get(), TEXT("ScctForceCloak="), ForcedCloak))
	{
		ForcedCloak = FMath::Clamp(ForcedCloak, 0.0f, 1.0f);
	}
	bNoViewModel = FParse::Param(FCommandLine::Get(), TEXT("ScctNoViewModel"));
	bLaserProbe = FParse::Param(FCommandLine::Get(), TEXT("ScctLaserProbe"));
}

void UScctEquipmentSubsystem::Deinitialize()
{
	if (UScctSessionSubsystem* Match = Session.Get())
	{
		Match->OnMatchStarted.Remove(StartedHandle);
		Match->OnMatchStopped.Remove(StoppedHandle);
		Match->OnSessionChanged.Remove(ChangedHandle);
		Match->OnFrame.Remove(FrameHandle);
	}
	DropEverything();
	Super::Deinitialize();
}

void UScctEquipmentSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	// The camo suit keeps the materials it took off the meshes; nothing else holds them while it is on.
	CastChecked<UScctEquipmentSubsystem>(InThis)->CloakLook.AddReferencedObjects(Collector);
	Super::AddReferencedObjects(InThis, Collector);
}

void UScctEquipmentSubsystem::OnMatchStarted()
{
	FString Error;
	const FScctEquipmentApi* Api = ScctFeature<FScctEquipmentApi>(&Error);
	if (Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Equipment: the bridge has no equipment table (%s); weapons, torches, lasers and the first-person view are not drawn"), *Error);
		bReportedNoTable = true;
		return;
	}

	Weapons = UScctWeaponSet::Get();
	GlowMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Weapons/Materials/M_ScctGlow.M_ScctGlow"), nullptr, LOAD_NoWarn);
	LaserBeamMaterial = LoadObject<UMaterialInterface>(nullptr, ScctLaserBeam::MaterialPath, nullptr, LOAD_NoWarn);
	MercTorchProfile = LoadObject<UTextureLightProfile>(nullptr, TEXT("/Game/OpenSCCT/Weapons/Materials/MercTorchProfile.MercTorchProfile"));
	if (GlowMaterial == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Equipment: no /Game/OpenSCCT/Weapons/Materials/M_ScctGlow (Tools/Import/Import-Characters.ps1 builds it); laser dots and torch coronas are not drawn"));
	}

	// Every mesh now, so the first shot or the first drawn pistol does not hitch on a load. Not for the match behind
	// the main menu: most of the catalog is first-person models nobody sees there, and loading it all would hold
	// the menu for most of a second; what the bots carry is loaded as it is first drawn.
	if (const UScctSessionSubsystem* StartedSession = GetWorld()->GetSubsystem<UScctSessionSubsystem>(); StartedSession == nullptr || !StartedSession->IsBackdrop())
	{
		Models.LoadCatalog(*Api);
	}

	// The view model shadows itself from every light, the torch and the muzzle flash included.
	SetIntCVar(TEXT("r.FirstPerson.SelfShadow"), 1);
	SetIntCVar(TEXT("r.FirstPerson.SelfShadow.LightTypes"), 2);

	Equipment.SetNumUninitialized(MaxEquipment);
	Attachments.SetNumUninitialized(MaxAttachments);
	PoseBuffer.SetNumUninitialized(MaxAttachmentBones);
}

void UScctEquipmentSubsystem::OnMatchStopped()
{
	DropEverything();
}

void UScctEquipmentSubsystem::OnSessionChanged(int32 OldSessionId, int32 NewSessionId)
{
	// The proxies of the match before are gone and with them everything that hung on them.
	DropEverything();
}

void UScctEquipmentSubsystem::DropEverything()
{
	for (TPair<int32, FPawnGear>& Pair : Gear)
	{
		DropGear(Pair.Value);
	}
	Gear.Reset();
	EquipmentCount = 0;
	FirstPerson = {};
	FirstPersonShots = -1;
	FirstPersonActor = 0;
	if (IsValid(FirstPersonView))
	{
		FirstPersonView->Hide(0.0f);
		FirstPersonView->Destroy();
	}
	FirstPersonView = nullptr;
	LaserVision.Drop();
	CloakLook.Trim();
}

void UScctEquipmentSubsystem::DropGear(FPawnGear& Dropped)
{
	for (FAttachmentView& Slot : Dropped.Slots)
	{
		Slot.View.Destroy();
	}
	for (USceneComponent* Component : { static_cast<USceneComponent*>(Dropped.Torch.Get()), static_cast<USceneComponent*>(Dropped.Corona.Get()),
		static_cast<USceneComponent*>(Dropped.Beam.Get()), static_cast<USceneComponent*>(Dropped.Dot.Get()), static_cast<USceneComponent*>(Dropped.DotLight.Get()) })
	{
		if (Component != nullptr)
		{
			Component->DestroyComponent();
		}
	}
	if (AScctPawnProxy* Proxy = Dropped.Proxy.Get())
	{
		CloakLook.Apply(*Proxy->GetBodyMesh(), 0.0f);
	}
	Dropped = FPawnGear();
}

const FScctEquipment* UScctEquipmentSubsystem::FindEquipment(int32 ActorId) const
{
	for (int32 Index = 0; Index < EquipmentCount; ++Index)
	{
		if (Equipment[Index].ActorId == ActorId)
		{
			return &Equipment[Index];
		}
	}
	return nullptr;
}

const USceneComponent* UScctEquipmentSubsystem::GetCarriedWeapon(int32 ActorId) const
{
	const FPawnGear* Found = Gear.Find(ActorId);
	return Found ? Found->Slots[EScctAttachmentSlot::Carried].View.GetBody() : nullptr;
}

bool UScctEquipmentSubsystem::FindMuzzle(int32 ActorId, FTransform& OutWorld) const
{
	if (ActorId == FirstPerson.ActorId && IsValid(FirstPersonView) && FirstPersonView->IsShown())
	{
		if (!FirstPersonView->FindMuzzle(OutWorld))
		{
			return false;
		}
		OutWorld.SetLocation(FirstPersonView->ProjectSocket(OutWorld.GetLocation(), DrawnFirstPersonScale));
		return true;
	}
	const FPawnGear* Found = Gear.Find(ActorId);
	return Found != nullptr && Found->Slots[EScctAttachmentSlot::Weapon].View.FindMuzzle(OutWorld);
}

bool UScctEquipmentSubsystem::FindCasingPort(int32 ActorId, FTransform& OutWorld) const
{
	if (ActorId == FirstPerson.ActorId && IsValid(FirstPersonView) && FirstPersonView->IsShown())
	{
		const bool bFound = FirstPersonView->FindCasingPort(OutWorld);
		if (bFound && FirstPersonView->UsesSightProjection())
		{
			OutWorld.SetLocation(FirstPersonView->ProjectSocket(OutWorld.GetLocation(), DrawnFirstPersonScale));
		}
		return bFound;
	}
	const FPawnGear* Found = Gear.Find(ActorId);
	if (Found == nullptr)
	{
		return false;
	}
	const FScctEquipmentView& Weapon = Found->Slots[EScctAttachmentSlot::Weapon].View;
	return Weapon.IsShown() && Weapon.GetBody()->IsVisible() && Weapon.FindPoint(TEXT("EjectionPort"), TEXT("FX_DOUILLE"), OutWorld);
}

bool UScctEquipmentSubsystem::DrawsLocalMuzzleFlash() const
{
	return IsValid(FirstPersonView) && FirstPersonView->IsShown();
}

FScctWeaponMotion UScctEquipmentSubsystem::MotionOf(const FScctEquipment& Pawn)
{
	FScctWeaponMotion Motion;
	Motion.ShotCount = Pawn.ShotCount;
	Motion.RateOfFire = Pawn.RateOfFire;
	Motion.bFiring = (Pawn.Flags & EScctEquipmentFlags::Firing) != 0;
	Motion.bReloading = (Pawn.Flags & EScctEquipmentFlags::Reloading) != 0;
	return Motion;
}

float UScctEquipmentSubsystem::VisibleCloak(float Amount) const
{
	const UScctVisionSubsystem* Vision = UScctVisionSubsystem::Find(GetWorld());
	if (Vision && (Vision->IsHeatOrEmf() || Vision->GetVisionMode() == 37))
	{
		return 0.0f;
	}
	return ForcedCloak >= 0.0f ? ForcedCloak : Amount;
}

FTransform UScctEquipmentSubsystem::GetCamera() const
{
	const FScctBridgeView& View = Session->GetView();
	return FTransform(FRotator(View.Pitch, View.Yaw, View.Roll), FVector(View.X, View.Y, View.Z));
}

void UScctEquipmentSubsystem::OnFrame(float DeltaSeconds)
{
	UScctSessionSubsystem* Match = Session.Get();
	FString Error;
	const FScctEquipmentApi* Api = ScctFeature<FScctEquipmentApi>(&Error);
	if (Match == nullptr || Api == nullptr || Equipment.Num() == 0)
	{
		if (!bReportedNoTable && Api == nullptr)
		{
			bReportedNoTable = true;
			UE_LOG(LogOpenSCCT, Error, TEXT("Equipment: the bridge has no equipment table (%s)"), *Error);
		}
		return;
	}

	FrameSeconds = DeltaSeconds;

	// The order the bridge asks for: equipment, attachments (before any attachment pose), first person.
	EquipmentCount = FMath::Clamp(Api->GetEquipment(Equipment.GetData(), Equipment.Num()), 0, Equipment.Num());
	const int32 AttachmentCount = FMath::Clamp(Api->GetAttachments(Attachments.GetData(), Attachments.Num()), 0, Attachments.Num());

	for (TPair<int32, FPawnGear>& Pair : Gear)
	{
		Pair.Value.bSeen = false;
		for (FAttachmentView& Slot : Pair.Value.Slots)
		{
			Slot.bSeen = false;
		}
	}

	const auto GearOf = [this, Match](int32 ActorId) -> FPawnGear*
	{
		AScctPawnProxy* Proxy = Match->FindProxy(ActorId);
		if (Proxy == nullptr || !Proxy->HasModel())
		{
			return nullptr;
		}
		FPawnGear& Found = Gear.FindOrAdd(ActorId);
		if (Found.Proxy.Get() != Proxy)
		{
			// A new body under an id seen before.
			DropGear(Found);
			Found.Proxy = Proxy;
		}
		Found.bSeen = true;
		return &Found;
	};

	const FScctWeaponAttachmentsApi* AttachmentApi = ScctFeature<FScctWeaponAttachmentsApi>();
	for (int32 Index = 0; Index < EquipmentCount; ++Index)
	{
		const FScctEquipment& Pawn = Equipment[Index];
		if (FPawnGear* Found = GearOf(Pawn.ActorId))
		{
			Found->AttachmentMask = (Pawn.Flags & EScctEquipmentFlags::Merc) && AttachmentApi
				? AttachmentApi->GetEquippedForActor(Pawn.ActorId) : 0;
		}
	}
	for (int32 Index = 0; Index < AttachmentCount; ++Index)
	{
		const FScctAttachment& Attachment = Attachments[Index];
		if (Attachment.Slot < 0 || Attachment.Slot >= UE_ARRAY_COUNT(FPawnGear::Slots))
		{
			continue;
		}
		if (FPawnGear* Found = GearOf(Attachment.ActorId))
		{
			UpdateAttachment(*Api, *Found, *Found->Proxy, Attachment);
		}
	}

	// Before the lasers: the local player's beam starts at the view model's muzzle.
	UpdateFirstPerson(*Api, DeltaSeconds);
	const bool bViewModelShown = IsValid(FirstPersonView) && FirstPersonView->IsShown();

	LaserVision.BeginFrame();
	for (int32 Index = 0; Index < EquipmentCount; ++Index)
	{
		const FScctEquipment& Pawn = Equipment[Index];
		FPawnGear* Found = GearOf(Pawn.ActorId);
		if (Found == nullptr)
		{
			continue;
		}

		AScctPawnProxy& Proxy = *Found->Proxy;
		// The proxy's own flag, not the equipment's: a tour camera looking at the player draws his body.
		const bool bFromEye = Proxy.IsFirstPerson() && !Match->IsTourView();
		UpdateTorch(*Found, Proxy, Pawn, bFromEye);
		UpdateLaser(*Found, Proxy, Pawn, bFromEye && bViewModelShown && Pawn.ActorId == FirstPerson.ActorId);

		const float Cloak = VisibleCloak(Pawn.Cloak);
		CloakLook.Apply(*Proxy.GetBodyMesh(), Cloak);
		for (int32 Slot : { EScctAttachmentSlot::Weapon, EScctAttachmentSlot::Carried })
		{
			Found->Slots[Slot].View.ForEachMesh([this, Cloak](UMeshComponent& Mesh) { CloakLook.Apply(Mesh, Cloak); });
		}
		LogChanges(*Api, *Found, Pawn);
	}
	LaserVision.EndFrame();

	for (auto It = Gear.CreateIterator(); It; ++It)
	{
		FPawnGear& Kept = It.Value();
		if (!Kept.bSeen || !Kept.Proxy.IsValid())
		{
			DropGear(Kept);
			It.RemoveCurrent();
			continue;
		}
		for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Kept.Slots); ++Slot)
		{
			if (!Kept.Slots[Slot].bSeen && Kept.Slots[Slot].View.IsShown())
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Equipment: pawn %d no longer has a %s attachment"), It.Key(), SlotName(Slot));
				Kept.Slots[Slot].View.Destroy();
				Kept.Slots[Slot] = FAttachmentView();
			}
		}
	}
	CloakLook.Trim();
}

void UScctEquipmentSubsystem::UpdateAttachment(const FScctEquipmentApi& Api, FPawnGear& Found, AScctPawnProxy& Proxy, const FScctAttachment& Attachment)
{
	FAttachmentView& Slot = Found.Slots[Attachment.Slot];
	Slot.bSeen = true;

	const bool bNewMesh = Slot.Mesh != Attachment.Mesh || !Slot.View.IsShown();
	if (bNewMesh)
	{
		const FScctWeaponPresentation* Row = Weapons != nullptr ? Weapons->Find(FName(*Models.Name(Api, Attachment.Class))) : nullptr;
		if (Row != nullptr && Row->bFirstPerson)
		{
			Row = nullptr;
		}
		const FScctEquipmentModel* Model = Models.Find(Api, Attachment.Mesh, Attachment.MeshPath, Row != nullptr && Row->Mesh.IsValid() ? &Row->Mesh : nullptr);
		if (Slot.Mesh != Attachment.Mesh)
		{
			// Said once per mesh, also when it cannot be drawn.
			UE_LOG(LogOpenSCCT, Log, TEXT("Equipment: pawn %d (%s) %s is '%s' (class %s) on bone '%s'%s"), Attachment.ActorId,
				Proxy.GetKind() == 1 ? TEXT("merc") : TEXT("spy"), SlotName(Attachment.Slot), *Models.Name(Api, Attachment.Mesh),
				*Models.Name(Api, Attachment.Class), *Models.Name(Api, Attachment.Bone), Model != nullptr ? TEXT("") : TEXT(": NOT DRAWN, no imported mesh"));
		}
		Slot.Mesh = Attachment.Mesh;
		Slot.Bone = 0;
		Slot.Skin = -1;
		Slot.bAnimated = false;
		Slot.View.Show(Proxy, Model, Row, false);
		if (Attachment.Slot == EScctAttachmentSlot::GoggleGlow)
		{
			// Light itself: it throws no shadow.
			Slot.View.ForEachMesh([](UMeshComponent& Mesh) { Mesh.SetCastShadow(false); });
		}
	}
	if (!Slot.View.IsShown())
	{
		return;
	}

	UPoseableMeshComponent* Body = Slot.View.GetBody();
	const FTransform Relative = FScctPose::ToUnreal(Attachment.ModelToBone);
	if (Slot.Bone != Attachment.Bone)
	{
		// The bone changes when a weapon is drawn or put away (hand and holster).
		if (Slot.Bone != 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Equipment: pawn %d %s moves to bone '%s'"), Attachment.ActorId, SlotName(Attachment.Slot), *Models.Name(Api, Attachment.Bone));
		}
		Slot.Bone = Attachment.Bone;
		if (!Proxy.AttachToBone(Body, Models.Name(Api, Attachment.Bone), Relative))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Equipment: pawn %d has no bone '%s' for its %s"), Attachment.ActorId, *Models.Name(Api, Attachment.Bone), SlotName(Attachment.Slot));
		}
		// What hangs on the attachment is part of the body as well.
		const bool bHiddenShadow = Proxy.GetBodyMesh()->bCastHiddenShadow;
		Slot.View.ForEachMesh([bHiddenShadow](UMeshComponent& Mesh) { Mesh.SetCastHiddenShadow(bHiddenShadow); });
	}
	else
	{
		Body->SetRelativeTransform(Relative);
	}

	const bool bAnimated = (Attachment.Flags & EScctAttachmentFlags::Animated) != 0;
	if (bAnimated)
	{
		const int32 Bones = FMath::Clamp(Api.GetAttachmentPose(Attachment.ActorId, Attachment.Slot, PoseBuffer.GetData(), PoseBuffer.Num()), 0, PoseBuffer.Num());
		Slot.View.ApplyPose(TConstArrayView<FScctBridgeBone>(PoseBuffer.GetData(), Bones));
	}
	else if (Slot.bAnimated)
	{
		Slot.View.ResetPose();
	}
	Slot.bAnimated = bAnimated;
	// A world weapon can be rigid in the hand with no animation clip of its own.
	if (Attachment.Slot == EScctAttachmentSlot::Weapon)
	{
		Slot.View.UpdateRailModule(Found.AttachmentMask);
	}
	else if (Attachment.Slot == EScctAttachmentSlot::Carried && Slot.View.SupportsWeaponAttachments())
	{
		const auto* AttachmentApi = ScctFeature<FScctWeaponAttachmentsApi>();
		Slot.View.UpdateRailModule(AttachmentApi ? AttachmentApi->GetHolsteredEquippedForActor(Attachment.ActorId) : 0);
	}
	if (Attachment.Slot == EScctAttachmentSlot::Weapon)
	{
		// A model drawn in place of the original: its own parts move with the simulation, and while the gun is
		// in the hands the body's left hand closes on its grip.
		if (const FScctEquipment* Pawn = Slot.View.HasReplacement() || Slot.View.HasSupportGrip() ? FindEquipment(Attachment.ActorId) : nullptr)
		{
			Slot.View.Animate(MotionOf(*Pawn), FrameSeconds);
			const bool bSupportHandIsFree = Slot.View.HasSupportGrip() && (Pawn->Flags & EScctEquipmentFlags::Reloading) != 0;
			if ((Pawn->Flags & EScctEquipmentFlags::WeaponInHand) != 0 && !bSupportHandIsFree)
			{
				Slot.View.HoldWithLeftHand(*Proxy.GetBodyMesh());
			}
		}
		Slot.View.LogProbe(Proxy.GetBodyMesh());
	}

	if (Slot.Skin != Attachment.Skin && Slot.View.GetModel() != nullptr)
	{
		// The goggle lenses change colour with the vision mode. Skin 0 is the mesh's own material.
		Slot.Skin = Attachment.Skin;
		UMaterialInterface* Skin = Attachment.Skin > 0 ? Models.FindSkin(Slot.View.GetModel()->Package, Models.Name(Api, Attachment.Skin)) : nullptr;
		if (Skin != nullptr || Attachment.Skin <= 0)
		{
			Body->SetMaterial(0, Skin);
		}
	}
}

UStaticMeshComponent* UScctEquipmentSubsystem::MakeGlowShape(AScctPawnProxy& Proxy, const TCHAR* MeshPath, const FLinearColor& Color,
	float Intensity, float Sharpness, UMaterialInterface* Material)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
	UMaterialInterface* Parent = Material != nullptr ? Material : GlowMaterial.Get();
	if (Mesh == nullptr || Parent == nullptr)
	{
		return nullptr;
	}

	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(&Proxy, NAME_None, RF_Transient);
	Shape->SetStaticMesh(Mesh);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCanEverAffectNavigation(false);
	Shape->SetCastShadow(false);
	Shape->SetupAttachment(Proxy.GetRootComponent());
	Shape->SetUsingAbsoluteLocation(true);
	Shape->SetUsingAbsoluteRotation(true);
	Shape->SetUsingAbsoluteScale(true);
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, Shape);
	Instance->SetVectorParameterValue(TEXT("Color"), Color);
	Instance->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	if (Material == nullptr)
	{
		Instance->SetScalarParameterValue(TEXT("Sharpness"), Sharpness);
	}
	Shape->SetMaterial(0, Instance);
	Shape->RegisterComponent();
	Proxy.AddInstanceComponent(Shape);
	return Shape;
}

void UScctEquipmentSubsystem::UpdateTorch(FPawnGear& Found, AScctPawnProxy& Proxy, const FScctEquipment& Pawn, bool bFromEye)
{
	const bool bOn = (Pawn.Flags & EScctEquipmentFlags::TorchOn) != 0 && Proxy.IsShown();
	USpotLightComponent* Torch = Found.Torch.Get();
	if (!bOn)
	{
		if (Torch != nullptr && Torch->IsVisible())
		{
			Torch->SetVisibility(false);
		}
		if (Found.Corona.IsValid() && Found.Corona->IsVisible())
		{
			Found.Corona->SetVisibility(false);
		}
		return;
	}

	if (Torch == nullptr)
	{
		Torch = NewObject<USpotLightComponent>(&Proxy, NAME_None, RF_Transient);
		Torch->SetMobility(EComponentMobility::Movable);
		Torch->SetupAttachment(Proxy.GetRootComponent());
		Torch->SetUsingAbsoluteLocation(true);
		Torch->SetUsingAbsoluteRotation(true);
		Torch->SetIntensityUnits(ELightUnits::Candelas);
		Torch->SetCastShadows(true);
		// The torch follows aim directly. MegaLights' temporal denoising leaves rings at its previous positions.
		Torch->bAllowMegaLights = false;
		Torch->SetSourceRadius(1.5f);
		UScctEchelonLightFilter::RouteNewWorldLight(*Torch);
		Torch->RegisterComponent();
		Proxy.AddInstanceComponent(Torch);
		Found.Torch = Torch;
	}

	// Camnet supplies its own projector. In the world, use the moving weapon outlet for
	// first-person and third-person alike. The bridge carries the fallback for a missing model.
	FVector Location(Pawn.TorchX, Pawn.TorchY, Pawn.TorchZ);
	FQuat Rotation = FRotator(Pawn.TorchPitch, Pawn.TorchYaw, Pawn.TorchRoll).Quaternion();
	const bool bInCamera = (Pawn.Flags & EScctEquipmentFlags::TorchInCamera) != 0;
	bool bCorona = (Pawn.Flags & EScctEquipmentFlags::TorchCorona) != 0;
	// Thermal and EMF draw no coronas (the goggles say so), or the bright card would read as hot or glowing.
	const UScctVisionSubsystem* Vision = UScctVisionSubsystem::Find(GetWorld());
	bCorona = bCorona && (Vision == nullptr || !Vision->ShouldHideCoronasAndSky());
	bool bMercCamera = false;
	if (bFromEye && !bInCamera)
	{
		bCorona = false;
		const APlayerController* Player = GetWorld()->GetFirstPlayerController();
		const auto* View = Player ? Cast<AScctViewPawn>(Player->GetViewTarget()) : nullptr;
		bMercCamera = View && View->UsesMercCamera();
	}
	if (!bInCamera)
	{
		const FScctEquipmentView& Gun = Found.Slots[EScctAttachmentSlot::Weapon].View;
		FTransform Lamp;
		bool bMounted = bFromEye && IsValid(FirstPersonView) && FirstPerson.ActorId == Pawn.ActorId
			&& FirstPersonView->FindMercEmitter(true, DrawnFirstPersonScale, Lamp);
		if (!bMounted && (Pawn.Flags & EScctEquipmentFlags::WeaponInHand) != 0 && Gun.IsShown())
		{
			bMounted = Gun.FindMercEmitter(true, Lamp);
		}
		if (bMounted)
		{
			Location = Lamp.GetLocation();
			Rotation = Lamp.GetRotation();
			if (bFromEye)
			{
				// First-person primitives can draw across level collision. Keep the real shadowed light
				// on the player's side only when the level actually intersects the camera-to-lamp segment.
				const FVector Eye = GetCamera().GetLocation();
				float HitDistance;
				if (FScctExposure::TraceLevel(Eye, Location, HitDistance))
				{
					Location = Eye + (Location - Eye).GetSafeNormal() * FMath::Max(0.0f, HitDistance - 2.0f);
				}
			}
		}
		else
		{
			const TConstArrayView<int32> BoneMap = Proxy.GetBoneMap();
			const int32 MeshBone = BoneMap.IsValidIndex(Pawn.TorchBone) ? BoneMap[Pawn.TorchBone] : INDEX_NONE;
			if (MeshBone != INDEX_NONE)
			{
				const FTransform Bone = Proxy.GetBodyMesh()->GetBoneTransform(MeshBone);
				Location = Bone.GetLocation();
				if ((Pawn.Flags & EScctEquipmentFlags::TorchFollowsBone) != 0)
				{
					Rotation = Bone.GetRotation();
				}
			}
		}
	}

	static const IConsoleVariable* EffectsQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
	// Both paths use one shadowed light with the torch's original reach. Low uses the native cone.
	const bool bUseProfile = bMercCamera && MercTorchProfile && (!EffectsQuality || EffectsQuality->GetInt() > 0);
	Torch->SetIESTexture(bUseProfile ? MercTorchProfile.Get() : nullptr);
	Torch->SetWorldLocationAndRotation(Location, Rotation);
	Torch->SetOuterConeAngle(Pawn.TorchHalfAngle);
	Torch->SetInnerConeAngle(Pawn.TorchHalfAngle * (bUseProfile ? 0.72f : TorchInnerCone));
	Torch->SetAttenuationRadius(Pawn.TorchRange);
	Torch->SetLightColor(FLinearColor(Pawn.TorchR, Pawn.TorchG, Pawn.TorchB));
	Torch->SetUseInverseSquaredFalloff(!bMercCamera);
	Torch->SetIntensityUnits(bMercCamera ? ELightUnits::Unitless : ELightUnits::Candelas);
	// The first-person gun lamp has a bounded falloff. With inverse-square falloff a tile under the
	// player clips long before the spot is bright enough on a door several metres away. In this
	// mode the GPU evaluates (1 - distance² / range²)² per surface, bounded even at the emitter.
	const float RatedCandelas = Pawn.TorchBrightness * CVarTorchCandelas.GetValueOnGameThread();
	float Intensity = RatedCandelas;
	if (bMercCamera)
	{
		Torch->SetLightFalloffExponent(2.0f);
		Intensity = RatedCandelas / (StockTorchBrightness * TorchCandelasPerBrightness)
			* FMath::Max(0.0f, CVarMercTorchPeak.GetValueOnGameThread());
		// UE applies the IES import multiplier even with UseIESBrightness off. Use just the angular
		// distribution, so a reimport or switching between the profile and Low keeps the peak value.
		if (bUseProfile && MercTorchProfile->TextureMultiplier > 0.0f)
		{
			Intensity /= MercTorchProfile->TextureMultiplier;
		}
	}
	Torch->SetIntensity(Intensity);
	// The reference has a bright spot on the receiver. Applying the original 12x fog enhancement to
	// that peak turns the air white when the lamp crosses a dark doorway.
	const float Fog = CVarTorchFog.GetValueOnGameThread();
	Torch->SetVolumetricScatteringIntensity(bMercCamera ? FMath::Min(Fog, 1.0f) : Fog);
	if (!Torch->IsVisible())
	{
		Torch->SetVisibility(true);
	}

	// The lamp itself as others see it: a small hot point at the source.
	if (bCorona && !Found.Corona.IsValid())
	{
		Found.Corona = MakeGlowShape(Proxy, SpherePath, FLinearColor(Pawn.TorchR, Pawn.TorchG, Pawn.TorchB), 60.0f, 2.0f);
	}
	if (UStaticMeshComponent* Corona = Found.Corona.Get())
	{
		Corona->SetWorldLocation(Location + Rotation.GetForwardVector() * 2.0);
		Corona->SetWorldScale3D(FVector(0.05));
		if (Corona->IsVisible() != bCorona)
		{
			Corona->SetVisibility(bCorona);
		}
	}
}

void UScctEquipmentSubsystem::UpdateLaser(FPawnGear& Found, AScctPawnProxy& Proxy, const FScctEquipment& Pawn, bool bFromViewModel)
{
	bool bBeam = (Pawn.Flags & EScctEquipmentFlags::LaserBeam) != 0 && Proxy.IsShown();
	bool bDot = bBeam || ((Pawn.Flags & EScctEquipmentFlags::LaserDot) != 0 && Proxy.IsShown());
	const bool bMerc = (Pawn.Flags & EScctEquipmentFlags::Merc) != 0;
	const bool bInCamera = (Pawn.Flags & EScctEquipmentFlags::TorchInCamera) != 0;
	const bool bEquipped = (Found.AttachmentMask & EScctWeaponAttachment::Laser) != 0;
	if (bMerc && !bInCamera && !bEquipped)
	{
		bBeam = bDot = false;
	}
	const bool bMounted = bMerc && bEquipped && !bInCamera;

	FVector End(Pawn.LaserEndX, Pawn.LaserEndY, Pawn.LaserEndZ);
	FVector Start(Pawn.LaserStartX, Pawn.LaserStartY, Pawn.LaserStartZ);
	bool bAttachedMuzzle = false;
	bool bMountedRay = false;
	if (bBeam)
	{
		FTransform Source;
		FVector Holder = FVector::ZeroVector;
		if (bMounted)
		{
			if (bFromViewModel && FirstPersonView->FindMercEmitter(false, DrawnFirstPersonScale, Source, true))
			{
				Start = Source.GetLocation();
				bAttachedMuzzle = true;
				bMountedRay = true;
				Holder = GetCamera().GetLocation();
			}
			else if (!Proxy.IsFirstPerson()
				&& (Pawn.Flags & EScctEquipmentFlags::WeaponInHand) != 0)
			{
				const FScctEquipmentView& Gun = Found.Slots[EScctAttachmentSlot::Weapon].View;
				if (Found.Slots[EScctAttachmentSlot::Weapon].bSeen && Gun.IsShown() && Gun.GetBody()->IsVisible()
					&& Gun.FindMercEmitter(false, Source, true))
				{
					Start = Source.GetLocation();
					bAttachedMuzzle = true;
					bMountedRay = true;
					Holder = Gun.GetBody()->GetComponentLocation();
				}
			}
		}
		else if (bFromViewModel && FirstPersonView->FindLaserStart(Source))
		{
			Start = Source.GetLocation();
		}
		else if (!bFromViewModel && (Pawn.Flags & EScctEquipmentFlags::WeaponInHand) != 0
			&& Found.Slots[EScctAttachmentSlot::Weapon].View.FindMuzzle(Source))
		{
			Start = Source.GetLocation();
		}
		if (bMountedRay)
		{
			// A clear ray has no impact dot. If cover already separates the holder and outlet,
			// nothing emerges on the far side. The legacy rifle/camera traces use a different axis.
			bDot = TraceMountedLaser(Pawn.ActorId, Holder, Source, End) > 0;
		}
		else if (bMounted)
		{
			// On a ladder or the first frame of a draw there may be no posed outlet. Wait for it
			// before showing either effect, so the dot never jumps from a cached simulation hit.
			bBeam = false;
			bDot = false;
		}
	}

	const FVector Along = End - Start;
	const double Length = Along.Size();
	const FVector Direction = Length > UE_KINDA_SMALL_NUMBER ? Along / Length : FVector::ForwardVector;
	const bool bDrawBeam = bBeam && Length > 1.0;

	if (bLaserProbe && bBeam && &Proxy == Session->FindLocalProxy())
	{
		ProbeLaser(Start, End);
	}

	// Thermal draws both weapon beams hot and EMF the merc's sight as a dark grey streak; the goggles' own beam then
	// stands in for the red one, whose added light the mode's material would read as brightness.
	const UScctVisionSubsystem* Goggles = UScctVisionSubsystem::Find(GetWorld());
	const int32 GoggleMode = Goggles != nullptr ? Goggles->GetVisionMode() : 0;
	const bool bNormalVision = GoggleMode == 0 || GoggleMode == 5;
	const bool bGreen = bNormalVision && bMounted;
	const FLinearColor& BeamColor = bGreen ? MountedLaserColor : LaserColor;
	const float BeamWidthRatio = bGreen ? MountedBeamWidthRatio : 1.0f;
	const float BeamIntensity = (LaserBeamMaterial ? 1.0f : LaserBeamIntensity) * (bGreen ? MountedBeamGain : 1.0f);
	const float DotIntensity = bGreen ? MountedDotIntensity : LaserDotIntensity;
	if (Found.bGreenLaser != bGreen)
	{
		// Existing instances survive weapon and camera changes.
		const auto SetAppearance = [&BeamColor](UStaticMeshComponent* Shape, float Intensity)
		{
			if (auto* Material = Shape ? Cast<UMaterialInstanceDynamic>(Shape->GetMaterial(0)) : nullptr)
			{
				Material->SetVectorParameterValue(TEXT("Color"), BeamColor);
				Material->SetScalarParameterValue(TEXT("Intensity"), Intensity);
			}
		};
		SetAppearance(Found.Beam.Get(), BeamIntensity);
		SetAppearance(Found.Dot.Get(), DotIntensity);
		if (Found.DotLight.IsValid())
		{
			Found.DotLight->SetLightColor(BeamColor);
		}
		Found.bGreenLaser = bGreen;
	}
	const int32 GoggleStencil = bDrawBeam ? FScctLaserVision::StencilFor(GoggleMode, bMerc) : 0;
	if (GoggleStencil != 0)
	{
		LaserVision.Show(*GetWorld(), Pawn.ActorId, Start, End, GetCamera().GetLocation(), GoggleStencil);
	}
	if (Found.LaserVisionStencil != GoggleStencil)
	{
		Found.LaserVisionStencil = GoggleStencil;
		UE_LOG(LogOpenSCCT, Log, TEXT("Equipment: pawn %d (%s) laser beam, %.0f cm, is drawn %s (vision mode %d)"), Pawn.ActorId,
			bMerc ? TEXT("merc") : TEXT("spy"), Length,
			GoggleStencil == 0 ? (bDrawBeam ? TEXT("in normal vision") : TEXT("not at all")) : GoggleStencil == UScctVisionSubsystem::StencilEmfStreak ? TEXT("for the goggles as a dark grey EMF streak") : TEXT("for the goggles as heat 1"), GoggleMode);
	}

	if (bDrawBeam && !Found.Beam.IsValid())
	{
		if (LaserBeamMaterial != nullptr)
		{
			UStaticMeshComponent* Ribbon = MakeGlowShape(Proxy, ScctLaserBeam::PlanePath, BeamColor, BeamIntensity, 1.0f, LaserBeamMaterial.Get());
			if (Ribbon != nullptr)
			{
				CastChecked<UMaterialInstanceDynamic>(Ribbon->GetMaterial(0))->SetScalarParameterValue(TEXT("WeaponProfile"), 1.0f);
			}
			Found.Beam = Ribbon;
		}
		else
		{
			Found.Beam = MakeGlowShape(Proxy, CylinderPath, BeamColor, BeamIntensity, LaserBeamSharpness);
		}
	}
	if (UStaticMeshComponent* Beam = Found.Beam.Get())
	{
		const bool bShow = bDrawBeam && GoggleStencil == 0;
		if (bShow)
		{
			if (LaserBeamMaterial != nullptr)
			{
				ScctLaserBeam::Place(*Beam, Start, End, GetCamera().GetLocation(), LaserRibbonWidth * BeamWidthRatio, bAttachedMuzzle);
			}
			else
			{
				// The fallback cylinder is 100 across and 100 long along Z.
				Beam->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Direction).ToQuat(), Start + Along * 0.5,
					FVector(LaserBeamWidth * BeamWidthRatio / 100.0, LaserBeamWidth * BeamWidthRatio / 100.0, Length / 100.0)));
			}
		}
		if (Beam->IsVisible() != bShow)
		{
			Beam->SetVisibility(bShow);
		}
	}

	if (bDot && !Found.Dot.IsValid())
	{
		Found.Dot = MakeGlowShape(Proxy, SpherePath, BeamColor, DotIntensity, 2.0f);
	}
	// The mounted emitter paints a small emissive spot. A second light at the hit illuminates
	// a 60 cm patch of wall at close range and overwhelms the actual point.
	const bool bDotSpill = bDot && !bGreen;
	if (bDotSpill && !Found.DotLight.IsValid())
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(&Proxy, NAME_None, RF_Transient);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(Proxy.GetRootComponent());
		Light->SetUsingAbsoluteLocation(true);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		// A faint spill on the surface around the dot and no more: at 0.6 cd a merc with a dot on his chest was
		// red from head to foot, and so was the door frame beside him.
		Light->SetIntensity(0.04f);
		Light->SetAttenuationRadius(30.0f);
		Light->SetLightColor(BeamColor);
		Light->SetCastShadows(false);
		UScctEchelonLightFilter::RouteNewWorldLight(*Light);
		Light->RegisterComponent();
		Proxy.AddInstanceComponent(Light);
		Found.DotLight = Light;
	}
	if (UStaticMeshComponent* Dot = Found.Dot.Get())
	{
		if (bDot)
		{
			Dot->SetWorldLocation(End - Direction * (bGreen ? 0.12 : 0.4));
			Dot->SetWorldScale3D(FVector((bGreen ? MountedDotWidth : LaserDotWidth) / 100.0));
		}
		if (Dot->IsVisible() != bDot)
		{
			Dot->SetVisibility(bDot);
		}
	}
	if (UPointLightComponent* Light = Found.DotLight.Get())
	{
		if (bDotSpill)
		{
			// Just off the surface, so the spill lands on it.
			Light->SetWorldLocation(End - Direction * 4.0);
		}
		if (Light->IsVisible() != bDotSpill)
		{
			Light->SetVisibility(bDotSpill);
		}
	}
}

void UScctEquipmentSubsystem::ProbeLaser(const FVector& Start, const FVector& End)
{
	// Where the beam ends as the camera sees it: a unit vector in the camera's own frame. A beam laid for this
	// frame's view keeps that vector while the view turns; one laid for the last tick's swings against the turn.
	const FTransform Camera = GetCamera();
	const FVector Seen = Camera.InverseTransformPositionNoScale(End).GetSafeNormal();
	const float Yaw = Camera.Rotator().Yaw;
	const float OffCentre = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(Seen.X), -1.0f, 1.0f)));
	if (bLaserProbeHasLast)
	{
		const float Step = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Seen, LaserProbeLast)), -1.0f, 1.0f)));
		LaserProbeLargestStep = FMath::Max(LaserProbeLargestStep, Step);
		LaserProbeTurned += FMath::Abs(FMath::FindDeltaAngleDegrees(LaserProbeLastYaw, Yaw));
	}
	LaserProbeLargestOffCentre = FMath::Max(LaserProbeLargestOffCentre, OffCentre);
	LaserProbeLast = Seen;
	LaserProbeLastYaw = Yaw;
	bLaserProbeHasLast = true;
	++LaserProbeFrames;
	LaserProbeSeconds += FrameSeconds;
	if (LaserProbeSeconds >= 1.0f)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Laser probe: %d frames in %.2f s, the view turned %.1f degrees; the local beam's end was at most %.2f degrees off the centre of the view and moved at most %.2f degrees across the view between two frames; now %.0f cm long from (%.0f %.0f %.0f)"),
			LaserProbeFrames, LaserProbeSeconds, LaserProbeTurned, LaserProbeLargestOffCentre, LaserProbeLargestStep,
			FVector::Dist(Start, End), Start.X, Start.Y, Start.Z);
		LaserProbeSeconds = 0.0f;
		LaserProbeLargestStep = 0.0f;
		LaserProbeLargestOffCentre = 0.0f;
		LaserProbeTurned = 0.0f;
		LaserProbeFrames = 0;
	}
}

void UScctEquipmentSubsystem::UpdateFirstPerson(const FScctEquipmentApi& Api, float DeltaSeconds)
{
	UScctSessionSubsystem* Match = Session.Get();
	const bool bHasView = Api.GetFirstPerson(&FirstPerson) != 0 && FirstPerson.Kind != EScctFirstPersonKind::None;
	const bool bDraw = bHasView && !bNoViewModel && !Match->IsTourView() && Match->GetView().Valid != 0;
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	const APawn* ViewPawn = Player ? Player->GetPawn() : nullptr;
	UCameraComponent* Camera = ViewPawn ? ViewPawn->FindComponentByClass<UCameraComponent>() : nullptr;
	DrawnFirstPersonScale = bDraw && Camera ? FMath::Clamp(CVarFirstPersonScale.GetValueOnGameThread(), 0.05f, 1.0f) : 1.0f;
	if (Camera)
	{
		Camera->SetEnableFirstPersonScale(bDraw);
		Camera->SetFirstPersonScale(DrawnFirstPersonScale);
	}

	if (bDraw && !IsValid(FirstPersonView))
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		FirstPersonView = GetWorld()->SpawnActor<AScctFirstPersonView>(FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
	}
	if (!IsValid(FirstPersonView))
	{
		return;
	}
	if (!bDraw)
	{
		FirstPersonView->Hide(DeltaSeconds);
		// Shots fired while no view model is drawn (through the scope) are not owed a flash when it comes back:
		// the count is taken up afresh then.
		FirstPersonActor = 0;
		return;
	}

	FScctWeaponMotion Motion;
	if (const FScctEquipment* Own = FindEquipment(FirstPerson.ActorId))
	{
		Motion = MotionOf(*Own);
	}

	if (const FPawnGear* Own = Gear.Find(FirstPerson.ActorId))
	{
		Motion.AttachmentMask = Own->AttachmentMask;
	}
	const float Cloak = VisibleCloak(FirstPerson.Cloak);
	FirstPersonView->Present(Api, Models, Weapons, FirstPerson, GetCamera(), Cloak, Motion, DeltaSeconds);

	if (FirstPerson.ActorId != FirstPersonActor)
	{
		// Another pawn (a respawn): its shot count starts wherever it is.
		FirstPersonActor = FirstPerson.ActorId;
		FirstPersonShots = FirstPerson.ShotCount;
	}
	if (FirstPerson.ShotCount != FirstPersonShots)
	{
		FirstPersonShots = FirstPerson.ShotCount;
		const bool bIntegralSuppressor = FirstPerson.Kind == EScctFirstPersonKind::Merc && FirstPerson.PartCount > 0
			&& Models.Name(Api, FirstPerson.Parts[0].Mesh) == TEXT("OpenScctSidearms.suppressed_first");
		if (!bIntegralSuppressor && !(Motion.AttachmentMask & EScctWeaponAttachment::Suppressor))
		{
			FirstPersonView->PlayMuzzleFlash();
		}
	}
}

void UScctEquipmentSubsystem::LogChanges(const FScctEquipmentApi& Api, FPawnGear& Found, const FScctEquipment& Pawn)
{
	int32 Mask = 0;
	for (const FFlagName& Flag : LoggedFlags)
	{
		Mask |= Flag.Flag;
	}
	if (((Found.Flags ^ Pawn.Flags) & Mask) == 0 && Found.ShotCount >= 0 && Found.WeaponClass == Pawn.WeaponClass && Found.WeaponSlot == Pawn.ActiveWeaponSlot)
	{
		Found.ShotCount = Pawn.ShotCount;
		return;
	}

	FString Names;
	for (const FFlagName& Flag : LoggedFlags)
	{
		if ((Pawn.Flags & Flag.Flag) != 0)
		{
			Names += Names.IsEmpty() ? TEXT("") : TEXT(" ");
			Names += Flag.Name;
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Equipment: pawn %d weapon %s slot %d loaded %d [%s] shots %d cloak %.2f goggles %d; torch at (%.0f %.0f %.0f) half angle %.1f range %.0f; laser (%.0f %.0f %.0f) to (%.0f %.0f %.0f)"),
		Pawn.ActorId, *Models.Name(Api, Pawn.WeaponClass), Pawn.ActiveWeaponSlot, Pawn.WeaponLoaded, *Names, Pawn.ShotCount, Pawn.Cloak, Pawn.GoggleMode,
		Pawn.TorchX, Pawn.TorchY, Pawn.TorchZ, Pawn.TorchHalfAngle, Pawn.TorchRange,
		Pawn.LaserStartX, Pawn.LaserStartY, Pawn.LaserStartZ, Pawn.LaserEndX, Pawn.LaserEndY, Pawn.LaserEndZ);
	Found.Flags = Pawn.Flags;
	Found.ShotCount = Pawn.ShotCount;
	Found.WeaponClass = Pawn.WeaponClass;
	Found.WeaponSlot = Pawn.ActiveWeaponSlot;
}
