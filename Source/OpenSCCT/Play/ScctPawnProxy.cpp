#include "Play/ScctPawnProxy.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "OpenSCCT.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// The imported original models, by pawn kind (0 spy, 1 merc). See Tools/Import/import_characters.py.
	const TCHAR* const ModelPaths[] = {
		TEXT("/Game/Imported/Chars/SPerso/ATT_01/SkeletalMeshes/ATT_01.ATT_01"),
		TEXT("/Game/Imported/Chars/SPerso/DEF_01/SkeletalMeshes/DEF_01.DEF_01"),
	};

	// The models face the character file's +Z, which the importer turns into Unreal's +Y: a quarter turn from
	// the actor's forward (the original mesh's RotOrigin).
	const FRotator ModelRotation(0.0, -90.0, 0.0);

	/** Bone names compared by letters and digits only: importers differ in how they treat spaces. */
	FString BoneKey(const FString& Name)
	{
		FString Key;
		for (const TCHAR Character : Name)
		{
			if (FChar::IsAlnum(Character))
			{
				Key.AppendChar(FChar::ToLower(Character));
			}
		}
		return Key;
	}
}

AScctPawnProxy::AScctPawnProxy()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Body = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetRelativeRotation(ModelRotation);
	// The simulation does all collision; Unreal's physics must not see these.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(true);

	StandIn = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandIn"));
	StandIn->SetupAttachment(Root);
	StandIn->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		StandIn->SetStaticMesh(Cylinder.Object);
	}
}

void AScctPawnProxy::SetKind(int32 NewKind, TConstArrayView<FScctBridgeBoneInfo> Skeleton)
{
	Kind = NewKind;
	bHasModel = false;
	BoneMap.Reset();

	USkeletalMesh* Model = Kind >= 0 && Kind < UE_ARRAY_COUNT(ModelPaths) ? LoadObject<USkeletalMesh>(nullptr, ModelPaths[Kind]) : nullptr;
	if (Model == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("No character model for pawn kind %d; run Tools/Import/import_characters.py"), Kind);
		return;
	}

	Body->SetSkinnedAssetAndUpdate(Model);

	TMap<FString, int32> MeshBones;
	const FReferenceSkeleton& Reference = Model->GetRefSkeleton();
	for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
	{
		MeshBones.Add(BoneKey(Reference.GetBoneName(Index).ToString()), Index);
	}

	int32 Missing = 0;
	BoneMap.Reserve(Skeleton.Num());
	for (const FScctBridgeBoneInfo& Bone : Skeleton)
	{
		const int32* Found = MeshBones.Find(BoneKey(FString(Bone.Name)));
		BoneMap.Add(Found != nullptr ? *Found : INDEX_NONE);
		Missing += Found == nullptr ? 1 : 0;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Pawn kind %d: model has %d bones, the simulation %d, %d unmatched"), Kind, Reference.GetNum(), Skeleton.Num(), Missing);
	bHasModel = true;
}

void AScctPawnProxy::Present(const FScctBridgePawn& Pawn, TConstArrayView<FScctBridgeBoneInfo> Skeleton, TConstArrayView<FScctBridgeBone> Pose, bool bHideBody)
{
	if (Pawn.Kind != Kind)
	{
		SetKind(Pawn.Kind, Skeleton);
	}

	// The model's origin is the pawn's location plus its PrePivot (a crouch moves both in one tick).
	const FVector Location(Pawn.X + Pawn.PrePivotX, Pawn.Y + Pawn.PrePivotY, Pawn.Z + Pawn.PrePivotZ);
	SetActorLocationAndRotation(Location, FRotator(0.0, Pawn.Yaw, 0.0));

	const bool bVisible = (Pawn.Flags & EScctPawnFlags::Visible) != 0 && !bHideBody;
	const bool bPosed = bHasModel && Pose.Num() > 0;
	Body->SetVisibility(bVisible && bPosed);
	StandIn->SetVisibility(bVisible && !bHasModel);

	if (bPosed)
	{
		TArray<FTransform>& Local = Body->BoneSpaceTransforms;
		const int32 Count = FMath::Min(Pose.Num(), BoneMap.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 MeshBone = BoneMap[Index];
			if (Local.IsValidIndex(MeshBone))
			{
				Local[MeshBone] = FScctPose::ToUnreal(Pose[Index]);
			}
		}
		Body->RefreshBoneTransforms();
	}
	else if (!bHasModel)
	{
		// The engine cylinder is 100 across and 100 tall; the pawn's cylinder is given as radius and half height.
		StandIn->SetWorldLocation(FVector(Pawn.X, Pawn.Y, Pawn.Z));
		StandIn->SetWorldScale3D(FVector(Pawn.CollisionRadius / 50.0, Pawn.CollisionRadius / 50.0, Pawn.CollisionHeight / 50.0));
	}
}
