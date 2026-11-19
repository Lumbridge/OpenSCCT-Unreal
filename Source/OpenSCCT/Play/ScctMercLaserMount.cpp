#include "Play/ScctMercLaserMount.h"

FVector ScctMercLaserMount::BoreOffset(const FString& ModelName)
{
	// Centred on the barrel rims in the shipped meshes, expressed in each FX bone's Unreal axes.
	// Shotguns use the midpoint of the adjacent rims. The AK's muzzle brake opens on a slanted face.
	static const TMap<FName, FVector> Offsets = {
		{ TEXT("SWeaponFirst.1DEF_01"),      FVector(-0.47, -0.43, -0.36) },
		{ TEXT("SWeaponFirst.M160"),         FVector(-0.89, -0.13,  0.12) },
		{ TEXT("SWeaponFirst.1DEF_Uzi"),     FVector(-0.43, -0.15, -0.13) },
		{ TEXT("SWeaponFirst.Hk81"),         FVector(-0.21, -0.15, -0.34) },
		{ TEXT("SWeaponFirst.1DEF_shotgun"), FVector(-0.67,  0.23, -0.66) },
		{ TEXT("SWeaponFirst.BM401"),        FVector(-0.49,  0.01, -0.52) },
		{ TEXT("OpenScctRifles.ak47_first"),  FVector(-0.81,  0.00, -2.12) },
		{ TEXT("OpenScctRifles.ak47"),        FVector(-0.81,  0.00, -2.12) },
		{ TEXT("OpenScctRifles.famas_first"), FVector(-0.01, -0.11,  0.90) },
		{ TEXT("OpenScctRifles.famas"),       FVector(-0.01, -0.11,  0.90) },
		{ TEXT("OpenScctRifles.galil_first"), FVector(-0.04,  0.00, -1.24) },
		{ TEXT("OpenScctRifles.galil"),       FVector(-0.04,  0.00, -1.24) },
	};
	if (const FVector* Offset = Offsets.Find(FName(*ModelName, FNAME_Find)))
	{
		return *Offset;
	}
	// The M4A1-S bore is already centred on FX. Unknown models also keep their authored point.
	return FVector::ZeroVector;
}
