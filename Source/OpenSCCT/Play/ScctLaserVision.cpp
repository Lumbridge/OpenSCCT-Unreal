#include "Play/ScctLaserVision.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Hud/ScctVisionSubsystem.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

namespace ScctLaserVisionLook
{
	// SLaserBeam's HeatIntensity, which both weapon beams keep (the detector's mine beam lowers it to 0.4).
	constexpr float WeaponBeamHeat = 1.0f;

	// The red beam's own width, and how much wider a piece grows per centimetre from the eye so that it stays about
	// two pixels across however far away it is.
	constexpr double MinWidth = 0.7;
	constexpr double WidthPerDistance = 0.003;

	// Where the pieces meet along the beam, from the end nearer the eye: each piece is at most twice as far from
	// the eye at its far end as at its near one (but the first).
	constexpr double PieceEnds[] = { 0.0, 0.125, 0.25, 0.5, 1.0 };
}

int32 FScctLaserVision::StencilFor(int32 VisionMode, bool bMerc)
{
	if (VisionMode == 36)
	{
		return UScctVisionSubsystem::HeatStencil(ScctLaserVisionLook::WeaponBeamHeat);
	}
	if (VisionMode == 38 && bMerc)
	{
		return UScctVisionSubsystem::StencilEmfStreak;
	}
	return 0;
}

void FScctLaserVision::BeginFrame()
{
	for (TPair<int32, FBeam>& Pair : Beams)
	{
		Pair.Value.bSeen = false;
	}
	Shown = 0;
}

UStaticMeshComponent* FScctLaserVision::MakePiece(UWorld& World)
{
	AActor* Owner = Host.Get();
	if (Owner == nullptr)
	{
		FActorSpawnParameters Parameters;
		Parameters.ObjectFlags |= RF_Transient;
		Owner = World.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
		if (Owner == nullptr)
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Host = Owner;
	}

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Mesh == nullptr)
	{
		return nullptr;
	}
	// Never drawn in the picture, only into the custom depth stencil, so any opaque material will do.
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Material == nullptr)
	{
		Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
	Piece->SetMobility(EComponentMobility::Movable);
	Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Piece->SetCanEverAffectNavigation(false);
	Piece->SetCastShadow(false);
	Piece->SetStaticMesh(Mesh);
	Piece->SetMaterial(0, Material);
	Piece->SetRenderInMainPass(false);
	Piece->SetRenderInDepthPass(false);
	Piece->SetRenderCustomDepth(true);
	Piece->SetVisibility(false);
	Piece->SetupAttachment(Owner->GetRootComponent());
	Piece->RegisterComponent();
	return Piece;
}

void FScctLaserVision::Show(UWorld& World, int32 ActorId, const FVector& Start, const FVector& End, const FVector& Eye, int32 Stencil)
{
	FBeam& Beam = Beams.FindOrAdd(ActorId);
	Beam.bSeen = true;
	Beam.bVisible = true;
	++Shown;

	// From the end nearer the eye.
	const bool bFromStart = FVector::DistSquared(Eye, Start) <= FVector::DistSquared(Eye, End);
	const FVector Near = bFromStart ? Start : End;
	const FVector Far = bFromStart ? End : Start;
	// The engine's cylinder is 100 across and 100 long, along Z.
	const FQuat Along = FRotationMatrix::MakeFromZ(Far - Near).ToQuat();
	for (int32 Index = 0; Index < PieceCount; ++Index)
	{
		UStaticMeshComponent* Piece = Beam.Pieces[Index].Get();
		if (Piece == nullptr)
		{
			Piece = MakePiece(World);
			Beam.Pieces[Index] = Piece;
			if (Piece == nullptr)
			{
				continue;
			}
		}

		const FVector From = FMath::Lerp(Near, Far, ScctLaserVisionLook::PieceEnds[Index]);
		const FVector To = FMath::Lerp(Near, Far, ScctLaserVisionLook::PieceEnds[Index + 1]);
		const double Distance = FVector::Dist(Eye, FMath::ClosestPointOnSegment(Eye, From, To));
		const double Width = FMath::Max(ScctLaserVisionLook::MinWidth, Distance * ScctLaserVisionLook::WidthPerDistance);
		Piece->SetWorldTransform(FTransform(Along, (From + To) * 0.5, FVector(Width / 100.0, Width / 100.0, FVector::Dist(From, To) / 100.0)));
		if (Piece->CustomDepthStencilValue != Stencil)
		{
			Piece->SetCustomDepthStencilValue(Stencil);
		}
		if (!Piece->IsVisible())
		{
			Piece->SetVisibility(true);
		}
	}
}

void FScctLaserVision::EndFrame()
{
	for (auto It = Beams.CreateIterator(); It; ++It)
	{
		FBeam& Beam = It.Value();
		if (Beam.bSeen || !Beam.bVisible)
		{
			continue;
		}
		Beam.bVisible = false;
		for (TWeakObjectPtr<UStaticMeshComponent>& Piece : Beam.Pieces)
		{
			if (UStaticMeshComponent* Component = Piece.Get())
			{
				Component->SetVisibility(false);
			}
		}
	}
}

void FScctLaserVision::Drop()
{
	if (AActor* Owner = Host.Get())
	{
		Owner->Destroy();
	}
	Host = nullptr;
	Beams.Reset();
	Shown = 0;
}
