#include "Play/ScctEquipmentView.h"

#include "AnimationRuntime.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctArmIk.h"
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
			// Arms and gun are drawn pulled in towards the eye, at a place the shadow maps do not have them: they
			// throw no shadow, on the world or on themselves (a muzzle flash a hand's width away striped the
			// sleeves black and white), as Godot's view model leaves them out of the shadow cameras.
			Primitive.SetCastShadow(false);
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

	// The bolt handle is thrown back in a fiftieth of a second and runs home in the next twentieth: too quick to
	// follow, long enough to be seen as a flicker of the handle at any frame rate.
	constexpr float BoltBackSeconds = 0.02f;
	constexpr float BoltHomeSeconds = 0.05f;
	// A gun on a body has no reload clip of its own: its magazine is out of the well this long after the reload
	// begins and back this long after, whatever the reload's length (the shortest is longer).
	constexpr float MagazineOutStart = 0.25f;
	constexpr float MagazineOutEnd = 0.5f;
	constexpr float MagazineInStart = 1.0f;
	constexpr float MagazineInEnd = 1.3f;
	// How fast the trigger and the selector follow the simulation (they ease, they do not snap).
	constexpr float TriggerSpeed = 45.0f;   // pulls per second
	constexpr float SelectorSpeed = 18.0f;  // FInterpTo speed

	/** A bone's place in its mesh's own space in the rest pose. */
	FTransform RestInMesh(const USkinnedMeshComponent& Mesh, int32 Bone)
	{
		const USkinnedAsset* Asset = Mesh.GetSkinnedAsset();
		return Asset != nullptr ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), Bone) : FTransform::Identity;
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
	GunBone = Model->MeshBoneByName(*Row->GunBone.ToString());
	OriginalMagazine = Model->MeshBoneByName(*Row->OriginalMagazineBone.ToString());
	// Posed like the original, so its own bones can be moved (Animate); it has no animation of its own.
	UPoseableMeshComponent* Drawn = NewObject<UPoseableMeshComponent>(&Owner, NAME_None, RF_Transient);
	Prepare(*Drawn, bFirstPerson);
	Drawn->SetSkinnedAssetAndUpdate(Other);
	if (bFirstPerson)
	{
		// The model's first level of detail is the one made to be seen from the eye, whatever size the engine
		// takes it to have on screen.
		Drawn->SetForcedLOD(1);
	}
	Drawn->SetupAttachment(Posed, GunBone != INDEX_NONE ? Posed->GetBoneName(GunBone) : NAME_None);
	Drawn->SetRelativeTransform(Row->AttachOffset);
	Drawn->RegisterComponent();
	Owner.AddInstanceComponent(Drawn);
	Replacement = Drawn;
	BoltBone = Drawn->GetBoneIndex(Row->BoltBone);
	TriggerBone = Drawn->GetBoneIndex(Row->TriggerBone);
	SelectorBone = Drawn->GetBoneIndex(Row->SelectorBone);
	MagazineBone = Drawn->GetBoneIndex(Row->MagazineBone);

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
	const int32 ProbedBone = Model->MeshBoneByName(*GunBoneName.ToString());
	const FTransform Gun = ProbedBone != INDEX_NONE ? Posed->GetBoneTransform(ProbedBone) : Posed->GetComponentTransform();
	UE_LOG(LogOpenSCCT, Log, TEXT("Weapon probe: '%s', %d bones, in the space of bone '%s'%s (cm; where each bone's X, Y and Z axes point)"),
		*Model->Name, Posed->GetNumBones(), *GunBoneName.ToString(), ProbedBone != INDEX_NONE ? TEXT("") : TEXT(" (NOT FOUND: the model's own space)"));
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
		if (Name.Contains(TEXT("Hand")) || Name.Contains(TEXT("Finger")) || Name.Contains(TEXT("Arm")))
		{
			const FTransform Local = Holder->GetBoneTransform(Bone).GetRelativeTransform(Gun);
			const FVector At = Local.GetLocation();
			const FVector X = Local.GetUnitAxis(EAxis::X);
			const FVector Y = Local.GetUnitAxis(EAxis::Y);
			const FVector Z = Local.GetUnitAxis(EAxis::Z);
			UE_LOG(LogOpenSCCT, Log, TEXT("Weapon probe:   holder's bone '%s' (child of '%s') at (%.1f %.1f %.1f) X (%.2f %.2f %.2f) Y (%.2f %.2f %.2f) Z (%.2f %.2f %.2f)"),
				*Name, *Holder->GetParentBone(Holder->GetBoneName(Bone)).ToString(), At.X, At.Y, At.Z, X.X, X.Y, X.Z, Y.X, Y.Y, Y.Z, Z.X, Z.Y, Z.Z);
		}
	}
	if (const UPoseableMeshComponent* Other = Replacement.Get())
	{
		const FTransform Offset = Other->GetRelativeTransform();
		UE_LOG(LogOpenSCCT, Log, TEXT("Weapon probe: replacement %s sits at (%.1f %.1f %.1f) rotation (pitch %.1f yaw %.1f roll %.1f) in the gun bone's space; its sockets in its own space:"),
			*GetNameSafe(Other->GetSkinnedAsset()), Offset.GetLocation().X, Offset.GetLocation().Y, Offset.GetLocation().Z,
			Offset.Rotator().Pitch, Offset.Rotator().Yaw, Offset.Rotator().Roll);
		// The numbers the row's left hand is worked out from: the hand bone as it is drawn, seen from the grip.
		const USkinnedMeshComponent* Arms = Holder != nullptr ? Holder : Posed;
		const int32 HandBone = Row != nullptr ? Arms->GetBoneIndex(Row->LeftHandBone) : INDEX_NONE;
		if (HandBone != INDEX_NONE && Other->DoesSocketExist(Row->LeftHandSocket))
		{
			const FTransform Local = Arms->GetBoneTransform(HandBone).GetRelativeTransform(Other->GetSocketTransform(Row->LeftHandSocket));
			const FVector At = Local.GetLocation();
			UE_LOG(LogOpenSCCT, Log, TEXT("Weapon probe:   left hand bone '%s' as drawn, in the space of socket '%s': at (%.1f %.1f %.1f) rotation (pitch %.1f yaw %.1f roll %.1f)"),
				*Row->LeftHandBone.ToString(), *Row->LeftHandSocket.ToString(), At.X, At.Y, At.Z, Local.Rotator().Pitch, Local.Rotator().Yaw, Local.Rotator().Roll);
		}
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
	GunBone = OriginalMagazine = BoltBone = TriggerBone = SelectorBone = MagazineBone = INDEX_NONE;
	bPosed = bWasReloading = bSelectorSet = bArmReported = false;
	LastShot = -1;
	BoltAge = ReloadAge = -1.0f;
	TriggerPulled = SelectorTurned = 0.0f;
}

void FScctEquipmentView::ApplyPose(TConstArrayView<FScctBridgeBone> Pose)
{
	UPoseableMeshComponent* Posed = Body.Get();
	if (Posed == nullptr || Model == nullptr)
	{
		return;
	}

	Model->ApplyPose(*Posed, Pose);
	bPosed = Pose.Num() > 0;
	CopyPoseToFollowers();
}

void FScctEquipmentView::CopyPoseToFollowers()
{
	UPoseableMeshComponent* Posed = Body.Get();
	for (const TWeakObjectPtr<UPoseableMeshComponent>& Follower : Followers)
	{
		// The same skeleton, so the same bones in the same order.
		if (Posed != nullptr && Follower.IsValid() && Follower->BoneSpaceTransforms.Num() == Posed->BoneSpaceTransforms.Num())
		{
			Follower->BoneSpaceTransforms = Posed->BoneSpaceTransforms;
			Follower->RefreshBoneTransforms();
		}
	}
}

void FScctEquipmentView::Animate(const FScctWeaponMotion& Motion, float DeltaSeconds)
{
	UPoseableMeshComponent* Gun = Replacement.Get();
	UPoseableMeshComponent* Posed = Body.Get();
	const USkinnedAsset* Asset = Gun != nullptr ? Gun->GetSkinnedAsset() : nullptr;
	if (Asset == nullptr || Row == nullptr || Posed == nullptr)
	{
		return;
	}
	const TArray<FTransform>& Rest = Asset->GetRefSkeleton().GetRefBonePose();
	TArray<FTransform>& Local = Gun->BoneSpaceTransforms;
	const float Step = FMath::Max(DeltaSeconds, 0.0f);

	// A shot: the count went up. The first count seen is only where the pawn's shots stand.
	if (Motion.ShotCount >= 0 && LastShot >= 0 && Motion.ShotCount != LastShot)
	{
		BoltAge = 0.0f;
	}
	else if (BoltAge >= 0.0f)
	{
		BoltAge += Step;
		if (BoltAge > BoltBackSeconds + BoltHomeSeconds)
		{
			BoltAge = -1.0f;
		}
	}
	LastShot = Motion.ShotCount;
	if (Local.IsValidIndex(BoltBone))
	{
		const float Back = BoltAge < 0.0f ? 0.0f : BoltAge < BoltBackSeconds ? BoltAge / BoltBackSeconds : 1.0f - (BoltAge - BoltBackSeconds) / BoltHomeSeconds;
		Local[BoltBone] = Rest[BoltBone];
		// The model's barrel runs along +X and its bones have the model's axes.
		Local[BoltBone].AddToTranslation(FVector(-Row->BoltTravel * FMath::Clamp(Back, 0.0f, 1.0f), 0.0, 0.0));
	}

	if (Local.IsValidIndex(TriggerBone))
	{
		TriggerPulled = FMath::FInterpConstantTo(TriggerPulled, Motion.bFiring ? 1.0f : 0.0f, Step, TriggerSpeed);
		Local[TriggerBone] = FTransform(FRotator(Row->TriggerPull * TriggerPulled, 0.0, 0.0)) * Rest[TriggerBone];
	}

	if (Local.IsValidIndex(SelectorBone) && Motion.RateOfFire >= 0 && Motion.RateOfFire <= 2)
	{
		const float Wanted = static_cast<float>(Row->SelectorTurn[Motion.RateOfFire]);
		// Where the weapon stands when it is first seen; a turn of the selector after that is seen to turn.
		SelectorTurned = bSelectorSet ? FMath::FInterpTo(SelectorTurned, Wanted, Step, SelectorSpeed) : Wanted;
		bSelectorSet = true;
		Local[SelectorBone] = FTransform(FRotator(SelectorTurned, 0.0, 0.0)) * Rest[SelectorBone];
	}

	if (Motion.bReloading && !bWasReloading)
	{
		ReloadAge = 0.0f;
	}
	else if (ReloadAge >= 0.0f)
	{
		ReloadAge += Step;
		if (ReloadAge > MagazineInEnd && !Motion.bReloading)
		{
			ReloadAge = -1.0f;
		}
	}
	bWasReloading = Motion.bReloading;
	if (Local.IsValidIndex(MagazineBone))
	{
		Local[MagazineBone] = Rest[MagazineBone];
		if (bPosed && Posed->BoneSpaceTransforms.IsValidIndex(OriginalMagazine) && Posed->BoneSpaceTransforms.IsValidIndex(GunBone))
		{
			// The original's magazine as the bridge has it this frame, against where it rests in the gun: the
			// same movement about the gun for the replacement's magazine. Both in the gun bone's space; the
			// replacement sits in that space at the row's offset.
			const FTransform Now = Posed->GetBoneTransform(OriginalMagazine).GetRelativeTransform(Posed->GetBoneTransform(GunBone));
			const FTransform AtRest = RestInMesh(*Posed, OriginalMagazine).GetRelativeTransform(RestInMesh(*Posed, GunBone));
			FTransform Moved = AtRest.Inverse() * Now;
			Moved.SetScale3D(FVector::OneVector);
			const FTransform InGun = RestInMesh(*Gun, MagazineBone) * Row->AttachOffset;
			const FTransform InMesh = (InGun * Moved).GetRelativeTransform(Row->AttachOffset);
			const int32 Parent = Asset->GetRefSkeleton().GetParentIndex(MagazineBone);
			Local[MagazineBone] = Parent != INDEX_NONE ? InMesh.GetRelativeTransform(RestInMesh(*Gun, Parent)) : InMesh;
		}
		else if (ReloadAge >= 0.0f)
		{
			const float Out = FMath::SmoothStep(MagazineOutStart, MagazineOutEnd, ReloadAge) - FMath::SmoothStep(MagazineInStart, MagazineInEnd, ReloadAge);
			// Out of the well: down, in the model's axes.
			Local[MagazineBone].AddToTranslation(FVector(0.0, 0.0, -Row->MagazineDrop * FMath::Clamp(Out, 0.0f, 1.0f)));
		}
	}
	Gun->RefreshBoneTransforms();
}

void FScctEquipmentView::HoldWithLeftHand(UPoseableMeshComponent& Holder)
{
	UPoseableMeshComponent* Gun = Replacement.Get();
	UPoseableMeshComponent* Posed = Body.Get();
	if (Gun == nullptr || Posed == nullptr || Row == nullptr || Row->LeftHandBone.IsNone() || !Gun->DoesSocketExist(Row->LeftHandSocket)
		|| !Posed->BoneSpaceTransforms.IsValidIndex(GunBone))
	{
		return;
	}
	const int32 UpperArm = Holder.GetBoneIndex(Row->LeftUpperArmBone);
	const int32 Forearm = Holder.GetBoneIndex(Row->LeftForearmBone);
	const int32 Hand = Holder.GetBoneIndex(Row->LeftHandBone);
	if (UpperArm == INDEX_NONE || Forearm == INDEX_NONE || Hand == INDEX_NONE)
	{
		return;
	}

	// How much the hand is the gun's: all of it while the animation holds the original, none once a clip has
	// taken the hand away from it.
	const FTransform Animated = Holder.GetBoneTransform(Hand);
	const FTransform GunSpace = Posed->GetBoneTransform(GunBone);
	const double Away = FVector::Dist(GunSpace.InverseTransformPosition(Animated.GetLocation()), Row->OriginalLeftHand);
	const float Weight = 1.0f - FMath::SmoothStep(Row->LeftHandReach / 3.0f, Row->LeftHandReach, static_cast<float>(Away));
	if (Weight <= 0.0f)
	{
		return;
	}

	const FTransform Gripping = Row->LeftHandInSocket * Gun->GetSocketTransform(Row->LeftHandSocket);
	FTransform Wanted = Animated;
	Wanted.SetLocation(FMath::Lerp(Animated.GetLocation(), Gripping.GetLocation(), Weight));
	if (!Row->bLeftHandKeepsTurn)
	{
		Wanted.SetRotation(FQuat::Slerp(Animated.GetRotation(), Gripping.GetRotation(), Weight));
	}
	const FVector Elbow = Holder.GetBoneTransform(Forearm).GetLocation() + Gun->GetComponentTransform().TransformVector(Row->LeftElbowPush) * Weight;
	if (!FScctArmIk::Reach(Holder, UpperArm, Forearm, Hand, Wanted, Elbow))
	{
		if (!bArmReported)
		{
			bArmReported = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Equipment: the left arm of weapon table row %s ('%s', '%s', '%s') is not a chain of three bones in %s; the hand keeps the animation's place"),
				*Row->SimulationClass.ToString(), *Row->LeftUpperArmBone.ToString(), *Row->LeftForearmBone.ToString(), *Row->LeftHandBone.ToString(), *GetNameSafe(Holder.GetSkinnedAsset()));
		}
		return;
	}
	if (&Holder == Posed)
	{
		CopyPoseToFollowers();
	}
}

void FScctEquipmentView::ResetPose()
{
	bPosed = false;
	ForEachMesh([this](UMeshComponent& Mesh)
	{
		// The replacement's own bones are Animate's.
		if (UPoseableMeshComponent* Posed = &Mesh != Replacement.Get() ? Cast<UPoseableMeshComponent>(&Mesh) : nullptr)
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
