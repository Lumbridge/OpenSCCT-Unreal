#include "Cook/ScctMapPakAssetManager.h"

#include "OpenSCCT.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScctMapPakAssetManager)

#if WITH_EDITOR

namespace
{
	const TCHAR* const ImportedRoot = TEXT("/Game/Imported/");
	const TCHAR* const ImportedMapsRoot = TEXT("/Game/Imported/Maps/");
	// Found by looking at run time, so cooked whole whatever refers to them (see the class comment).
	const TCHAR* const SharedFolders[] = { TEXT("/Game/Imported/Chars"), TEXT("/Game/Imported/Fx") };

	/** "AquaE" for "/Game/Imported/Maps/AquaE/world/...", empty for anything else. */
	FString MapFolderOf(const FString& PackageName)
	{
		if (!PackageName.StartsWith(ImportedMapsRoot, ESearchCase::IgnoreCase))
		{
			return FString();
		}
		const FString Rest = PackageName.Mid(FCString::Strlen(ImportedMapsRoot));
		int32 Slash = INDEX_NONE;
		return Rest.FindChar(TEXT('/'), Slash) ? Rest.Left(Slash) : FString();
	}

	void AddPackagesUnder(IAssetRegistry& Registry, const FString& Folder, TArray<FName>& PackagesToCook)
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = true;
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		TSet<FName> Packages;
		for (const FAssetData& Asset : Assets)
		{
			Packages.Add(Asset.PackageName);
		}
		for (const FName Package : Packages)
		{
			PackagesToCook.AddUnique(Package);
		}
		UE_LOG(LogOpenSCCT, Display, TEXT("Map paks: %d packages under %s added to the cook"), Packages.Num(), *Folder);
	}
}

const TArray<FString>& UScctMapPakAssetManager::MapCodes() const
{
	if (!CachedMapCodes.IsSet())
	{
		TArray<FString> Codes;
		FString List;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctMapPaks="), List))
		{
			List.ParseIntoArray(Codes, TEXT("+"), true);
		}
		CachedMapCodes = MoveTemp(Codes);
	}
	return CachedMapCodes.GetValue();
}

void UScctMapPakAssetManager::ModifyCook(TConstArrayView<const ITargetPlatform*> TargetPlatforms, TArray<FName>& PackagesToCook,
	TArray<FName>& PackagesToNeverCook)
{
	Super::ModifyCook(TargetPlatforms, PackagesToCook, PackagesToNeverCook);
	if (MapCodes().IsEmpty())
	{
		return;
	}

	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.WaitForCompletion();
	for (const TCHAR* Folder : SharedFolders)
	{
		AddPackagesUnder(Registry, Folder, PackagesToCook);
	}
	for (int32 Index = 0; Index < MapCodes().Num(); ++Index)
	{
		const FString& Code = MapCodes()[Index];
		const FString Level = FString::Printf(TEXT("%s%s/%s"), ImportedMapsRoot, *Code, *Code);
		if (!FPackageName::DoesPackageExist(Level))
		{
			UE_LOG(LogOpenSCCT, Error, TEXT("Map paks: %s has no level %s (import it first: Tools/Import/Import-Map.ps1)"), *Code, *Level);
			continue;
		}
		AddPackagesUnder(Registry, FString(ImportedMapsRoot) + Code, PackagesToCook);
		UE_LOG(LogOpenSCCT, Display, TEXT("Map paks: %s is chunk %d"), *Code, FirstMapChunk + Index);
	}
}

void UScctMapPakAssetManager::BuildUsers() const
{
	if (bUsersBuilt)
	{
		return;
	}
	bUsersBuilt = true;

	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	const UE::AssetRegistry::FDependencyQuery Query;	// hard and soft, game and editor-only: whatever a map can load
	for (int32 Index = 0; Index < MapCodes().Num(); ++Index)
	{
		const FString& Code = MapCodes()[Index];
		// The map's own folder is what the game finds by looking: each of its packages counts as used by the map,
		// and what they refer to is followed from there.
		TArray<FName> Pending;
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*(FString(ImportedMapsRoot) + Code)));
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = true;
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		for (const FAssetData& Asset : Assets)
		{
			Pending.AddUnique(Asset.PackageName);
		}

		TSet<FName> Seen(Pending);
		while (!Pending.IsEmpty())
		{
			const FName Package = Pending.Pop(EAllowShrinking::No);
			Users.FindOrAdd(Package).AddUnique(Index);
			TArray<FName> Dependencies;
			Registry.GetDependencies(Package, Dependencies, UE::AssetRegistry::EDependencyCategory::Package, Query);
			for (const FName Dependency : Dependencies)
			{
				if (!Seen.Contains(Dependency) && Dependency.ToString().StartsWith(ImportedRoot, ESearchCase::IgnoreCase))
				{
					Seen.Add(Dependency);
					Pending.Add(Dependency);
				}
			}
		}
	}

	// What a map uses from another map's folder is shared: say so, as it is usually an importer slip.
	for (const TPair<FName, TArray<int32, TInlineAllocator<2>>>& Pair : Users)
	{
		const FString Name = Pair.Key.ToString();
		const FString Folder = MapFolderOf(Name);
		if (!Folder.IsEmpty() && (Pair.Value.Num() > 1 || !MapCodes()[Pair.Value[0]].Equals(Folder, ESearchCase::IgnoreCase)))
		{
			TArray<FString> Names;
			for (const int32 Index : Pair.Value)
			{
				Names.Add(MapCodes()[Index]);
			}
			UE_LOG(LogOpenSCCT, Display, TEXT("Map paks: %s is used by %s: it goes into the shared pack"), *Name, *FString::Join(Names, TEXT(", ")));
		}
	}
}

int32 UScctMapPakAssetManager::ImportedChunk(const FString& PackageName) const
{
	const FString Folder = MapFolderOf(PackageName);
	if (Folder.IsEmpty())
	{
		return SharedChunk;
	}
	BuildUsers();
	const int32 Owner = MapCodes().IndexOfByPredicate([&Folder](const FString& Code) { return Code.Equals(Folder, ESearchCase::IgnoreCase); });
	const TArray<int32, TInlineAllocator<2>>* MapUsers = Users.Find(FName(*PackageName));
	if (Owner == INDEX_NONE || (MapUsers != nullptr && (MapUsers->Num() > 1 || (*MapUsers)[0] != Owner)))
	{
		return SharedChunk;
	}
	return FirstMapChunk + Owner;
}

bool UScctMapPakAssetManager::GetPackageChunkIds(FName PackageName, const ITargetPlatform* TargetPlatform, TArrayView<const int32> ExistingChunkList,
	TArray<int32>& OutChunkList, TArray<int32>* OutOverrideChunkList) const
{
	if (MapCodes().IsEmpty())
	{
		return Super::GetPackageChunkIds(PackageName, TargetPlatform, ExistingChunkList, OutChunkList, OutOverrideChunkList);
	}

	const FString Name = PackageName.ToString();
	const int32 Chunk = Name.StartsWith(ImportedRoot, ESearchCase::IgnoreCase) ? ImportedChunk(Name) : 0;
	OutChunkList.Reset();
	OutChunkList.Add(Chunk);
	if (OutOverrideChunkList != nullptr)
	{
		OutOverrideChunkList->Reset();
		OutOverrideChunkList->Add(Chunk);
	}
	return true;
}

#endif
