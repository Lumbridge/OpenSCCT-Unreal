#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ScctImportLibrary.generated.h"

/**
 * What the map import script (Tools/Import/import_map.py) needs and the editor's Python does not offer when it
 * runs without a window.
 */
UCLASS()
class UScctImportLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Spawns an actor into a world that is being built as an asset (not the level open in the editor). */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Import")
	static AActor* SpawnActorInWorld(UWorld* World, TSubclassOf<AActor> ActorClass, const FTransform& Transform);
};
