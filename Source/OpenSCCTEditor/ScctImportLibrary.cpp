#include "ScctImportLibrary.h"

#include "Engine/World.h"

AActor* UScctImportLibrary::SpawnActorInWorld(UWorld* World, TSubclassOf<AActor> ActorClass, const FTransform& Transform)
{
	if (World == nullptr || ActorClass == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.OverrideLevel = World->PersistentLevel;
	return World->SpawnActor<AActor>(ActorClass, Transform, Parameters);
}
