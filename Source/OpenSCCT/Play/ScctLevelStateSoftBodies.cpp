#include "Play/ScctLevelStateSoftBodies.h"

#include "Play/ScctEchelonLightFilter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctWorldApi.h"

namespace
{
	/** The importer's tag on a placed soft body mesh: "ScctSoftBody:patch" or "ScctSoftBody:strip". */
	const TCHAR* SoftBodyTagPrefix = TEXT("ScctSoftBody:");

	/**
	 * A body counts as drawn this frame when Unreal drew it on screen within this many seconds. The engine marks a
	 * primitive drawn on screen only on the frames its occlusion test says it is certainly in sight, which for a thin
	 * see-through cloth is not every frame.
	 */
	constexpr float DrawnWithin = 1.0f;

	/** The bridge steps a body not drawn for this long no more; its mesh is not rewritten either. */
	constexpr float DrawnRecently = 5.0f;
}

bool UScctSoftBodySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctSoftBodySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	if (Session == nullptr)
	{
		return;
	}

	Session->OnMatchStarted.AddUObject(this, &UScctSoftBodySubsystem::HandleMatchStarted);
	Session->OnMatchStopped.AddUObject(this, &UScctSoftBodySubsystem::HandleMatchStopped);
	Session->OnSessionChanged.AddUObject(this, &UScctSoftBodySubsystem::HandleSessionChanged);
	Session->OnFrame.AddUObject(this, &UScctSoftBodySubsystem::HandleFrame);

	bLog = FParse::Param(FCommandLine::Get(), TEXT("ScctSoftBodyLog"));

	// -ScctSoftBodyTest="walk@6;shoot@10"
	FString Actions;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctSoftBodyTest="), Actions, false))
	{
		bLog = true;
		TArray<FString> Entries;
		Actions.ParseIntoArray(Entries, TEXT(";"));
		for (const FString& Entry : Entries)
		{
			FString What;
			FString When;
			if (Entry.Split(TEXT("@"), &What, &When))
			{
				FTestAction Action;
				Action.What = What.TrimStartAndEnd().ToLower();
				Action.Seconds = FCString::Atof(*When);
				Tests.Add(MoveTemp(Action));
			}
		}
	}
}

void UScctSoftBodySubsystem::Deinitialize()
{
	Restore();
	if (Session != nullptr)
	{
		Session->OnMatchStarted.RemoveAll(this);
		Session->OnMatchStopped.RemoveAll(this);
		Session->OnSessionChanged.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	Super::Deinitialize();
}

void UScctSoftBodySubsystem::HandleMatchStarted()
{
	Restore();
	Build();
}

void UScctSoftBodySubsystem::HandleMatchStopped()
{
	Restore();
}

void UScctSoftBodySubsystem::HandleSessionChanged(int32 OldId, int32 NewId)
{
	if (NewId != OldId)
	{
		Restore();
	}
}

void UScctSoftBodySubsystem::Build()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("ScctSoftBodyOff")))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Soft bodies: left as the level's still meshes (-ScctSoftBodyOff)"));
		return;
	}

	FString Error;
	Api = ScctFeature<FScctSoftBodyApi>(&Error);
	if (Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Soft bodies: none this match, the bridge has no soft body table: %s"), *Error);
		return;
	}

	FScctSoftBodyState State = {};
	if (Api->GetState(&State) == 0)
	{
		return;
	}

	// The importer's soft body meshes, by the map actor name of their "Scct:" tag.
	TMap<FName, UStaticMeshComponent*> Tagged;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		bool bSoft = false;
		FName Name;
		for (const FName& Tag : It->Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(SoftBodyTagPrefix))
			{
				bSoft = true;
			}
			else if (Text.StartsWith(TEXT("Scct:")))
			{
				Name = Tag;
			}
		}
		if (bSoft && !Name.IsNone())
		{
			if (UStaticMeshComponent* Mesh = It->FindComponentByClass<UStaticMeshComponent>())
			{
				Tagged.Add(Name, Mesh);
			}
		}
	}

	TArray<int32> Indices;
	TArray<float> RawUVs;
	TArray<FColor> NoColors;
	TArray<FProcMeshTangent> NoTangents;
	int32 Missing = 0;
	float WorstOffset = 0.0f;
	for (int32 Index = 0; Index < State.Count; ++Index)
	{
		FBody Body;
		if (Api->GetInfo(Index, &Body.Info) == 0)
		{
			continue;
		}
		TCHAR NameBuffer[128];
		const int32 Length = Api->GetName(Index, NameBuffer, UE_ARRAY_COUNT(NameBuffer));
		Body.Name = FString::ConstructFromPtrSize(NameBuffer, Length);

		UStaticMeshComponent* const* Found = Tagged.Find(FScctWorld::LevelTag(*Body.Name));
		if (Found == nullptr || *Found == nullptr)
		{
			// Bodies are kept by index so the bridge's seen flags line up; one without a mesh is just never drawn.
			++Missing;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Soft bodies: %s has no level mesh tagged ScctSoftBody"), *Body.Name);
			Bodies.Add(MoveTemp(Body));
			continue;
		}
		UStaticMeshComponent* LevelMesh = *Found;
		AActor* Owner = LevelMesh->GetOwner();

		const int32 Vertices = Body.Info.Vertices;
		Indices.SetNumUninitialized(Body.Info.Indices);
		RawUVs.SetNumUninitialized(Vertices * 2);
		if (Api->GetMesh(Index, Indices.GetData(), Indices.Num(), RawUVs.GetData(), RawUVs.Num()) == 0)
		{
			++Missing;
			Bodies.Add(MoveTemp(Body));
			continue;
		}
		Body.UVs.SetNumUninitialized(Vertices);
		for (int32 V = 0; V < Vertices; ++V)
		{
			Body.UVs[V] = FVector2D(RawUVs[V * 2], RawUVs[V * 2 + 1]);
		}
		Body.Positions.SetNumZeroed(Vertices);
		Body.Normals.SetNumZeroed(Vertices);
		VertexBuffer.SetNumUninitialized(FMath::Max(VertexBuffer.Num(), Body.Info.Particles * 6));
		if (Api->GetVertices(Index, VertexBuffer.GetData(), VertexBuffer.Num()) == Body.Info.Particles)
		{
			const int32 N = Body.Info.Particles;
			for (int32 P = 0; P < N; ++P)
			{
				const float* V = &VertexBuffer[P * 6];
				Body.Positions[P] = Body.Positions[P + N] = FVector(V[0], V[1], V[2]);
				Body.Normals[P] = Body.Normals[P + N] = FVector(V[3], V[4], V[5]);
			}
		}

		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, *FString::Printf(TEXT("SoftBody_%s"), *Body.Name), RF_Transient);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->bUseAsyncCooking = false;
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(LevelMesh->CastShadow);
		Mesh->CreateMeshSection(0, Body.Positions, Indices, Body.Normals, Body.UVs, NoColors, NoTangents, false);
		for (int32 Slot = 0; Slot < LevelMesh->GetNumMaterials(); ++Slot)
		{
			Mesh->SetMaterial(Slot, LevelMesh->GetMaterial(Slot));
		}
		// The importer's per-mesh switches for the shared surface material (world-aligned detail, baked colours).
		const TArray<float>& Custom = LevelMesh->GetDefaultCustomPrimitiveData().Data;
		for (int32 Slot = 0; Slot < Custom.Num(); ++Slot)
		{
			Mesh->SetDefaultCustomPrimitiveDataFloat(Slot, Custom[Slot]);
		}
		if (auto* Filter = GetWorld()->GetSubsystem<UScctEchelonLightFilter>()) Filter->RouteReceiverComponent(*Mesh);
		Mesh->RegisterComponent();
		Mesh->SetWorldTransform(LevelMesh->GetComponentTransform());
		Owner->AddInstanceComponent(Mesh);

		Body.LevelMesh = LevelMesh;
		Body.Mesh = Mesh;
		Body.bLevelMeshWasVisible = LevelMesh->IsVisible();
		LevelMesh->SetVisibility(false);

		// The bridge places the body from the map's render data, the level mesh is where the importer put it: they
		// must agree, or pawns and shots would push it in the wrong place.
		const FVector BridgeCenter(Body.Info.CenterX, Body.Info.CenterY, Body.Info.CenterZ);
		const float Offset = FVector::Dist(BridgeCenter, Mesh->Bounds.Origin);
		WorstOffset = FMath::Max(WorstOffset, Offset);

		// A camera for -ScctCams: in front of the body (its local -Y, the side the cloth's rest normal faces is
		// either way) at twice its radius, looking at its middle.
		const FTransform& Frame = LevelMesh->GetComponentTransform();
		const FVector Front = -Frame.GetUnitAxis(EAxis::Y);
		const FVector Eye = BridgeCenter + Front * FMath::Max(200.0f, Body.Info.Radius * 2.2f) + FVector(0, 0, 40);
		const FRotator Look = (BridgeCenter - Eye).Rotation();
		UE_LOG(LogOpenSCCT, Log, TEXT("Soft bodies: %d %s (%s, %d particles, %s%s) bridge vs level %.1f cm, camera %.0f,%.0f,%.0f,%.0f,%.0f"),
			Index, *Body.Name, Body.Info.Kind == 1 ? TEXT("strip") : TEXT("patch"), Body.Info.Particles,
			(Body.Info.Flags & EScctSoftBodyFlags::CollActor) != 0 ? TEXT("pawns push it") : TEXT("wind only"),
			Body.Info.NormalSign < 0.0f ? TEXT(", normals turned") : TEXT(""), Offset,
			Eye.X, Eye.Y, Eye.Z, Look.Yaw, Look.Pitch);
		Bodies.Add(MoveTemp(Body));
	}

	Seen.SetNumZeroed(Bodies.Num());
	bBuilt = true;
	MatchSeconds = 0.0f;
	LogTimer = 0.0f;
	LoggedCollSounds = State.CollSounds;
	LoggedShotSounds = State.ShotSounds;
	LoggedShotHits = State.ShotHits;
	UE_LOG(LogOpenSCCT, Log, TEXT("Soft bodies %d built (%d without a level mesh, %d instances the bridge could not build) in %.1f ms; bridge and level agree within %.1f cm"),
		Bodies.Num() - Missing, Missing, State.Warnings, State.LoadMilliseconds, WorstOffset);
}

void UScctSoftBodySubsystem::Restore()
{
	if (bBuilt)
	{
		FScctSoftBodyState State = {};
		if (Api != nullptr && Api->GetState(&State) != 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Soft bodies summary: %lld steps, peak displacement %.1f cm, %d mesh rewrites, %d shot hits, %d collision sounds, %d shot sounds"),
				State.Steps, PeakDisplacement, Rewrites, State.ShotHits, State.CollSounds, State.ShotSounds);
		}
	}
	for (FBody& Body : Bodies)
	{
		if (UProceduralMeshComponent* Mesh = Body.Mesh.Get())
		{
			if (AActor* Owner = Mesh->GetOwner())
			{
				Owner->RemoveInstanceComponent(Mesh);
			}
			Mesh->DestroyComponent();
		}
		if (UStaticMeshComponent* LevelMesh = Body.LevelMesh.Get())
		{
			LevelMesh->SetVisibility(Body.bLevelMeshWasVisible);
		}
	}
	Bodies.Reset();
	Seen.Reset();
	bBuilt = false;
	PeakDisplacement = 0.0f;
	Rewrites = 0;
	for (FTestAction& Test : Tests)
	{
		Test.bDone = false;
	}
}

void UScctSoftBodySubsystem::HandleFrame(float DeltaSeconds)
{
	if (!bBuilt || Api == nullptr || Bodies.Num() == 0)
	{
		return;
	}
	MatchSeconds += DeltaSeconds;
	RunTests();

	// Drawn on screen, as the Godot client's on-screen notifier says it: a shadow or a scene capture that drew the
	// mesh does not count (WasRecentlyRendered would).
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		const UProceduralMeshComponent* Mesh = Bodies[Index].Mesh.Get();
		const bool bDrawn = Mesh != nullptr && Now - Mesh->GetLastRenderTimeOnScreen() <= DrawnWithin;
		Seen[Index] = bDrawn ? 1 : 0;
		if (bDrawn)
		{
			Bodies[Index].LastSeenSeconds = MatchSeconds;
		}
	}

	const int32 Stepped = Api->Update(DeltaSeconds, Seen.GetData(), Seen.Num());
	SecondStepped = FMath::Max(SecondStepped, Stepped);
	if (Stepped > 0)
	{
		// Only bodies the bridge may have moved are rewritten: the ones drawn lately (a shot wakes one, and it is
		// drawn again before it is out of that window).
		const int32 Floats = VertexBuffer.Num();
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			FBody& Body = Bodies[Index];
			UProceduralMeshComponent* Mesh = Body.Mesh.Get();
			if (Mesh == nullptr || MatchSeconds - Body.LastSeenSeconds > DrawnRecently)
			{
				continue;
			}
			const int32 N = Body.Info.Particles;
			if (Floats < N * 6 || Api->GetVertices(Index, VertexBuffer.GetData(), Floats) != N)
			{
				continue;
			}
			for (int32 P = 0; P < N; ++P)
			{
				const float* V = &VertexBuffer[P * 6];
				Body.Positions[P] = Body.Positions[P + N] = FVector(V[0], V[1], V[2]);
				Body.Normals[P] = Body.Normals[P + N] = FVector(V[3], V[4], V[5]);
			}
			Mesh->UpdateMeshSection(0, Body.Positions, Body.Normals, Body.UVs, TArray<FColor>(), TArray<FProcMeshTangent>());
			++Rewrites;
		}
	}

	FScctSoftBodyState State = {};
	if (Api->GetState(&State) != 0)
	{
		SecondMaxDisplacement = FMath::Max(SecondMaxDisplacement, State.MaxDisplacement);
		PeakDisplacement = FMath::Max(PeakDisplacement, State.MaxDisplacement);
		SecondColliders = FMath::Max(SecondColliders, State.Colliders);
	}

	LogTimer += DeltaSeconds;
	if (LogTimer >= 1.0f)
	{
		LogTimer = 0.0f;
		LogSecond();
	}
}

void UScctSoftBodySubsystem::LogSecond()
{
	FScctSoftBodyState State = {};
	if (Api->GetState(&State) == 0)
	{
		return;
	}
	const int32 CollSounds = State.CollSounds - LoggedCollSounds;
	const int32 ShotSounds = State.ShotSounds - LoggedShotSounds;
	const int32 ShotHits = State.ShotHits - LoggedShotHits;
	if (bLog || CollSounds > 0 || ShotSounds > 0)
	{
		int32 Drawn = 0;
		for (const uint8 Flag : Seen)
		{
			Drawn += Flag;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Soft bodies: max displacement %.1f cm this second, %d drawn, %d stepped, %d pawns pushing, %d shot hits, %d collision sounds, %d shot sounds (%.2f ms)"),
			SecondMaxDisplacement, Drawn, SecondStepped, SecondColliders, ShotHits, CollSounds, ShotSounds, State.UpdateMilliseconds);
	}
	LoggedCollSounds = State.CollSounds;
	LoggedShotSounds = State.ShotSounds;
	LoggedShotHits = State.ShotHits;
	SecondMaxDisplacement = 0.0f;
	SecondStepped = 0;
	SecondColliders = 0;
}

void UScctSoftBodySubsystem::RunTests()
{
	for (FTestAction& Test : Tests)
	{
		if (Test.bDone || MatchSeconds < Test.Seconds)
		{
			continue;
		}
		Test.bDone = true;
		int32 Count = 0;
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			if (Test.What == TEXT("walk"))
			{
				if ((Bodies[Index].Info.Flags & EScctSoftBodyFlags::CollActor) != 0)
				{
					Count += Api->Walk(Index, 2.0f);
				}
			}
			else if (Test.What == TEXT("shoot"))
			{
				Count += Api->Shoot(Index);
			}
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Soft bodies: test '%s' at %.1f s on %d bodies"), *Test.What, MatchSeconds, Count);
	}
}
