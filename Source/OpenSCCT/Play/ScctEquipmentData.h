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

	/** A model drawn in place of the original gun, when the asset exists and scct.Weapons.Replacements is 1 (it is 0 unless set: the original guns are drawn). */
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

	/**
	 * The left arm that holds the replacement: its upper arm, forearm and hand bones, named as the mesh that
	 * carries the arms names them (the first-person model itself, or the body the gun is attached to). With all
	 * three set and LeftHandSocket on the replacement, the arm is bent so the hand closes on the socket (two-bone
	 * IK, Play/ScctArmIk.h). Left empty the arm keeps the original animation's pose.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Left hand")
	FName LeftUpperArmBone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Left hand")
	FName LeftForearmBone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Left hand")
	FName LeftHandBone;

	/**
	 * Where the left hand's bone is in GunBone's space while the original animation holds the original gun
	 * (centimetres; -ScctWeaponProbe logs it). The hand is taken to the replacement's grip only while the
	 * animation has it within LeftHandReach of this place, fully within a third of that: when a clip takes the
	 * hand off the gun (a reload, a throw) it is the animation's again.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Left hand")
	FVector OriginalLeftHand = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Left hand")
	float LeftHandReach = 18.0f;

	/** The hand bone in LeftHandSocket's space when the hand is closed on the grip (the mesh's axes, centimetres). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Left hand")
	FTransform LeftHandInSocket = FTransform::Identity;

	/**
	 * The hand is only moved to LeftHandInSocket's place and keeps the animation's turn (its rotation is then
	 * ignored): for a hand that has no fingers to close, which already lies the right way on the gun and only
	 * needs bringing against it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Left hand")
	bool bLeftHandKeepsTurn = false;

	/** Pushes the elbow this way before the arm is solved, in the replacement's own axes (centimetres): which way the arm bends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Left hand")
	FVector LeftElbowPush = FVector::ZeroVector;

	/**
	 * The replacement's moving parts, by bone; a bone it lacks is left alone. The simulation drives them: the
	 * bolt handle kicks back with every shot, the trigger follows the firing state, the selector the rate of
	 * fire, the magazine leaves and returns with a reload.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving parts")
	FName BoltBone = TEXT("Bolt");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving parts")
	FName TriggerBone = TEXT("Trigger");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving parts")
	FName SelectorBone = TEXT("Selector");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving parts")
	FName MagazineBone = TEXT("Magazine");

	/** How far the bolt handle runs back along the barrel with a shot, centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving parts")
	float BoltTravel = 3.2f;

	/** The trigger's turn when pulled, degrees of pitch about its bone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving parts")
	float TriggerPull = -13.0f;

	/** The selector's turn for single, burst and automatic fire, degrees of pitch about its bone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving parts")
	FVector SelectorTurn = FVector(0.0, -22.0, -44.0);

	/**
	 * The original model's magazine bone (as the bridge names it). While the bridge animates the original (the
	 * first-person reload), the replacement's magazine makes the same movement about the gun; where it does not
	 * (a gun on a body) the magazine drops MagazineDrop centimetres out of the well and comes back.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving parts")
	FName OriginalMagazineBone = TEXT("chargeur");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving parts")
	float MagazineDrop = 16.0f;
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
