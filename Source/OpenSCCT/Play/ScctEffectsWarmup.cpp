// The effects warm-up behind the loading page (Play/ScctEffectsWarmup.h), and the effects subsystem's samples for it
// (UScctEffectsSubsystem::SpawnWarmupSamples and ClearWarmupSamples).

#include "Play/ScctEffectsWarmup.h"

#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Hud/ScctVisionSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Menu/ScctLoadingSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "PipelineStateCache.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Play/ScctSessionSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "ShaderCompiler.h"
#include "ShaderPipelineCache.h"

namespace
{
	/** The goggle modes, in the order they are held: night, thermal, motion, EMF. */
	constexpr int32 WarmupVisionModes[] = { 34, 36, 37, 38 };
	/** Frames each mode is held: its first frame fades it in, the next draw it whole enough to use every pass. */
	constexpr int32 WarmupFramesPerMode = 3;
	/** The fewest frames the samples are drawn, so particles, lights and decals are each drawn more than once. */
	constexpr int32 WarmupLeastFrames = 16;
	/** The longest the page is held for compilation that does not end (a first run of an uncooked game). */
	constexpr double WarmupLongestSeconds = 20.0;
	/** The longest the page covers the running match at all, counted from its first frame, however slow the frames are. */
	constexpr double WarmupMostCoverSeconds = 30.0;
	/** The longest the effects may take to become ready after the match started, in frames. */
	constexpr int32 WarmupReadyFrameCap = 120;

	/** Where the samples are drawn: this far in front of the camera, with the backdrop for the decals behind them. */
	constexpr float WarmupSampleDistance = 70.0f;
	constexpr float WarmupBackdropDistance = 110.0f;

	/** The effect meshes' grid: cells this wide, this many to a row. */
	constexpr float WarmupMeshCell = 7.0f;
	constexpr int32 WarmupMeshColumns = 12;

	/** -ScctWarmupLog: how long play is measured, and the slow frame that is logged as it happens. */
	constexpr double WarmupMeasureSeconds = 30.0;
	constexpr double WarmupSlowFrameMs = 50.0;

	/** -ScctWarmupGoggleTest: from when, how long each mode is held, and how many frames after a switch are looked at. */
	constexpr double WarmupGoggleTestStart = 10.0;
	constexpr double WarmupGoggleTestHold = 1.5;
	constexpr int32 WarmupGoggleFramesWatched = 4;

	const TCHAR* const WarmupSolidMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxSolid.M_ScctFxSolid");
	const TCHAR* const WarmupProjectionMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxProjection.M_ScctFxProjection");

	int32 WarmupShaderJobs()
	{
		return GShaderCompilingManager != nullptr ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// The effects subsystem's samples
// ---------------------------------------------------------------------------------------------------------------

int32 UScctEffectsSubsystem::SpawnWarmupSamples(const FVector& At, const FVector& Forward, const FVector& Backdrop)
{
	if (!bSceneReady || !IsValid(Host))
	{
		return INDEX_NONE;
	}
	const FVector Ahead = Forward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
	const FVector Up = FVector::CrossProduct(Ahead, Right);
	USceneComponent* Root = Host->GetRootComponent();

	auto AddMesh = [this, Root](UStaticMesh* Mesh, UMaterialInterface* Material, const FVector& Where, const FRotator& Turn, const FVector& Scale)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Host, NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetStaticMesh(Mesh);
		if (Material != nullptr)
		{
			Component->SetMaterial(0, Material);
		}
		Component->SetupAttachment(Root);
		Component->RegisterComponent();
		Component->SetWorldLocationAndRotation(Where, Turn);
		Component->SetWorldScale3D(Scale);
		WarmupComponents.Add(Component);
		return Component;
	};

	// A dark card behind the samples, for the decals to land on whatever the camera faces.
	UMaterialInterface* Solid = LoadObject<UMaterialInterface>(nullptr, WarmupSolidMaterialPath);
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		// The engine's plane is 100 cm square facing +Z: turned to face the camera.
		AddMesh(Plane, StandInMaterial != nullptr ? StandInMaterial.Get() : Solid, Backdrop, FRotationMatrix::MakeFromZX(-Ahead, Right).Rotator(), FVector(1.2f));
	}

	// Every particle look, at once.
	SpawnExplosion(At - Up * 20.0);
	SpawnFlashBang(At + Right * 20.0);
	SpawnMuzzleFlash(At - Right * 15.0, Ahead, 1.0f, false, false, false);
	SpawnMuzzleFlash(At - Right * 10.0, Ahead, 1.0f, true, false, false);
	SpawnMuzzleFlash(At - Right * 5.0, Ahead, 1.0f, false, true, true);
	for (const EStyle Style : { EStyle::Smoke, EStyle::Gas, EStyle::Wisp, EStyle::Sparks, EStyle::Electric, EStyle::Flare, EStyle::Water })
	{
		float Debt = 3.0f;
		Emit(Style, At + Up * 10.0, FVector::ZeroVector, 0.0f, Debt);
	}
	for (const TCHAR* Surface : { TEXT("Shocker"), TEXT("Blood"), TEXT("Water"), TEXT("Metal"), TEXT("Wood"), TEXT("Glass"), TEXT("Sand"), TEXT("Concrete") })
	{
		SpawnImpact(Backdrop - Ahead * 2.0, -Ahead, FString(Surface), FString());
	}
	SpawnGlassBurst(At + Up * 15.0, -Up);
	SpawnSparkBurst(At - Up * 10.0, Up, 8);
	SpawnPs2ModemEffect(At, Ahead.Rotation());
	Sprites.DrawStreak(At - Right * 30.0, At + Right * 30.0, 3.5f, FLinearColor(6.0f, 4.0f, 1.5f, 1.0f));
	// Timed lights both ways (the explosion and the flash bang above cast shadows).
	AcquireLight(At + Up * 20.0, FLinearColor(1.0f, 0.8f, 0.5f), 80.0f, 600.0f, 0.4f, false);

	// The bullet mark and the projection decal, on the card.
	const FRotator IntoCard = Ahead.Rotation();
	if (DecalMaterial != nullptr)
	{
		UDecalComponent* Decal = NewObject<UDecalComponent>(Host, NAME_None, RF_Transient);
		Decal->SetDecalMaterial(DecalMaterial);
		Decal->SetupAttachment(Root);
		Decal->RegisterComponent();
		Decal->DecalSize = FVector(8.0, 12.0, 12.0);
		Decal->SetWorldLocationAndRotation(Backdrop - Right * 20.0, IntoCard);
		TintDecal(Decal->CreateDynamicMaterialInstance(), TEXT("Concrete"), FString());
		WarmupComponents.Add(Decal);
	}
	if (UMaterialInterface* Projection = ProjectionMaterial != nullptr ? ProjectionMaterial.Get() : LoadObject<UMaterialInterface>(nullptr, WarmupProjectionMaterialPath))
	{
		UDecalComponent* Decal = NewObject<UDecalComponent>(Host, NAME_None, RF_Transient);
		Decal->SetDecalMaterial(Projection);
		Decal->SetupAttachment(Root);
		Decal->RegisterComponent();
		Decal->SortOrder = 1;
		Decal->DecalSize = FVector(8.0, 12.0, 12.0);
		Decal->SetWorldLocationAndRotation(Backdrop + Right * 20.0, IntoCard);
		WarmupComponents.Add(Decal);
	}

	// A casing and a stand-in shape.
	if (EnsureCasingMesh())
	{
		AddMesh(CasingMesh, CasingStandInMaterial, At + Up * 18.0 - Right * 18.0, IntoCard, CasingScale)->SetCastShadow(false);
	}
	if (StandInMesh != nullptr)
	{
		AddMesh(StandInMesh, StandInMaterial, At + Up * 18.0 + Right * 18.0, FRotator::ZeroRotator, FVector(0.05f));
	}

	// Every imported effect mesh, each shrunk into its cell of a grid across the view.
	TArray<UStaticMesh*> Loaded;
	for (const TPair<FString, FSoftObjectPath>& Asset : MeshAssets)
	{
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.Value.TryLoad()))
		{
			Loaded.Add(Mesh);
		}
	}
	const int32 Rows = FMath::DivideAndRoundUp(Loaded.Num(), WarmupMeshColumns);
	for (int32 Index = 0; Index < Loaded.Num(); ++Index)
	{
		UStaticMesh* Mesh = Loaded[Index];
		const int32 Column = Index % WarmupMeshColumns;
		const int32 Row = Index / WarmupMeshColumns;
		const FVector Cell = At + Right * ((Column - (WarmupMeshColumns - 1) * 0.5f) * WarmupMeshCell) + Up * (((Rows - 1) * 0.5f - Row) * WarmupMeshCell);
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const float Fit = WarmupMeshCell * 0.45f / FMath::Max(static_cast<float>(Bounds.SphereRadius), 0.01f);
		// Its own materials, so each is drawn as the effect draws it; centred on its cell.
		UStaticMeshComponent* Component = AddMesh(Mesh, nullptr, Cell - Bounds.Origin * Fit, FRotator::ZeroRotator, FVector(Fit));
		Component->SetCastShadow(true);
	}
	return Loaded.Num();
}

void UScctEffectsSubsystem::ClearWarmupSamples()
{
	for (USceneComponent* Component : WarmupComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	WarmupComponents.Reset();
	// The samples' timed flashes; a light an effect actor holds is the match's own and stays.
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		if (Lights[Index].bUsed && Lights[Index].Life > 0.0f)
		{
			ReleaseLight(Index);
		}
	}
	Sprites.Clear();
}

// ---------------------------------------------------------------------------------------------------------------
// The warm-up
// ---------------------------------------------------------------------------------------------------------------

bool UScctEffectsWarmupSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctEffectsWarmupSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	Collection.InitializeDependency<UScctEffectsSubsystem>();
	if (Session == nullptr)
	{
		return;
	}
	bDisabled = FParse::Param(FCommandLine::Get(), TEXT("ScctNoWarmup"));
	bLog = FParse::Param(FCommandLine::Get(), TEXT("ScctWarmupLog"));
	bGoggleTest = bLog && FParse::Param(FCommandLine::Get(), TEXT("ScctWarmupGoggleTest"));
	Session->OnMatchStarted.AddUObject(this, &UScctEffectsWarmupSubsystem::HandleMatchStarted);
	Session->OnSessionChanged.AddUObject(this, &UScctEffectsWarmupSubsystem::HandleSessionChanged);
	Session->OnFrame.AddUObject(this, &UScctEffectsWarmupSubsystem::HandleFrame);
}

void UScctEffectsWarmupSubsystem::Deinitialize()
{
	if (Phase == EPhase::Showing)
	{
		Finish(TEXT("the level closed"));
	}
	if (Session != nullptr)
	{
		Session->OnMatchStarted.RemoveAll(this);
		Session->OnSessionChanged.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	Super::Deinitialize();
}

void UScctEffectsWarmupSubsystem::HandleMatchStarted()
{
	if (Phase != EPhase::Idle)
	{
		return;
	}
	if (bDisabled || IsRunningDedicatedServer())
	{
		Phase = EPhase::Done;
		UE_LOG(LogOpenSCCT, Log, TEXT("Warm-up: none (%s)"), bDisabled ? TEXT("-ScctNoWarmup") : TEXT("no picture"));
		bMeasuring = bLog;
		LastFrameSeconds = 0.0;
		return;
	}

	// The level is still inside its BeginPlay and the movie player's page is up: the same page takes over from it.
	Phase = EPhase::Showing;
	StartSeconds = FPlatformTime::Seconds();
	Frames = 0;
	SpawnFrame = INDEX_NONE;
	VisionStep = 0;
	if (UScctLoadingSubsystem* Loading = GetWorld()->GetGameInstance() != nullptr ? GetWorld()->GetGameInstance()->GetSubsystem<UScctLoadingSubsystem>() : nullptr)
	{
		Loading->BeginCover();
	}
	// The PSO cache compiles what it has recorded in large batches while nothing is played.
	FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Fast);
	UE_LOG(LogOpenSCCT, Log, TEXT("Warm-up: the loading page stays up while every effect is drawn once"));
}

void UScctEffectsWarmupSubsystem::HandleSessionChanged(int32 OldId, int32 NewId)
{
	if (Phase == EPhase::Showing && NewId == 0)
	{
		Finish(TEXT("the match ended"));
	}
}

void UScctEffectsWarmupSubsystem::HandleFrame(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();
	const double FrameMs = LastFrameSeconds > 0.0 ? (Now - LastFrameSeconds) * 1000.0 : 0.0;
	LastFrameSeconds = Now;
	MatchSeconds += DeltaSeconds;

	if (Phase == EPhase::Showing)
	{
		if (StepWarmup())
		{
			Finish(nullptr);
			// The frame that ends the warm-up is not one of play.
			LastFrameSeconds = FPlatformTime::Seconds();
		}
		return;
	}
	if (bMeasuring && FrameMs > 0.0)
	{
		MeasurePlayFrame(FrameMs);
	}
}

bool UScctEffectsWarmupSubsystem::StepWarmup()
{
	// Timed from the first frame of the match: the level's own first frame (its streaming, the bridge's first tick)
	// came before it and is not the warm-up's.
	if (++Frames == 1)
	{
		StartSeconds = FPlatformTime::Seconds();
	}
	const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
	// The match runs on behind the page (the server does not wait for this client): on a machine so busy that each
	// frame takes seconds, the page goes after this long whatever is left, so the player never misses much of play.
	if (Frames > 1 && Elapsed >= WarmupMostCoverSeconds)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Warm-up: the page has covered the match for %.0f s (%d frames, samples %s, %d of 4 goggle modes held): it goes now, what is left compiles in play"),
			Elapsed, Frames, SpawnFrame != INDEX_NONE ? TEXT("drawn") : TEXT("not drawn yet"), FMath::Clamp(VisionStep, 0, 4));
		return true;
	}
	UScctEffectsSubsystem* Effects = GetWorld()->GetSubsystem<UScctEffectsSubsystem>();
	UScctVisionSubsystem* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>();

	// The first frame lets the camera take its place; then the samples, once the effects are ready.
	if (SpawnFrame == INDEX_NONE && Frames >= 2)
	{
		FVector Eye = FVector::ZeroVector;
		FRotator Look = FRotator::ZeroRotator;
		APlayerController* Player = GetWorld()->GetFirstPlayerController();
		if (Player != nullptr && Player->PlayerCameraManager != nullptr)
		{
			Player->GetPlayerViewPoint(Eye, Look);
		}
		else
		{
			const FScctBridgeView& View = Session->GetView();
			Eye = FVector(View.X, View.Y, View.Z);
			Look = FRotator(View.Pitch, View.Yaw, View.Roll);
		}
		const FVector Forward = Look.Vector();
		const int32 Meshes = Effects != nullptr ? Effects->SpawnWarmupSamples(Eye + Forward * WarmupSampleDistance, Forward, Eye + Forward * WarmupBackdropDistance) : 0;
		if (Meshes != INDEX_NONE || Frames >= WarmupReadyFrameCap)
		{
			SpawnFrame = Frames;
			SpawnSeconds = FPlatformTime::Seconds();
			NumMeshes = FMath::Max(Meshes, 0);
			NumGlass = SpawnGlassSamples(Eye + Forward * (WarmupSampleDistance + 5.0f), Forward);
			if (bLog)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Warm-up: samples drawn on frame %d (%.0f ms in) in front of the eye at (%.0f, %.0f, %.0f) looking %.0f, %.0f: %d effect meshes, %d glass materials, %u PSO precache requests and %d shader jobs waiting"),
					Frames, Elapsed * 1000.0, Eye.X, Eye.Y, Eye.Z, Look.Yaw, Look.Pitch, NumMeshes, NumGlass, PipelineStateCache::NumActivePrecacheRequests(), WarmupShaderJobs());
			}
		}
		return false;
	}
	if (SpawnFrame == INDEX_NONE)
	{
		return false;
	}

	// Each goggle mode in turn, then plain sight again.
	const int32 Since = Frames - SpawnFrame;
	const int32 ModeIndex = Since / WarmupFramesPerMode;
	if (Vision != nullptr)
	{
		if (ModeIndex < static_cast<int32>(UE_ARRAY_COUNT(WarmupVisionModes)))
		{
			Vision->HoldModeForWarmup(WarmupVisionModes[ModeIndex]);
			VisionStep = ModeIndex + 1;
		}
		else if (VisionStep <= static_cast<int32>(UE_ARRAY_COUNT(WarmupVisionModes)))
		{
			Vision->HoldModeForWarmup(INDEX_NONE);
			VisionStep = UE_ARRAY_COUNT(WarmupVisionModes) + 1;
		}
	}

	const uint32 Requests = PipelineStateCache::NumActivePrecacheRequests();
	const int32 Jobs = WarmupShaderJobs();
	PeakPrecacheRequests = FMath::Max(PeakPrecacheRequests, Requests);
	PeakShaderJobs = FMath::Max(PeakShaderJobs, Jobs);
	const bool bVisionDone = Since >= static_cast<int32>(UE_ARRAY_COUNT(WarmupVisionModes)) * WarmupFramesPerMode + 1;
	const bool bCompiled = Requests == 0 && Jobs == 0;
	// Every sample and every goggle mode is drawn whatever the time; only the wait for compilation is capped, counted
	// from when the samples went up (the level's own first frame can take long on a busy machine and is not this wait).
	if (!bVisionDone || Since < WarmupLeastFrames)
	{
		return false;
	}
	if (bCompiled)
	{
		return true;
	}
	if (FPlatformTime::Seconds() - SpawnSeconds >= WarmupLongestSeconds)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Warm-up: still compiling after %.0f s (%u PSO precache requests, %d shader jobs): the page goes, what is left compiles in play"),
			WarmupLongestSeconds, Requests, Jobs);
		return true;
	}
	return false;
}

int32 UScctEffectsWarmupSubsystem::SpawnGlassSamples(const FVector& At, const FVector& Forward)
{
	UScctLevelStateSubsystem* LevelState = GetWorld()->GetSubsystem<UScctLevelStateSubsystem>();
	if (LevelState == nullptr)
	{
		return 0;
	}
	TArray<UStaticMeshComponent*> Panes;
	LevelState->GatherPaneMeshes(Panes);
	// One piece per glass material (with the custom data of the first pane that wears it).
	TArray<TPair<UMaterialInterface*, const UStaticMeshComponent*>> Looks;
	for (const UStaticMeshComponent* Pane : Panes)
	{
		for (int32 Slot = 0; Slot < Pane->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = Pane->GetMaterial(Slot);
			if (Material != nullptr && !Looks.ContainsByPredicate([Material](const TPair<UMaterialInterface*, const UStaticMeshComponent*>& Each) { return Each.Key == Material; }))
			{
				Looks.Emplace(Material, Pane);
			}
		}
	}
	if (Looks.Num() == 0)
	{
		return 0;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GlassHost = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (GlassHost == nullptr)
	{
		return 0;
	}
	USceneComponent* Root = NewObject<USceneComponent>(GlassHost, TEXT("Root"), RF_Transient);
	Root->SetMobility(EComponentMobility::Movable);
	GlassHost->SetRootComponent(Root);
	Root->RegisterComponent();

	const FVector Ahead = Forward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
	const FVector Up = FVector::CrossProduct(Ahead, Right);
	// A small slab as a shard is built: two faces and no tangents, from a procedural mesh like the shards'.
	const float Half = 4.0f;
	const TArray<FVector> Vertices = { FVector(0, -Half, -Half), FVector(0, Half, -Half), FVector(0, Half, Half), FVector(0, -Half, Half) };
	const TArray<int32> Indices = { 0, 2, 1, 0, 3, 2, 0, 1, 2, 0, 2, 3 };
	const TArray<FVector> Normals = { -FVector::XAxisVector, -FVector::XAxisVector, -FVector::XAxisVector, -FVector::XAxisVector };
	const TArray<FVector2D> Uvs = { FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) };
	const TArray<FColor> NoColors;
	const TArray<FProcMeshTangent> NoTangents;
	for (int32 Index = 0; Index < Looks.Num(); ++Index)
	{
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(GlassHost, NAME_None, RF_Transient);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->bUseAsyncCooking = false;
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->CreateMeshSection(0, Vertices, Indices, Normals, Uvs, NoColors, NoTangents, false);
		Mesh->SetMaterial(0, Looks[Index].Key);
		const TArray<float>& Custom = Looks[Index].Value->GetDefaultCustomPrimitiveData().Data;
		for (int32 Slot = 0; Slot < Custom.Num(); ++Slot)
		{
			Mesh->SetDefaultCustomPrimitiveDataFloat(Slot, Custom[Slot]);
		}
		Mesh->SetupAttachment(Root);
		Mesh->RegisterComponent();
		const FVector Where = At + Right * ((Index % 8 - 3.5f) * 9.0f) - Up * (28.0f + (Index / 8) * 9.0f);
		Mesh->SetWorldLocationAndRotation(Where, FRotationMatrix::MakeFromXZ(Ahead, Up).Rotator());
	}
	return Looks.Num();
}

void UScctEffectsWarmupSubsystem::Finish(const TCHAR* Why)
{
	const double Ms = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	Phase = EPhase::Done;
	if (UScctEffectsSubsystem* Effects = GetWorld()->GetSubsystem<UScctEffectsSubsystem>())
	{
		Effects->ClearWarmupSamples();
	}
	if (UScctVisionSubsystem* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>())
	{
		Vision->HoldModeForWarmup(INDEX_NONE);
	}
	if (IsValid(GlassHost))
	{
		GlassHost->Destroy();
	}
	GlassHost = nullptr;
	if (UScctLoadingSubsystem* Loading = GetWorld()->GetGameInstance() != nullptr ? GetWorld()->GetGameInstance()->GetSubsystem<UScctLoadingSubsystem>() : nullptr)
	{
		Loading->EndCover();
	}
	FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Background);

	UE_LOG(LogOpenSCCT, Log, TEXT("Warm-up: done in %.0f ms%s%s (%d frames; %d effect meshes, %d glass materials and %d goggle modes drawn; at most %u PSO precache requests and %d shader jobs waiting, %u and %d left)"),
		Ms, Why != nullptr ? TEXT(", cut short: ") : TEXT(""), Why != nullptr ? Why : TEXT(""), Frames, NumMeshes, NumGlass, FMath::Min(FMath::Max(VisionStep, 0), 4),
		PeakPrecacheRequests, PeakShaderJobs, PipelineStateCache::NumActivePrecacheRequests(), WarmupShaderJobs());

	bMeasuring = bLog;
	PlaySeconds = 0.0;
}

void UScctEffectsWarmupSubsystem::MeasurePlayFrame(double FrameMs)
{
	PlaySeconds += FrameMs / 1000.0;
	++NumPlayFrames;
	if (FrameMs > WorstMs)
	{
		WorstMs = FrameMs;
		WorstAt = PlaySeconds;
	}
	NumOver33 += FrameMs > 33.3 ? 1 : 0;
	NumOver50 += FrameMs > 50.0 ? 1 : 0;
	NumOver100 += FrameMs > 100.0 ? 1 : 0;
	if (FrameMs > WarmupSlowFrameMs)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Warm-up log: a frame of %.0f ms at %.2f s of play (match second %.2f)"), FrameMs, PlaySeconds, MatchSeconds);
	}
	if (GoggleFramesLeft > 0)
	{
		GoggleWorstMs = FMath::Max(GoggleWorstMs, FrameMs);
		if (--GoggleFramesLeft == 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Warm-up log: goggles mode %d on: worst of the next %d frames %.0f ms"),
				GoggleMode, WarmupGoggleFramesWatched, GoggleWorstMs);
		}
	}
	if (bGoggleTest)
	{
		StepGoggleTest();
	}
	if (PlaySeconds >= WarmupMeasureSeconds)
	{
		bMeasuring = false;
		UE_LOG(LogOpenSCCT, Log, TEXT("Warm-up log: worst frame in the first %.0f s of play %.0f ms at %.2f s (%d frames, %d over 33 ms, %d over 50 ms, %d over 100 ms; warm-up %s)"),
			WarmupMeasureSeconds, WorstMs, WorstAt, NumPlayFrames, NumOver33, NumOver50, NumOver100, bDisabled ? TEXT("off") : TEXT("on"));
		if (UScctVisionSubsystem* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>(); Vision != nullptr && bGoggleTest)
		{
			Vision->HoldModeForWarmup(INDEX_NONE);
		}
	}
}

void UScctEffectsWarmupSubsystem::StepGoggleTest()
{
	UScctVisionSubsystem* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>();
	if (Vision == nullptr)
	{
		return;
	}
	const int32 Count = UE_ARRAY_COUNT(WarmupVisionModes);
	const int32 Wanted = PlaySeconds < WarmupGoggleTestStart ? 0 : static_cast<int32>((PlaySeconds - WarmupGoggleTestStart) / WarmupGoggleTestHold) + 1;
	// One step at a time, so a long frame that jumps past a switch time does not skip a mode; a switch waits for the
	// frames after the previous one to be measured.
	if (Wanted <= GoggleStep || GoggleStep > Count || GoggleFramesLeft > 0)
	{
		return;
	}
	++GoggleStep;
	if (GoggleStep >= 1 && GoggleStep <= Count)
	{
		GoggleMode = WarmupVisionModes[GoggleStep - 1];
		Vision->HoldModeForWarmup(GoggleMode);
		GoggleFramesLeft = WarmupGoggleFramesWatched;
		GoggleWorstMs = 0.0;
	}
	else if (GoggleStep > Count)
	{
		Vision->HoldModeForWarmup(INDEX_NONE);
	}
}
