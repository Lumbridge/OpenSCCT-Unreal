#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMapBrowser.h"
#include "Online/ScctHostSetup.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SEditableTextBox;
class SScrollBox;
class SWrapBox;
class UTexture2D;
struct FSlateBrush;

DECLARE_DELEGATE_OneParam(FOnScctMapPicked, const FString& /*Map*/);

/**
 * A map list for Create Session and the lobby (the Godot client's MapPickerView and MapBrowserGrid): every hostable
 * map with an imported level. Above the list: a search box (a map's name, its code, its authors or tags), the map
 * group (All installed maps, Enhanced, Stock, Community, 3v3 optimised: the catalog's own groups, the choice saved
 * for every map list), how the list is drawn (picture grid, list or thumbnail list, saved too), a mode filter (a map
 * shows under a mode when it offers it; not in the Godot client) and RANDOM MAP, which draws one of the maps the
 * filters show with a reel and a choice to use the result or draw again. Under them the number of maps shown
 * and RESET FILTERS. A click picks a map; the selected map is lit and scrolled into view. A picture is read from
 * the content folder when the list first draws it.
 */
class SScctMapPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctMapPicker)
		: _TileWidth(132.0f)
	{}
		SLATE_ARGUMENT(TArray<FScctHostMap>, Maps)
		SLATE_ARGUMENT(TWeakPtr<class SScctDialogLayer>, Dialogs)
		/** The map that is lit. */
		SLATE_ATTRIBUTE(FString, Selected)
		SLATE_ARGUMENT(float, TileWidth)
		SLATE_EVENT(FOnScctMapPicked, OnPicked)
		SLATE_EVENT(FSimpleDelegate, OnConfirmed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Shows only the maps that offer a mode key (empty: every map). */
	void SetModeFilter(const FString& ModeKey);

	/** Draws a random map among those shown (never the selected one when there is another) and offers the result. Presets can accept the draw directly. */
	void PickRandom(bool bConfirm = true);

	static void ShowDraw(TWeakPtr<SScctDialogLayer> Layer, const TArray<FScctHostMap>& Candidates, FOnScctMapPicked Use);

	/** Shows a group by its name or the start of it ("Stock"); unknown names show every map. Saved as the player's choice unless bSave is false (unattended runs). */
	void SetGroup(const FString& Name, bool bSave = true);

	/** Puts a text in the search box. */
	void SetSearch(const FString& Text);

	/** Draws the list another way. Saved unless bSave is false. */
	void SetView(EScctMapView NewView, bool bSave = true);

	/** How many maps the filters show. */
	int32 GetShownCount() const { return ShownCount; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** The maps that loaded a picture (for logs). */
	int32 GetPictureCount() const { return Pictures.Num(); }

private:
	void Refill();
	bool Shows(const FScctHostMap& Map) const;
	bool IsFiltered() const;
	void ResetFilters();
	void Reveal(const FString& MapId);
	TSharedRef<SWidget> MakeTile(const FScctHostMap& Map);
	const FSlateBrush* PictureOf(const FScctHostMap& Map);

	TArray<FScctHostMap> Maps;
	TAttribute<FString> Selected;
	FOnScctMapPicked OnPicked;
	FSimpleDelegate OnConfirmed;
	TWeakPtr<SScctDialogLayer> Dialogs;
	float TileWidth = 132.0f;
	FString ModeFilter;
	TArray<FString> ModeFilters;
	TArray<FScctMapGroup> Groups;
	int32 GroupIndex = 0;
	TMap<FString, FScctMapDetails> Details;
	FString Search;
	EScctMapView View = EScctMapView::PictureGrid;
	int32 ShownCount = 0;
	/** The selected map that was last scrolled into view. */
	FString Revealed;
	TSharedPtr<SBox> ListHost;
	TSharedPtr<SScrollBox> Scroll;
	TSharedPtr<SEditableTextBox> SearchBox;
	TSharedPtr<SWrapBox> FilterRow;
	TMap<FString, TWeakPtr<SWidget>> TileOf;

	/** A map's picture: its texture (kept from garbage collection while the picker lives) and the brush that draws it. */
	struct FPicture
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
	};
	TMap<FString, FPicture> Pictures;
	TSet<FString> MissingPictures;
};
