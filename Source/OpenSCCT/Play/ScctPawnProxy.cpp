#include "Play/ScctPawnProxy.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "OpenSCCT.h"
#include "Play/ScctPawnLookSubsystem.h"
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
	// A player looking through their own eyes still sees their shadow on the floor and walls: the body is hidden
	// in game for that view and keeps its shadow through bCastHiddenShadow. Present switches that on only while
	// the simulation shows the pawn, because the engine draws a hidden shadow for invisible components too.
	Body->bCastHiddenShadow = false;

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
	BonesByBridgeName.Reset();
	MeshBonesByKey.Reset();

	// A map outfit's model, when the map dresses the team in one (the bridge poses its skeleton).
	const FString OutfitPath = UScctPawnLookSubsystem::OutfitModelPath(Kind);
	USkeletalMesh* Model = OutfitPath.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *OutfitPath, nullptr, LOAD_NoWarn);
	UE_CLOG(!OutfitPath.IsEmpty(), LogOpenSCCT, Log, TEXT("Pawn kind %d wears the map's outfit model %s%s"), Kind, *OutfitPath,
		Model != nullptr ? TEXT("") : TEXT(": NOT IMPORTED (Tools/Import/Import-Outfits.ps1), the stock mesh is drawn"));
	Model = Model != nullptr ? Model : Kind >= 0 && Kind < UE_ARRAY_COUNT(ModelPaths) ? LoadObject<USkeletalMesh>(nullptr, ModelPaths[Kind]) : nullptr;
	if (Model == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("No character model for pawn kind %d; run Tools/Import/import_characters.py"), Kind);
		return;
	}

	Body->SetSkinnedAssetAndUpdate(Model);

	const FReferenceSkeleton& Reference = Model->GetRefSkeleton();
	for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
	{
		MeshBonesByKey.Add(BoneKey(Reference.GetBoneName(Index).ToString()), Index);
	}

	int32 Missing = 0;
	BoneMap.Reserve(Skeleton.Num());
	for (const FScctBridgeBoneInfo& Bone : Skeleton)
	{
		const FString Name(Bone.Name);
		const int32* Found = MeshBonesByKey.Find(BoneKey(Name));
		BoneMap.Add(Found != nullptr ? *Found : INDEX_NONE);
		if (Found != nullptr)
		{
			BonesByBridgeName.Add(FName(*Name), *Found);
		}
		Missing += Found == nullptr ? 1 : 0;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Pawn kind %d: model has %d bones, the simulation %d, %d unmatched"), Kind, Reference.GetNum(), Skeleton.Num(), Missing);
	bHasModel = true;
}

void AScctPawnProxy::Present(const FScctBridgePawn& NewPawn, TConstArrayView<FScctBridgeBoneInfo> Skeleton, TConstArrayView<FScctBridgeBone> Pose)
{
	Pawn = NewPawn;
	if (Pawn.Kind != Kind)
	{
		SetKind(Pawn.Kind, Skeleton);
	}

	// The model's origin is the pawn's location plus its PrePivot (a crouch moves both in one tick).
	const FVector Location(Pawn.X + Pawn.PrePivotX, Pawn.Y + Pawn.PrePivotY, Pawn.Z + Pawn.PrePivotZ);
	SetActorLocationAndRotation(Location, FRotator(0.0, Pawn.Yaw, 0.0));

	// Two different kinds of "not drawn". A pawn the simulation does not show is gone, shadow and all
	// (visibility, and no hidden shadow: the engine would otherwise still draw the shadow of an invisible
	// component that has bCastHiddenShadow). The local player seen from their own eyes is only out of the main
	// view: hidden in game, which with bCastHiddenShadow leaves the shadow. Both reach what features attached
	// to the body.
	const bool bPosed = bHasModel && Pose.Num() > 0;
	const bool bBodyVisible = IsShown() && bPosed;
	if (Body->IsVisible() != bBodyVisible)
	{
		Body->SetVisibility(bBodyVisible, true);
	}
	if (static_cast<bool>(Body->bCastHiddenShadow) != bBodyVisible)
	{
		Body->SetCastHiddenShadow(bBodyVisible);
		TArray<USceneComponent*> Attached;
		Body->GetChildrenComponents(true, Attached);
		for (USceneComponent* Child : Attached)
		{
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Child))
			{
				Primitive->SetCastHiddenShadow(bBodyVisible);
			}
		}
	}
	if (Body->bHiddenInGame != IsFirstPerson())
	{
		Body->SetHiddenInGame(IsFirstPerson(), true);
	}
	StandIn->SetVisibility(IsShown() && !bHasModel && !IsFirstPerson());

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

void AScctPawnProxy::SetNearFade(float Distance, const FVector& Eye)
{
	Distance = FMath::Max(Distance, 0.0f);
	if (Distance <= 0.0f && NearFade <= 0.0f && !bHeadHidden)
	{
		return;
	}
	NearFade = Distance;

	const FName Head = FindMeshBoneName(TEXT("B Head"));
	bool bHideHead = false;
	if (Distance > 0.0f && !Head.IsNone())
	{
		const float Reach = Distance * HeadGoneFactor + (bHeadHidden ? HeadBackMargin : 0.0f);
		bHideHead = FVector::DistSquared(Body->GetBoneLocation(Head), Eye) < FMath::Square(Reach);
	}
	if (bHideHead != bHeadHidden)
	{
		bHeadHidden = bHideHead;
		if (bHideHead)
		{
			Body->HideBoneByName(Head, PBO_None);
		}
		else
		{
			Body->UnHideBoneByName(Head);
		}
	}

	// A distance far beyond any view: whatever carries it is gone from the picture.
	constexpr float GoneEverywhere = 1.0e6f;

	// Setting a value that is already there costs nothing, so attachments made since the last call get theirs.
	Body->SetCustomPrimitiveDataFloat(NearFadeSlot, Distance);
	TArray<USceneComponent*> Attached;
	Body->GetChildrenComponents(true, Attached);
	for (USceneComponent* Child : Attached)
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Child))
		{
			// Hanging on the head: the part of its chain that sits on the body sits on the head bone.
			bool bOnHead = false;
			if (bHeadHidden)
			{
				const USceneComponent* Link = Child;
				while (Link->GetAttachParent() != nullptr && Link->GetAttachParent() != Body)
				{
					Link = Link->GetAttachParent();
				}
				bOnHead = Link->GetAttachParent() == Body && Link->GetAttachSocketName() == Head;
			}
			Primitive->SetCustomPrimitiveDataFloat(NearFadeSlot, bOnHead ? GoneEverywhere : Distance);
		}
	}
}

int32 AScctPawnProxy::FindMeshBone(const FString& BridgeBoneName) const
{
	const int32* Found = MeshBonesByKey.Find(BoneKey(BridgeBoneName));
	return Found != nullptr ? *Found : INDEX_NONE;
}

FName AScctPawnProxy::FindMeshBoneName(const FString& BridgeBoneName) const
{
	const int32 Bone = FindMeshBone(BridgeBoneName);
	return Bone != INDEX_NONE ? Body->GetBoneName(Bone) : NAME_None;
}

bool AScctPawnProxy::AttachToBone(USceneComponent* Component, const FString& BridgeBoneName, const FTransform& RelativeTransform)
{
	const FName Bone = FindMeshBoneName(BridgeBoneName);
	if (Component == nullptr || Bone.IsNone())
	{
		return false;
	}

	Component->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform, Bone);
	Component->SetRelativeTransform(RelativeTransform);

	// From here on it is part of the body: drawn, hidden and shadowed as the body is.
	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
	{
		Primitive->SetCastHiddenShadow(Body->bCastHiddenShadow);
	}
	Component->SetVisibility(Body->IsVisible(), true);
	Component->SetHiddenInGame(Body->bHiddenInGame, true);
	return true;
}
