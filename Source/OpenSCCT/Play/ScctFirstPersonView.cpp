#include "Play/ScctFirstPersonView.h"

#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctEquipmentData.h"
#include "Play/ScctEquipmentModels.h"
#include "Play/ScctExposure.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 MaxViewBones = 160;

	// The flash is over in a few frames: long enough to be seen at any frame rate, short enough to read as a
	// crack of light and not a glow. Its light outlasts it by a little, as the eye's impression does.
	constexpr float FlashSeconds = 0.05f;
	constexpr float FlashLightSeconds = 0.085f;
	// In a moderately lit room; scaled by the picture's exposure when it fires (Play/ScctExposure.h), so the
	// flash does not white out the screen when the eye is open for the dark. At 26 cd a shot lit nothing: in
	// a lamp-lit room of the maps the exposure is at its dark end and the scale at its least (0.04), which left
	// one candela. Looked at with the flash held in such a room (AquaE's Pirates' Room): 500 still shows nothing,
	// 800 warms the floor, the posts and the ceiling beams around the gun, 1250 is a strong glare and 3000 burns
	// the floor white.
	constexpr float FlashLightCandelas = 800.0f;

	TAutoConsoleVariable<float> CVarFlashCandelas(TEXT("scct.Flash.Candelas"), FlashLightCandelas,
		TEXT("Candelas of the light of the local player's muzzle flash in a moderately lit room (it is scaled by the picture's exposure)."));

	struct FFlashShape
	{
		FVector Offset;  // cm from the muzzle, X along the barrel
		FVector Size;    // cm across
		float Intensity;
		float Sharpness;
	};

	// A hot core, a tongue along the barrel and two thin flares across it (turned to a new angle every shot).
	const FFlashShape FlashShapeList[] = {
		{ FVector(1.5, 0.0, 0.0), FVector(4.5, 4.5, 4.5), 90.0f, 1.5f },
		{ FVector(8.0, 0.0, 0.0), FVector(17.0, 3.5, 3.5), 45.0f, 2.0f },
		{ FVector(2.0, 0.0, 0.0), FVector(1.8, 15.0, 1.8), 35.0f, 2.5f },
		{ FVector(2.0, 0.0, 0.0), FVector(1.8, 1.8, 10.0), 35.0f, 2.5f },
	};
}

AScctFirstPersonView::AScctFirstPersonView()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	FlashRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FlashRoot"));
	FlashRoot->SetupAttachment(Root);
	FlashRoot->SetUsingAbsoluteLocation(true);
	FlashRoot->SetUsingAbsoluteRotation(true);
	FlashRoot->SetUsingAbsoluteScale(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(FlashShapeList); ++Index)
	{
		UStaticMeshComponent* Shape = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Flash%d"), Index));
		Shape->SetupAttachment(FlashRoot);
		Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shape->SetCastShadow(false);
		Shape->SetVisibility(false);
		// Drawn with the gun, in front of any wall the gun is in front of.
		Shape->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
		if (Sphere.Succeeded())
		{
			Shape->SetStaticMesh(Sphere.Object);
		}
		FlashShapes.Add(Shape);
	}

	FlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashLight"));
	FlashLight->SetupAttachment(Root);
	FlashLight->SetUsingAbsoluteLocation(true);
	FlashLight->SetIntensityUnits(ELightUnits::Candelas);
	FlashLight->SetIntensity(0.0f);
	FlashLight->SetAttenuationRadius(900.0f);
	FlashLight->SetSourceRadius(3.0f);
	FlashLight->SetLightColor(FLinearColor(1.0f, 0.74f, 0.42f));
	FlashLight->SetCastShadows(true);
	// A flash is too short to hang in the air: almost none of it scatters in the fog.
	FlashLight->SetVolumetricScatteringIntensity(0.2f);
	FlashLight->SetVisibility(false);
}

void AScctFirstPersonView::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	// The camo suit keeps the materials it took off the view model.
	CastChecked<AScctFirstPersonView>(InThis)->CloakLook.AddReferencedObjects(Collector);
	Super::AddReferencedObjects(InThis, Collector);
}

void AScctFirstPersonView::Present(const FScctEquipmentApi& Api, FScctEquipmentModels& Models, const UScctWeaponSet* Weapons,
	const FScctFirstPerson& View, const FTransform& Camera, float Cloak, const FScctWeaponMotion& Motion, float DeltaSeconds)
{
	SetActorTransform(Camera);
	LastCamera = Camera;
	bHasMuzzle = false;
	bHasLaser = false;

	const int32 PartCount = View.Kind != EScctFirstPersonKind::None ? FMath::Clamp(View.PartCount, 0, 2) : 0;
	// The part that is the weapon: the merc's one model, the spy's second (the first is his arms).
	const int32 WeaponPart = View.Kind == EScctFirstPersonKind::Spy ? 1 : 0;
	const bool bWasShown = bShown;
	bShown = false;

	PoseBuffer.SetNumUninitialized(MaxViewBones);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Parts); ++Index)
	{
		FScctEquipmentView& Drawn = Parts[Index];
		if (Index >= PartCount)
		{
			if (Drawn.IsShown() && Drawn.GetBody()->IsVisible())
			{
				Drawn.GetBody()->SetVisibility(false, true);
			}
			continue;
		}

		const FScctFirstPersonPart& Part = View.Parts[Index];
		if (PartMeshes[Index] != Part.Mesh || !Drawn.IsShown())
		{
			const FScctWeaponPresentation* Row = Weapons != nullptr && Index == WeaponPart ? Weapons->Find(FName(*Models.Name(Api, View.WeaponClass))) : nullptr;
			if (Row != nullptr && !Row->bFirstPerson)
			{
				Row = nullptr;
			}
			const FScctEquipmentModel* Model = Models.Find(Api, Part.Mesh, Part.MeshPath, Row != nullptr && Row->Mesh.IsValid() ? &Row->Mesh : nullptr);
			Drawn.Show(*this, Model, Row, true);
			PartMeshes[Index] = Part.Mesh;
			RateOfFireSkin = -1;
			if (Model != nullptr)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("First person: part %d is '%s' (weapon class %s), %d bones posed by the bridge, muzzle bone %d, laser bone %d"),
					Index, *Model->Name, *Models.Name(Api, View.WeaponClass), Part.BoneCount, Part.MuzzleBone, Part.LaserBone);
			}
		}
		if (!Drawn.IsShown())
		{
			continue;
		}

		UPoseableMeshComponent* Body = Drawn.GetBody();
		// A model can come with a scale of its own above its skeleton (OpenSCCT's pistols are authored a hundred
		// times too large under a frame scaled 0.01). On the component that scale also shrinks the mesh's bounds
		// to a speck, and Unreal then does not draw the mesh at all. It is moved onto the skeleton's root bones,
		// which puts every bone in the same place with the same size and leaves the bounds as the mesh has them.
		FTransform ModelToCamera = FScctPose::ToUnreal(Part.ModelToCamera);
		const float ModelScale = static_cast<float>(ModelToCamera.GetScale3D().X);
		const bool bModelScaled = ModelScale > UE_SMALL_NUMBER && !FMath::IsNearlyEqual(ModelScale, 1.0f, 0.001f) && ModelToCamera.GetScale3D().IsUniform(0.001);
		if (bModelScaled)
		{
			ModelToCamera.SetScale3D(FVector::OneVector);
		}
		Body->SetRelativeTransform(ModelToCamera);
		if (!Body->IsVisible())
		{
			Body->SetVisibility(true, true);
		}
		const int32 Bones = FMath::Clamp(Api.GetFirstPersonPose(Index, PoseBuffer.GetData(), PoseBuffer.Num()), 0, PoseBuffer.Num());
		if (bModelScaled && Drawn.GetModel() != nullptr)
		{
			const TArray<FScctBridgeBoneInfo>& Skeleton = Drawn.GetModel()->Skeleton;
			for (int32 Bone = 0; Bone < Bones && Bone < Skeleton.Num(); ++Bone)
			{
				if (Skeleton[Bone].Parent < 0)
				{
					FScctBridgeBone& Top = PoseBuffer[Bone];
					Top.Px *= ModelScale; Top.Py *= ModelScale; Top.Pz *= ModelScale;
					Top.Sx *= ModelScale; Top.Sy *= ModelScale; Top.Sz *= ModelScale;
				}
			}
		}
		Drawn.ApplyPose(TConstArrayView<FScctBridgeBone>(PoseBuffer.GetData(), Bones));
		if (Index == WeaponPart && Drawn.HasReplacement())
		{
			// A model drawn in place of the original gun: its parts move with the simulation and the left hand
			// of the arms (this same model) closes on its grip.
			Drawn.Animate(Motion, DeltaSeconds);
			Drawn.HoldWithLeftHand(*Body);
		}
		Drawn.LogProbe();
		LogViewProbe(Index, *Body, Camera);
		Drawn.ForEachMesh([this, Cloak](UMeshComponent& Mesh) { CloakLook.Apply(Mesh, Cloak); });
		bShown = true;

		// The bridge's own muzzle and laser socket are exact for the original model; a replacement gun has its own.
		if (Index == WeaponPart || PartCount == 1)
		{
			if (Part.MuzzleBone >= 0)
			{
				Muzzle = FScctPose::ToUnreal(Part.Muzzle) * Camera;
				bHasMuzzle = true;
			}
			if (Part.LaserBone >= 0)
			{
				Laser = FScctPose::ToUnreal(Part.Laser) * Camera;
				bHasLaser = true;
			}
			FTransform Socket;
			if (Drawn.HasReplacement())
			{
				if (Drawn.FindPoint(Drawn.GetRow()->MuzzleSocket, TEXT(""), Socket))
				{
					Muzzle = Socket;
					bHasMuzzle = true;
				}
				if (Drawn.FindPoint(Drawn.GetRow()->LaserSocket, TEXT(""), Socket))
				{
					Laser = Socket;
					bHasLaser = true;
				}
			}
		}

		// The rifle's rate-of-fire display is a material of its own per setting (material slot 3 of the merc's model).
		if (Index == 0 && View.Kind == EScctFirstPersonKind::Merc && RateOfFireSkin != View.RateOfFireSkin && Drawn.GetModel() != nullptr)
		{
			RateOfFireSkin = View.RateOfFireSkin;
			UMaterialInterface* Skin = View.RateOfFireSkin > 0 ? Models.FindSkin(Drawn.GetModel()->Package, Models.Name(Api, View.RateOfFireSkin)) : nullptr;
			if (Body->GetNumMaterials() > 3 && (Skin != nullptr || View.RateOfFireSkin <= 0))
			{
				Body->SetMaterial(3, Skin);
			}
		}
	}

	if (bShown != bWasShown)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("First person: view model %s (kind %d, %d parts); muzzle %.1f cm from the eye"),
			bShown ? TEXT("shown") : TEXT("hidden"), View.Kind, PartCount, bHasMuzzle ? FVector::Dist(Muzzle.GetLocation(), Camera.GetLocation()) : 0.0);
	}
	CloakLook.Trim();
	TickFlash(DeltaSeconds);
}

void AScctFirstPersonView::LogViewProbe(int32 Index, UPoseableMeshComponent& Body, const FTransform& Camera)
{
	// -ScctWeaponProbe: where the view model's bones are from the eye (X ahead, Y right, Z up), every few seconds.
	static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("ScctWeaponProbe"));
	if (!bProbe || FPlatformTime::Seconds() < NextViewProbe[Index])
	{
		return;
	}
	NextViewProbe[Index] = FPlatformTime::Seconds() + 4.0;

	const FTransform Model = Body.GetRelativeTransform();
	const FVector Centre = Camera.InverseTransformPosition(Body.Bounds.Origin);
	UE_LOG(LogOpenSCCT, Log, TEXT("First person probe: part %d %s sits at (%.1f %.1f %.1f) rotation (pitch %.1f yaw %.1f roll %.1f) scale (%.2f %.2f %.2f) from the eye; bounds centre (%.1f %.1f %.1f) radius %.1f; %d bones, from the eye:"),
		Index, *GetNameSafe(Body.GetSkinnedAsset()), Model.GetLocation().X, Model.GetLocation().Y, Model.GetLocation().Z,
		Model.Rotator().Pitch, Model.Rotator().Yaw, Model.Rotator().Roll, Model.GetScale3D().X, Model.GetScale3D().Y, Model.GetScale3D().Z,
		Centre.X, Centre.Y, Centre.Z, Body.Bounds.SphereRadius, Body.GetNumBones());
	for (int32 Bone = 0; Bone < Body.GetNumBones(); ++Bone)
	{
		const FTransform World = Body.GetBoneTransform(Bone);
		const FVector At = Camera.InverseTransformPosition(World.GetLocation());
		const FTransform Rest = Body.GetSkinnedAsset() != nullptr && Body.GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose().IsValidIndex(Bone)
			? Body.GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose()[Bone] : FTransform::Identity;
		UE_LOG(LogOpenSCCT, Log, TEXT("First person probe:   bone %2d '%s' at (%.1f %.1f %.1f) scale (%.2f %.2f %.2f); at rest (%.1f %.1f %.1f) scale %.3f from its parent"), Bone, *Body.GetBoneName(Bone).ToString(),
			At.X, At.Y, At.Z, World.GetScale3D().X, World.GetScale3D().Y, World.GetScale3D().Z, Rest.GetLocation().X, Rest.GetLocation().Y, Rest.GetLocation().Z, Rest.GetScale3D().X);
	}
}

void AScctFirstPersonView::Hide(float DeltaSeconds)
{
	if (bShown)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("First person: view model hidden"));
	}
	bShown = false;
	bHasMuzzle = false;
	bHasLaser = false;
	for (FScctEquipmentView& Drawn : Parts)
	{
		if (Drawn.IsShown() && Drawn.GetBody()->IsVisible())
		{
			Drawn.GetBody()->SetVisibility(false, true);
		}
	}
	TickFlash(DeltaSeconds);
}

bool AScctFirstPersonView::FindMuzzle(FTransform& OutWorld) const
{
	OutWorld = Muzzle;
	return bShown && bHasMuzzle;
}

bool AScctFirstPersonView::FindLaserStart(FTransform& OutWorld) const
{
	OutWorld = bHasLaser ? Laser : Muzzle;
	return bShown && (bHasLaser || bHasMuzzle);
}

void AScctFirstPersonView::PlayMuzzleFlash()
{
	if (!bShown || !bHasMuzzle)
	{
		return;
	}

	if (FlashMaterials.Num() == 0)
	{
		UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Weapons/Materials/M_ScctGlow.M_ScctGlow"), nullptr, LOAD_NoWarn);
		if (Glow == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("First person: no /Game/OpenSCCT/Weapons/Materials/M_ScctGlow (Tools/Import/Import-Characters.ps1 builds it); the muzzle flash is only its light"));
		}
		for (int32 Index = 0; Index < FlashShapes.Num(); ++Index)
		{
			UMaterialInstanceDynamic* Material = Glow != nullptr ? UMaterialInstanceDynamic::Create(Glow, this) : nullptr;
			if (Material != nullptr)
			{
				Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.62f, 0.26f));
				Material->SetScalarParameterValue(TEXT("Intensity"), FlashShapeList[Index].Intensity);
				Material->SetScalarParameterValue(TEXT("Sharpness"), FlashShapeList[Index].Sharpness);
				FlashShapes[Index]->SetMaterial(0, Material);
			}
			FlashMaterials.Add(Material);
		}
	}

	// No two flashes alike: a new size and a new turn about the barrel each shot.
	const float Size = FMath::FRandRange(0.8f, 1.2f);
	const FRotator Turn(0.0, 0.0, FMath::FRandRange(0.0f, 180.0f));
	for (int32 Index = 0; Index < FlashShapes.Num(); ++Index)
	{
		const FFlashShape& Shape = FlashShapeList[Index];
		// The engine's sphere is 100 across.
		FlashShapes[Index]->SetRelativeTransform(FTransform(Turn, Shape.Offset * Size, Shape.Size * Size / 100.0));
		FlashShapes[Index]->SetVisibility(FlashMaterials[Index] != nullptr);
	}
	FlashLight->SetVisibility(true);
	FlashAge = 0.0f;
	FlashExposureScale = FScctExposure::FlashScale();
	++FlashesPlayed;
	UE_LOG(LogOpenSCCT, Log, TEXT("First person: muzzle flash %d at (%.0f %.0f %.0f), %.1f cm from the eye, its light %.0f cd (exposure scale %.2f)"), FlashesPlayed,
		Muzzle.GetLocation().X, Muzzle.GetLocation().Y, Muzzle.GetLocation().Z, FVector::Dist(Muzzle.GetLocation(), LastCamera.GetLocation()),
		CVarFlashCandelas.GetValueOnGameThread() * FlashExposureScale, FlashExposureScale);
	TickFlash(0.0f);
}

void AScctFirstPersonView::TickFlash(float DeltaSeconds)
{
	if (FlashAge < 0.0f)
	{
		return;
	}

	FlashAge += DeltaSeconds;
	// -ScctFlashHold=S keeps every flash at full strength for S seconds first, so a screenshot can catch one.
	static const float Hold = []
	{
		float Seconds = 0.0f;
		FParse::Value(FCommandLine::Get(), TEXT("ScctFlashHold="), Seconds);
		return FMath::Max(Seconds, 0.0f);
	}();
	const float Age = FMath::Max(FlashAge - Hold, 0.0f);
	const bool bGunShown = bShown && bHasMuzzle;
	const float Fade = bGunShown ? 1.0f - FMath::Square(FMath::Clamp(Age / FlashSeconds, 0.0f, 1.0f)) : 0.0f;
	if (bGunShown)
	{
		FlashRoot->SetWorldLocationAndRotation(Muzzle.GetLocation(), Muzzle.GetRotation());
		// The light sits short of the muzzle, so it is never on the far side of a wall the barrel would reach
		// through, and ahead of the view model as it is drawn (pulled in towards the eye), so it rakes the gun
		// from the front without burning it white.
		FlashLight->SetWorldLocation(FMath::Lerp(LastCamera.GetLocation(), Muzzle.GetLocation(), 0.6));
	}
	for (int32 Index = 0; Index < FlashShapes.Num(); ++Index)
	{
		if (FlashMaterials.IsValidIndex(Index) && FlashMaterials[Index] != nullptr)
		{
			FlashMaterials[Index]->SetScalarParameterValue(TEXT("Fade"), Fade);
		}
		if (Fade <= 0.0f && FlashShapes[Index]->IsVisible())
		{
			FlashShapes[Index]->SetVisibility(false);
		}
	}

	const float LightFade = bGunShown ? 1.0f - FMath::Clamp(Age / FlashLightSeconds, 0.0f, 1.0f) : 0.0f;
	FlashLight->SetIntensity(CVarFlashCandelas.GetValueOnGameThread() * FlashExposureScale * LightFade);
	if (LightFade <= 0.0f)
	{
		FlashLight->SetVisibility(false);
		FlashAge = -1.0f;
	}
}
