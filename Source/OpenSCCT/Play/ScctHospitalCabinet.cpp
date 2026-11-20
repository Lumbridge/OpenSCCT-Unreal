#include "Play/ScctHospitalCabinet.h"

#include "OpenSCCT.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"

FString ScctHospitalCabinet::AtlasFor(const FString& RenderFolder)
{
	const FString Map = FPaths::GetPath(RenderFolder);
	if (!FPaths::GetCleanFilename(Map).Equals(TEXT("MounE"), ESearchCase::IgnoreCase)) return {};

	const TCHAR* AtlasName = TEXT("Prison_TEX_machine_HD.png");
	const FString Override = RenderFolder / TEXT("overrides") / AtlasName;
	if (FPaths::FileExists(Override)) return Override;
#if WITH_EDITOR
	const FString ArtistCopy = FPaths::ProjectDir() / TEXT("Art/Maps/MounE") / AtlasName;
	if (FPaths::FileExists(ArtistCopy)) return ArtistCopy;
#endif
	const FString Tools = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../contenttools"));
	const FString RecipeDir = Tools / TEXT("community/MounE");
	const FString Recipe = RecipeDir / TEXT("build_vending_override.py");
	const FString Python = Tools / TEXT("python/python.exe");
	if (!FPaths::FileExists(Python) || !FPaths::FileExists(Recipe)) return {};

	const FString Sources[] = {
		RenderFolder / TEXT("textures/Prison_TEX_machineEMF.png"),
		RenderFolder / TEXT("textures/Prison_TEX_machine.png"), Recipe,
		RecipeDir / TEXT("Cabinet-Art.png"), RecipeDir / TEXT("Shelf-Art.png"), RecipeDir / TEXT("Panel-Art.png")
	};
	FMD5 Identity;
	for (const FString& File : Sources)
	{
		const FMD5Hash Hash = FMD5Hash::HashFile(*File);
		if (!Hash.IsValid()) return {};
		Identity.Update(Hash.GetBytes(), 16);
	}
	FMD5Hash Key;
	Key.Set(Identity);
	FString UserDir;
	const FString CacheRoot = !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDir)
		? FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("OpenSCCT/unreal/cache"))
		: FPaths::ProjectSavedDir() / TEXT("Cache");
	const FString Folder = FPaths::ConvertRelativePathToFull(CacheRoot / TEXT("MounE") / LexToString(Key));
	const FString Atlas = Folder / AtlasName;
	if (IFileManager::Get().FileSize(*Atlas) > 0) return Atlas;
	if (!IFileManager::Get().MakeDirectory(*Folder, true)) return {};

	// Only the installed recipe executes. The generator publishes with one atomic rename after successful validation.
	const FString Args = FString::Printf(TEXT("-I \"%s\" \"%s\" --plates \"%s\" --output \"%s\""),
		*Recipe, *FPaths::ConvertRelativePathToFull(Map), *RecipeDir, *Atlas);
	int32 ExitCode = -1;
	FString Error;
	if (FPlatformProcess::ExecProcess(*Python, *Args, &ExitCode, nullptr, &Error) && ExitCode == 0 && FPaths::FileExists(Atlas))
		return Atlas;
	UE_LOG(LogOpenSCCT, Warning, TEXT("Mount Hospital cabinet restoration failed (%d). Using its original textures. %s"), ExitCode, *Error.Left(400));
	return {};
}
