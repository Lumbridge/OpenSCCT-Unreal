#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UStaticMeshComponent;

/** Centers of the visible rear aperture and front post in the animated ADS socket's frame (cm). */
struct FScctIronSightLine
{
	FVector Rear;
	FVector Front;
};

struct FScctWeaponMountProfile
{
	/** Offsets from this game mesh's FX origin in the animated barrel frame. */
	FVector Rail;
	FVector Iron;
	float DotHeight;
	/** Authored FX node in the rigid world mesh when its unweighted joint imports at the root. */
	FVector WorldMeshFx = FVector::ZeroVector;
	float AccessoryScale = 1.0f;
	float NativeSuppressorLength = 0.0f;
	bool bM4 = false;
	bool bPistol = false;
	bool bIntegralSuppressor = false;
	/** Default sight mesh, positioned at the rear opening in the same barrel frame as Iron. */
	const TCHAR* DefaultSightMesh = nullptr;
	FVector DefaultOcular = FVector::ZeroVector;
	const FScctIronSightLine* IronSightLine = nullptr;
	FVector OpticRailOffset = FVector::ZeroVector;
};

namespace ScctMercRailModule
{
	const FScctWeaponMountProfile* FindProfile(const FString& ModelName);
	UStaticMesh* LoadPart(const TCHAR* Name);
	bool FindEmitter(const UStaticMeshComponent& Module, bool bTorch, FTransform& OutWorld);
}
