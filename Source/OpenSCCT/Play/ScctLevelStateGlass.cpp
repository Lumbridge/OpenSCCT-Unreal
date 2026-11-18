// The level state's glass: a pane that breaks is cut into shards (UScctGlassShardsSubsystem), and -ScctLevelDo
// "breakglass" breaks every pane drawn now on the local server, so the shards can be checked unattended.

#include "Components/StaticMeshComponent.h"
#include "OpenSCCT.h"
#include "Play/ScctGlassShards.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Runtime/ScctFeatures.h"

void UScctLevelStateSubsystem::BreakPaneShards(const FScctWorldEvent& Event)
{
	UScctGlassShardsSubsystem* Glass = GetWorld()->GetSubsystem<UScctGlassShardsSubsystem>();
	if (Glass == nullptr || !ActorStates.IsValidIndex(Event.NameId))
	{
		return;
	}
	// The pane's mesh is only hidden when it breaks (it shows no mesh and is not visible), so it is still there to cut.
	UStaticMeshComponent* Pane = ActorStates[Event.NameId].Mesh.Get();
	const bool bHitKnown = Event.A != 0;
	Glass->Break(GetName(Event.NameId), Pane, FVector(Event.X, Event.Y, Event.Z),
		bHitKnown ? FVector(Event.DX, Event.DY, Event.DZ) : FVector::ZeroVector, bHitKnown ? Event.B : 0);
}

void UScctLevelStateSubsystem::GatherPaneMeshes(TArray<UStaticMeshComponent*>& Out)
{
	for (int32 NameId = 0; NameId < ActorStates.Num(); ++NameId)
	{
		const FActorState& State = ActorStates[NameId];
		if (State.Mesh.IsValid() && GetName(State.Info.ClassId) == TEXT("SVitre"))
		{
			Out.Add(State.Mesh.Get());
		}
	}
}

int32 UScctLevelStateSubsystem::BreakEveryPane(const FVector& Eye)
{
	FString Error;
	const FScctGlassApi* Glass = ScctFeature<FScctGlassApi>(&Error);
	if (Glass == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Level state: breakglass does nothing, the bridge's glass table is missing: %s"), *Error);
		return 0;
	}

	TArray<int32> Panes;
	for (int32 NameId = 0; NameId < ActorStates.Num(); ++NameId)
	{
		const FActorState& State = ActorStates[NameId];
		if (State.bSeen && State.bShown && State.Mesh.IsValid() && GetName(State.Info.ClassId) == TEXT("SVitre"))
		{
			Panes.Add(NameId);
		}
	}
	if (Panes.Num() == 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: breakglass finds no unbroken pane drawn on this map"));
		return 0;
	}

	// A pane's break reaches this client only while the pane is relevant to the player: the player is stood
	// beside the lowest pane first (the same server step breaks them all).
	Panes.Sort([this](int32 A, int32 B) { return ActorStates[A].Info.Z < ActorStates[B].Info.Z; });
	const FScctWorldActorInfo& Lowest = ActorStates[Panes[0]].Info;
	Glass->StandBeside(Lowest.X, Lowest.Y, Lowest.Z);

	int32 Queued = 0;
	for (const int32 NameId : Panes)
	{
		const FActorState& State = ActorStates[NameId];
		const UStaticMeshComponent* Pane = State.Mesh.Get();
		FVector Center, Normal, Across;
		if (!UScctGlassShardsSubsystem::FindPaneFace(Pane, Center, Normal, Across))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Level state: breakglass leaves %s, its mesh is not a flat pane"), *GetName(NameId));
			continue;
		}
		// A third of the way from the middle to a corner, shot from the camera's side of the pane.
		const FVector Toward = FVector::DotProduct(Normal, Eye - Center) >= 0.0 ? Normal : -Normal;
		const FVector Hit = Center + Across * 0.3;
		const FVector Direction = -Toward;
		if (Glass->BreakPane(State.Info.X, State.Info.Y, State.Info.Z, Hit.X, Hit.Y, Hit.Z, Direction.X, Direction.Y, Direction.Z) != 0)
		{
			++Queued;
		}
		// A camera for -ScctCams on the camera's side, a little above, looking down past the pane to the floor beyond.
		const FVector CameraAt = Center + Toward * 320.0 + FVector(0.0, 0.0, 90.0);
		const FRotator Look = (Center - Toward * 60.0 - FVector(0.0, 0.0, 110.0) - CameraAt).Rotation();
		UE_LOG(LogOpenSCCT, Log, TEXT("Level state: breakglass hits %s at %.0f,%.0f,%.0f; camera %.0f,%.0f,%.0f,%.0f,%.0f"), *GetName(NameId), Hit.X, Hit.Y, Hit.Z,
			CameraAt.X, CameraAt.Y, CameraAt.Z, Look.Yaw, Look.Pitch);
	}
	return Queued > 0 ? 1 : 0;
}
