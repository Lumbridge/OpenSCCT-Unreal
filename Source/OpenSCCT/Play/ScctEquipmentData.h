#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ScctEquipmentData.generated.h"

class USkeletalMesh;

/**
 * How one of the simulation's weapon classes is drawn: which mesh stands for it and where things hang on it.
 *
 * The simulation names a weapon's mesh itself (the original game's model, imported under /Game/Imported/Chars),
 * and the bridge poses that model's skeleton. A row can put another model in its place: ReplacementMesh is drawn
 * instead, fixed to the original model's gun bone, and effects then start from the replacement's sockets. A
 * replacement that is not in the project (its asset does not exist) is ignored, so a row can name a model that
 * arrives later.
 */
USTRUCT(BlueprintType)
struct FScctWeaponPresentation
{
	GENERATED_BODY()

	/**
	 * The simulation's class of the weapon as it is seen: for the local player's first-person view the weapon
	 * itself (SWeaponDefense), for a gun on a body the class of the attached actor (SWeaponDefenseDummy).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName SimulationClass;

	/**
	 * The model the bridge poses for this class. Empty: the one the simulation names. Set it to draw a class
	 * with another of the original models (it must have the skeleton the simulation's poses are computed for).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowedClasses = "/Script/Engine.SkeletalMesh"))
	FSoftObjectPath Mesh;

	/** A model drawn in place of the original gun, when the asset exists. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replacement", meta = (AllowedClasses = "/Script/Engine.SkeletalMesh"))
	FSoftObjectPath ReplacementMesh;

	/**
	 * The row is for a first-person model (arms and gun in one skeleton): the arms stay, only the bones in
	 * HiddenBones are hidden under the replacement. Otherwise the whole original model is hidden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replacement")
	bool bFirstPerson = false;

	/** The bone of the original model the replacement is fixed to: the gun's body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replacement")
	FName GunBone = TEXT("corps");

	/** Where the replacement sits in GunBone's space (centimetres, the imported mesh's axes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replacement")
	FTransform AttachOffset = FTransform::Identity;

	/** First person: the original model's bones that carry the old gun (hidden with everything below them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replacement")
	TArray<FName> HiddenBones = { TEXT("corps") };

	/** The replacement's sockets. A socket it lacks falls back to the original model's bone for the same thing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sockets")
	FName MuzzleSocket = TEXT("Muzzle");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sockets")
	FName SightSocket = TEXT("Sight");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sockets")
	FName LightSocket = TEXT("LightModule");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sockets")
	FName LaserSocket = TEXT("LaserModule");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sockets")
	FName MagWellSocket = TEXT("MagWell");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sockets")
	FName EjectionSocket = TEXT("EjectionPort");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sockets")
	FName LeftHandSocket = TEXT("LeftHandGrip");
};

/**
 * The table of weapon presentations: /Game/OpenSCCT/Weapons/DA_ScctWeapons, made by
 * Tools/Import/build_equipment_assets.py and edited in the editor. Swapping a weapon's model is a change to this
 * asset, not to code. A class with no row is drawn with the model the simulation names.
 */
UCLASS(BlueprintType)
class OPENSCCT_API UScctWeaponSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons", meta = (TitleProperty = "SimulationClass"))
	TArray<FScctWeaponPresentation> Weapons;

	/** The row of a simulation class, or null. */
	const FScctWeaponPresentation* Find(FName SimulationClass) const;

	/** The project's table, loaded once; null when the asset has not been built. */
	static const UScctWeaponSet* Get();
};
