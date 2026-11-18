#pragma once

#include "CoreMinimal.h"

/** How a map list is drawn (the Godot client's MapBrowserView; the number is what the settings keep). */
enum class EScctMapView : uint8
{
	PictureGrid = 0,
	List = 1,
	ThumbnailList = 2,
};

/** A group a map list can be narrowed to (MapCatalog.GetMapFilters). */
struct FScctMapGroup
{
	/** What the settings keep ("All", "Enhanced maps", "Stock maps", "Community maps", "3v3 optimised maps"). */
	FString Name;
	/** What the list shows ("All installed maps" for All). */
	FString Title;
	FString Description;
	/** The ids of its maps, lower case. Not looked at for the group of every map. */
	TSet<FString> Maps;
	/** The group of every installed map. */
	bool bAll = false;

	bool Holds(const FString& MapId) const { return bAll || Maps.Contains(MapId.ToLower()); }
};

/** What a map list knows about a map besides its name: the search box's words and the tooltip's lines. */
struct FScctMapDetails
{
	FString SearchWords;
	FString Description;
	FString Credit;
};

/**
 * The map lists' shared state (the Godot client's MapPickerView, MapBrowserGrid and their two settings): the groups
 * of the catalog (Runtime/ScctMapBrowserApi.h), the group picked last (active_map_playlist there, ActiveMapPlaylist
 * in this game's settings) and how the lists are drawn (map view, MapView). Both choices are one for every map
 * list and are saved when they change.
 */
namespace ScctMapBrowser
{
	/** The groups, All first. Without the bridge's table: All alone. Asked once per call (the catalog may have grown). */
	TArray<FScctMapGroup> Groups();
	TArray<FString> TrainingMaps(int32 Team);

	/** The details of every host map by lower case id. */
	TMap<FString, FScctMapDetails> Details();

	/** The saved group's name ("All" at first). */
	FString ActiveGroup();
	void SetActiveGroup(const FString& Name);

	/** The saved group itself (All when the name is unknown). */
	FScctMapGroup ActiveGroupEntry();

	EScctMapView View();
	void SetView(EScctMapView View);

	/** "Picture grid", "List view", "Thumbnail list". */
	FString ViewTitle(EScctMapView View);

	/** Whether a search text is found in a map (its code, its name, its search words, its description and credit). An empty text finds every map. */
	bool Matches(const FString& Search, const FString& MapId, const FString& MapName, const FScctMapDetails* Details);
}
