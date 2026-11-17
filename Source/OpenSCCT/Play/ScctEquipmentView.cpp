#include "Play/ScctEquipmentView.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctEquipmentData.h"
#include "Play/ScctEquipmentModels.h"

namespace
{
	// Read when a weapon's components are made (a new match, a weapon drawn for the first time), to compare a
	// replacement model with the original it stands in for.
	TAutoConsoleVariable<int32> CVarReplacements(TEXT("scct.Weapons.Replacements"), 1,
		TEXT("1: draw the weapon table's replacement models where their assets exist. 0: always the original models."));

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

	USkeletalMesh* Other = Row != nullptr && Row->ReplacementMesh.IsValid() && CVarReplacements.GetValueOnGameThread() != 0
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

void FScctEquipmentView::LogProbe(const USkinnedMeshComponent* Holder)
{
	static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("ScctWeaponProbe"));
	UPoseableMeshComponent* Posed = Body.Get();
	if (!bProbe || Posed == nullptr || Model == nullptr || FPlatformTime::Seconds() < NextProbe)
	{
		return;
	}
	NextProbe = FPlatformTime::Seconds() + 4.0;

	const FName GunBoneName = Row != nullptr ? Row->GunBone : FName(TEXT("corps"));
	const int32 GunBone = Model->MeshBoneByName(*GunBoneName.ToString());
	const FTransform Gun = GunBone != INDEX_NONE ? Posed->GetBoneTransform(GunBone) : Posed->GetComponentTransform();
	UE_LOG(LogOpenSCCT, Log, TEXT("Weapon probe: '%s', %d bones, in the space of bone '%s'%s (cm; where each bone's X, Y and Z axes point)"),
		*Model->Name, Posed->GetNumBones(), *GunBoneName.ToString(), GunBone != INDEX_NONE ? TEXT("") : TEXT(" (NOT FOUND: the model's own space)"));
	for (int32 Bone = 0; Bone < Posed->GetNumBones(); ++Bone)
	{
		const FTransform Local = Posed->GetBoneTransform(Bone).GetRelativeTransform(Gun);
		const FVector At = Local.GetLocation();
		const FVector X = Local.GetUnitAxis(EAxis::X);
		const FVector Y = Local.GetUnitAxis(EAxis::Y);
		const FVector Z = Local.GetUnitAxis(EAxis::Z);
		UE_LOG(LogOpenSCCT, Log, TEXT("Weapon probe:   bone %2d '%s' at (%.1f %.1f %.1f) X (%.2f %.2f %.2f) Y (%.2f %.2f %.2f) Z (%.2f %.2f %.2f)"),
			Bone, *Posed->GetBoneName(Bone).ToString(), At.X, At.Y, At.Z, X.X, X.Y, X.Z, Y.X, Y.Y, Y.Z, Z.X, Z.Y, Z.Z);
	}
	for (int32 Bone = 0; Holder != nullptr && Bone < Holder->GetNumBones(); ++Bone)
	{
		const FString Name = Holder->GetBoneName(Bone).ToString();
		if (Name.Contains(TEXT("Hand")) || Name.Contains(TEXT("Finger")) || Name.Contains(TEXT("Forearm")))
		{
			const FTransform Local = Holder->GetBoneTransform(Bone).GetRelativeTransform(Gun);
			const FVector At = Local.GetLocation();
			const FVector X = Local.GetUnitAxis(EAxis::X);
			UE_LOG(LogOpenSCCT, Log, TEXT("Weapon probe:   holder's bone '%s' at (%.1f %.1f %.1f) X (%.2f %.2f %.2f)"), *Name, At.X, At.Y, At.Z, X.X, X.Y, X.Z);
		}
	}
	if (const USkeletalMeshComponent* Other = Replacement.Get())
	{
		const FTransform Offset = Other->GetRelativeTransform();
		UE_LOG(LogOpenSCCT, Log, TEXT("Weapon probe: replacement %s sits at (%.1f %.1f %.1f) rotation (pitch %.1f yaw %.1f roll %.1f) in the gun bone's space; its sockets in its own space:"),
			*GetNameSafe(Other->GetSkeletalMeshAsset()), Offset.GetLocation().X, Offset.GetLocation().Y, Offset.GetLocation().Z,
			Offset.Rotator().Pitch, Offset.Rotator().Yaw, Offset.Rotator().Roll);
		for (const FName& Socket : Other->GetAllSocketNames())
		{
			const FVector At = Other->GetSocketTransform(Socket, RTS_Component).GetLocation();
			UE_LOG(LogOpenSCCT, Log, TEXT("Weapon probe:   socket or bone '%s' at (%.1f %.1f %.1f)"), *Socket.ToString(), At.X, At.Y, At.Z);
		}
	}
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
	NextProbe = 0.0;
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
				Component.SetMaterial(Slot, State->Originals[Slot]);
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

void FScctCloakLook::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (TPair<TWeakObjectPtr<UMeshComponent>, FState>& Pair : Cloaked)
	{
		Collector.AddReferencedObjects(Pair.Value.Originals);
	}
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
