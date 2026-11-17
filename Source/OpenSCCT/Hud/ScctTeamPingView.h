#pragma once

#include "CoreMinimal.h"

/** One team ping placed on the screen for this frame (UScctTeamPingSubsystem fills them, ScctHudElementsTeamPing.cpp draws them). */
struct FScctTeamPingBadge
{
	int32 Kind = 0;                                // EScctTeamPingKind
	int32 Team = 0;                                // the team it was placed for: its colour
	FVector2f Screen = FVector2f::ZeroVector;      // the spot, or the point on the screen's edge it is clamped to, in the full-screen layer's units
	FVector2f Direction = FVector2f::ZeroVector;   // at the edge: the way to the spot (unit length), else zero
	bool bAtEdge = false;                          // the spot is off the screen or behind the camera
	float Opacity = 1.0f;                          // 1, fading to 0 over the ping's last 0.75 s
	float Distance = 0.0f;                         // simulation units from the local player's eyes
	FString Sender;                                // the sender's name when known (the player's own pings)
	bool bOwn = false;
};

/** The team ping for this frame: the badges and the wheel while the TeamPing key is held. */
struct FScctTeamPingView
{
	TArray<FScctTeamPingBadge> Badges;
	bool bWheelOpen = false;
	int32 WheelTeam = 0;    // the wheel's team: spies have four kinds, mercs two
	int32 Selection = -1;   // EScctTeamPingKind on the wheel, -1 for none (releasing then cancels)
	float Margin = 31.0f;   // how far in from the screen's edge a clamped badge's centre stays, in the layer's units
};
