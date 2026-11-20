#include "Play/ScctMercRailModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/UObjectGlobals.h"

const FScctWeaponMountProfile* ScctMercRailModule::FindProfile(const FString& ModelName)
{
	// The first/world meshes of each weapon share the placement relative to FX. The M4's
	// FX is at the end of its native suppressor, including when that section is hidden.
	static const FScctWeaponMountProfile Rifle{{-25, 2.7, 0}, {-42, 0, 6.0}, 3.0f};
	static const FScctWeaponMountProfile Uzi{{-14, 2.4, 0}, {-36, 0, 5.2}, 3.0f};
	static const FScctWeaponMountProfile Shotgun{{-27, 2.5, 0}, {-55, 0, 3.6}, 3.6f};
	// The new models have matching FX, Rail, Sight and Optic markers in both scene files.
	// Measurements are offsets from FX, in centimeters, in the weapon's local forward/right/up frame.
	static const FScctWeaponMountProfile Compact{{-7.28, 2.70, -.09}, {-24.94, 0, 3.45}, 3.15f};
	// The SPAS-12's side clamp and receiver sit at different heights. Iron is the
	// authored rear sight; the added optic's mounting shoe sits forward on the receiver.
	static const FScctWeaponMountProfile Pump{
		.Rail = {-25.16, 1.26, .37}, .Iron = {-69.93, 0, 2.11}, .DotHeight = 3.0f,
		.OpticRailOffset = {10.80, 0, .27}};
	static const FScctWeaponMountProfile Bolt{{-35.38, 2.1, -2.71}, {-75.73, 0, 2.63}, 3.05f,
		FVector::ZeroVector, 1, 0, false, false, false, TEXT("SM_MercBoltScope"), {-77.27, 0, 5.88}};
	static const FScctWeaponMountProfile Dmr{{-43.29, 1.78, .63}, {-60.94, 0, 3.37}, 3.05f,
		FVector::ZeroVector, 1, 0, false, false, false, TEXT("SM_MercDmrScope"), {-85.14, 0, 7.63}};
	// These four world GLBs provide the FX node translation but give its unweighted joint an identity inverse bind.
	static const FScctWeaponMountProfile Ak{{-25, 2.3, 0}, {-36.4, 0, 3.1}, 3.4f, {61.799, 0, 9.6}};
	static const FScctWeaponMountProfile Famas{{-20, 3.1, 0}, {-45.7, -.1, 11.1}, 3.0f, {36.559, 0, 9.6}};
	static const FScctIronSightLine GalilSights{{-.369, 0, .097}, {42.614, -.0015, -.808}};
	static const FScctWeaponMountProfile Galil{
		.Rail = {-27, 2.7, 0}, .Iron = {-61, 0, 4.0}, .DotHeight = 3.4f,
		.WorldMeshFx = {65.732, 0, 9.6}, .IronSightLine = &GalilSights};
	static const FScctWeaponMountProfile M4{
		.Rail = {-44.7, 2.95, .15}, .Iron = {-61.3, 0, 6.65}, .DotHeight = 1.35f,
		.WorldMeshFx = {73.2, 0, 9.6}, .NativeSuppressorLength = 16.5f, .bM4 = true,
		.DefaultSightMesh = TEXT("SM_MercM4CarryHandle"), .DefaultOcular = {-77.9, -.02, 6.64}};
	// Rear notch and front blade as authored, in the animated FX socket's local centimeters.
	// Iron also sets the accessory mount position, which is independent of these sight landmarks.
	static const FScctIronSightLine UspSights{{-20.235, -.007, 2.060}, {-3.520, -.007, 2.273}};
	static const FScctIronSightLine SilencedUspSights{{-35.935, -.007, 2.060}, {-19.220, -.007, 2.273}};
	static const FScctIronSightLine AutomaticSights{{-17.720, 0, 2.160}, {-1.275, 0, 2.041}};
	static const FScctIronSightLine EagleSights{{-22.680, 0, 2.522}, {-1.226, 0, 2.538}};
	static const FScctIronSightLine MagnumSights{{-27.517, 0, 2.561}, {-1.680, 0, 2.660}};
	static const FScctWeaponMountProfile Usp{.Rail = {-5.8, .15, -2.85}, .Iron = {-14, 0, 2.4}, .DotHeight = 1.4f,
		.AccessoryScale = .55f, .bPistol = true, .IronSightLine = &UspSights};
	static const FScctWeaponMountProfile SilencedUsp{.Rail = {-21.5, .15, -2.85}, .Iron = {-29.7, 0, 2.4}, .DotHeight = 1.4f,
		.AccessoryScale = .55f, .NativeSuppressorLength = 15.7f, .bPistol = true, .bIntegralSuppressor = true, .IronSightLine = &SilencedUspSights};
	static const FScctWeaponMountProfile Automatic{.Rail = {-5.2, .15, -2.8}, .Iron = {-13, 0, 2.4}, .DotHeight = 1.4f,
		.AccessoryScale = .55f, .bPistol = true, .IronSightLine = &AutomaticSights};
	static const FScctWeaponMountProfile Eagle{.Rail = {-7.3, .20, -3.1}, .Iron = {-17, 0, 2.8}, .DotHeight = 1.5f,
		.AccessoryScale = .60f, .bPistol = true, .IronSightLine = &EagleSights};
	static const FScctWeaponMountProfile Magnum{.Rail = {-12, 1.42, -.8}, .Iron = {-24, 0, 3.1}, .DotHeight = 1.7f,
		.AccessoryScale = .60f, .bPistol = true, .IronSightLine = &MagnumSights};
	static const TMap<FString, const FScctWeaponMountProfile*> Profiles = {
		{TEXT("SWeaponFirst.1DEF_01"), &Rifle}, {TEXT("SWeaponFirst.M160"), &Rifle},
		{TEXT("SWeaponFirst.1DEF_Uzi"), &Uzi}, {TEXT("SWeaponFirst.Hk81"), &Uzi},
		{TEXT("SWeaponFirst.1DEF_shotgun"), &Shotgun}, {TEXT("SWeaponFirst.BM401"), &Shotgun},
		{TEXT("OpenScctRifles.ak47"), &Ak}, {TEXT("OpenScctRifles.famas"), &Famas},
		{TEXT("OpenScctRifles.galil"), &Galil}, {TEXT("OpenScctRifles.m4a1s"), &M4},
		{TEXT("OpenScctRifles.compactsmg"), &Compact}, {TEXT("OpenScctRifles.pumpshotgun"), &Pump},
		{TEXT("OpenScctRifles.boltrifle"), &Bolt}, {TEXT("OpenScctRifles.designatedmarksman"), &Dmr},
		{TEXT("OpenScctSidearms.unsuppressed"), &Usp}, {TEXT("OpenScctSidearms.suppressed"), &SilencedUsp},
		{TEXT("OpenScctSidearms.automatic"), &Automatic}, {TEXT("OpenScctSidearms.desert_eagle"), &Eagle},
		{TEXT("OpenScctSidearms.magnum44"), &Magnum},
	};
	FString Base = ModelName;
	Base.RemoveFromEnd(TEXT("_first"));
	return Profiles.FindRef(Base);
}

UStaticMesh* ScctMercRailModule::LoadPart(const TCHAR* Name)
{
	const FString Path = FString::Printf(TEXT("/Game/OpenSCCT/Weapons/%s.%s"), Name, Name);
	return LoadObject<UStaticMesh>(nullptr, *Path);
}

bool ScctMercRailModule::FindEmitter(const UStaticMeshComponent& Module, bool bTorch, FTransform& OutWorld)
{
	static const FName Laser(TEXT("LaserEmitter"));
	static const FName Torch(TEXT("TorchEmitter"));
	const FName Socket = bTorch ? Torch : Laser;
	if (!Module.IsVisible() || !Module.DoesSocketExist(Socket))
	{
		return false;
	}
	OutWorld = Module.GetSocketTransform(Socket);
	return true;
}
