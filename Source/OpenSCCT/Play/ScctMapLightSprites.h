#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctMapLightSprites.generated.h"

class ULightComponent;
class UPrimitiveComponent;

/** Visible Light actors use the texture's own blend mode and depth test, even for a lamp that emits no light. */
UCLASS()
class UScctMapLightSprites : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	void Load();
	void Clear();
	void Frame(float DeltaSeconds);
	bool bPendingLoad = false;
	TWeakObjectPtr<AActor> SpriteHost;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> SkyGroups;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ReplacedCoronas;
	TMap<TWeakObjectPtr<ULightComponent>, float> OriginalScatter;
};
