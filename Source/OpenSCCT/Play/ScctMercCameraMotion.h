#pragma once

#include "CoreMinimal.h"

/** The control pivot and camera follow the held weapon aim continuously. */
struct FScctMercCameraMotion
{
	/** AimTurn contains the weapon's look input this frame in yaw/pitch degrees, excluding recoil and free-look. */
	FRotator Update(const FVector& Eye, const FRotator& Aim, FVector2D AimTurn, float PawnSpeed,
		bool bCrouched, float AimBlend, float DeltaSeconds, bool bCut);

	/** Aim and the camera anchor use world yaw and pitch, including when looking steeply up or down. */
	FRotator CameraAnchor(const FRotator& Aim) const;

private:
	bool bSampled = false;
	FVector LastEye = FVector::ZeroVector;
	FRotator LastAim = FRotator::ZeroRotator;
	FRotator Offset = FRotator::ZeroRotator; // Last displayed offset, retained when the game is paused.
	float WalkPhase = 0.0f;
	float WalkWeight = 0.0f;
	float Bank = 0.0f;
	float DownwardSpeed = 0.0f;
	float LandingDip = 0.0f;
	double CameraFollowSpeed = 2.0;
	FVector2D PivotOffset = FVector2D::ZeroVector;
	FVector2D GunOffset = FVector2D::ZeroVector; // yaw, pitch in degrees
};
