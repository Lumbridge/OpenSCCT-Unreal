#include "Hub/ScctHubProps.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Hub/ScctHubActivities.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OpenSCCT.h"
#include "Play/ScctExposure.h"

namespace
{
	const TCHAR* const PropMaterialPath = TEXT("/Game/OpenSCCT/Hub/M_ScctHubProp.M_ScctHubProp");
	const TCHAR* const GlowMaterialPath = TEXT("/Game/OpenSCCT/Hub/M_ScctHubGlow.M_ScctHubGlow");
	const TCHAR* const CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const SpherePath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	/** The engine's basic shapes are 100 units across, their pivot in the middle. */
	constexpr float ShapeSize = 100.0f;

	// HubPlayK: the targets' body and head, the ball.
	constexpr float TargetHalfWidth = 22.0f;
	constexpr float TargetHalfDepth = 6.0f;
	constexpr float TargetBodyHeight = 128.0f;
	constexpr float TargetHeadHalfWidth = 13.0f;
	constexpr float TargetHeadGap = 4.0f;
	constexpr float TargetHeadHeight = 30.0f;
	constexpr float BallRadius = 24.0f;

	/** A travel point is marked at pawn-centre height; its disc belongs on the floor (HubProps.MakeMark). */
	constexpr float TravelDiscDrop = 86.0f;

	/** A glow's light in the picture is bounded both ways, so a wrong exposure reading cannot blind or hide it. */
	constexpr float LeastGlow = 0.05f;
	constexpr float MostGlow = 400.0f;
}

FScctHubProps::FScctHubProps(TSharedPtr<FScctHubActivities> InActivities)
	: Activities(MoveTemp(InActivities))
{
}

void FScctHubProps::Tick(UWorld* World, float DeltaSeconds, bool bVerbose)
{
	if (World == nullptr || !Activities.IsValid() || !Activities->IsValid() || !EnsureScene(World))
	{
		return;
	}

	const FScctHubActivityState& State = Activities->GetState();
	if (!bBuilt || BuiltRevision != State.MarkRevision)
	{
		BuildMarks();
		BuiltRevision = State.MarkRevision;
		bBuilt = true;
	}
	UpdateMarks(State.Now);
	UpdateTargets();
	UpdateBall();
	UpdateGlow();

	LogTimer -= DeltaSeconds;
	if (bVerbose && LogTimer <= 0.0f)
	{
		LogTimer = 8.0f;
		int32 Shown = 0;
		for (const TPair<FString, FMarkView>& Pair : MarkViews)
		{
			const USceneComponent* Root = Pair.Value.Root.Get();
			Shown += Root != nullptr && Root->IsVisible() ? 1 : 0;
		}
		const UStaticMeshComponent* BallComponent = Ball.Get();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubProps: %d marks drawn (%d shown), %d targets, ball %s, %d materials, exposure %.3f"), MarkViews.Num(), Shown, TargetViews.Num(),
			BallComponent != nullptr ? *BallComponent->GetComponentLocation().ToCompactString() : TEXT("none"), Materials.Num(), FScctExposure::Get());
	}
}

void FScctHubProps::Stop()
{
	if (AActor* Actor = Host.Get())
	{
		Actor->Destroy();
	}
	Host.Reset();
	SceneWorld.Reset();
	MarkViews.Reset();
	TargetViews.Reset();
	Ball.Reset();
	Materials.Reset();
	bBuilt = false;
	bSceneFailed = false;
}

bool FScctHubProps::EnsureScene(UWorld* World)
{
	if (SceneWorld.Get() != World)
	{
		// A new hub level: its actors are new.
		SceneWorld = World;
		Host.Reset();
		MarkViews.Reset();
		TargetViews.Reset();
		Ball.Reset();
		Materials.Reset();
		bBuilt = false;
		bSceneFailed = false;
	}
	if (Host.IsValid())
	{
		return true;
	}
	if (bSceneFailed)
	{
		return false;
	}

	Cube = LoadObject<UStaticMesh>(nullptr, CubePath);
	Cylinder = LoadObject<UStaticMesh>(nullptr, CylinderPath);
	Sphere = LoadObject<UStaticMesh>(nullptr, SpherePath);
	if (LoadObject<UMaterialInterface>(nullptr, PropMaterialPath) == nullptr || LoadObject<UMaterialInterface>(nullptr, GlowMaterialPath) == nullptr
		|| !Cube.IsValid() || !Cylinder.IsValid() || !Sphere.IsValid())
	{
		bSceneFailed = true;
		UE_LOG(LogOpenSCCT, Error, TEXT("ScctHubProps: the prop materials (run Tools/Hub/build_hub_assets.py) or the engine's basic shapes are missing: no activity props"));
		return false;
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	if (Actor == nullptr)
	{
		bSceneFailed = true;
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Host = Actor;
	return true;
}

// ---- materials and shapes -------------------------------------------------------------------------------------------

UMaterialInstanceDynamic* FScctHubProps::Solid(const FLinearColor& Colour, float Glow)
{
	const FString Key = FString::Printf(TEXT("S%s/%.2f"), *Colour.ToFColor(false).ToHex(), Glow);
	if (const FGlowMaterial* Found = Materials.Find(Key); Found != nullptr && Found->Material.IsValid())
	{
		return Found->Material.Get();
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr, PropMaterialPath), Host.Get());
	Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Colour.R, Colour.G, Colour.B, 1.0f));
	Material->SetScalarParameterValue(TEXT("Glow"), 0.0f);
	Materials.Add(Key, { Material, Glow });
	return Material;
}

UMaterialInstanceDynamic* FScctHubProps::Glow(const FLinearColor& Colour, float Brightness)
{
	const FString Key = FString::Printf(TEXT("G%s/%.2f"), *Colour.ToFColor(false).ToHex(), Brightness);
	if (const FGlowMaterial* Found = Materials.Find(Key); Found != nullptr && Found->Material.IsValid())
	{
		return Found->Material.Get();
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr, GlowMaterialPath), Host.Get());
	Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Colour.R, Colour.G, Colour.B, 1.0f));
	Material->SetScalarParameterValue(TEXT("Opacity"), Colour.A);
	Materials.Add(Key, { Material, Brightness });
	return Material;
}

void FScctHubProps::UpdateGlow()
{
	// Picture = scene light x exposure: a glow of brightness B in the picture is B / exposure in the scene.
	const float Exposure = FMath::Max(FScctExposure::Get(), 1e-6f);
	for (TPair<FString, FGlowMaterial>& Pair : Materials)
	{
		if (UMaterialInstanceDynamic* Material = Pair.Value.Material.Get(); Material != nullptr && Pair.Value.Brightness > 0.0f)
		{
			Material->SetScalarParameterValue(TEXT("Glow"), FMath::Clamp(Pair.Value.Brightness / Exposure, LeastGlow, MostGlow));
		}
	}
}

USceneComponent* FScctHubProps::MakeRoot(const FVector& At)
{
	AActor* Actor = Host.Get();
	USceneComponent* Root = NewObject<USceneComponent>(Actor);
	Root->SetMobility(EComponentMobility::Movable);
	Root->SetupAttachment(Actor->GetRootComponent());
	Root->RegisterComponent();
	Root->SetWorldLocation(At);
	return Root;
}

UStaticMeshComponent* FScctHubProps::Shape(UStaticMesh* Mesh, USceneComponent* Parent, const FVector& Centre, const FVector& Scale, UMaterialInstanceDynamic* Material)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Host.Get());
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->SetStaticMesh(Mesh);
	Component->SetMaterial(0, Material);
	Component->SetupAttachment(Parent);
	Component->SetRelativeLocation(Centre - Parent->GetComponentLocation());
	Component->SetRelativeScale3D(Scale);
	Component->RegisterComponent();
	return Component;
}

UStaticMeshComponent* FScctHubProps::Box(USceneComponent* Parent, const FVector& Centre, const FVector& Size, UMaterialInstanceDynamic* Material)
{
	return Shape(Cube.Get(), Parent, Centre, Size / ShapeSize, Material);
}

// ---- the marks ------------------------------------------------------------------------------------------------------

void FScctHubProps::BuildMarks()
{
	for (TPair<FString, FMarkView>& Pair : MarkViews)
	{
		if (USceneComponent* Root = Pair.Value.Root.Get())
		{
			TArray<USceneComponent*> Children;
			Root->GetChildrenComponents(true, Children);
			for (USceneComponent* Child : Children)
			{
				Child->DestroyComponent();
			}
			Root->DestroyComponent();
		}
	}
	MarkViews.Reset();
	FString Where;
	for (const FScctHubActivityMark& Mark : Activities->GetMarks())
	{
		BuildMark(Mark);
		const EScctHubMarkKind Kind = static_cast<EScctHubMarkKind>(Mark.Kind);
		if (Kind == EScctHubMarkKind::Start || Kind == EScctHubMarkKind::FiringLine || Kind == EScctHubMarkKind::Jukebox || Kind == EScctHubMarkKind::Goal)
		{
			Where += FString::Printf(TEXT(" %s (%.0f, %.0f, %.0f)"), *FString(Mark.Id), Mark.X, Mark.Y, Mark.Z);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubProps: %d activity marks built (gates, firing line, jukebox, laptops, goals):%s"), MarkViews.Num(), *Where);
}

void FScctHubProps::BuildMark(const FScctHubActivityMark& Mark)
{
	const EScctHubMarkKind Kind = static_cast<EScctHubMarkKind>(Mark.Kind);
	const FVector At(Mark.X, Mark.Y, Mark.Z);
	const FVector MarkAt = Kind == EScctHubMarkKind::Travel ? At - FVector(0.0, 0.0, TravelDiscDrop) : At;
	// The checkpoint's pulse scales this root: its pivot is at the mark.
	USceneComponent* Root = MakeRoot(MarkAt);
	FMarkView& View = MarkViews.Add(Mark.Id);
	View.Root = Root;
	View.Kind = Mark.Kind;

	switch (Kind)
	{
	case EScctHubMarkKind::Start:
	case EScctHubMarkKind::Finish:
	case EScctHubMarkKind::Checkpoint:
	case EScctHubMarkKind::Travel:
	{
		const FLinearColor Colour = Kind == EScctHubMarkKind::Start ? FLinearColor(0.3f, 1.0f, 0.45f, 0.35f)
			: Kind == EScctHubMarkKind::Travel ? FLinearColor(0.25f, 0.75f, 1.0f, 0.45f)
			: Kind == EScctHubMarkKind::Finish ? FLinearColor(1.0f, 1.0f, 1.0f, 0.4f)
			: FLinearColor(1.0f, 0.75f, 0.2f, 0.22f);
		const float Diameter = Mark.Radius * 2.0f / ShapeSize;
		Shape(Cylinder.Get(), Root, MarkAt + FVector(0.0, 0.0, 4.0), FVector(Diameter, Diameter, 6.0f / ShapeSize), Glow(Colour));
		if (Kind != EScctHubMarkKind::Travel)
		{
			// A post at the gate so it shows from afar.
			Box(Root, MarkAt + FVector(0.0, 0.0, 160.0), FVector(12.0, 12.0, 320.0), Solid(Colour, 0.6f));
		}
		break;
	}
	case EScctHubMarkKind::FiringLine:
		Box(Root, At + FVector(0.0, 0.0, 2.0), FVector(16.0, Mark.Radius * 2.0, 4.0), Glow(FLinearColor(1.0f, 0.85f, 0.1f, 0.9f)));
		break;
	case EScctHubMarkKind::Jukebox:
		Box(Root, At + FVector(0.0, 0.0, 64.0), FVector(72.0, 48.0, 128.0), Solid(FLinearColor(0.12f, 0.1f, 0.16f)));
		Box(Root, At + FVector(-25.0, 0.0, 96.0), FVector(4.0, 40.0, 40.0), Solid(FLinearColor(0.9f, 0.3f, 1.0f), 0.6f));
		Box(Root, At + FVector(-25.0, 0.0, 40.0), FVector(4.0, 40.0, 24.0), Solid(FLinearColor(0.2f, 0.9f, 1.0f), 0.6f));
		break;
	case EScctHubMarkKind::Laptop:
		BuildLaptop(Root, At);
		break;
	case EScctHubMarkKind::Goal:
	{
		UMaterialInstanceDynamic* Post = Solid(FLinearColor(0.95f, 0.95f, 0.95f));
		const double W = Mark.Radius;
		Box(Root, At + FVector(0.0, -W, 120.0), FVector(10.0, 10.0, 240.0), Post);
		Box(Root, At + FVector(0.0, W, 120.0), FVector(10.0, 10.0, 240.0), Post);
		Box(Root, At + FVector(0.0, 0.0, 240.0), FVector(10.0, W * 2.0 + 10.0, 10.0), Post);
		Box(Root, At + FVector(0.0, 0.0, 1.0), FVector(24.0, W * 2.0, 2.0), Glow(FLinearColor(1.0f, 1.0f, 1.0f, 0.6f), 0.4f));
		break;
	}
	default:
		break;
	}
}

void FScctHubProps::BuildLaptop(USceneComponent* Root, const FVector& At)
{
	// HubProps.MakeLaptop: a desk, the laptop open on it with its lit screen facing south (where the player stands),
	// and a glowing square on the floor in front marking where to stand.
	constexpr double DeskTop = 72.0;
	UMaterialInstanceDynamic* Frame = Solid(FLinearColor(0.16f, 0.17f, 0.2f));
	Box(Root, At + FVector(0.0, 0.0, DeskTop - 3.0), FVector(192.0, 128.0, 6.0), Solid(FLinearColor(0.34f, 0.28f, 0.22f)));
	Box(Root, At + FVector(-88.0, 0.0, (DeskTop - 6.0) / 2.0), FVector(8.0, 120.0, DeskTop - 6.0), Frame);
	Box(Root, At + FVector(88.0, 0.0, (DeskTop - 6.0) / 2.0), FVector(8.0, 120.0, DeskTop - 6.0), Frame);

	// The base on the desk, the lid hinged at the base's back edge and leaning back 15 degrees.
	UMaterialInstanceDynamic* Shell = Solid(FLinearColor(0.55f, 0.57f, 0.6f));
	Box(Root, At + FVector(0.0, -6.0, DeskTop + 1.5), FVector(44.0, 30.0, 3.0), Shell);
	constexpr double Lean = 15.0;
	constexpr double LidHeight = 28.0;
	const double Rad = FMath::DegreesToRadians(Lean);
	const FVector Up(0.0, FMath::Sin(Rad), FMath::Cos(Rad));
	const FVector Facing(0.0, -FMath::Cos(Rad), FMath::Sin(Rad));
	const FRotator Tilt = FRotationMatrix::MakeFromXZ(FVector::ForwardVector, Up).Rotator();
	const FVector Hinge = At + FVector(0.0, 9.0, DeskTop + 3.0);
	const FVector LidCentre = Hinge + Up * (LidHeight / 2.0);
	Box(Root, LidCentre, FVector(44.0, 2.0, LidHeight), Shell)->SetWorldRotation(Tilt);
	Box(Root, LidCentre + Facing * 1.2, FVector(40.0, 0.5, LidHeight - 4.0), Glow(FLinearColor(0.35f, 0.85f, 1.0f, 1.0f), 0.8f))->SetWorldRotation(Tilt);

	// Where to stand to use it.
	Box(Root, At + FVector(0.0, -112.0, 1.0), FVector(96.0, 64.0, 2.0), Glow(FLinearColor(0.35f, 0.85f, 1.0f, 0.25f), 0.5f));
}

void FScctHubProps::UpdateMarks(float Now)
{
	const float Pulse = 1.0f + 0.08f * FMath::Sin(Now * 6.0f);
	for (const FScctHubActivityMark& Mark : Activities->GetMarks())
	{
		const FMarkView* View = MarkViews.Find(Mark.Id);
		USceneComponent* Root = View != nullptr ? View->Root.Get() : nullptr;
		if (Root == nullptr || Mark.Kind != static_cast<int32>(EScctHubMarkKind::Checkpoint))
		{
			continue;
		}
		const bool bShown = (Mark.Flags & EScctHubActivityFlags::Shown) != 0;
		const float Scale = (Mark.Flags & EScctHubActivityFlags::Next) != 0 ? Pulse : 1.0f;
		Root->SetVisibility(bShown, true);
		Root->SetWorldScale3D(FVector(Scale, Scale, 1.0f));
	}
}

// ---- the targets and the ball ---------------------------------------------------------------------------------------

void FScctHubProps::UpdateTargets()
{
	TSet<int32> Seen;
	for (const FScctHubActivityTarget& Target : Activities->GetTargets())
	{
		Seen.Add(Target.Id);
		const bool bMine = (Target.Flags & EScctHubActivityFlags::Mine) != 0;
		FTargetView& View = TargetViews.FindOrAdd(Target.Id);
		if (!View.Root.IsValid() || View.bMine != bMine)
		{
			if (USceneComponent* Old = View.Root.Get())
			{
				TArray<USceneComponent*> Children;
				Old->GetChildrenComponents(true, Children);
				for (USceneComponent* Child : Children)
				{
					Child->DestroyComponent();
				}
				Old->DestroyComponent();
			}
			// HubProps.MakeTarget: a body and a head, red and yellow for the player's own round, grey for others'.
			USceneComponent* Root = MakeRoot(FVector::ZeroVector);
			const FLinearColor Body = bMine ? FLinearColor(0.85f, 0.15f, 0.12f) : FLinearColor(0.45f, 0.45f, 0.48f);
			const FLinearColor Head = bMine ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor(0.6f, 0.6f, 0.62f);
			const double Depth = TargetHalfDepth * 2.0;
			Box(Root, FVector(0.0, 0.0, TargetBodyHeight / 2.0), FVector(Depth, TargetHalfWidth * 2.0, TargetBodyHeight), Solid(Body, bMine ? 0.6f : 0.0f));
			Box(Root, FVector(0.0, 0.0, TargetBodyHeight + TargetHeadGap + TargetHeadHeight / 2.0), FVector(Depth, TargetHeadHalfWidth * 2.0, TargetHeadHeight), Solid(Head, bMine ? 0.6f : 0.0f));
			View.Root = Root;
			View.bMine = bMine;
		}
		if (USceneComponent* Root = View.Root.Get())
		{
			// It flips about its foot across the lane, falling along the line of fire.
			Root->SetVisibility((Target.Flags & EScctHubActivityFlags::Up) != 0, true);
			Root->SetWorldLocationAndRotation(FVector(Target.X, Target.Y, Target.Z), FRotator(-Target.Angle, 0.0f, 0.0f));
		}
	}
	for (auto It = TargetViews.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			if (USceneComponent* Root = It.Value().Root.Get())
			{
				Root->SetVisibility(false, true);
			}
		}
	}
}

void FScctHubProps::UpdateBall()
{
	const FScctHubActivityState& State = Activities->GetState();
	if ((State.Flags & EScctHubActivityFlags::HasBall) == 0)
	{
		return;
	}
	if (!Ball.IsValid())
	{
		USceneComponent* Root = Host->GetRootComponent();
		const float Diameter = BallRadius * 2.0f / ShapeSize;
		Ball = Shape(Sphere.Get(), Root, Root->GetComponentLocation(), FVector(Diameter), Solid(FLinearColor(1.0f, 0.55f, 0.1f)));
	}
	Ball->SetWorldLocation(FVector(State.BallX, State.BallY, State.BallZ));
}
