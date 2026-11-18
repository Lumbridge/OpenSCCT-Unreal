#include "Play/ScctLevelStateSubsystem.h"
#include "Play/ScctStudioProp.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Light.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctLevelStateDebug.h"
#include "Play/ScctLightPlan.h"
#include "Play/ScctLightRules.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	const TCHAR* const LevelTagPrefix = TEXT("Scct:");
	const TCHAR* const LevelEffectMeshFolder = TEXT("/Game/Imported/Fx");
	const FName LevelEmissiveParameter(TEXT("Emissive"));

	// What the importer's map effects (Tools/Import/scct_fx.py, scct_effects.py) are found by.
	const TCHAR* const LevelCoronaTagPrefix = TEXT("ScctCoronaOf:");
	const TCHAR* const LevelWetTagPrefix = TEXT("ScctWetOf:");
	const FName LevelEmitterBurstTag(TEXT("ScctEmitterBurst"));
	// Custom primitive data slot 0 of the surface material (Tools/Import/scct_materials.py): 1 = the detail layer
	// is sampled in the mesh's own space, so the grain travels with a thing that moves.
	constexpr int32 LevelLocalDetailSlot = 0;
	// The importer gives up part of a map light's colour toward white (LIGHT_SATURATION in Tools/Import/import_map.py).
	constexpr float LevelLightSaturation = 0.55f;
	const TCHAR* const LevelLightTypeTagPrefix = TEXT("ScctLightType:");
	const FName LevelLightTypeParameter(TEXT("LightType"));
	const FName LevelCardBrightnessParameter(TEXT("Brightness"));
	const FName LevelCardEdgeParameter(TEXT("EdgeFade")); // only the glow card material has it
	constexpr int32 LevelCoronaColourSlot = 0;            // custom primitive data 0..2: the corona's colour
	constexpr float LevelSteadyLightType = 1.0f;

	/** The original's light types whose brightness moves by itself: pulse, blink, flicker, strobe, subtle pulse. */
	bool IsAnimatedLightType(int32 Type)
	{
		return Type == 2 || Type == 3 || Type == 4 || Type == 5 || Type == 7;
	}

	// A switched lamp comes up and goes down over about this long, like a filament, instead of popping.
	constexpr float LampFadeOnSeconds = 0.22f;
	constexpr float LampFadeOffSeconds = 0.16f;

	// How far from a switched light its lamp mesh may be, and how big a mesh still counts as a lamp (not a wall).
	// The maps hang a breakable lamp (an SGamePlayObject) up to a metre and a half from the light it carries;
	// a plain glowing mesh farther than about a metre is somebody else's sign or screen.
	constexpr float FixtureReach = 110.0f;
	constexpr float FixtureReachBreakable = 170.0f;
	constexpr float FixtureMaxExtent = 260.0f;
	const TCHAR* const BreakableTagPrefix = TEXT("Scct:SGamePlayObject");

	/** A name reduced to its letters and digits: how imported assets are matched to the game's object paths. */
	FString LevelKey(const FString& Name)
	{
		FString Key;
		Key.Reserve(Name.Len());
		for (const TCHAR Character : Name)
		{
			if (FChar::IsAlnum(Character))
			{
				Key.AppendChar(FChar::ToLower(Character));
			}
		}
		return Key;
	}

	/** "Package.Group.Name" as the effect meshes are named: package and name, without the group. */
	FString LevelPackageKey(const FString& Path)
	{
		int32 First = INDEX_NONE;
		int32 Last = INDEX_NONE;
		if (!Path.FindChar(TEXT('.'), First) || !Path.FindLastChar(TEXT('.'), Last))
		{
			return FString();
		}
		return LevelKey(Path.Left(First)) + LevelKey(Path.Mid(Last + 1));
	}

	/**
	 * The map's light colours are display values (from hue and saturation). The importer turned them the same way
	 * (import_map.py, the lights): to linear, then part of the way to their own grey, so a lamp the simulation
	 * recolours keeps the strength of colour of the lamps beside it.
	 */
	FLinearColor ToLightColor(const FLinearColor& Display)
	{
		const FLinearColor Linear(FMath::Pow(FMath::Max(Display.R, 0.0f), 2.2f), FMath::Pow(FMath::Max(Display.G, 0.0f), 2.2f), FMath::Pow(FMath::Max(Display.B, 0.0f), 2.2f));
		// The importer's grey: the colour's own luminance, so the light keeps its strength as it pales.
		const float Grey = 0.2126f * Linear.R + 0.7152f * Linear.G + 0.0722f * Linear.B;
		if (FScctLightPlan::IsOn())
		{
			// The lighting plan's lights keep their own colour, and their strength is not in it.
			return FScctLightRules::LightColour(Display);
		}
		return FLinearColor(Grey + (Linear.R - Grey) * LevelLightSaturation, Grey + (Linear.G - Grey) * LevelLightSaturation, Grey + (Linear.B - Grey) * LevelLightSaturation);
	}

	/**
	 * Nanite draws only opaque and masked materials: a Nanite mesh that wears glass or a glow is drawn with the
	 * engine's checkerboard instead. Such a component is drawn the ordinary way. Returns true when it was switched.
	 */
	bool LevelAvoidNanite(UStaticMeshComponent* Component)
	{
		const UStaticMesh* Mesh = Component != nullptr ? Component->GetStaticMesh() : nullptr;
		if (Mesh == nullptr || Component->IsForceDisableNanite())
		{
			return false;
		}
#if WITH_EDITOR
		// The mesh's setting: its Nanite data may still be building.
		const bool bNanite = Mesh->IsNaniteEnabled();
#else
		// A cooked game has no setting, only the data the cook built.
		const bool bNanite = Mesh->HasValidNaniteData();
#endif
		if (!bNanite)
		{
			return false;
		}
		for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
		{
			const UMaterialInterface* Material = Component->GetMaterial(Slot);
			if (Material != nullptr && !IsOpaqueOrMaskedBlendMode(Material->GetBlendMode()))
			{
				Component->SetForceDisableNanite(true);
				return true;
			}
		}
		return false;
	}

	void ScanLevelFolder(const FString& Folder, const UClass* Class, TArray<FAssetData>& OutAssets)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.ScanPathsSynchronous({ Folder }, false);
		TArray<FAssetData> Found;
		Registry.GetAssetsByPath(FName(*Folder), Found, true);
		for (const FAssetData& Asset : Found)
		{
			if (Asset.IsInstanceOf(Class))
			{
				OutAssets.Add(Asset);
			}
		}
	}
}

bool UScctLevelStateSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctLevelStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	if (Session == nullptr)
	{
		return;
	}

	Session->OnSessionChanged.AddUObject(this, &UScctLevelStateSubsystem::HandleSessionChanged);
	Session->OnMatchStarted.AddUObject(this, &UScctLevelStateSubsystem::HandleMatchStarted);
	Session->OnFrame.AddUObject(this, &UScctLevelStateSubsystem::HandleFrame);

	bLog = FParse::Param(FCommandLine::Get(), TEXT("ScctLevelLog"));

	// -ScctLevelDo="movers@8;breaklights@14" (also flare: a merc flare thrown from the camera; dirtyfeet: every pawn's
	// next steps leave footprints; camnetoff, modem and modemhit: the header says; smoke: a smoke grenade popped ahead; shootsmoke: shots through its cloud). -ScctMarkTest shoots the nearest doors and panes, then opens the
	// doors and breaks the panes, so the marks can be seen to go with them.
	FString Actions;
	FParse::Value(FCommandLine::Get(), TEXT("ScctLevelDo="), Actions, false);
	if (FParse::Param(FCommandLine::Get(), TEXT("ScctMarkTest")))
	{
		Actions += TEXT(";shootmovers@4;shootprops@4.5;movers@9;props@15");
	}
	if (!Actions.IsEmpty())
	{
		TArray<FString> Entries;
		Actions.ParseIntoArray(Entries, TEXT(";"));
		for (const FString& Entry : Entries)
		{
			FString What;
			FString When;
			if (Entry.Split(TEXT("@"), &What, &When))
			{
				FDebugAction Action;
				Action.What = What.TrimStartAndEnd().ToLower();
				Action.Seconds = FCString::Atof(*When);
				DebugActions.Add(MoveTemp(Action));
			}
		}
	}

	ActorBuffer.SetNumUninitialized(512);
	LightBuffer.SetNumUninitialized(512);
	EventBuffer.SetNumUninitialized(128);
}

void UScctLevelStateSubsystem::Deinitialize()
{
	MeshParticles.Reset();
	if (Session != nullptr)
	{
		Session->OnSessionChanged.RemoveAll(this);
		Session->OnMatchStarted.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------------------------------------------

void UScctLevelStateSubsystem::HandleSessionChanged(int32 OldId, int32 NewId)
{
	if (OldId != 0 && bBound)
	{
		LogSummary();
	}

	// Name ids, actor states and what was spawned belong to one match.
	MeshParticles.Reset();
	RestoreLevel();
	Names.Empty();
	ActorStates.Reset();
	LightStates.Reset();
	MentionedTags.Reset();
	ReportedMissing.Reset();
	NamesWithoutActor.Reset();
	for (FDebugAction& Action : DebugActions)
	{
		Action.bDone = false;
	}
	MatchSeconds = 0.0f;
	bBound = false;
	NumLevelActors = NumStandIns = NumNothingToDraw = NumHiddenAtStart = 0;
	NumLightsOffAtStart = NumLightsFound = NumMobilityChanged = 0;
	NumCoronasBound = NumRhythmsHeld = NumGlowCards = 0;
	PlacementError = PlacementTurnError = 0.0f;
	PlacementChecked = 0;
	FMemory::Memzero(NumEvents);
	NumEmittersReached = 0;
	PropStates.Reset();
	RidingEmitters.Reset();
	PropsReported.Reset();
	NumPropsBound = NumPropsPlayed = NumBoneLights = 0;
}

void UScctLevelStateSubsystem::HandleMatchStarted()
{
	FString Error;
	World = ScctFeature<FScctWorldApi>(&Error);
	if (World == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Level state: the level stays as imported this match, the bridge's world table is missing: %s"), *Error);
		return;
	}

	if (DebugActions.Num() > 0 && Debug == nullptr)
	{
		Debug = static_cast<const FScctWorldDebugApi*>(FOpenSCCTModule::Get().FindFeatureTable(
			TEXT("OpenScct.UnrealBridge.WorldDebugExports"), sizeof(FScctWorldDebugApi), ScctWorldDebugApiVersion, Error));
		if (Debug == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Level state: -ScctLevelDo does nothing, the bridge's check entry points are missing: %s"), *Error);
		}
	}

	if (Session != nullptr && Session->IsBackdrop())
	{
		// Behind the main menu one map follows another in the same world: the index is of the map shown now.
		bIndexed = false;
		TaggedActors.Reset();
		TaggedLights.Reset();
		TaggedCoronas.Reset();
		TaggedEmitters.Reset();
		TaggedWetGround.Reset();
		MovableTags.Reset();
		MeshAssets.Reset();
		MaterialAssets.Reset();
		NumEffectActors = 0;
	}
	IndexLevel();
	MeshParticles.Begin(*this);

	// The lighting plan dresses the level's lights before any is bound: a bound light's strength is what it has then.
	bLightPlanDressed = false;
	LightPlanWaited = 0;
	DressForLightPlan();

	// The simulation's state of the first tick is applied before the first frame is drawn, so nothing the map
	// hides or switches off shows for a frame.
	HandleFrame(0.0f);
}

void UScctLevelStateSubsystem::IndexLevel()
{
	if (bIndexed)
	{
		return;
	}
	bIndexed = true;

	int32 NumLights = 0;
	int32 NumNaniteAvoided = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			const FString TagText = Tag.ToString();
			if (TagText.StartsWith(LevelCoronaTagPrefix))
			{
				TaggedCoronas.Add(FScctWorld::LevelTag(*TagText.RightChop(FCString::Strlen(LevelCoronaTagPrefix))), *It);
				continue;
			}
			if (TagText.StartsWith(LevelWetTagPrefix))
			{
				TaggedWetGround.Add(FScctWorld::LevelTag(*TagText.RightChop(FCString::Strlen(LevelWetTagPrefix))), *It);
				continue;
			}
			if (!TagText.StartsWith(LevelTagPrefix))
			{
				continue;
			}
			// A light can carry the name of the mesh it belongs to (a lift door and its lamp), so the two are kept apart.
			if (It->IsA<ALight>())
			{
				TaggedLights.Add(Tag, *It);
				++NumLights;
				continue;
			}
			// Only meshes stand for a followed actor. The importer's other effect actors carry the same kind of tag
			// (the sprite emitters of one Emitter actor share one, rain volumes, caustic lights): they are not
			// moved, hidden or re-skinned from here.
			if (!It->IsA<AStaticMeshActor>() && !It->IsA<ASkeletalMeshActor>())
			{
				++NumEffectActors;
				// The map's particle emitters and rain: started and stopped by the simulation's triggers.
				const ANiagaraActor* Particles = Cast<ANiagaraActor>(*It);
				if (UNiagaraComponent* Component = Particles != nullptr ? Particles->GetNiagaraComponent() : nullptr)
				{
					FLevelEmitter Emitter;
					Emitter.Component = Component;
					Emitter.bBurst = It->Tags.Contains(LevelEmitterBurstTag);
					Emitter.bOriginalActive = Emitter.bOn = Component->bAutoActivate;
					TaggedEmitters.Add(Tag, Emitter);
				}
				continue;
			}
			TaggedActors.Add(Tag, *It);
			const AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(*It);
			NumNaniteAvoided += MeshActor != nullptr && LevelAvoidNanite(MeshActor->GetStaticMeshComponent()) ? 1 : 0;
			if (MeshActor != nullptr && MeshActor->GetStaticMeshComponent() != nullptr && MeshActor->GetStaticMeshComponent()->Mobility == EComponentMobility::Movable)
			{
				MovableTags.Add(Tag);
			}
		}
	}

	// The map's meshes and materials are original content imported on the player's machine, so they are found by
	// looking, not by fixed references. A mesh of the map's own file is named Package_Group_Name, with "_vc" and
	// eight hex digits added for each differently lit copy; the plain name stands for any of them.
	// The match behind the main menu plays in a level streamed into the front end's world: its assets are the
	// map's, not the front end's.
	const FString MapFolder = FPackageName::GetLongPackagePath(Session != nullptr && Session->IsBackdrop()
		? UScctOnlineSubsystem::ImportedLevelOf(Session->GetSessionInfo().Map)
		: GetWorld()->GetOutermost()->GetName());
	TArray<FAssetData> Assets;
	ScanLevelFolder(MapFolder / TEXT("world/StaticMeshes"), UStaticMesh::StaticClass(), Assets);
	ScanLevelFolder(LevelEffectMeshFolder, UStaticMesh::StaticClass(), Assets);
	for (const FAssetData& Asset : Assets)
	{
		MeshAssets.Add(LevelKey(Asset.AssetName.ToString()), Asset.GetSoftObjectPath());
	}
	for (const FAssetData& Asset : Assets)
	{
		const FString Name = Asset.AssetName.ToString();
		const int32 Variant = Name.Find(TEXT("_vc"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Variant != INDEX_NONE && Name.Len() - Variant == 11)
		{
			const FString Plain = LevelKey(Name.Left(Variant));
			if (!MeshAssets.Contains(Plain))
			{
				MeshAssets.Add(Plain, Asset.GetSoftObjectPath());
			}
		}
	}

	// Materials: "MI_" + the material's name, with "__lm0", "__unlit" and the like for its lighting variants.
	Assets.Reset();
	ScanLevelFolder(MapFolder / TEXT("Materials"), UMaterialInterface::StaticClass(), Assets);
	ScanLevelFolder(LevelEffectMeshFolder, UMaterialInterface::StaticClass(), Assets);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const FAssetData& Asset : Assets)
		{
			FString Name = Asset.AssetName.ToString();
			Name.RemoveFromStart(TEXT("MI_"));
			const int32 Variant = Name.Find(TEXT("__"));
			if (Pass == 0 && Variant == INDEX_NONE)
			{
				MaterialAssets.Add(LevelKey(Name), Asset.GetSoftObjectPath());
			}
			else if (Pass == 1 && Variant != INDEX_NONE && !MaterialAssets.Contains(LevelKey(Name.Left(Variant))))
			{
				MaterialAssets.Add(LevelKey(Name.Left(Variant)), Asset.GetSoftObjectPath());
			}
		}
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Level state: the level has %d tagged actors (%d movable meshes, %d lights), %d corona cards, %d particle emitters that follow the simulation's triggers, %d wet-ground decals and %d tagged effect actors in all; %d imported meshes and %d materials can be swapped in (%s)"),
		TaggedActors.Num() + TaggedLights.Num(), MovableTags.Num(), NumLights, TaggedCoronas.Num(), TaggedEmitters.Num(), TaggedWetGround.Num(), NumEffectActors, MeshAssets.Num(), MaterialAssets.Num(), *MapFolder);
	if (NumNaniteAvoided > 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %d level meshes with glass or glowing materials were imported as Nanite meshes, which cannot draw those; they are drawn the ordinary way"), NumNaniteAvoided);
	}
}

void UScctLevelStateSubsystem::RestoreLevel()
{
	const UWorld* ThisWorld = GetWorld();
	const bool bTearingDown = ThisWorld == nullptr || ThisWorld->bIsTearingDown;

	// The lamps before the actors: a lamp mesh that also wore a skin has all its slots cleared by its actor below.
	for (FLightState& State : LightStates)
	{
		if (bTearingDown)
		{
			break;
		}
		if (ULightComponent* Light = State.Light.Get())
		{
			Light->SetIntensity(State.OriginalIntensity);
			Light->SetLightColor(State.OriginalColor);
			Light->SetVisibility(State.bOriginalVisible);
			if (State.bLightFunctionHeld)
			{
				// The importer's own rhythm again, for the level without a match.
				Light->SetLightFunctionMaterial(State.OriginalLightFunction.Get());
			}
			if (State.bMoved && State.Actor.IsValid())
			{
				State.Actor->SetActorTransform(State.OriginalTransform);
			}
		}
		for (const FCorona& Corona : State.Coronas)
		{
			AActor* Actor = Corona.Actor.Get();
			UStaticMeshComponent* Card = Corona.Card.Get();
			if (Actor == nullptr || Card == nullptr)
			{
				continue;
			}
			Actor->SetActorHiddenInGame(Corona.bOriginalHidden);
			Actor->SetActorLocation(Corona.OriginalLocation);
			Card->SetCustomPrimitiveDataVector3(LevelCoronaColourSlot, FVector(Corona.OriginalColor.R, Corona.OriginalColor.G, Corona.OriginalColor.B));
		}
		for (const FFixture& Fixture : State.Fixtures)
		{
			if (UStaticMeshComponent* Mesh = Fixture.Mesh.Get())
			{
				Mesh->SetMaterial(Fixture.Slot, Fixture.OriginalOverride.Get());
			}
		}
	}

	for (FActorState& State : ActorStates)
	{
		AActor* Actor = State.Actor.Get();
		if (!State.bSeen || !State.bLevelActor || Actor == nullptr || bTearingDown)
		{
			continue;
		}
		if (State.AttachedPawnId != 0)
		{
			Detach(State);
		}
		Actor->SetActorTransform(State.OriginalTransform);
		Actor->SetActorHiddenInGame(State.bOriginalHidden);
		if (UStaticMeshComponent* Mesh = State.Mesh.Get())
		{
			if (Mesh->GetStaticMesh() != State.OriginalMesh.Get() && State.OriginalMesh.IsValid())
			{
				Mesh->SetStaticMesh(State.OriginalMesh.Get());
			}
			if (State.bSkinned)
			{
				Mesh->EmptyOverrideMaterials();
			}
		}
	}

	// Restore skeletal props too: most have no static-mesh entry in ActorStates.
	for (TPair<int32, FPropState>& Pair : PropStates)
	{
		FPropState& State = Pair.Value;
		ASkeletalMeshActor* Actor = State.Actor.Get();
		USkeletalMeshComponent* Component = Actor != nullptr ? Actor->GetSkeletalMeshComponent() : nullptr;
		if (Component == nullptr || bTearingDown)
		{
			continue;
		}
		ScctStudioProp::Restore(*Component);
		if (State.bMoved)
		{
			Actor->SetActorTransform(State.ImportedTransform);
		}
		if (!State.bDriven)
		{
			continue;
		}
		Component->SetAnimation(State.OriginalAnimation.Get());
		Component->SetPosition(0.0f, false);
		if (State.bOriginalPlaying)
		{
			Component->Play(State.bOriginalLooping);
		}
		else
		{
			Component->Stop();
		}
	}

	for (AActor* Actor : Spawned)
	{
		if (IsValid(Actor))
		{
			for (const FName& Tag : Actor->Tags)
			{
				TaggedActors.Remove(Tag);
			}
			Actor->Destroy();
		}
	}
	Spawned.Reset();

	RestoreAttachedEmitters(bTearingDown);

	// The map's emitters as imported: the running ones run, the waiting ones wait, the ground under the rain is wet.
	for (TPair<FName, FLevelEmitter>& Pair : TaggedEmitters)
	{
		FLevelEmitter& Emitter = Pair.Value;
		UNiagaraComponent* Component = Emitter.Component.Get();
		if (Component == nullptr || bTearingDown || Emitter.bOn == Emitter.bOriginalActive)
		{
			continue;
		}
		Emitter.bOn = Emitter.bOriginalActive;
		if (Emitter.bOn)
		{
			Component->Activate(true);
		}
		else
		{
			Component->DeactivateImmediate();
		}
	}
	for (const TPair<FName, TWeakObjectPtr<AActor>>& Pair : TaggedWetGround)
	{
		if (AActor* Wet = Pair.Value.Get(); Wet != nullptr && !bTearingDown)
		{
			Wet->SetActorHiddenInGame(false);
		}
	}
}

int32 UScctLevelStateSubsystem::TriggerEmitters(const FString& Name)
{
	const FName Tag = FScctWorld::LevelTag(*Name);
	int32 Reached = 0;
	bool bAnyOn = false;
	for (auto It = TaggedEmitters.CreateKeyIterator(Tag); It; ++It)
	{
		FLevelEmitter& Emitter = It.Value();
		UNiagaraComponent* Component = Emitter.Component.Get();
		if (Component == nullptr)
		{
			continue;
		}
		++Reached;
		// The original's ParticleEmitter.Trigger: an emitter that a trigger disables is toggled; one that only
		// spawns on its trigger fires again. A burst that has played itself out counts as off, so the next
		// trigger plays it again.
		const bool bRunning = Emitter.bOn && Component->IsActive();
		if (Emitter.bBurst || !bRunning)
		{
			Component->Activate(true);
			Emitter.bOn = true;
		}
		else
		{
			Component->Deactivate(); // no new particles; the live ones run out
			Emitter.bOn = false;
		}
		bAnyOn |= Emitter.bOn;
	}
	if (Reached > 0)
	{
		for (auto It = TaggedWetGround.CreateKeyIterator(Tag); It; ++It)
		{
			if (AActor* Wet = It.Value().Get())
			{
				Wet->SetActorHiddenInGame(!bAnyOn);
			}
		}
	}
	return Reached;
}

// ---------------------------------------------------------------------------------------------------------------
// Names and assets
// ---------------------------------------------------------------------------------------------------------------

void UScctLevelStateSubsystem::RefreshNames()
{
	const int32 Count = World->GetNameCount();
	TCHAR Buffer[256];
	while (Names.Num() < Count)
	{
		Buffer[0] = 0;
		World->GetName(Names.Num(), Buffer, UE_ARRAY_COUNT(Buffer));
		Names.Add(new FString(Buffer));
	}
}

const FString& UScctLevelStateSubsystem::GetName(int32 Id)
{
	static const FString None;
	if (World != nullptr && Id >= Names.Num())
	{
		RefreshNames();
	}
	return Names.IsValidIndex(Id) ? Names[Id] : None;
}

AActor* UScctLevelStateSubsystem::FindLevelActor(const FString& Name) const
{
	const TWeakObjectPtr<AActor>* Found = TaggedActors.Find(FScctWorld::LevelTag(*Name));
	return Found != nullptr ? Found->Get() : nullptr;
}

UStaticMesh* UScctLevelStateSubsystem::FindMesh(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return nullptr;
	}
	const FString Key = LevelKey(Name);
	if (const TObjectPtr<UStaticMesh>* Known = LoadedMeshes.Find(Key))
	{
		return Known->Get();
	}

	// The map's own meshes go by their whole path, the effect meshes by package and name.
	const FSoftObjectPath* Path = MeshAssets.Find(Key);
	if (Path == nullptr)
	{
		Path = MeshAssets.Find(LevelPackageKey(Name));
	}
	UStaticMesh* Mesh = Path != nullptr ? Cast<UStaticMesh>(Path->TryLoad()) : nullptr;
	LoadedMeshes.Add(Key, Mesh);
	return Mesh;
}

UMaterialInterface* UScctLevelStateSubsystem::FindMaterial(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return nullptr;
	}
	const FString Key = LevelKey(Name);
	if (const TObjectPtr<UMaterialInterface>* Known = LoadedMaterials.Find(Key))
	{
		return Known->Get();
	}

	const FSoftObjectPath* Path = MaterialAssets.Find(Key);
	if (Path == nullptr)
	{
		Path = MaterialAssets.Find(LevelPackageKey(Name));
	}
	UMaterialInterface* Material = Path != nullptr ? Cast<UMaterialInterface>(Path->TryLoad()) : nullptr;
	LoadedMaterials.Add(Key, Material);
	return Material;
}

// ---------------------------------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------------------------------

void UScctLevelStateSubsystem::HandleFrame(float DeltaSeconds)
{
	if (World == nullptr || !Session->IsPlaying())
	{
		return;
	}

	MatchSeconds += DeltaSeconds;
	FScctLightRules::Tick(GetWorld());
	RefreshNames();
	RunDebugActions();
	UpdateActors();
	UpdateProps();
	UpdateAttachedEmitters();
	DressForLightPlan();
	UpdateLights(DeltaSeconds);
	DrainEvents();
	MeshParticles.Draw();

	// The table is empty until the client has loaded its map; the first frame with anything in it is the map's
	// state as the match begins.
	if (!bBound && (ActorStates.Num() > 0 || LightStates.Num() > 0))
	{
		bBound = true;
		LogBound();
	}
}

void UScctLevelStateSubsystem::RunDebugActions()
{
	if (Debug == nullptr)
	{
		return;
	}
	for (FDebugAction& Action : DebugActions)
	{
		if (Action.bDone || MatchSeconds < Action.Seconds)
		{
			continue;
		}
		Action.bDone = true;
		const FScctBridgeView& View = Session->GetView();
		int32 Queued = -1;
		if (Action.What == TEXT("movers"))
		{
			Queued = Debug->FireMovers();
		}
		else if (Action.What == TEXT("lights"))
		{
			Queued = Debug->FireLights();
		}
		else if (Action.What == TEXT("breaklights"))
		{
			Queued = Debug->BreakLights(View.X, View.Y, View.Z, 3);
		}
		else if (Action.What == TEXT("props"))
		{
			Queued = Debug->BreakProps(View.X, View.Y, View.Z, 8);
		}
		else if (Action.What == TEXT("emitters"))
		{
			Queued = Debug->FireEmitters();
		}
		else if (Action.What == TEXT("shootmovers"))
		{
			Queued = Debug->ShootNear(View.X, View.Y, View.Z, 2, 0);
		}
		else if (Action.What == TEXT("shootprops"))
		{
			Queued = Debug->ShootNear(View.X, View.Y, View.Z, 3, 1);
		}
		else if (Action.What == TEXT("flare"))
		{
			// Ahead of the camera and a little down, so it catches on the floor or the wall in front.
			FRotator Aim(View.Pitch, View.Yaw, 0.0f);
			Aim.Pitch = FMath::Min(Aim.Pitch, -12.0f);
			const FVector Direction = Aim.Vector();
			const FVector From = FVector(View.X, View.Y, View.Z) + Direction * 40.0;
			Queued = Debug->ThrowFlare(From.X, From.Y, From.Z, Direction.X, Direction.Y, Direction.Z);
		}
		else if (Action.What == TEXT("dirtyfeet"))
		{
			const int32 Pawns = Debug->DirtyFeet();
			UE_LOG(LogOpenSCCT, Log, TEXT("Level state: dirty feet on %d pawns of the client's world"), Pawns);
			Queued = Pawns > 0 ? 1 : 0;
		}
		else if (Action.What == TEXT("camnetoff"))
		{
			Queued = Debug->CamnetOff();
		}
		else if (Action.What == TEXT("modem"))
		{
			// On the wall straight ahead of the camera, at its own height.
			const FVector Direction = FRotator(0.0f, View.Yaw, 0.0f).Vector();
			Queued = Debug->PlaceModem(View.X, View.Y, View.Z, Direction.X, Direction.Y, Direction.Z);
		}
		else if (Action.What == TEXT("modemhit"))
		{
			Queued = Debug->ShootModems();
		}
		else if (Action.What == TEXT("smoke"))
		{
			// On the floor straight ahead of the camera, short of a wall.
			const FVector Direction = FRotator(0.0f, View.Yaw, 0.0f).Vector();
			Queued = Debug->PopSmoke(View.X, View.Y, View.Z, Direction.X, Direction.Y, Direction.Z);
		}
		else if (Action.What == TEXT("shootsmoke"))
		{
			// From the camera, as the player's own shots go: each hole runs along the line of sight, a tunnel to see through.
			Queued = Debug->ShootSmoke(View.X, View.Y, View.Z, 5);
		}
		else if (Action.What == TEXT("breakglass"))
		{
			Queued = BreakEveryPane(FVector(View.X, View.Y, View.Z));
		}
		else if (Action.What == TEXT("rain"))
		{
			// Every pawn into the map's biggest rain volume, to see the rain splash on the bodies.
			const FScctFlicFlocApi* Rain = ScctFeature<FScctFlicFlocApi>();
			Queued = Rain != nullptr ? Rain->StandInRain() : 0;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: -ScctLevelDo '%s' at %.1f s (camera at %.0f, %.0f, %.0f): %s"), *Action.What, MatchSeconds, View.X, View.Y, View.Z,
			Queued == 1 ? TEXT("handed to the server") : Queued == 0 ? TEXT("no local server to do it") : TEXT("not a known action"));
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Actors
// ---------------------------------------------------------------------------------------------------------------

void UScctLevelStateSubsystem::UpdateActors()
{
	int32 Total = World->GetActors(ActorBuffer.GetData(), ActorBuffer.Num());
	if (Total > ActorBuffer.Num())
	{
		ActorBuffer.SetNumUninitialized(Total + 64);
		Total = World->GetActors(ActorBuffer.GetData(), ActorBuffer.Num());
	}
	const int32 Count = FMath::Clamp(Total, 0, ActorBuffer.Num());

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctWorldActor& Record = ActorBuffer[Index];
		if (Record.NameId <= 0)
		{
			continue;
		}
		if (Record.NameId >= ActorStates.Num())
		{
			ActorStates.SetNum(Record.NameId + 64);
		}
		FActorState& State = ActorStates[Record.NameId];
		if (!State.bSeen)
		{
			FirstSight(State, Record);
		}

		const bool bVisible = (Record.Flags & EScctWorldActorFlags::Visible) != 0;
		const bool bAttached = (Record.Flags & EScctWorldActorFlags::Attached) != 0;
		const FVector Location(Record.X, Record.Y, Record.Z);
		const FRotator Rotation = FScctWorld::ToRotator(Record.Pitch, Record.Yaw, Record.Roll);

		if (!bAttached && (State.Info.Flags & EScctWorldActorInfoFlags::Mover) != 0)
		{
			State.MaxOffset = FMath::Max(State.MaxOffset, static_cast<float>(FVector::Dist(Location, State.FirstLocation)));
			State.MaxTurn = FMath::Max(State.MaxTurn, static_cast<float>(FMath::RadiansToDegrees(Rotation.Quaternion().AngularDistance(State.FirstRotation))));
		}

		// The level has no actor for it: it gets one the first time the simulation shows it.
		if (!State.Actor.IsValid() && bVisible && !State.bSpawnTried)
		{
			SpawnStandIn(State, Record);
		}
		AActor* Actor = State.Actor.Get();
		if (Actor == nullptr)
		{
			continue;
		}

		if (Record.MeshId != State.MeshId)
		{
			ApplyMesh(State, Record.MeshId);
		}
		if (bVisible != State.bShown)
		{
			State.bShown = bVisible;
			Actor->SetActorHiddenInGame(!bVisible);
		}

		if (bAttached)
		{
			// Carried on a pawn's bone (the disk on a spy's thigh): the table gives the place from the bone.
			if (State.AttachedPawnId != Record.AttachPawnId)
			{
				// The mesh keeps its scale and hangs by its pivot, as it stands in the world: the origin goes to the
				// attachment's place less the turned pivot.
				const FVector Scale = Actor->GetActorScale3D();
				const FVector Pivot = FVector(State.Info.PrePivotX, State.Info.PrePivotY, State.Info.PrePivotZ) * Scale;
				AScctPawnProxy* Proxy = Session->FindProxy(Record.AttachPawnId);
				if (Proxy != nullptr && Proxy->AttachToBone(Actor->GetRootComponent(), GetName(Record.AttachBoneId), FTransform(Rotation, Location - Rotation.RotateVector(Pivot), Scale)))
				{
					State.AttachedPawnId = Record.AttachPawnId;
				}
			}
			continue;
		}
		if (State.AttachedPawnId != 0)
		{
			Detach(State);
			State.Location = FVector(UE_BIG_NUMBER);
		}

		// An actor that never left its map placement keeps the importer's transform; one spawned here has no
		// other placement than the table's.
		const bool bPlace = ((Record.Flags & EScctWorldActorFlags::Moved) != 0 || !State.bLevelActor) && !State.bVisibilityOnly;
		if (bPlace && (!Location.Equals(State.Location, 0.005) || !Rotation.Equals(State.Rotation, 0.005)))
		{
			State.Location = Location;
			State.Rotation = Rotation;
			Actor->SetActorLocationAndRotation(Location, Rotation);
		}
	}
}

void UScctLevelStateSubsystem::Detach(FActorState& State)
{
	State.AttachedPawnId = 0;
	AActor* Actor = State.Actor.Get();
	if (Actor == nullptr)
	{
		return;
	}
	const FVector Scale = Actor->GetActorScale3D();
	Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Actor->SetActorScale3D(Scale);
	// On the bone it was shown and hidden with the body; in the world again only the simulation hides it.
	if (USceneComponent* Root = Actor->GetRootComponent())
	{
		Root->SetVisibility(true, true);
		Root->SetHiddenInGame(false, true);
	}
}

void UScctLevelStateSubsystem::FirstSight(FActorState& State, const FScctWorldActor& Record)
{
	State.bSeen = true;
	World->GetActorInfo(Record.NameId, &State.Info);
	State.FirstLocation = FVector(Record.X, Record.Y, Record.Z);
	State.FirstRotation = FScctWorld::ToRotator(Record.Pitch, Record.Yaw, Record.Roll).Quaternion();

	const FString& Name = GetName(Record.NameId);
	const FName Tag = FScctWorld::LevelTag(*Name);
	MentionedTags.Add(Tag);

	AActor* Found = TaggedActors.FindRef(Tag).Get();
	if (Found == nullptr)
	{
		NamesWithoutActor.Add(Name);
		if (bBound)
		{
			// Later arrivals (a prop spawned in play) are said one by one; the map's own are listed when it binds.
			UE_LOG(LogOpenSCCT, Log, TEXT("Level state: new followed actor '%s' (%s) has no level actor"), *Name, *GetName(State.Info.ClassId));
		}
		return;
	}

	State.Actor = Found;
	State.bLevelActor = true;
	// A skeletal prop is placed by the importer with its mesh's own rotation origin, which the table's place does
	// not have: it is shown and hidden from here and stays where it was imported.
	State.bVisibilityOnly = Found->IsA<ASkeletalMeshActor>();
	State.Mesh = Found->FindComponentByClass<UStaticMeshComponent>();
	State.OriginalTransform = Found->GetActorTransform();
	State.bOriginalHidden = Found->IsHidden();
	State.OriginalMesh = State.Mesh.IsValid() ? State.Mesh->GetStaticMesh() : nullptr;
	State.Location = Found->GetActorLocation();
	State.Rotation = Found->GetActorRotation();
	State.bShown = !Found->IsHidden();
	State.MeshId = State.Info.MeshId;
	++NumLevelActors;
	NumHiddenAtStart += (Record.Flags & EScctWorldActorFlags::Visible) == 0 ? 1 : 0;

	// The importer makes movers and gameplay meshes movable; anything else the simulation follows is made so here.
	USceneComponent* Root = Found->GetRootComponent();
	if (Root != nullptr && Root->Mobility != EComponentMobility::Movable && !State.bVisibilityOnly)
	{
		Root->SetMobility(EComponentMobility::Movable);
		State.bMobilityChanged = true;
		++NumMobilityChanged;
	}

	// Evidence that the table's placement and the importer's are the same.
	if ((Record.Flags & (EScctWorldActorFlags::Moved | EScctWorldActorFlags::Attached)) == 0 && !State.bVisibilityOnly)
	{
		++PlacementChecked;
		PlacementError = FMath::Max(PlacementError, static_cast<float>(FVector::Dist(State.Location, State.FirstLocation)));
		PlacementTurnError = FMath::Max(PlacementTurnError, static_cast<float>(FMath::RadiansToDegrees(Found->GetActorQuat().AngularDistance(State.FirstRotation))));
	}

	int32 Skins[ScctWorldSkinSlots] = {};
	if (World->GetActorSkins(Record.NameId, Skins, ScctWorldSkinSlots) > 0)
	{
		for (int32 Slot = 0; Slot < ScctWorldSkinSlots; ++Slot)
		{
			if (Skins[Slot] != 0)
			{
				ApplySkin(State, Slot, Skins[Slot]);
			}
		}
	}
}

bool UScctLevelStateSubsystem::SpawnStandIn(FActorState& State, const FScctWorldActor& Record)
{
	State.bSpawnTried = true;
	const FString& Name = GetName(Record.NameId);

	// The mesh of its (hidden) instance in the map's own file first, then the mesh the simulation names.
	UStaticMesh* Mesh = FindMesh(GetName(State.Info.RenderMeshId));
	if (Mesh == nullptr)
	{
		Mesh = FindMesh(GetName(Record.MeshId));
	}
	if (Mesh == nullptr)
	{
		++NumNothingToDraw;
		const FString Missing = GetName(Record.MeshId != 0 ? Record.MeshId : State.Info.RenderMeshId);
		if (!ReportedMissing.Contains(Missing))
		{
			ReportedMissing.Add(Missing);
			UE_LOG(LogOpenSCCT, Warning, TEXT("Level state: '%s' (%s) is shown by the simulation but its mesh '%s' was not imported; nothing is drawn for it and others like it"),
				*Name, *GetName(State.Info.ClassId), *Missing);
		}
		return false;
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Location(Record.X, Record.Y, Record.Z);
	const FRotator Rotation = FScctWorld::ToRotator(Record.Pitch, Record.Yaw, Record.Roll);
	AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(Location, Rotation, Parameters);
	if (Actor == nullptr)
	{
		return false;
	}

	UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); // the simulation owns collision
	Component->SetStaticMesh(Mesh);
	// It moves: the surface material's detail layer goes with it (the importer sets this on its own movable meshes).
	Component->SetCustomPrimitiveDataFloat(LevelLocalDetailSlot, 1.0f);
	LevelAvoidNanite(Component);
	FVector Scale(State.Info.ScaleX, State.Info.ScaleY, State.Info.ScaleZ);
	Actor->SetActorScale3D(Scale.IsNearlyZero() ? FVector::OneVector : Scale);
	Actor->Tags.Add(FScctWorld::LevelTag(*Name));

	State.Actor = Actor;
	State.Mesh = Component;
	State.bLevelActor = false;
	State.Location = Location;
	State.Rotation = Rotation;
	State.bShown = true;
	State.MeshId = Record.MeshId;
	Spawned.Add(Actor);
	TaggedActors.Add(FScctWorld::LevelTag(*Name), Actor);
	++NumStandIns;

	int32 Skins[ScctWorldSkinSlots] = {};
	if (World->GetActorSkins(Record.NameId, Skins, ScctWorldSkinSlots) > 0)
	{
		for (int32 Slot = 0; Slot < ScctWorldSkinSlots; ++Slot)
		{
			if (Skins[Slot] != 0)
			{
				ApplySkin(State, Slot, Skins[Slot]);
			}
		}
	}

	if (bLog)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: spawned '%s' (%s) as mesh %s at (%.0f, %.0f, %.0f)"), *Name, *GetName(State.Info.ClassId), *Mesh->GetName(), Location.X, Location.Y, Location.Z);
	}
	return true;
}

void UScctLevelStateSubsystem::ApplyMesh(FActorState& State, int32 MeshId)
{
	State.MeshId = MeshId;
	UStaticMeshComponent* Component = State.Mesh.Get();
	if (Component == nullptr || MeshId == 0)
	{
		// Showing no mesh (a pane that broke): the table clears Visible for it as well, which hides the actor.
		return;
	}

	const FString& Name = GetName(MeshId);
	// Back to the map's own mesh: the level actor's original, which may be one of its differently lit copies.
	UStaticMesh* Mesh = MeshId == State.Info.MeshId && State.OriginalMesh.IsValid() ? State.OriginalMesh.Get() : FindMesh(Name);
	if (Mesh == nullptr)
	{
		if (!ReportedMissing.Contains(Name))
		{
			ReportedMissing.Add(Name);
			UE_LOG(LogOpenSCCT, Warning, TEXT("Level state: '%s' changed to mesh '%s', which was not imported; it keeps the mesh it had"), *GetName(State.Info.NameId), *Name);
		}
		return;
	}
	Component->SetStaticMesh(Mesh);
	LevelAvoidNanite(Component);
}

void UScctLevelStateSubsystem::ApplySkin(FActorState& State, int32 Slot, int32 SkinId)
{
	UStaticMeshComponent* Component = State.Mesh.Get();
	if (Component == nullptr || Slot < 0 || Slot >= Component->GetNumMaterials())
	{
		return;
	}
	if (SkinId == 0)
	{
		// Back to the mesh's own material, or to the glowing copy of it a switched light keeps in this slot.
		Component->SetMaterial(Slot, FindFixtureMaterial(Component, Slot));
		return;
	}

	const FString& Name = GetName(SkinId);
	if (UMaterialInterface* Material = FindMaterial(Name))
	{
		Component->SetMaterial(Slot, Material);
		State.bSkinned = true;
		LevelAvoidNanite(Component);
	}
	else if (!ReportedMissing.Contains(Name))
	{
		ReportedMissing.Add(Name);
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: '%s' wears skin '%s' in slot %d, which was not imported; it keeps its material"), *GetName(State.Info.NameId), *Name, Slot);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------------------------------------------

void UScctLevelStateSubsystem::DressForLightPlan()
{
	if (bLightPlanDressed || !FScctLightPlan::IsOn())
	{
		return;
	}
	if (!FScctLightRules::DressLevel(GetWorld(), TaggedLights, [this](int32 Id) -> const FString& { return GetName(Id); }))
	{
		++LightPlanWaited;
		return;
	}
	bLightPlanDressed = true;
	// A light bound while the simulation's light table was not there yet has the strength it was made with: what
	// the plan gave it since is its strength now.
	int32 NumRebound = 0;
	for (FLightState& State : LightStates)
	{
		if (ULightComponent* Light = State.Light.Get())
		{
			State.OriginalIntensity = Light->Intensity;
			State.OriginalColor = Light->GetLightColor();
			State.AppliedScale = -1.0f;
			++NumRebound;
		}
	}
	if (LightPlanWaited > 0 || NumRebound > 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: the lighting plan dressed the lights %d frames into the match (%.2f s); %d lights bound before it took the plan's strength then."),
			LightPlanWaited, MatchSeconds, NumRebound);
	}
}

void UScctLevelStateSubsystem::UpdateLights(float DeltaSeconds)
{
	// One read a frame: the flicker draws a new value and the strobe flips on every read.
	int32 Total = World->GetLights(LightBuffer.GetData(), LightBuffer.Num());
	if (Total > LightBuffer.Num())
	{
		LightBuffer.SetNumUninitialized(Total + 64);
		return; // the next frame reads them all, once
	}
	const int32 Count = FMath::Clamp(Total, 0, LightBuffer.Num());
	if (LightStates.Num() < Count)
	{
		LightStates.SetNum(Count);
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctWorldLight& Record = LightBuffer[Index];
		FLightState& State = LightStates[Index];
		const bool bOn = (Record.Flags & EScctWorldLightFlags::On) != 0;
		const bool bTriggered = (Record.Flags & EScctWorldLightFlags::Triggered) != 0;
		const bool bBreaking = (Record.Flags & EScctWorldLightFlags::Breaking) != 0;
		const float Level = bOn ? FMath::Max(Record.Scale, 0.0f) : 0.0f;

		if (!State.bSeen || State.NameId != Record.NameId)
		{
			State = FLightState();
			State.NameId = Record.NameId;
			State.bSeen = true;
			State.bOn = bOn;
			State.Shown = State.MinScale = State.MaxScale = Level;
			BindLight(State, Record);
			NumLightsOffAtStart += bOn ? 0 : 1;
		}
		else
		{
			if (bOn != State.bOn)
			{
				++State.Switches;
				State.bShotOut = !bOn && State.bBreaking;
				if (State.bShotOut)
				{
					// The last of the flicker: the lamp dies with a few sparks.
					if (UScctEffectsSubsystem* Effects = GetWorld()->GetSubsystem<UScctEffectsSubsystem>())
					{
						Effects->SpawnSparkBurst(FVector(Record.X, Record.Y, Record.Z), FVector::DownVector, 8);
					}
				}
			}

			if (!bTriggered || bBreaking || (State.bShotOut && !bOn) || (bOn && IsAnimatedLightType(Record.LightType)))
			{
				// Pulses and flickers are the simulation's own curve; a shot-out lamp is dark at once.
				State.Shown = Level;
			}
			else if (!FMath::IsNearlyEqual(State.Shown, Level))
			{
				const float Seconds = Level > State.Shown ? LampFadeOnSeconds : LampFadeOffSeconds;
				State.Shown = FMath::FInterpConstantTo(State.Shown, Level, DeltaSeconds, FMath::Max3(Level, State.Shown, 1.0f) / Seconds);
			}
			State.bOn = bOn;
			State.MinScale = FMath::Min(State.MinScale, Level);
			State.MaxScale = FMath::Max(State.MaxScale, Level);
		}
		State.bBreaking = bBreaking;

		if (ULightComponent* Light = State.Light.Get())
		{
			if (!FMath::IsNearlyEqual(State.Shown, State.AppliedScale, 0.003f) || (State.Shown == 0.0f) != (State.AppliedScale == 0.0f))
			{
				State.AppliedScale = State.Shown;
				Light->SetIntensity(State.OriginalIntensity * State.Shown);
				const bool bVisible = State.Shown > 0.0f;
				if (Light->IsVisible() != bVisible)
				{
					Light->SetVisibility(bVisible);
				}
			}

			// A colour is applied only once the simulation changes it: until then the imported one stands.
			const FLinearColor Color(Record.R, Record.G, Record.B);
			if (!Color.Equals(State.AppliedColor, 0.004f))
			{
				State.AppliedColor = Color;
				Light->SetLightColor(Color.Equals(State.FirstColor, 0.004f) ? State.OriginalColor : ToLightColor(Color));
			}

			// A lamp on a skeletal prop's bone goes with the bone; one that rides a lift or a mover goes where the
			// table says.
			if ((Record.Flags & EScctWorldLightFlags::OnBone) != 0)
			{
				FollowBone(State, Record);
			}
			else if ((Record.Flags & EScctWorldLightFlags::Moved) != 0 && State.Actor.IsValid())
			{
				const FVector Location(Record.X, Record.Y, Record.Z);
				const FRotator Rotation = FScctWorld::ToRotator(Record.Pitch, Record.Yaw, Record.Roll);
				AActor* Actor = State.Actor.Get();
				if (!Location.Equals(Actor->GetActorLocation(), 0.005) || !Rotation.Equals(Actor->GetActorRotation(), 0.005))
				{
					State.bMoved = true;
					Actor->SetActorLocationAndRotation(Location, Rotation);
				}
			}
		}

		// The corona goes with its light, whether or not the level has a light for it: as bright, in the same
		// place, and gone when the simulation says a switched light draws none.
		const float Glow = FMath::Clamp(State.Shown, 0.0f, 1.0f);
		if (State.Coronas.Num() > 0)
		{
			const bool bCorona = Glow > 0.0f && (!bTriggered || (Record.Flags & EScctWorldLightFlags::Corona) != 0);
			const bool bFollow = (Record.Flags & EScctWorldLightFlags::Moved) != 0 && (Record.Flags & EScctWorldLightFlags::OnBone) == 0;
			const bool bChanged = bCorona != State.bCoronaShown || !FMath::IsNearlyEqual(Glow, State.AppliedCorona, 0.01f);
			if (bChanged || bFollow)
			{
				State.bCoronaShown = bCorona;
				State.AppliedCorona = Glow;
				for (const FCorona& Corona : State.Coronas)
				{
					AActor* Actor = Corona.Actor.Get();
					UStaticMeshComponent* Card = Corona.Card.Get();
					if (Actor == nullptr || Card == nullptr)
					{
						continue;
					}
					if (bChanged)
					{
						Actor->SetActorHiddenInGame(!bCorona);
						Card->SetCustomPrimitiveDataVector3(LevelCoronaColourSlot, FVector(Corona.OriginalColor.R, Corona.OriginalColor.G, Corona.OriginalColor.B) * Glow);
					}
					if (bFollow)
					{
						const FVector Location(Record.X, Record.Y, Record.Z);
						if (!Location.Equals(Actor->GetActorLocation(), 0.005))
						{
							Actor->SetActorLocation(Location);
						}
					}
				}
			}
		}

		// The lamp mesh glows as bright as its light, up to what it was imported with.
		if (State.Fixtures.Num() > 0 && !FMath::IsNearlyEqual(Glow, State.AppliedFixture, 0.01f))
		{
			State.AppliedFixture = Glow;
			for (const FFixture& Fixture : State.Fixtures)
			{
				if (UMaterialInstanceDynamic* Material = Fixture.Material.Get())
				{
					Material->SetScalarParameterValue(Fixture.Parameter, Fixture.Emissive * Glow);
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Skeletal props
// ---------------------------------------------------------------------------------------------------------------

void UScctLevelStateSubsystem::UpdateProps()
{
	int32 Total = World->GetAnimated(PropBuffer.GetData(), PropBuffer.Num());
	if (Total > PropBuffer.Num())
	{
		PropBuffer.SetNumUninitialized(Total + 16);
		Total = World->GetAnimated(PropBuffer.GetData(), PropBuffer.Num());
	}
	const int32 Count = FMath::Clamp(Total, 0, PropBuffer.Num());

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctWorldAnimated& Record = PropBuffer[Index];
		if (Record.NameId <= 0)
		{
			continue;
		}
		FPropState& State = PropStates.FindOrAdd(Record.NameId);
		if (!State.bBound)
		{
			BindProp(State, Record);
		}
		ASkeletalMeshActor* Actor = State.Actor.Get();
		USkeletalMeshComponent* Component = Actor != nullptr ? Actor->GetSkeletalMeshComponent() : nullptr;
		if (Component == nullptr)
		{
			continue;
		}

		// Its place: the table's, with the mesh's own rotation origin and scale on top as the importer put them.
		// One that never left its first place keeps the importer's transform.
		const FVector Location(Record.X, Record.Y, Record.Z);
		const FQuat Turn = FScctWorld::ToRotator(Record.Pitch, Record.Yaw, Record.Roll).Quaternion();
		if (State.bMoved || !Location.Equals(State.FirstLocation, 1.0) || Turn.AngularDistance(State.FirstQuat) > FMath::DegreesToRadians(0.5))
		{
			State.bMoved = true;
			const FVector TableScale(Record.ScaleX, Record.ScaleY, Record.ScaleZ);
			const FTransform Wanted(Turn * State.OriginQuat, Location + Turn.RotateVector(State.LocalOffset * TableScale), State.ScaleRatio * TableScale);
			if (!Wanted.Equals(Actor->GetActorTransform(), 0.005))
			{
				Actor->SetActorTransform(Wanted);
			}
		}

		// Its sequence.
		if (Record.SequenceId != State.SequenceId)
		{
			State.SequenceId = Record.SequenceId;
			State.Sequence = Record.SequenceId != 0 ? FindPropSequence(*Actor, GetName(Record.SequenceId)) : nullptr;
			if (Record.SequenceId != 0 && !State.Sequence.IsValid())
			{
				const FString Key = GetName(Record.NameId) + TEXT("/") + GetName(Record.SequenceId);
				if (!PropsReported.Contains(Key))
				{
					PropsReported.Add(Key);
					UE_LOG(LogOpenSCCT, Warning, TEXT("Level state: skeletal prop '%s' plays '%s', which was not imported with its mesh; it keeps what it showed"),
						*GetName(Record.NameId), *GetName(Record.SequenceId));
				}
			}
			else if (State.Sequence.IsValid())
			{
				if (Component->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
				{
					Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				}
				Component->SetAnimation(State.Sequence.Get());
				State.AppliedFrame = -1.0f;
				if (!State.bDriven)
				{
					State.bDriven = true;
					++NumPropsPlayed;
				}
			}
		}

		UAnimSequenceBase* Sequence = State.Sequence.Get();
		if (ScctStudioProp::Apply(*Component, Record, Sequence ? Sequence->GetPlayLength() : 0.f, *this))
		{
			State.AppliedFrame = -1.f;
			continue;
		}
		if (Sequence == nullptr)
		{
			continue;
		}
		if (Record.Frame < 0.0f)
		{
			// Only a loop is named (a fish swims): it plays at its own pace.
			if (!Component->IsPlaying())
			{
				Component->Play(true);
			}
		}
		else
		{
			// The frame the simulation is at, every frame: the channel's rate, its stops and its loops are its own.
			if (Component->IsPlaying())
			{
				Component->Stop();
			}
			const float Frame = FMath::Clamp(Record.Frame, 0.0f, 1.0f);
			if (Frame != State.AppliedFrame)
			{
				State.AppliedFrame = Frame;
				Component->SetPosition(Frame * Sequence->GetPlayLength(), false);
			}
		}
	}
}

void UScctLevelStateSubsystem::BindProp(FPropState& State, const FScctWorldAnimated& Record)
{
	State.bBound = true;
	const FString& Name = GetName(Record.NameId);
	ASkeletalMeshActor* Actor = Cast<ASkeletalMeshActor>(TaggedActors.FindRef(FScctWorld::LevelTag(*Name)).Get());
	USkeletalMeshComponent* Component = Actor != nullptr ? Actor->GetSkeletalMeshComponent() : nullptr;
	if (Component == nullptr)
	{
		return;
	}

	State.ImportedTransform = Actor->GetActorTransform();
	// A fish has already picked a random position and heading by the first draw. Only the authored
	// placement separates the mesh's fixed axis correction from the actor's live swimming pose.
	FScctWorldActorInfo Placement{};
	if (!World->GetActorInfo(Record.NameId, &Placement))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Level state: no map placement for skeletal prop '%s'"), *Name);
		return;
	}
	State.Actor = Actor;
	State.FirstLocation = FVector(Placement.X, Placement.Y, Placement.Z);
	State.FirstQuat = FScctWorld::ToRotator(Placement.Pitch, Placement.Yaw, Placement.Roll).Quaternion();
	State.OriginQuat = State.FirstQuat.Inverse() * State.ImportedTransform.GetRotation();
	const FVector Scale(Placement.ScaleX, Placement.ScaleY, Placement.ScaleZ);
	const FVector Imported = Actor->GetActorScale3D();
	State.ScaleRatio = FVector(
		FMath::Abs(Scale.X) > UE_SMALL_NUMBER ? Imported.X / Scale.X : 1.0,
		FMath::Abs(Scale.Y) > UE_SMALL_NUMBER ? Imported.Y / Scale.Y : 1.0,
		FMath::Abs(Scale.Z) > UE_SMALL_NUMBER ? Imported.Z / Scale.Z : 1.0);
	// Any shift the importer gave the actor from the table's place (a pivot) goes with the prop when it moves,
	// turned and scaled with it.
	const FVector Shift = State.FirstQuat.UnrotateVector(Actor->GetActorLocation() - State.FirstLocation);
	State.LocalOffset = FVector(
		FMath::Abs(Scale.X) > UE_SMALL_NUMBER ? Shift.X / Scale.X : Shift.X,
		FMath::Abs(Scale.Y) > UE_SMALL_NUMBER ? Shift.Y / Scale.Y : Shift.Y,
		FMath::Abs(Scale.Z) > UE_SMALL_NUMBER ? Shift.Z / Scale.Z : Shift.Z);
	if (Shift.Size() > 1.0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: skeletal prop '%s' was imported %.1f cm from the table's place; the shift goes with it"), *Name, Shift.Size());
	}
	State.OriginalAnimation = Component->AnimationData.AnimToPlay;
	if (UAnimationAsset* Original = Component->AnimationData.AnimToPlay)
	{
		PropAssets.AddUnique(Original);
	}
	State.bOriginalPlaying = Component->AnimationData.bSavedPlaying;
	State.bOriginalLooping = Component->AnimationData.bSavedLooping;
	++NumPropsBound;
}

UAnimSequenceBase* UScctLevelStateSubsystem::FindPropSequence(const ASkeletalMeshActor& Actor, const FString& Sequence)
{
	const USkeletalMeshComponent* Component = Actor.GetSkeletalMeshComponent();
	const UObject* Mesh = Component != nullptr ? Component->GetSkinnedAsset() : nullptr;
	if (Mesh == nullptr || Sequence.IsEmpty())
	{
		return nullptr;
	}

	// The importer brings a prop's sequences in beside its mesh (Tools/Import/scct_fx.py, prop_assets).
	const FString Folder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
	TArray<TWeakObjectPtr<UAnimSequenceBase>>* Found = PropSequences.Find(Folder);
	if (Found == nullptr)
	{
		Found = &PropSequences.Add(Folder);
		TArray<FAssetData> Assets;
		ScanLevelFolder(Folder, UAnimSequenceBase::StaticClass(), Assets);
		for (const FAssetData& Asset : Assets)
		{
			if (UAnimSequenceBase* Loaded = Cast<UAnimSequenceBase>(Asset.GetAsset()))
			{
				Found->Add(Loaded);
				PropAssets.Add(Loaded);
			}
		}
	}

	// By name: the same letters, or the importer's mesh name put before the sequence's.
	const FString Wanted = LevelKey(Sequence);
	UAnimSequenceBase* Suffixed = nullptr;
	for (const TWeakObjectPtr<UAnimSequenceBase>& Each : *Found)
	{
		UAnimSequenceBase* Candidate = Each.Get();
		if (Candidate == nullptr)
		{
			continue;
		}
		const FString Key = LevelKey(Candidate->GetName());
		if (Key == Wanted)
		{
			return Candidate;
		}
		if (Suffixed == nullptr && Key.EndsWith(Wanted))
		{
			Suffixed = Candidate;
		}
	}
	// A file with one sequence comes in under the mesh's name ("<mesh>_Anim"), whatever the sequence was called.
	if (Suffixed == nullptr && Found->Num() == 1)
	{
		Suffixed = (*Found)[0].Get();
	}
	return Suffixed;
}

bool UScctLevelStateSubsystem::FollowBone(FLightState& State, const FScctWorldLight& Record)
{
	AActor* LightActor = State.Actor.Get();
	const FPropState* Prop = PropStates.Find(Record.AttachNameId);
	const ASkeletalMeshActor* PropActor = Prop != nullptr ? Prop->Actor.Get() : nullptr;
	const USkeletalMeshComponent* Component = PropActor != nullptr ? PropActor->GetSkeletalMeshComponent() : nullptr;
	if (LightActor == nullptr || Component == nullptr)
	{
		return false;
	}
	const int32 Bone = Component->GetBoneIndex(FName(*GetName(Record.AttachBoneId)));
	if (Bone == INDEX_NONE)
	{
		const FString Key = GetName(Record.NameId) + TEXT("@") + GetName(Record.AttachBoneId);
		if (!PropsReported.Contains(Key))
		{
			PropsReported.Add(Key);
			UE_LOG(LogOpenSCCT, Warning, TEXT("Level state: light '%s' hangs on bone '%s' of '%s', which the imported mesh does not have; it stays where it was imported"),
				*GetName(Record.NameId), *GetName(Record.AttachBoneId), *GetName(Record.AttachNameId));
		}
		return false;
	}

	// Where the light was imported, against the bone as it stood when it was first seen: the importer placed both
	// from the same first frame, so this keeps the original's offset without depending on the bone's axes.
	FTransform BoneNow = Component->GetBoneTransform(Bone);
	BoneNow.SetScale3D(FVector::OneVector);
	if (!State.bOnBone)
	{
		State.bOnBone = true;
		FTransform Placed = State.OriginalTransform;
		Placed.SetScale3D(FVector::OneVector);
		State.BoneOffset = Placed.GetRelativeTransform(BoneNow);
		++NumBoneLights;
		USceneComponent* Root = LightActor->GetRootComponent();
		if (Root != nullptr && Root->Mobility != EComponentMobility::Movable)
		{
			Root->SetMobility(EComponentMobility::Movable);
		}
	}
	FTransform Wanted = State.BoneOffset * BoneNow;
	Wanted.SetScale3D(State.OriginalTransform.GetScale3D());
	if (!Wanted.Equals(LightActor->GetActorTransform(), 0.005))
	{
		State.bMoved = true;
		LightActor->SetActorTransform(Wanted);
	}
	return true;
}

void UScctLevelStateSubsystem::BindLight(FLightState& State, const FScctWorldLight& Record)
{
	State.FirstColor = State.AppliedColor = FLinearColor(Record.R, Record.G, Record.B);
	const FString& Name = GetName(Record.NameId);

	// The corona cards first: many of the original's coronas sit on lights the level has no light for.
	TArray<TWeakObjectPtr<AActor>> Cards;
	TaggedCoronas.MultiFind(FScctWorld::LevelTag(*Name), Cards);
	for (const TWeakObjectPtr<AActor>& Each : Cards)
	{
		const AStaticMeshActor* CardActor = Cast<AStaticMeshActor>(Each.Get());
		UStaticMeshComponent* Card = CardActor != nullptr ? CardActor->GetStaticMeshComponent() : nullptr;
		if (Card == nullptr)
		{
			continue;
		}
		FCorona& Corona = State.Coronas.AddDefaulted_GetRef();
		Corona.Actor = Each;
		Corona.Card = Card;
		Corona.OriginalLocation = CardActor->GetActorLocation();
		Corona.bOriginalHidden = CardActor->IsHidden();
		const TArray<float>& Data = Card->GetCustomPrimitiveData().Data.Num() > LevelCoronaColourSlot + 2 ? Card->GetCustomPrimitiveData().Data : Card->GetDefaultCustomPrimitiveData().Data;
		if (Data.Num() > LevelCoronaColourSlot + 2)
		{
			Corona.OriginalColor = FLinearColor(Data[LevelCoronaColourSlot], Data[LevelCoronaColourSlot + 1], Data[LevelCoronaColourSlot + 2]);
		}
		++NumCoronasBound;
	}

	ALight* Actor = Cast<ALight>(TaggedLights.FindRef(FScctWorld::LevelTag(*Name)).Get());
	ULightComponent* Light = Actor != nullptr ? Actor->GetLightComponent() : nullptr;
	if (Light == nullptr)
	{
		// The importer leaves out the lights the map hides; the table lists them all the same.
		NamesWithoutActor.Add(Name);
		return;
	}

	State.Actor = Actor;
	State.Light = Light;
	State.OriginalTransform = Actor->GetActorTransform();
	State.OriginalIntensity = Light->Intensity;
	State.OriginalColor = Light->GetLightColor();
	State.bOriginalVisible = Light->IsVisible();
	++NumLightsFound;

	// One owner for the rhythm: the importer's light function animates this light by itself; while a match plays
	// the simulation's value is applied instead, so the function is held steady (its picture and shape stay).
	const bool bImporterAnimates = Actor->Tags.ContainsByPredicate([](const FName& Tag) { return Tag.ToString().StartsWith(LevelLightTypeTagPrefix); });
	if (bImporterAnimates && Light->LightFunctionMaterial != nullptr)
	{
		State.OriginalLightFunction = Light->LightFunctionMaterial;
		UMaterialInstanceDynamic* Steady = UMaterialInstanceDynamic::Create(Light->LightFunctionMaterial, Light);
		Steady->SetScalarParameterValue(LevelLightTypeParameter, LevelSteadyLightType);
		Light->SetLightFunctionMaterial(Steady);
		State.bLightFunctionHeld = true;
		++NumRhythmsHeld;
	}

	if ((Record.Flags & EScctWorldLightFlags::Triggered) != 0)
	{
		BindFixtures(State);
	}
}

UMaterialInstanceDynamic* UScctLevelStateSubsystem::FindFixtureMaterial(const UStaticMeshComponent* Component, int32 Slot) const
{
	for (const FLightState& State : LightStates)
	{
		for (const FFixture& Fixture : State.Fixtures)
		{
			if (Fixture.Mesh.Get() == Component && Fixture.Slot == Slot)
			{
				return Fixture.Material.Get();
			}
		}
	}
	return nullptr;
}

void UScctLevelStateSubsystem::BindFixtures(FLightState& State)
{
	// The lamp of a switched light is the nearest small mesh beside it that glows by itself (the importer gives
	// the original's unlit lamp, screen and sign materials an "Emissive" strength).
	const FVector At = State.Actor->GetActorLocation();
	UStaticMeshComponent* Best = nullptr;
	double BestDistance = UE_BIG_NUMBER;
	for (const TPair<FName, TWeakObjectPtr<AActor>>& Pair : TaggedActors)
	{
		const AStaticMeshActor* Actor = Cast<AStaticMeshActor>(Pair.Value.Get());
		UStaticMeshComponent* Component = Actor != nullptr ? Actor->GetStaticMeshComponent() : nullptr;
		if (Component == nullptr || Component->Bounds.BoxExtent.GetMax() > FixtureMaxExtent)
		{
			continue;
		}
		const double Reach = Pair.Key.ToString().StartsWith(BreakableTagPrefix) ? FixtureReachBreakable : FixtureReach;
		const double Distance = Component->Bounds.GetBox().ComputeSquaredDistanceToPoint(At);
		if (Distance >= BestDistance || Distance > FMath::Square(Reach))
		{
			continue;
		}
		for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
		{
			float Emissive = 0.0f;
			const UMaterialInterface* Material = Component->GetMaterial(Slot);
			// A lamp another light of the same fitting already took glows with that one.
			if (Material != nullptr && !Material->IsA<UMaterialInstanceDynamic>() && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(LevelEmissiveParameter), Emissive) && Emissive > 0.0f)
			{
				Best = Component;
				BestDistance = Distance;
				break;
			}
		}
	}

	// The beam and flare cards the original's artists laid on this lamp (the importer's glow cards) go dark with
	// it as well. A card belongs to the light that stands in or right beside it; a beam is long, so no size limit.
	TArray<UStaticMeshComponent*> Lit;
	if (Best != nullptr)
	{
		Lit.Add(Best);
	}
	for (const TPair<FName, TWeakObjectPtr<AActor>>& Pair : TaggedActors)
	{
		const AStaticMeshActor* Actor = Cast<AStaticMeshActor>(Pair.Value.Get());
		UStaticMeshComponent* Component = Actor != nullptr ? Actor->GetStaticMeshComponent() : nullptr;
		if (Component == nullptr || Component == Best || Component->Bounds.GetBox().ComputeSquaredDistanceToPoint(At) > FMath::Square(FixtureReach))
		{
			continue;
		}
		for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
		{
			float Edge = 0.0f;
			const UMaterialInterface* Material = Component->GetMaterial(Slot);
			if (Material != nullptr && !Material->IsA<UMaterialInstanceDynamic>() && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(LevelCardEdgeParameter), Edge))
			{
				Lit.Add(Component);
				++NumGlowCards;
				break;
			}
		}
	}

	if (bLog)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: switched light '%s': %s"), *GetName(State.NameId),
			Best == nullptr ? TEXT("no glowing lamp mesh beside it") : *FString::Printf(TEXT("its lamp is mesh '%s', %.0f cm away"), *Best->GetOwner()->GetActorNameOrLabel(), FMath::Sqrt(BestDistance)));
	}
	for (UStaticMeshComponent* Mesh : Lit)
	{
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = Mesh->GetMaterial(Slot);
			if (Material == nullptr || Material->IsA<UMaterialInstanceDynamic>())
			{
				continue;
			}
			// A lamp glows by "Emissive"; a beam or flare card (the one material with "EdgeFade") by "Brightness".
			float Glow = 0.0f;
			float Edge = 0.0f;
			FName Parameter = LevelEmissiveParameter;
			if (Material->GetScalarParameterValue(FHashedMaterialParameterInfo(LevelCardEdgeParameter), Edge))
			{
				Parameter = LevelCardBrightnessParameter;
			}
			if (!Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Parameter), Glow) || Glow <= 0.0f)
			{
				continue;
			}
			FFixture Fixture;
			Fixture.Parameter = Parameter;
			Fixture.Mesh = Mesh;
			Fixture.Slot = Slot;
			Fixture.Emissive = Glow;
			Fixture.OriginalOverride = Mesh->OverrideMaterials.IsValidIndex(Slot) ? Mesh->OverrideMaterials[Slot].Get() : nullptr;
			UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, Mesh);
			Mesh->SetMaterial(Slot, Dynamic);
			Fixture.Material = Dynamic;
			State.Fixtures.Add(Fixture);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------------------------------

void UScctLevelStateSubsystem::DrainEvents()
{
	UScctEffectsSubsystem* Effects = GetWorld()->GetSubsystem<UScctEffectsSubsystem>();
	for (;;)
	{
		const int32 Count = World->GetEvents(EventBuffer.GetData(), EventBuffer.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FScctWorldEvent& Event = EventBuffer[Index];
			if (Event.Kind > 0 && Event.Kind < UE_ARRAY_COUNT(NumEvents))
			{
				++NumEvents[Event.Kind];
			}
			const FVector At(Event.X, Event.Y, Event.Z);
			const FString& Name = GetName(Event.NameId);

			switch (Event.Kind)
			{
			case EScctWorldEventKind::MeshChanged:
				// Applied from the actor's state in UpdateActors; this is the record of it.
				UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s '%s' now shows mesh '%s' (was '%s')"), MatchSeconds, *Name, *GetName(Event.A), *GetName(Event.B));
				break;

			case EScctWorldEventKind::SkinChanged:
				if (ActorStates.IsValidIndex(Event.NameId) && ActorStates[Event.NameId].bSeen)
				{
					ApplySkin(ActorStates[Event.NameId], Event.A, Event.B);
				}
				if (bLog)
				{
					UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s '%s' slot %d now wears '%s'"), MatchSeconds, *Name, Event.A, *GetName(Event.B));
				}
				break;

			case EScctWorldEventKind::GlassBroken:
				UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s glass '%s' broke at (%.0f, %.0f, %.0f)%s"), MatchSeconds, *Name, Event.X, Event.Y, Event.Z,
					Event.A != 0 ? TEXT(", hit point known") : TEXT(""));
				if (Effects != nullptr)
				{
					Effects->SpawnGlassBurst(At, Event.A != 0 ? FVector(Event.DX, Event.DY, Event.DZ) : FVector::ZeroVector);
				}
				BreakPaneShards(Event);
				break;

			case EScctWorldEventKind::LightSwitched:
				UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s light '%s' switched %s (light type %d)"), MatchSeconds, *Name, Event.A != 0 ? TEXT("on") : TEXT("off"), Event.B);
				break;

			case EScctWorldEventKind::LightBreaking:
				UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s light '%s' was shot out at (%.0f, %.0f, %.0f) and flickers"), MatchSeconds, *Name, Event.X, Event.Y, Event.Z);
				if (Effects != nullptr)
				{
					Effects->SpawnSparkBurst(At, FVector::DownVector, 22);
				}
				break;

			case EScctWorldEventKind::MarkerChanged:
				if (bLog)
				{
					UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s marker '%s' state %d flags %d"), MatchSeconds, *Name, Event.A, Event.B);
				}
				break;

			case EScctWorldEventKind::EmitterTriggered:
			{
				const int32 Reached = TriggerEmitters(Name) + MeshParticles.Trigger(Event.NameId);
				NumEmittersReached += Reached;
				if (bLog || Reached == 0)
				{
					UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s emitter '%s' triggered at (%.0f, %.0f, %.0f): %d drawable sub-emitters"), MatchSeconds, *Name, Event.X, Event.Y, Event.Z, Reached);
				}
				break;
			}

			case EScctWorldEventKind::MoverMoving:
				if (ActorStates.IsValidIndex(Event.NameId))
				{
					ActorStates[Event.NameId].Starts += Event.A != 0 ? 1 : 0;
				}
				// A searchlight or a fan starts and stops all match: each one is said only on request, the travel
				// of every mover is in the summary.
				if (bLog)
				{
					UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s mover '%s' %s at (%.0f, %.0f, %.0f), key %d"), MatchSeconds, *Name,
						Event.A != 0 ? TEXT("starts moving") : TEXT("stops"), Event.X, Event.Y, Event.Z, Event.B);
				}
				break;

			case EScctWorldEventKind::ShownChanged:
				if (Event.A == 0 && Event.B == 1 && ActorStates.IsValidIndex(Event.NameId))
				{
					// A prop spawned in play was destroyed and left the table: what stood for it goes too, and the
					// name starts afresh if the simulation spawns it again.
					FActorState& State = ActorStates[Event.NameId];
					if (!State.bLevelActor && State.Actor.IsValid())
					{
						TaggedActors.Remove(FScctWorld::LevelTag(*Name));
						Spawned.Remove(State.Actor.Get());
						State.Actor->Destroy();
					}
					State = FActorState();
				}
				if (bLog)
				{
					UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s '%s' %s"), MatchSeconds, *Name, Event.A != 0 ? TEXT("shown") : Event.B != 0 ? TEXT("destroyed") : TEXT("hidden"));
				}
				break;

			default:
				break;
			}
		}
		if (Count < EventBuffer.Num())
		{
			break;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Reports
// ---------------------------------------------------------------------------------------------------------------

void UScctLevelStateSubsystem::LogBound()
{
	int32 NumFollowed = 0;
	int32 NumMovers = 0;
	for (const FActorState& State : ActorStates)
	{
		NumFollowed += State.bSeen ? 1 : 0;
		NumMovers += State.bSeen && (State.Info.Flags & EScctWorldActorInfoFlags::Mover) != 0 ? 1 : 0;
	}
	int32 NumSwitched = 0;
	int32 NumFixtures = 0;
	for (int32 Index = 0; Index < LightStates.Num(); ++Index)
	{
		NumSwitched += (LightBuffer[Index].Flags & EScctWorldLightFlags::Triggered) != 0 ? 1 : 0;
		NumFixtures += LightStates[Index].Fixtures.Num() > 0 ? 1 : 0;
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Level state: bound at the first frame: %d followed actors (%d are level actors, %d of them hidden by the simulation at the start, %d made movable here), %d movers; "
		"%d lights (%d found in the level, %d off at the start, %d switched, %d of those with a lamp mesh that glows with them)"),
		NumFollowed, NumLevelActors, NumHiddenAtStart, NumMobilityChanged, NumMovers, LightStates.Num(), NumLightsFound, NumLightsOffAtStart, NumSwitched, NumFixtures);
	UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %d level actors that have not moved stand within %.3f cm and %.3f degrees of where the table puts them"), PlacementChecked, PlacementError, PlacementTurnError);
	UE_LOG(LogOpenSCCT, Log, TEXT("Level state: map effects that go with their lights: %d corona cards of %d in the level are bound to a light of the table, %d beam or flare cards dim with a switched light, %d lights the importer animates take the simulation's rhythm instead for the match"),
		NumCoronasBound, TaggedCoronas.Num(), NumGlowCards, NumRhythmsHeld);

	if (bLog)
	{
		for (const FActorState& State : ActorStates)
		{
			if (State.bSeen && (State.Info.Flags & EScctWorldActorInfoFlags::Mover) != 0)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Level state: mover '%s' begins at (%.0f, %.0f, %.0f), mesh '%s', scale (%.2f, %.2f, %.2f), pivot (%.0f, %.0f, %.0f)"), *GetName(State.Info.NameId),
					State.FirstLocation.X, State.FirstLocation.Y, State.FirstLocation.Z, *GetName(State.Info.MeshId), State.Info.ScaleX, State.Info.ScaleY, State.Info.ScaleZ,
					State.Info.PrePivotX, State.Info.PrePivotY, State.Info.PrePivotZ);
			}
		}
		for (int32 Index = 0; Index < LightStates.Num(); ++Index)
		{
			const FScctWorldLight& Light = LightBuffer[Index];
			if ((Light.Flags & EScctWorldLightFlags::Triggered) != 0)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Level state: switched light '%s' at (%.0f, %.0f, %.0f) is %s, light type %d, %d glowing lamp materials"), *GetName(Light.NameId), Light.X, Light.Y, Light.Z,
					(Light.Flags & EScctWorldLightFlags::On) != 0 ? TEXT("on") : TEXT("off"), Light.LightType, LightStates[Index].Fixtures.Num());
			}
		}
	}

	// Every name the table sends that the level has nothing for, by class (lights have none).
	TMap<FString, TArray<FString>> ByClass;
	for (const FString& Name : NamesWithoutActor)
	{
		FString Class = TEXT("light");
		if (World->FindName(*Name) != 0)
		{
			const int32 Id = World->FindName(*Name);
			if (ActorStates.IsValidIndex(Id) && ActorStates[Id].bSeen)
			{
				const FActorState& State = ActorStates[Id];
				Class = GetName(State.Info.ClassId)
					+ ((State.Info.Flags & EScctWorldActorInfoFlags::HiddenInMap) != 0 ? TEXT(", hidden in the map") : TEXT(""))
					+ ((State.Info.Flags & EScctWorldActorInfoFlags::Spawned) != 0 ? TEXT(", spawned in play") : TEXT(""));
			}
		}
		ByClass.FindOrAdd(Class).Add(Name);
	}
	for (const TPair<FString, TArray<FString>>& Pair : ByClass)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: no level actor for %d x %s: %s"), Pair.Value.Num(), *Pair.Key, *FString::Join(Pair.Value, TEXT(" ")));
	}

	// Movable level meshes the table does not follow: the importer made them movable, the simulation never moves them.
	TArray<FString> Unmentioned;
	for (const FName& Tag : MovableTags)
	{
		if (!MentionedTags.Contains(Tag))
		{
			Unmentioned.Add(Tag.ToString().RightChop(FCString::Strlen(LevelTagPrefix)));
		}
	}
	if (Unmentioned.Num() > 0)
	{
		Unmentioned.Sort();
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %d movable level actors are never mentioned by the table: %s"), Unmentioned.Num(), *FString::Join(Unmentioned, TEXT(" ")));
	}
}

void UScctLevelStateSubsystem::LogSummary()
{
	int32 NumMovers = 0;
	int32 NumMoved = 0;
	for (const FActorState& State : ActorStates)
	{
		if (!State.bSeen || (State.Info.Flags & EScctWorldActorInfoFlags::Mover) == 0)
		{
			continue;
		}
		++NumMovers;
		const bool bMoved = State.MaxOffset > 1.0f || State.MaxTurn > 1.0f;
		NumMoved += bMoved ? 1 : 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: mover '%s' went up to %.1f cm and turned up to %.1f degrees from where it began (%d starts)%s"),
			*GetName(State.Info.NameId), State.MaxOffset, State.MaxTurn, State.Starts,
			State.Actor.IsValid() ? (State.bLevelActor ? TEXT("") : TEXT(", drawn by an actor spawned here"))
				: State.bSpawnTried ? TEXT(", NOT DRAWN: its mesh was not imported") : TEXT(", hidden by the simulation all match"));
	}

	int32 NumChanged = 0;
	for (int32 Index = 0; Index < LightStates.Num(); ++Index)
	{
		const FLightState& State = LightStates[Index];
		if (State.Switches > 0 || State.MaxScale - State.MinScale > 0.02f)
		{
			++NumChanged;
			if (State.Switches > 0 || bLog)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Level state: light '%s' switched %d times, brightness scale %.2f to %.2f, ends %s%s%s"), *GetName(State.NameId), State.Switches,
					State.MinScale, State.MaxScale, State.bOn ? TEXT("on") : State.bShotOut ? TEXT("shot out") : TEXT("off"),
					State.Fixtures.Num() > 0 ? TEXT(", lamp mesh follows") : TEXT(""), State.Light.IsValid() ? TEXT("") : TEXT(", NOT IN THE LEVEL"));
			}
		}
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Level state summary after %.0f s: %d of %d movers moved; %d of %d lights changed; %d actors spawned here for names the level lacks, %d shown names with no imported mesh; "
		"events: %d mesh, %d skin, %d glass, %d light switch, %d light break, %d marker, %d mover start/stop, %d shown/hidden, %d emitter triggers (%d particle emitters reached)"),
		MatchSeconds, NumMoved, NumMovers, NumChanged, LightStates.Num(), NumStandIns, NumNothingToDraw,
		NumEvents[EScctWorldEventKind::MeshChanged], NumEvents[EScctWorldEventKind::SkinChanged], NumEvents[EScctWorldEventKind::GlassBroken],
		NumEvents[EScctWorldEventKind::LightSwitched], NumEvents[EScctWorldEventKind::LightBreaking], NumEvents[EScctWorldEventKind::MarkerChanged],
		NumEvents[EScctWorldEventKind::MoverMoving], NumEvents[EScctWorldEventKind::ShownChanged], NumEvents[EScctWorldEventKind::EmitterTriggered], NumEmittersReached);

	int32 PropsMoved = 0;
	for (const TPair<int32, FPropState>& Pair : PropStates)
	{
		PropsMoved += Pair.Value.bMoved ? 1 : 0;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Level state: skeletal props: %d listed, %d found in the level, %d played by the simulation, %d moved; %d lights followed a prop's bone"),
		PropStates.Num(), NumPropsBound, NumPropsPlayed, PropsMoved, NumBoneLights);
	LogAttachedEmitters();
}
