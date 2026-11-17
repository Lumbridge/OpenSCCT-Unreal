#include "Play/ScctGlassShards.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctGlassFracture.h"
#include "Play/ScctSessionSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "Runtime/ScctFeatures.h"
#include "StaticMeshResources.h"

// The Godot client's GlassBreakViews numbers, in centimetres (it works in metres).
namespace ScctGlassK
{
	constexpr float Gravity = 980.0f;           // 9.8 m/s^2
	constexpr float Life = 4.5f;                // a burst's seconds
	constexpr float FadeSeconds = 0.8f;         // shrinking away over the last of them
	constexpr int32 MaxBursts = 6;
	constexpr int32 MaxBounces = 3;             // the third bounce lays it down
	constexpr float BounceKeep = 0.28f;         // of the reflected speed
	constexpr float SpinKeep = 0.45f;
	constexpr float RestSpeedSquared = 150.0f;  // 0.015 m^2/s^2
	constexpr float SurfaceLift = 0.8f;         // off the surface it lands on
	constexpr float Gap = 0.992f;               // the small separation that shows the fracture at once
	constexpr float PlaneTolerance = 0.05f;     // a vertex this far off the pane's plane is not on its face
}

namespace
{
	using ScctGlassFracture::FFragment;
	using ScctGlassFracture::FTriangle;
	using ScctGlassFracture::FVertex;

	/** A pane as its drawn mesh has it, in the mesh's own space (GlassPaneGeometry of the Godot client). */
	struct FGlassPaneShape
	{
		FVector Origin = FVector::ZeroVector;
		FVector U = FVector::ZeroVector;
		FVector V = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		float Thickness = 0.0f;
		int32 DepthSign = 1;
		/** +1 when Unreal draws the side where (B - A) x (C - A) points, -1 when the other: read off the mesh itself. */
		float Winding = 1.0f;
		int32 MaterialSlot = 0;
		FVector2f Center = FVector2f::ZeroVector;
		TArray<FTriangle> Triangles;

		FVector Point(const FVector2f& P) const { return Origin + U * P.X + V * P.Y; }
		FVector2f Project(const FVector& P) const
		{
			const FVector D = P - Origin;
			return FVector2f(static_cast<float>(FVector::DotProduct(D, U)), static_cast<float>(FVector::DotProduct(D, V)));
		}
	};

	enum class EGlassPaneRead
	{
		Pane,
		NotPane,
		NoCpuData,
	};

	EGlassPaneRead ReadGlassPane(const UStaticMesh* Mesh, FGlassPaneShape& Out)
	{
		const FStaticMeshRenderData* Data = Mesh != nullptr ? Mesh->GetRenderData() : nullptr;
		if (Data == nullptr || Data->LODResources.Num() == 0)
		{
			return EGlassPaneRead::NotPane;
		}
		const FStaticMeshLODResources& Lod = Data->LODResources[0];
		const FPositionVertexBuffer& Positions = Lod.VertexBuffers.PositionVertexBuffer;
		const FStaticMeshVertexBuffer& Attributes = Lod.VertexBuffers.StaticMeshVertexBuffer;
		if (Positions.GetNumVertices() == 0 || Positions.GetVertexData() == nullptr || Attributes.GetTangentData() == nullptr ||
			Attributes.GetTexCoordData() == nullptr)
		{
			return EGlassPaneRead::NoCpuData;
		}
		const FIndexArrayView Indices = Lod.IndexBuffer.GetArrayView();
		if (Indices.Num() == 0)
		{
			return EGlassPaneRead::NoCpuData;
		}

		struct FRaw
		{
			FVector A, B, C;
			FVector2f Ua, Ub, Uc;
		};
		TArray<FRaw> Raw;
		float Largest = 0.0f;
		const bool bUv = Attributes.GetNumTexCoords() > 0;
		for (const FStaticMeshSection& Section : Lod.Sections)
		{
			const uint32 End = Section.FirstIndex + Section.NumTriangles * 3;
			for (uint32 I = Section.FirstIndex; I + 2 < End && I + 2 < static_cast<uint32>(Indices.Num()); I += 3)
			{
				const uint32 X = Indices[I], Y = Indices[I + 1], Z = Indices[I + 2];
				const FVector Pa(Positions.VertexPosition(X)), Pb(Positions.VertexPosition(Y)), Pc(Positions.VertexPosition(Z));
				const FVector Cross = FVector::CrossProduct(Pb - Pa, Pc - Pa);
				const float Size = static_cast<float>(Cross.Size());
				if (Size < 1e-6f)
				{
					continue;
				}
				Raw.Add({ Pa, Pb, Pc, bUv ? Attributes.GetVertexUV(X, 0) : FVector2f::ZeroVector, bUv ? Attributes.GetVertexUV(Y, 0) : FVector2f::ZeroVector,
					bUv ? Attributes.GetVertexUV(Z, 0) : FVector2f::ZeroVector });
				if (Size > Largest)
				{
					Largest = Size;
					Out.Normal = Cross / Size;
					Out.Origin = Pa;
					Out.MaterialSlot = Section.MaterialIndex;
					// The authored normals point out of the side that is drawn.
					const FVector Authored = FVector(Attributes.VertexTangentZ(X)) + FVector(Attributes.VertexTangentZ(Y)) + FVector(Attributes.VertexTangentZ(Z));
					Out.Winding = FVector::DotProduct(Authored, Out.Normal) < 0.0 ? -1.0f : 1.0f;
				}
			}
		}
		if (Largest == 0.0f)
		{
			return EGlassPaneRead::NotPane;
		}

		const FVector Axis = FMath::Abs(Out.Normal.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector;
		Out.U = FVector::CrossProduct(Out.Normal, Axis).GetSafeNormal();
		Out.V = FVector::CrossProduct(Out.Normal, Out.U).GetSafeNormal();
		float Min = 0.0f, Max = 0.0f;
		for (const FRaw& T : Raw)
		{
			for (const FVector* P : { &T.A, &T.B, &T.C })
			{
				const float D = static_cast<float>(FVector::DotProduct(*P - Out.Origin, Out.Normal));
				Min = FMath::Min(Min, D);
				Max = FMath::Max(Max, D);
			}
		}
		// A thick or nonplanar prop is not a pane: it keeps its plain break burst.
		if (Max - Min > FMath::Sqrt(Largest) * 0.35f)
		{
			return EGlassPaneRead::NotPane;
		}
		Out.Thickness = FMath::Clamp(Max - Min, 0.2f, 15.0f);
		Out.DepthSign = Max > -Min ? 1 : -1;

		// The triangles on the face; a face drawn from both sides (the same triangle twice) is kept once.
		TSet<FString> Seen;
		for (const FRaw& T : Raw)
		{
			if (FMath::Abs(FVector::DotProduct(T.A - Out.Origin, Out.Normal)) > ScctGlassK::PlaneTolerance ||
				FMath::Abs(FVector::DotProduct(T.B - Out.Origin, Out.Normal)) > ScctGlassK::PlaneTolerance ||
				FMath::Abs(FVector::DotProduct(T.C - Out.Origin, Out.Normal)) > ScctGlassK::PlaneTolerance)
			{
				continue;
			}
			FTriangle Tri;
			Tri.A = { Out.Project(T.A), T.Ua };
			Tri.B = { Out.Project(T.B), T.Ub };
			Tri.C = { Out.Project(T.C), T.Uc };
			TArray<FString> Corners = {
				FString::Printf(TEXT("%.2f,%.2f"), Tri.A.Point.X, Tri.A.Point.Y),
				FString::Printf(TEXT("%.2f,%.2f"), Tri.B.Point.X, Tri.B.Point.Y),
				FString::Printf(TEXT("%.2f,%.2f"), Tri.C.Point.X, Tri.C.Point.Y) };
			Corners.Sort();
			bool bAlready = false;
			Seen.Add(FString::Join(Corners, TEXT(";")), &bAlready);
			if (!bAlready)
			{
				Out.Triangles.Add(Tri);
			}
		}
		if (Out.Triangles.Num() == 0)
		{
			return EGlassPaneRead::NotPane;
		}
		FVector2f Lo(TNumericLimits<float>::Max()), Hi(-TNumericLimits<float>::Max());
		for (const FTriangle& T : Out.Triangles)
		{
			for (const FVertex* V : { &T.A, &T.B, &T.C })
			{
				Lo = FVector2f::Min(Lo, V->Point);
				Hi = FVector2f::Max(Hi, V->Point);
			}
		}
		Out.Center = (Lo + Hi) * 0.5f;
		return EGlassPaneRead::Pane;
	}

	/** One flat-shaded slab triangle of a shard, wound so that Unreal draws the side Outward points to. */
	void AddGlassTriangle(const FGlassPaneShape& Pane, FVector A, FVector B, FVector C, FVector2f Ua, FVector2f Ub, FVector2f Uc, const FVector& Outward,
		TArray<FVector>& Vertices, TArray<int32>& Indices, TArray<FVector>& Normals, TArray<FVector2D>& Uvs)
	{
		if (FVector::DotProduct(FVector::CrossProduct(B - A, C - A), Outward) * Pane.Winding < 0.0)
		{
			Swap(B, C);
			Swap(Ub, Uc);
		}
		const int32 Base = Vertices.Num();
		Vertices.Append({ A, B, C });
		Normals.Append({ Outward, Outward, Outward });
		Uvs.Append({ FVector2D(Ua), FVector2D(Ub), FVector2D(Uc) });
		Indices.Append({ Base, Base + 1, Base + 2 });
	}

	/** The shard's slab (GlassPaneGeometry.MeshFor): its face, the face behind at the pane's thickness, and the walls round its outline, about its middle. */
	void BuildGlassShard(const FGlassPaneShape& Pane, const FFragment& Fragment, const FTransform& World, const FVector& Center,
		TArray<FVector>& Vertices, TArray<int32>& Indices, TArray<FVector>& Normals, TArray<FVector2D>& Uvs)
	{
		const FVector Depth = World.TransformVector(Pane.Normal * (Pane.Thickness * -Pane.DepthSign));
		const FVector Out = Depth.GetSafeNormal();
		auto At = [&](const FVector2f& P) { return World.TransformPosition(Pane.Point(P)) - Center; };
		for (const TArray<FVertex>& Polygon : Fragment.Polygons)
		{
			const FVector P0 = At(Polygon[0].Point);
			for (int32 I = 1; I + 1 < Polygon.Num(); ++I)
			{
				const FVector Pi = At(Polygon[I].Point);
				const FVector Pj = At(Polygon[I + 1].Point);
				AddGlassTriangle(Pane, P0, Pi, Pj, Polygon[0].Uv, Polygon[I].Uv, Polygon[I + 1].Uv, Out, Vertices, Indices, Normals, Uvs);
				AddGlassTriangle(Pane, Pj - Depth, Pi - Depth, P0 - Depth, Polygon[I + 1].Uv, Polygon[I].Uv, Polygon[0].Uv, -Out, Vertices, Indices, Normals, Uvs);
			}
		}
		for (const TPair<FVertex, FVertex>& Edge : ScctGlassFracture::Boundary(Fragment))
		{
			const FVector A = At(Edge.Key.Point);
			const FVector B = At(Edge.Value.Point);
			FVector Side = FVector::CrossProduct(B - A, Out).GetSafeNormal();
			// The middle of the shard is the origin here: a wall faces away from it.
			if (FVector::DotProduct(Side, (A + B) * 0.5) < 0.0)
			{
				Side = -Side;
			}
			AddGlassTriangle(Pane, A, B, B - Depth, Edge.Key.Uv, Edge.Value.Uv, Edge.Value.Uv, Side, Vertices, Indices, Normals, Uvs);
			AddGlassTriangle(Pane, A, B - Depth, A - Depth, Edge.Key.Uv, Edge.Value.Uv, Edge.Key.Uv, Side, Vertices, Indices, Normals, Uvs);
		}
	}
}

bool UScctGlassShardsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctGlassShardsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	if (Session != nullptr)
	{
		Session->OnFrame.AddUObject(this, &UScctGlassShardsSubsystem::HandleFrame);
		Session->OnSessionChanged.AddUObject(this, &UScctGlassShardsSubsystem::HandleSessionChanged);
	}
	FParse::Value(FCommandLine::Get(), TEXT("ScctFxTimeScale="), TimeScale);
	TimeScale = FMath::Clamp(TimeScale, 0.01f, 4.0f);
}

void UScctGlassShardsSubsystem::Deinitialize()
{
	if (Session != nullptr)
	{
		Session->OnFrame.RemoveAll(this);
		Session->OnSessionChanged.RemoveAll(this);
	}
	Clear();
	Super::Deinitialize();
}

void UScctGlassShardsSubsystem::HandleSessionChanged(int32 OldId, int32 NewId)
{
	if (OldId != 0 && NumBursts > 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Glass summary: %d panes broke into %d shards"), NumBursts, NumShards);
	}
	Clear();
	NumBursts = NumShards = 0;
}

bool UScctGlassShardsSubsystem::FindPaneFace(const UStaticMeshComponent* Pane, FVector& OutCenter, FVector& OutNormal, FVector& OutAcross)
{
	FGlassPaneShape Shape;
	if (Pane == nullptr || ReadGlassPane(Pane->GetStaticMesh(), Shape) != EGlassPaneRead::Pane)
	{
		return false;
	}
	const FTransform& World = Pane->GetComponentTransform();
	OutCenter = World.TransformPosition(Shape.Point(Shape.Center));
	OutNormal = World.TransformVectorNoScale(Shape.Normal).GetSafeNormal();
	// From the middle to one corner of the face's bounds, so a check can hit off the middle.
	FVector2f Lo(TNumericLimits<float>::Max()), Hi(-TNumericLimits<float>::Max());
	for (const FTriangle& T : Shape.Triangles)
	{
		for (const FVertex* V : { &T.A, &T.B, &T.C })
		{
			Lo = FVector2f::Min(Lo, V->Point);
			Hi = FVector2f::Max(Hi, V->Point);
		}
	}
	OutAcross = World.TransformPosition(Shape.Point(Hi)) - OutCenter;
	return true;
}

int32 UScctGlassShardsSubsystem::Break(const FString& Name, UStaticMeshComponent* Pane, const FVector& Impact, const FVector& Direction, int32 Serial)
{
	if (Pane == nullptr || Pane->GetStaticMesh() == nullptr)
	{
		return 0;
	}
	FGlassPaneShape Shape;
	const EGlassPaneRead Read = ReadGlassPane(Pane->GetStaticMesh(), Shape);
	if (Read == EGlassPaneRead::NoCpuData)
	{
		if (!bReportedNoCpuData)
		{
			bReportedNoCpuData = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Glass: the pane mesh %s has no CPU copy of its triangles (a cooked build needs Allow CPU Access on pane meshes), so panes break with their glints only"),
				*Pane->GetStaticMesh()->GetName());
		}
		return 0;
	}
	if (Read == EGlassPaneRead::NotPane)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Glass: %s (%s) is not a flat pane, it breaks with its glints only"), *Name, *Pane->GetStaticMesh()->GetName());
		return 0;
	}

	const FTransform World = Pane->GetComponentTransform();
	if (FMath::Abs(World.GetDeterminant()) < 1e-9f)
	{
		return 0;
	}
	const bool bHitKnown = Serial > 0;
	const FVector Hit = bHitKnown ? Impact : World.TransformPosition(Shape.Point(Shape.Center));
	const FVector2f ImpactOnPane = Shape.Project(World.InverseTransformPosition(Hit));

	// The seed of GlassBreakViews.Start: FNV-1a over the pane's name, then the impact serial.
	uint32 Seed = 2166136261u;
	for (const TCHAR Char : Name)
	{
		Seed = (Seed ^ static_cast<uint32>(Char)) * 16777619u;
	}
	Seed ^= static_cast<uint32>(Serial) * 2246822519u;
	const TArray<FFragment> Fragments = ScctGlassFracture::Break(Shape.Triangles, ImpactOnPane, Seed);
	if (Fragments.Num() == 0)
	{
		return 0;
	}
	auto Next = [&Seed]()
	{
		Seed = Seed * 1664525u + 1013904223u;
		return static_cast<float>(Seed >> 8) / 16777216.0f;
	};

	const FVector PaneNormal = World.TransformVectorNoScale(Shape.Normal).GetSafeNormal();
	FVector Impulse = Direction.GetSafeNormal();
	if (Impulse.SizeSquared() < 0.001)
	{
		Impulse = PaneNormal;
	}

	while (Bursts.Num() >= ScctGlassK::MaxBursts)
	{
		Remove(0);
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Host = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (Host == nullptr)
	{
		return 0;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("Root"), RF_Transient);
	Root->SetMobility(EComponentMobility::Movable);
	Host->SetRootComponent(Root);
	Root->RegisterComponent();

	UMaterialInterface* Material = Pane->GetMaterial(Shape.MaterialSlot);
	const TArray<float>& Custom = Pane->GetDefaultCustomPrimitiveData().Data;

	FBurst& Burst = Bursts.AddDefaulted_GetRef();
	Burst.Name = Name;
	Burst.Host = Host;

	TArray<FVector> Vertices;
	TArray<int32> Indices;
	TArray<FVector> Normals;
	TArray<FVector2D> Uvs;
	const TArray<FColor> NoColors;
	const TArray<FProcMeshTangent> NoTangents;
	float NearestToMiddle = 0.0f;
	for (int32 Index = 0; Index < Fragments.Num(); ++Index)
	{
		const FFragment& Fragment = Fragments[Index];
		const FVector Center = World.TransformPosition(Shape.Point(Fragment.Center));
		const float Distance = static_cast<float>(FVector::Dist(Center, Hit));
		const float Metres = Distance / 100.0f;
		FVector Radial = Center - Hit;
		Radial -= Impulse * FVector::DotProduct(Radial, Impulse);
		if (Radial.SizeSquared() > 0.1)
		{
			Radial.Normalize();
		}
		const float Sx = Next() - 0.5f;
		const float Sy = Next() - 0.5f;
		const float Sz = Next() - 0.5f;
		const FVector Scatter(Sx, Sy, Sz);
		const FVector Velocity = Impulse * (120.0f + 280.0f / (1.0f + Metres * 5.0f)) + Radial * (40.0f + Next() * 80.0f) + Scatter * 60.0f;
		const float Ax = Next() - 0.5f;
		const float Ay = Next() - 0.5f;
		const float Az = Next() - 0.5f;

		Vertices.Reset();
		Indices.Reset();
		Normals.Reset();
		Uvs.Reset();
		BuildGlassShard(Shape, Fragment, World, Center, Vertices, Indices, Normals, Uvs);

		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Host, NAME_None, RF_Transient);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->bUseAsyncCooking = false;
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->CreateMeshSection(0, Vertices, Indices, Normals, Uvs, NoColors, NoTangents, false);
		Mesh->SetMaterial(0, Material);
		for (int32 Slot = 0; Slot < Custom.Num(); ++Slot)
		{
			Mesh->SetDefaultCustomPrimitiveDataFloat(Slot, Custom[Slot]);
		}
		Mesh->SetupAttachment(Root);
		Mesh->RegisterComponent();
		Mesh->SetWorldLocationAndRotation(Center, FQuat::Identity);
		Mesh->SetWorldScale3D(FVector(ScctGlassK::Gap));
		Host->AddInstanceComponent(Mesh);

		FPiece& Piece = Burst.Pieces.AddDefaulted_GetRef();
		Piece.Mesh = Mesh;
		Piece.Location = Center;
		Piece.StartZ = static_cast<float>(Center.Z);
		Piece.Velocity = Velocity;
		Piece.Spin = FVector(Ax, Ay, Az) * 18.0f;
		Piece.Delay = FMath::Min(0.18f, Metres * 0.075f);
		Piece.PlaneNormal = PaneNormal;
		Piece.RestAngle = Next() * UE_TWO_PI;
		NearestToMiddle = Index == 0 ? Distance : FMath::Min(NearestToMiddle, Distance);
	}

	++NumBursts;
	NumShards += Burst.Pieces.Num();
	const FVector PaneMiddle = World.TransformPosition(Shape.Point(Shape.Center));
	UE_LOG(LogOpenSCCT, Log, TEXT("Glass: %d shards for %s (%d face triangles, %.1f cm thick; hit %s %.0f cm from the face's middle, the nearest shard %.1f cm from the hit; middle %.0f,%.0f,%.0f, normal %.2f,%.2f,%.2f)"),
		Burst.Pieces.Num(), *Name, Shape.Triangles.Num(), Shape.Thickness * World.GetScale3D().GetAbsMax(), bHitKnown ? TEXT("known,") : TEXT("unknown, the middle,"),
		FVector::Dist(Hit, PaneMiddle), NearestToMiddle, PaneMiddle.X, PaneMiddle.Y, PaneMiddle.Z, PaneNormal.X, PaneNormal.Y, PaneNormal.Z);
	return Burst.Pieces.Num();
}

void UScctGlassShardsSubsystem::HandleFrame(float DeltaSeconds)
{
	if (Bursts.Num() == 0)
	{
		return;
	}
	if (FxTrace == nullptr)
	{
		FString Error;
		FxTrace = ScctFeature<FScctFxTraceApi>(&Error);
		if (FxTrace == nullptr && !bReportedNoTrace)
		{
			bReportedNoTrace = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Glass: shards fall through the floor, the bridge's effect trace is missing: %s"), *Error);
		}
	}

	const float Seconds = FMath::Max(0.0f, DeltaSeconds) * TimeScale;
	for (int32 Index = Bursts.Num() - 1; Index >= 0; --Index)
	{
		FBurst& Burst = Bursts[Index];
		Burst.Age += Seconds;
		if (Burst.Age >= ScctGlassK::Life || !Burst.Host.IsValid())
		{
			Report(Burst);
			Remove(Index);
			continue;
		}
		Step(Burst, Seconds);
	}
}

void UScctGlassShardsSubsystem::Step(FBurst& Burst, float Seconds)
{
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Seconds * 90.0f), 1, 8);
	const float StepSeconds = FMath::Clamp(Seconds, 0.0f, 0.1f) / Steps;
	for (int32 S = 0; S < Steps; ++S)
	{
		for (FPiece& Piece : Burst.Pieces)
		{
			if (Burst.Age < Piece.Delay || Piece.Bounces >= ScctGlassK::MaxBounces)
			{
				continue;
			}
			const FVector From = Piece.Location;
			Piece.Velocity.Z -= ScctGlassK::Gravity * StepSeconds;
			FVector To = From + Piece.Velocity * StepSeconds;
			FScctFxHit Hit;
			if (FxTrace != nullptr && FxTrace->Trace(From.X, From.Y, From.Z, To.X, To.Y, To.Z, &Hit) != 0)
			{
				const FVector Normal = FVector(Hit.NormalX, Hit.NormalY, Hit.NormalZ).GetSafeNormal();
				To = FVector(Hit.X, Hit.Y, Hit.Z) + Normal * ScctGlassK::SurfaceLift;
				Piece.Velocity = Piece.Velocity.MirrorByVector(Normal) * ScctGlassK::BounceKeep;
				Piece.Spin *= ScctGlassK::SpinKeep;
				++Piece.Bounces;
				if (Piece.Velocity.SizeSquared() < ScctGlassK::RestSpeedSquared)
				{
					Piece.Bounces = ScctGlassK::MaxBounces;
				}
				if (Piece.Bounces >= ScctGlassK::MaxBounces)
				{
					// Flat on what it landed on, turned about that surface's normal by its own angle.
					Piece.Rotation = FQuat(Normal, Piece.RestAngle) * FQuat::FindBetweenNormals(Piece.PlaneNormal, Normal);
				}
			}
			Piece.Location = To;
			const float Angle = static_cast<float>(Piece.Spin.Size()) * StepSeconds;
			if (Piece.Bounces < ScctGlassK::MaxBounces && Angle > 1e-7f)
			{
				Piece.Rotation = FQuat(Piece.Spin.GetSafeNormal(), Angle) * Piece.Rotation;
			}
		}
	}

	const float Fade = FMath::Clamp((ScctGlassK::Life - Burst.Age) / ScctGlassK::FadeSeconds, 0.0f, 1.0f);
	for (FPiece& Piece : Burst.Pieces)
	{
		if (UProceduralMeshComponent* Mesh = Piece.Mesh.Get())
		{
			Mesh->SetWorldLocationAndRotation(Piece.Location, Piece.Rotation);
			Mesh->SetWorldScale3D(FVector(FMath::Max(ScctGlassK::Gap * Fade, 0.01f)));
		}
	}
	if (!Burst.bReported && Burst.Age >= ScctGlassK::Life - ScctGlassK::FadeSeconds)
	{
		Report(Burst);
	}
}

void UScctGlassShardsSubsystem::Report(FBurst& Burst)
{
	if (Burst.bReported || Burst.Pieces.Num() == 0)
	{
		return;
	}
	Burst.bReported = true;
	int32 Down = 0;
	double Drop = 0.0;
	double Lowest = UE_BIG_NUMBER;
	for (const FPiece& Piece : Burst.Pieces)
	{
		Down += Piece.Bounces >= ScctGlassK::MaxBounces ? 1 : 0;
		Drop += Piece.StartZ - Piece.Location.Z;
		Lowest = FMath::Min(Lowest, Piece.Location.Z);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Glass: %s after %.1f s: %d of %d shards lie flat, %.0f cm below where they broke off on average, the lowest at z %.0f"),
		*Burst.Name, Burst.Age, Down, Burst.Pieces.Num(), Drop / Burst.Pieces.Num(), Lowest);
}

void UScctGlassShardsSubsystem::Remove(int32 Index)
{
	if (AActor* Host = Bursts[Index].Host.Get())
	{
		Host->Destroy();
	}
	Bursts.RemoveAt(Index);
}

void UScctGlassShardsSubsystem::Clear()
{
	while (Bursts.Num() > 0)
	{
		Remove(Bursts.Num() - 1);
	}
}
