#pragma once

#include "Components/SpotLightComponent.h"
#include "ScctWaterRefractionComponent.generated.h"

/** A refracted light only follows a live fixture when that fixture can be switched by the match. */
UCLASS()
class OPENSCCT_API UScctWaterRefractionComponent : public USpotLightComponent
{
	GENERATED_BODY()
public:
	UScctWaterRefractionComponent();
	void Follow(ULightComponent* Fixture);
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;
private:
	UPROPERTY()
	TObjectPtr<ULightComponent> SourceFixture;
};
