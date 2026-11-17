#include "Online/ScctMapFolders.h"

#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	bool bApplied = false;

	FString ReadText(const FScctMapsApi& Maps, EScctMapsText Kind)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(Maps.GetText(static_cast<int32>(Kind), nullptr, 0) + 1);
		Maps.GetText(static_cast<int32>(Kind), Buffer.GetData(), Buffer.Num());
		return FString(Buffer.GetData());
	}

	const FScctMapsApi* MapsTable()
	{
		ScctMapFolders::Apply();
		return ScctFeature<FScctMapsApi>();
	}
}

void ScctMapFolders::Apply()
{
	if (bApplied)
	{
		return;
	}
	const FScctMapsApi* Maps = ScctFeature<FScctMapsApi>();
	if (Maps == nullptr)
	{
		// The bridge is not up yet (no content folder): asked again next time.
		return;
	}
	bApplied = true;

	// -ScctMaps= may be given more than once, and each may list several folders.
	TArray<FString> First;
	const TCHAR* Stream = FCommandLine::Get();
	FString Value;
	while (Stream != nullptr && *Stream != TEXT('\0') && FParse::Value(Stream, TEXT("ScctMaps="), Value, false, &Stream))
	{
		Value.TrimQuotesInline();
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(";"), true);
		for (FString& Part : Parts)
		{
			First.Add(FPaths::ConvertRelativePathToFull(Part.TrimStartAndEnd()));
		}
	}

	// The maps a release ships next to the game, as the Godot client's "maps next to the executable".
	TArray<FString> Last;
	Last.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("maps"))));
	Last.AddUnique(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("maps"))));

	const int32 Existing = Maps->SetFolders(*FString::Join(First, TEXT(";")), *FString::Join(Last, TEXT(";")));
	UE_LOG(LogOpenSCCT, Log, TEXT("Map folders: %d named on the command line (%s), %d of the named and executable folders exist; searched: %s"),
		First.Num(), First.Num() > 0 ? *FString::Join(First, TEXT("; ")) : TEXT("none"), Existing, *FString::Join(Roots(), TEXT("; ")));
}

FString ScctMapFolders::FolderOf(const FString& Map)
{
	const FScctMapsApi* Maps = MapsTable();
	if (Maps == nullptr || Map.IsEmpty())
	{
		return FString();
	}
	TArray<TCHAR> Buffer;
	Buffer.SetNumZeroed(Maps->GetMapFolder(*Map, nullptr, 0) + 1);
	Maps->GetMapFolder(*Map, Buffer.GetData(), Buffer.Num());
	return FString(Buffer.GetData());
}

bool ScctMapFolders::CanBuild(const FString& Map)
{
	const FString Folder = FolderOf(Map);
	return !Folder.IsEmpty() && FPaths::FileExists(Folder / TEXT("render/world.glb"));
}

TArray<FString> ScctMapFolders::RenderableMaps()
{
	TArray<FString> Lines;
	if (const FScctMapsApi* Maps = MapsTable())
	{
		ReadText(*Maps, EScctMapsText::RenderableMaps).ParseIntoArrayLines(Lines);
	}
	return Lines;
}

TArray<FString> ScctMapFolders::Roots()
{
	TArray<FString> Lines;
	if (const FScctMapsApi* Maps = ScctFeature<FScctMapsApi>())
	{
		ReadText(*Maps, EScctMapsText::Roots).ParseIntoArrayLines(Lines);
	}
	return Lines;
}
