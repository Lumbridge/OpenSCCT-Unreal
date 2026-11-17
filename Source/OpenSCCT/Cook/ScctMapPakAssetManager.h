#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "ScctMapPakAssetManager.generated.h"

/**
 * The asset manager of the map pak cook (Tools/Cook/Cook-MapPaks.ps1, Config/Custom/MapPaks). It only acts when the
 * cook is given -ScctMapPaks=<Code>+<Code>+...; otherwise it is the engine's asset manager.
 *
 * It decides which IoStore container every cooked package goes into:
 *   chunk 0                 everything outside /Game/Imported (the engine and this project's own content): the game
 *   chunk 100 (shared)      /Game/Imported/Chars, /Game/Imported/Fx, and anything in a map's folder that another
 *                           cooked map also uses
 *   chunk 101 + i           what only map i of the list uses: /Game/Imported/Maps/<Code>/...
 * Every package goes into exactly one chunk, so a map's pack holds nothing the shared pack or the game already has.
 *
 * It also adds to the cook every package of the listed maps' folders and of Chars and Fx, because the game finds them
 * by looking (the level's meshes and materials, the effect meshes, the equipment models), not through references.
 */
UCLASS()
class OPENSCCT_API UScctMapPakAssetManager : public UAssetManager
{
	GENERATED_BODY()

public:
	static constexpr int32 SharedChunk = 100;
	static constexpr int32 FirstMapChunk = 101;

#if WITH_EDITOR
	virtual void ModifyCook(TConstArrayView<const ITargetPlatform*> TargetPlatforms, TArray<FName>& PackagesToCook,
		TArray<FName>& PackagesToNeverCook) override;
	virtual bool GetPackageChunkIds(FName PackageName, const ITargetPlatform* TargetPlatform, TArrayView<const int32> ExistingChunkList,
		TArray<int32>& OutChunkList, TArray<int32>* OutOverrideChunkList = nullptr) const override;

private:
	/** The map codes from -ScctMapPaks, in chunk order. Empty: not a map pak cook. */
	const TArray<FString>& MapCodes() const;
	/** The chunk a package under /Game/Imported goes into. */
	int32 ImportedChunk(const FString& PackageName) const;
	/** Which listed maps (by index) use each /Game/Imported package, through any package dependency. */
	void BuildUsers() const;

	mutable TOptional<TArray<FString>> CachedMapCodes;
	mutable TMap<FName, TArray<int32, TInlineAllocator<2>>> Users;
	mutable bool bUsersBuilt = false;
#endif
};
