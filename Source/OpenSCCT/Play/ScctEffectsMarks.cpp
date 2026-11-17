// Impact marks that ride the actor they hit: UScctEffectsSubsystem::AttachMark and FollowMarkSurface.

#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "OpenSCCT.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** A place in a surface's frame is followed to this much; closer than that the decal is left alone. */
	constexpr float FollowTolerance = 0.05f;

	/** The frame of a level actor: its place and turn, without its scale (a mark keeps its own size). */
	FTransform ActorFrame(const AActor& Actor)
	{
		return FTransform(Actor.GetActorQuat(), Actor.GetActorLocation());
	}

	FTransform SimulationFrame(const FScctMarkSurface& Surface)
	{
		return FTransform(FRotator(Surface.Pitch, Surface.Yaw, Surface.Roll), FVector(Surface.X, Surface.Y, Surface.Z));
	}
}

void UScctEffectsSubsystem::AttachMark(int32 Slot, const FScctImpactMark& Mark, UDecalComponent& Decal)
{
	FMarkSurface& Surface = MarkSurfaces[Slot];
	Surface = FMarkSurface();
	Surface.ActorId = Mark.SurfaceActorId;
	if (Mark.SurfaceActorId == 0)
	{
		// The level's own geometry never moves: the decal stays where it landed.
		return;
	}

	FString Error;
	const FScctMarkSurfaceApi* Api = ScctFeature<FScctMarkSurfaceApi>(&Error);
	if (Api == nullptr)
	{
		static bool bReported = false;
		if (!bReported)
		{
			bReported = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Effects: marks stay where they land, the bridge cannot say what they hit: %s"), *Error);
		}
		return;
	}

	FScctMarkSurface Info = {};
	const int32 Flags = Api->GetSurface(Mark.SurfaceActorId, &Info);
	if ((Flags & EScctMarkSurfaceFlags::Exists) == 0)
	{
		// The actor went in the moment between the hit and now; the mark's own flags hide its decal.
		return;
	}

	// The level's actor for the surface, found as the level state finds it (the importer's tag, or the stand-in it
	// spawned). A surface the level has no actor for is followed by the simulation's own pose of it, as the Godot
	// client follows every surface.
	UScctLevelStateSubsystem* LevelState = GetWorld()->GetSubsystem<UScctLevelStateSubsystem>();
	const FString Name = LevelState != nullptr && Info.NameId > 0 ? LevelState->GetName(Info.NameId) : FString();
	AActor* Actor = !Name.IsEmpty() ? LevelState->FindLevelActor(Name) : nullptr;
	const FTransform SurfaceFrame = Actor != nullptr ? ActorFrame(*Actor) : SimulationFrame(Info);
	Surface.Actor = Actor;
	Surface.bSimulationFrame = Actor == nullptr;
	Surface.Local = FTransform(Decal.GetComponentQuat(), Decal.GetComponentLocation()).GetRelativeTransform(SurfaceFrame);
	Surface.bAttached = true;
	if (bLog)
	{
		const FVector At = Surface.Local.GetLocation();
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: mark slot %d attached to %s (surface #%d%s), at (%.0f, %.0f, %.0f) in its frame"), Slot,
			Name.IsEmpty() ? TEXT("an actor the level state does not follow") : *Name, Mark.SurfaceActorId,
			Actor != nullptr ? TEXT(", its level actor") : TEXT(", by the simulation's pose: no level actor"), At.X, At.Y, At.Z);
	}
}

bool UScctEffectsSubsystem::FollowMarkSurface(int32 Slot, UDecalComponent& Decal)
{
	FMarkSurface& Surface = MarkSurfaces[Slot];
	if (!Surface.bAttached)
	{
		return true;
	}

	// Drawn only while the surface is (ImpactMark.IsDrawn): a broken pane takes its marks with it.
	const FScctMarkSurfaceApi* Api = ScctFeature<FScctMarkSurfaceApi>();
	FScctMarkSurface Info = {};
	const int32 Flags = Api != nullptr ? Api->GetSurface(Surface.ActorId, &Info) : 0;
	AActor* Actor = Surface.Actor.Get();
	const bool bLevelActorGone = !Surface.bSimulationFrame && (Actor == nullptr || Actor->IsHidden());
	const bool bDrawn = (Flags & EScctMarkSurfaceFlags::Exists) != 0 && (Flags & EScctMarkSurfaceFlags::Drawn) != 0 && !bLevelActorGone;
	if (bLog && bDrawn == Surface.bHiddenWithSurface)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: mark slot %d %s with its surface #%d"), Slot, bDrawn ? TEXT("shown again") : TEXT("hidden"), Surface.ActorId);
	}
	Surface.bHiddenWithSurface = !bDrawn;
	if (!bDrawn)
	{
		return false;
	}

	const FTransform SurfaceFrame = Surface.bSimulationFrame ? SimulationFrame(Info) : ActorFrame(*Actor);
	const FTransform Place = Surface.Local * SurfaceFrame;
	if (!Decal.GetComponentLocation().Equals(Place.GetLocation(), FollowTolerance) ||
		!Decal.GetComponentQuat().Equals(Place.GetRotation(), 1.0e-4f))
	{
		Decal.SetWorldLocationAndRotation(Place.GetLocation(), Place.GetRotation());
	}
	return true;
}
