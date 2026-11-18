#include "Menu/ScctMapBrowser.h"

#include "Misc/ConfigCacheIni.h"
#include "OpenSCCT.h"
#include "Runtime/ScctMapBrowserApi.h"

namespace
{
	const TCHAR* const MapBrowserSection = TEXT("OpenSCCT.Settings");
	const TCHAR* const KeyActiveGroup = TEXT("ActiveMapPlaylist");
	const TCHAR* const KeyMapView = TEXT("MapView");
	const TCHAR* const AllGroupName = TEXT("All");

	FString MapBrowserText(EScctMapBrowserText Kind)
	{
		const FScctMapBrowserApi* Api = ScctFeature<FScctMapBrowserApi>();
		if (Api == nullptr || Api->GetText == nullptr)
		{
			return FString();
		}

		const int32 Length = Api->GetText(static_cast<int32>(Kind), nullptr, 0);
		if (Length <= 0)
		{
			return FString();
		}
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(Length + 1);
		Api->GetText(static_cast<int32>(Kind), Buffer.GetData(), Buffer.Num());
		return FString(Buffer.GetData());
	}

	TArray<TArray<FString>> MapBrowserLines(const FString& Text)
	{
		TArray<FString> Lines;
		Text.ParseIntoArray(Lines, TEXT("\n"), true);
		TArray<TArray<FString>> Out;
		for (const FString& Line : Lines)
		{
			Line.ParseIntoArray(Out.AddDefaulted_GetRef(), TEXT("\t"), false);
		}
		return Out;
	}

	FScctMapGroup AllGroup()
	{
		FScctMapGroup All;
		All.Name = AllGroupName;
		All.Title = TEXT("All installed maps");
		All.Description = TEXT("Every installed map.");
		All.bAll = true;
		return All;
	}
}

TArray<FScctMapGroup> ScctMapBrowser::Groups()
{
	TArray<FScctMapGroup> Groups;
	for (const TArray<FString>& Parts : MapBrowserLines(MapBrowserText(EScctMapBrowserText::Groups)))
	{
		if (Parts.Num() < 4 || Parts[0].IsEmpty())
		{
			continue;
		}

		FScctMapGroup& Group = Groups.AddDefaulted_GetRef();
		Group.Name = Parts[0];
		Group.Title = Parts[1].IsEmpty() ? Parts[0] : Parts[1];
		Group.Description = Parts[2];
		Group.bAll = Group.Name.Equals(AllGroupName, ESearchCase::IgnoreCase);
		TArray<FString> Ids;
		Parts[3].ParseIntoArray(Ids, TEXT(","), true);
		for (const FString& Id : Ids)
		{
			Group.Maps.Add(Id.ToLower());
		}
	}
	if (!Groups.ContainsByPredicate([](const FScctMapGroup& Group) { return Group.bAll; }))
	{
		Groups.Insert(AllGroup(), 0);
	}
	return Groups;
}

TMap<FString, FScctMapDetails> ScctMapBrowser::Details()
{
	TMap<FString, FScctMapDetails> Details;
	for (const TArray<FString>& Parts : MapBrowserLines(MapBrowserText(EScctMapBrowserText::Details)))
	{
		if (Parts.Num() < 4 || Parts[0].IsEmpty())
		{
			continue;
		}

		FScctMapDetails& Entry = Details.Add(Parts[0].ToLower());
		Entry.SearchWords = Parts[1];
		Entry.Description = Parts[2];
		Entry.Credit = Parts[3];
	}
	return Details;
}

FString ScctMapBrowser::ActiveGroup()
{
	FString Name;
	GConfig->GetString(MapBrowserSection, KeyActiveGroup, Name, GGameUserSettingsIni);
	return Name.IsEmpty() ? FString(AllGroupName) : Name;
}

void ScctMapBrowser::SetActiveGroup(const FString& Name)
{
	if (!ActiveGroup().Equals(Name, ESearchCase::CaseSensitive))
	{
		GConfig->SetString(MapBrowserSection, KeyActiveGroup, *Name, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: the map lists' group is now \"%s\" (saved)"), *Name);
	}
}

FScctMapGroup ScctMapBrowser::ActiveGroupEntry()
{
	const FString Name = ActiveGroup();
	for (FScctMapGroup& Group : Groups())
	{
		if (Group.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return MoveTemp(Group);
		}
	}
	return AllGroup();
}

EScctMapView ScctMapBrowser::View()
{
	int32 Value = 0;
	GConfig->GetInt(MapBrowserSection, KeyMapView, Value, GGameUserSettingsIni);
	return Value == 1 ? EScctMapView::List : Value == 2 ? EScctMapView::ThumbnailList : EScctMapView::PictureGrid;
}

void ScctMapBrowser::SetView(EScctMapView NewView)
{
	if (View() != NewView)
	{
		GConfig->SetInt(MapBrowserSection, KeyMapView, static_cast<int32>(NewView), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: the map lists are now drawn as \"%s\" (saved)"), *ViewTitle(NewView));
	}
}

FString ScctMapBrowser::ViewTitle(EScctMapView View)
{
	switch (View)
	{
	case EScctMapView::List: return TEXT("List view");
	case EScctMapView::ThumbnailList: return TEXT("Thumbnail list");
	default: return TEXT("Picture grid");
	}
}

bool ScctMapBrowser::Matches(const FString& Search, const FString& MapId, const FString& MapName, const FScctMapDetails* Details)
{
	return Search.IsEmpty() || MapId.Contains(Search, ESearchCase::IgnoreCase) || MapName.Contains(Search, ESearchCase::IgnoreCase)
		|| (Details != nullptr && (Details->SearchWords.Contains(Search, ESearchCase::IgnoreCase) || Details->Description.Contains(Search, ESearchCase::IgnoreCase)
			|| Details->Credit.Contains(Search, ESearchCase::IgnoreCase)));
}
