#include "Play/ScctFirstPersonView.h"

#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctEquipmentData.h"
#include "Play/ScctEquipmentModels.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 MaxViewBones = 160;

	// The flash is over in a few frames: long enough to be seen at any frame rate, short enough to read as a
	// crack of light and not a glow. Its light outlasts it by a little, as the eye's impression does.
	constexpr float FlashSeconds = 0.05f;
	constexpr float FlashLightSeconds = 0.085f;
	constexpr float FlashLightCandelas = 60.0f;

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

void AScctFirstPersonView::Present(const FScctEquipmentApi& Api, FScctEquipmentModels& Models, const UScctWeaponSet* Weapons,
	const FScctFirstPerson& View, const FTransform& Camera, float Cloak, float DeltaSeconds)
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
		Body->SetRelativeTransform(FScctPose::ToUnreal(Part.ModelToCamera));
		if (!Body->IsVisible())
		{
			Body->SetVisibility(true, true);
		}
		const int32 Bones = FMath::Clamp(Api.GetFirstPersonPose(Index, PoseBuffer.GetData(), PoseBuffer.Num()), 0, PoseBuffer.Num());
		Drawn.ApplyPose(TConstArrayView<FScctBridgeBone>(PoseBuffer.GetData(), Bones));
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
	++FlashesPlayed;
	UE_LOG(LogOpenSCCT, Log, TEXT("First person: muzzle flash %d at (%.0f %.0f %.0f), %.1f cm from the eye"), FlashesPlayed,
		Muzzle.GetLocation().X, Muzzle.GetLocation().Y, Muzzle.GetLocation().Z, FVector::Dist(Muzzle.GetLocation(), LastCamera.GetLocation()));
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
	FlashLight->SetIntensity(FlashLightCandelas * LightFade);
	if (LightFade <= 0.0f)
	{
		FlashLight->SetVisibility(false);
		FlashAge = -1.0f;
	}
}
