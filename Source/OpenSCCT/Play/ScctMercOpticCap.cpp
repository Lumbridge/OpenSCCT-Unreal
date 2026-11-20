#include "Play/ScctMercOpticCap.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

void ScctMercOpticCap::Configure(UStaticMeshComponent& Cap)
{
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Housing(TEXT("/Game/OpenSCCT/Weapons/SM_MercOpticHousing.SM_MercOpticHousing"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cover(TEXT("/Game/OpenSCCT/Weapons/SM_MercOpticCap.SM_MercOpticCap"));
	Cap.SetStaticMesh(Housing.Get() ? Housing.Get() : Cover.Get());
	Cap.SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cap.SetCanEverAffectNavigation(false);
	Cap.SetCastShadow(false);
	Cap.SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	Cap.SetVisibility(false);
}

bool ScctMercOpticCap::HasHousing(const UStaticMeshComponent& Cap)
{
	static const FName HousingName(TEXT("SM_MercOpticHousing"));
	const UStaticMesh* Mesh = Cap.GetStaticMesh();
	return Mesh && Mesh->GetFName() == HousingName && Mesh->GetStaticMaterials().Num() >= 2;
}

void ScctMercOpticCap::Update(UStaticMeshComponent& Cap, const UPoseableMeshComponent* Rifle)
{
	static const FName Sight(TEXT("ADS"));
	const bool bShow = Rifle && Cap.GetStaticMesh() && Rifle->DoesSocketExist(Sight);
	if (bShow)
	{
		FTransform Frame = Rifle->GetSocketTransform(Sight);
		// The imported skeleton carries its file-unit scale. The accessory is authored in Unreal centimetres.
		Frame.SetScale3D(FVector::OneVector);
		Cap.SetWorldTransform(Frame);
	}
	if (Cap.IsVisible() != bShow)
	{
		Cap.SetVisibility(bShow);
	}
}
