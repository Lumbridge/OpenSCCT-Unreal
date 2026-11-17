#include "Play/ScctLevelStateSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Light.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctLevelStateDebug.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	const TCHAR* const LevelTagPrefix = TEXT("Scct:");
	const TCHAR* const LevelEffectMeshFolder = TEXT("/Game/Imported/Fx");
	const FName LevelEmissiveParameter(TEXT("Emissive"));

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

	/** The map's light colours are display values (from hue and saturation); the importer turned them the same way. */
	FLinearColor ToLightColor(const FLinearColor& Display)
	{
		return FLinearColor(FMath::Pow(FMath::Max(Display.R, 0.0f), 2.2f), FMath::Pow(FMath::Max(Display.G, 0.0f), 2.2f), FMath::Pow(FMath::Max(Display.B, 0.0f), 2.2f));
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

	// -ScctLevelDo="movers@8;breaklights@14"
	FString Actions;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctLevelDo="), Actions, false))
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
	RestoreLevel();
	Names.Reset();
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
	PlacementError = 0.0f;
	PlacementChecked = 0;
	FMemory::Memzero(NumEvents);
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

	IndexLevel();

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
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			if (!Tag.ToString().StartsWith(LevelTagPrefix))
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
			TaggedActors.Add(Tag, *It);
			const AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(*It);
			if (MeshActor != nullptr && MeshActor->GetStaticMeshComponent() != nullptr && MeshActor->GetStaticMeshComponent()->Mobility == EComponentMobility::Movable)
			{
				MovableTags.Add(Tag);
			}
		}
	}

	// The map's meshes and materials are original content imported on the player's machine, so they are found by
	// looking, not by fixed references. A mesh of the map's own file is named Package_Group_Name, with "_vc" and
	// eight hex digits added for each differently lit copy; the plain name stands for any of them.
	const FString MapFolder = FPackageName::GetLongPackagePath(GetWorld()->GetOutermost()->GetName());
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

	UE_LOG(LogOpenSCCT, Log, TEXT("Level state: the level has %d tagged actors (%d movable meshes, %d lights); %d imported meshes and %d materials can be swapped in (%s)"),
		TaggedActors.Num() + TaggedLights.Num(), MovableTags.Num(), NumLights, MeshAssets.Num(), MaterialAssets.Num(), *MapFolder);
}

void UScctLevelStateSubsystem::RestoreLevel()
{
	const UWorld* ThisWorld = GetWorld();
	const bool bTearingDown = ThisWorld == nullptr || ThisWorld->bIsTearingDown;

	for (FActorState& State : ActorStates)
	{
		AActor* Actor = State.Actor.Get();
		if (!State.bSeen || !State.bLevelActor || Actor == nullptr || bTearingDown)
		{
			continue;
		}
		if (State.AttachedPawnId != 0)
		{
			Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
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
			if (State.bMoved && State.Actor.IsValid())
			{
				State.Actor->SetActorTransform(State.OriginalTransform);
			}
		}
		for (const FFixture& Fixture : State.Fixtures)
		{
			if (UStaticMeshComponent* Mesh = Fixture.Mesh.Get())
			{
				Mesh->SetMaterial(Fixture.Slot, Fixture.OriginalMaterial.Get());
			}
		}
	}
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
		Names.Emplace(Buffer);
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
	RefreshNames();
	RunDebugActions();
	UpdateActors();
	UpdateLights(DeltaSeconds);
	DrainEvents();

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
				AScctPawnProxy* Proxy = Session->FindProxy(Record.AttachPawnId);
				if (Proxy != nullptr && Proxy->AttachToBone(Actor->GetRootComponent(), GetName(Record.AttachBoneId), FTransform(Rotation, Location)))
				{
					State.AttachedPawnId = Record.AttachPawnId;
				}
			}
			continue;
		}
		if (State.AttachedPawnId != 0)
		{
			State.AttachedPawnId = 0;
			Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			State.Location = FVector(UE_BIG_NUMBER);
		}

		// An actor that never left its map placement keeps the importer's transform; one spawned here has no
		// other placement than the table's.
		const bool bPlace = (Record.Flags & EScctWorldActorFlags::Moved) != 0 || !State.bLevelActor;
		if (bPlace && (!Location.Equals(State.Location, 0.005) || !Rotation.Equals(State.Rotation, 0.005)))
		{
			State.Location = Location;
			State.Rotation = Rotation;
			Actor->SetActorLocationAndRotation(Location, Rotation);
		}
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
	if (Root != nullptr && Root->Mobility != EComponentMobility::Movable)
	{
		Root->SetMobility(EComponentMobility::Movable);
		State.bMobilityChanged = true;
		++NumMobilityChanged;
	}

	// Evidence that the table's placement and the importer's are the same.
	if ((Record.Flags & (EScctWorldActorFlags::Moved | EScctWorldActorFlags::Attached)) == 0)
	{
		++PlacementChecked;
		PlacementError = FMath::Max(PlacementError, static_cast<float>(FVector::Dist(State.Location, State.FirstLocation)));
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
		// Back to the mesh's own material.
		Component->SetMaterial(Slot, nullptr);
		return;
	}

	const FString& Name = GetName(SkinId);
	if (UMaterialInterface* Material = FindMaterial(Name))
	{
		Component->SetMaterial(Slot, Material);
		State.bSkinned = true;
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

			if (!bTriggered || bBreaking || (State.bShotOut && !bOn))
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

			// A lamp that rides a lift or a mover. Lights hung on a skeletal prop's bone stay where they were
			// imported: the skeletal props are not drawn yet.
			if ((Record.Flags & EScctWorldLightFlags::Moved) != 0 && (Record.Flags & EScctWorldLightFlags::OnBone) == 0 && State.Actor.IsValid())
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

		// The lamp mesh glows as bright as its light, up to what it was imported with.
		const float Glow = FMath::Clamp(State.Shown, 0.0f, 1.0f);
		if (State.Fixtures.Num() > 0 && !FMath::IsNearlyEqual(Glow, State.AppliedFixture, 0.01f))
		{
			State.AppliedFixture = Glow;
			for (const FFixture& Fixture : State.Fixtures)
			{
				if (UMaterialInstanceDynamic* Material = Fixture.Material.Get())
				{
					Material->SetScalarParameterValue(LevelEmissiveParameter, Fixture.Emissive * Glow);
				}
			}
		}
	}
}

void UScctLevelStateSubsystem::BindLight(FLightState& State, const FScctWorldLight& Record)
{
	State.FirstColor = State.AppliedColor = FLinearColor(Record.R, Record.G, Record.B);
	const FString& Name = GetName(Record.NameId);
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
	if ((Record.Flags & EScctWorldLightFlags::Triggered) != 0)
	{
		BindFixtures(State);
	}
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

	if (bLog)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: switched light '%s': %s"), *GetName(State.NameId),
			Best == nullptr ? TEXT("no glowing lamp mesh beside it") : *FString::Printf(TEXT("its lamp is mesh '%s', %.0f cm away"), *Best->GetOwner()->GetActorNameOrLabel(), FMath::Sqrt(BestDistance)));
	}
	if (Best == nullptr)
	{
		return;
	}

	for (int32 Slot = 0; Slot < Best->GetNumMaterials(); ++Slot)
	{
		float Emissive = 0.0f;
		UMaterialInterface* Material = Best->GetMaterial(Slot);
		if (Material == nullptr || Material->IsA<UMaterialInstanceDynamic>() || !Material->GetScalarParameterValue(FHashedMaterialParameterInfo(LevelEmissiveParameter), Emissive) || Emissive <= 0.0f)
		{
			continue;
		}
		FFixture Fixture;
		Fixture.Mesh = Best;
		Fixture.Slot = Slot;
		Fixture.Emissive = Emissive;
		Fixture.OriginalMaterial = Material;
		UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, Best);
		Best->SetMaterial(Slot, Dynamic);
		Fixture.Material = Dynamic;
		State.Fixtures.Add(Fixture);
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

			case EScctWorldEventKind::MoverMoving:
				if (ActorStates.IsValidIndex(Event.NameId))
				{
					ActorStates[Event.NameId].Starts += Event.A != 0 ? 1 : 0;
				}
				UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s mover '%s' %s at (%.0f, %.0f, %.0f), key %d"), MatchSeconds, *Name,
					Event.A != 0 ? TEXT("starts moving") : TEXT("stops"), Event.X, Event.Y, Event.Z, Event.B);
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
	UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %d level actors that have not moved stand within %.3f cm of where the table puts them"), PlacementChecked, PlacementError);

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
		"events: %d mesh, %d skin, %d glass, %d light switch, %d light break, %d marker, %d mover start/stop, %d shown/hidden"),
		MatchSeconds, NumMoved, NumMovers, NumChanged, LightStates.Num(), NumStandIns, NumNothingToDraw,
		NumEvents[EScctWorldEventKind::MeshChanged], NumEvents[EScctWorldEventKind::SkinChanged], NumEvents[EScctWorldEventKind::GlassBroken],
		NumEvents[EScctWorldEventKind::LightSwitched], NumEvents[EScctWorldEventKind::LightBreaking], NumEvents[EScctWorldEventKind::MarkerChanged],
		NumEvents[EScctWorldEventKind::MoverMoving], NumEvents[EScctWorldEventKind::ShownChanged]);
}
