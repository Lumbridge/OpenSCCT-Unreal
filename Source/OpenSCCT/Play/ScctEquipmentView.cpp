#include "Play/ScctEquipmentView.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "OpenSCCT.h"
#include "Play/ScctEquipmentData.h"
#include "Play/ScctEquipmentModels.h"

namespace
{
	void Prepare(UPrimitiveComponent& Primitive, bool bFirstPerson)
	{
		// The simulation does all collision; Unreal's physics must not see these.
		Primitive.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Primitive.SetCanEverAffectNavigation(false);
		if (bFirstPerson)
		{
			Primitive.SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
			// A posed mesh's bounds are those of its rest pose; arms that swing far from it must not be culled
			// while they are in the middle of the screen.
			Primitive.SetBoundsScale(4.0f);
		}
	}

	UPoseableMeshComponent* MakePosed(AActor& Owner, USkeletalMesh* Mesh, bool bFirstPerson)
	{
		UPoseableMeshComponent* Component = NewObject<UPoseableMeshComponent>(&Owner, NAME_None, RF_Transient);
		Prepare(*Component, bFirstPerson);
		Component->SetSkinnedAssetAndUpdate(Mesh);
		Component->SetupAttachment(Owner.GetRootComponent());
		Component->RegisterComponent();
		Owner.AddInstanceComponent(Component);
		return Component;
	}
}

void FScctEquipmentView::Show(AActor& Owner, const FScctEquipmentModel* NewModel, const FScctWeaponPresentation* NewRow, bool bFirstPerson)
{
	Destroy();
	Model = NewModel;
	Row = NewRow;
	if (Model == nullptr || !Model->Mesh.IsValid())
	{
		return;
	}

	UPoseableMeshComponent* Posed = MakePosed(Owner, Model->Mesh.Get(), bFirstPerson);
	Body = Posed;
	for (const TStrongObjectPtr<USkeletalMesh>& Extra : Model->ExtraMeshes)
	{
		UPoseableMeshComponent* Follower = MakePosed(Owner, Extra.Get(), bFirstPerson);
		Follower->AttachToComponent(Posed, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Followers.Add(Follower);
	}

	USkeletalMesh* Other = Row != nullptr && Row->ReplacementMesh.IsValid()
		? LoadObject<USkeletalMesh>(nullptr, *Row->ReplacementMesh.ToString(), nullptr, LOAD_NoWarn) : nullptr;
	if (Other == nullptr)
	{
		return;
	}

	// The replacement rides on the original's gun bone, so it follows every pose the bridge computes for the
	// original (recoil, reload, the draw); the original gun is hidden under it.
	const int32 GunBone = Model->MeshBoneByName(*Row->GunBone.ToString());
	USkeletalMeshComponent* Drawn = NewObject<USkeletalMeshComponent>(&Owner, NAME_None, RF_Transient);
	Prepare(*Drawn, bFirstPerson);
	Drawn->SetSkeletalMesh(Other);
	Drawn->SetupAttachment(Posed, GunBone != INDEX_NONE ? Posed->GetBoneName(GunBone) : NAME_None);
	Drawn->SetRelativeTransform(Row->AttachOffset);
	Drawn->RegisterComponent();
	Owner.AddInstanceComponent(Drawn);
	Replacement = Drawn;

	if (Row->bFirstPerson)
	{
		for (const FName& Bone : Row->HiddenBones)
		{
			const int32 MeshBone = Model->MeshBoneByName(*Bone.ToString());
			if (MeshBone != INDEX_NONE)
			{
				Posed->HideBone(MeshBone, PBO_None);
			}
		}
	}
	else
	{
		Posed->HideBone(0, PBO_None);
		for (const TWeakObjectPtr<UPoseableMeshComponent>& Follower : Followers)
		{
			Follower->HideBone(0, PBO_None);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Equipment: '%s' is drawn as %s (weapon table row %s), fixed to bone '%s'"),
		*Model->Name, *Row->ReplacementMesh.ToString(), *Row->SimulationClass.ToString(), *Row->GunBone.ToString());
}

void FScctEquipmentView::Destroy()
{
	if (Replacement.IsValid())
	{
		Replacement->DestroyComponent();
	}
	for (const TWeakObjectPtr<UPoseableMeshComponent>& Follower : Followers)
	{
		if (Follower.IsValid())
		{
			Follower->DestroyComponent();
		}
	}
	if (Body.IsValid())
	{
		Body->DestroyComponent();
	}
	Replacement.Reset();
	Followers.Reset();
	Body.Reset();
	Model = nullptr;
	Row = nullptr;
}

void FScctEquipmentView::ApplyPose(TConstArrayView<FScctBridgeBone> Pose)
{
	UPoseableMeshComponent* Posed = Body.Get();
	if (Posed == nullptr || Model == nullptr)
	{
		return;
	}

	Model->ApplyPose(*Posed, Pose);
	for (const TWeakObjectPtr<UPoseableMeshComponent>& Follower : Followers)
	{
		// The same skeleton, so the same bones in the same order.
		if (Follower.IsValid() && Follower->BoneSpaceTransforms.Num() == Posed->BoneSpaceTransforms.Num())
		{
			Follower->BoneSpaceTransforms = Posed->BoneSpaceTransforms;
			Follower->RefreshBoneTransforms();
		}
	}
}

void FScctEquipmentView::ResetPose()
{
	ForEachMesh([](UMeshComponent& Mesh)
	{
		if (UPoseableMeshComponent* Posed = Cast<UPoseableMeshComponent>(&Mesh))
		{
			if (const USkinnedAsset* Asset = Posed->GetSkinnedAsset())
			{
				Posed->BoneSpaceTransforms = Asset->GetRefSkeleton().GetRefBonePose();
				Posed->RefreshBoneTransforms();
			}
		}
	});
}

void FScctEquipmentView::ForEachMesh(TFunctionRef<void(UMeshComponent&)> Visit) const
{
	if (Body.IsValid())
	{
		Visit(*Body);
	}
	for (const TWeakObjectPtr<UPoseableMeshComponent>& Follower : Followers)
	{
		if (Follower.IsValid())
		{
			Visit(*Follower);
		}
	}
	if (Replacement.IsValid())
	{
		Visit(*Replacement);
	}
}

bool FScctEquipmentView::FindPoint(FName ReplacementSocket, const TCHAR* OriginalBone, FTransform& OutWorld) const
{
	if (Replacement.IsValid() && !ReplacementSocket.IsNone() && Replacement->DoesSocketExist(ReplacementSocket))
	{
		OutWorld = Replacement->GetSocketTransform(ReplacementSocket);
		return true;
	}

	const int32 Bone = Body.IsValid() && Model != nullptr ? Model->MeshBoneByName(OriginalBone) : INDEX_NONE;
	if (Bone == INDEX_NONE)
	{
		return false;
	}
	OutWorld = Body->GetBoneTransform(Bone);
	return true;
}

bool FScctEquipmentView::FindMuzzle(FTransform& OutWorld) const
{
	return FindPoint(Row != nullptr ? Row->MuzzleSocket : NAME_None, TEXT("FX"), OutWorld);
}

void FScctCloakLook::Apply(UMeshComponent& Component, float Amount)
{
	static const FName AmountName(TEXT("Amount"));
	static const FName BaseColorName(TEXT("BaseColor"));

	FState* State = Cloaked.Find(&Component);
	if (Amount <= 0.01f)
	{
		if (State != nullptr)
		{
			for (int32 Slot = 0; Slot < State->Originals.Num(); ++Slot)
			{
				Component.SetMaterial(Slot, State->Originals[Slot].Get());
			}
			Component.SetCastShadow(State->bCastShadow);
			Cloaked.Remove(&Component);
		}
		return;
	}

	if (State == nullptr)
	{
		static TWeakObjectPtr<UMaterialInterface> CloakMaterial;
		if (!CloakMaterial.IsValid())
		{
			CloakMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Weapons/Materials/M_ScctCloak.M_ScctCloak"), nullptr, LOAD_NoWarn);
			if (!CloakMaterial.IsValid())
			{
				return;
			}
			CloakMaterial->AddToRoot();
		}

		State = &Cloaked.Add(&Component);
		State->bCastShadow = Component.CastShadow;
		for (int32 Slot = 0; Slot < Component.GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Original = Component.GetMaterial(Slot);
			State->Originals.Add(Original);
			UMaterialInstanceDynamic* Cloak = UMaterialInstanceDynamic::Create(CloakMaterial.Get(), &Component);
			UTexture* Texture = nullptr;
			if (Original != nullptr && Original->GetTextureParameterValue(BaseColorName, Texture) && Texture != nullptr)
			{
				Cloak->SetTextureParameterValue(BaseColorName, Texture);
			}
			Component.SetMaterial(Slot, Cloak);
		}
	}

	for (int32 Slot = 0; Slot < Component.GetNumMaterials(); ++Slot)
	{
		if (UMaterialInstanceDynamic* Cloak = Cast<UMaterialInstanceDynamic>(Component.GetMaterial(Slot)))
		{
			Cloak->SetScalarParameterValue(AmountName, Amount);
		}
	}
	// Bent light throws no shadow: it thins out with the suit and is gone once the suit is mostly on.
	Component.SetCastShadow(State->bCastShadow && Amount < 0.5f);
}

void FScctCloakLook::Trim()
{
	for (auto It = Cloaked.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
