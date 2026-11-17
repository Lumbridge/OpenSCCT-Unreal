#pragma once

#include "CoreMinimal.h"
#include "Online/ScctHostSetup.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;
class SWrapBox;
class UTexture2D;
struct FSlateBrush;

DECLARE_DELEGATE_OneParam(FOnScctMapPicked, const FString& /*Map*/);

/**
 * A map list with pictures, for Create Session and the lobby (the Godot client's MapPickerView and MapBrowserGrid):
 * every hostable map with an imported level as a tile of its menu picture and its name, a mode filter above (All, then
 * each mode some map offers: a map shows under a mode when it offers it) and RANDOM MAP, which draws one of the maps
 * the filter shows (not the selected one; RandomMapDialog's draw without its reel). A click picks a map; the selected
 * map is lit. The pictures are read from the content folder once per picker.
 */
class SScctMapPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctMapPicker)
		: _TileWidth(132.0f)
	{}
		SLATE_ARGUMENT(TArray<FScctHostMap>, Maps)
		/** The map that is lit. */
		SLATE_ATTRIBUTE(FString, Selected)
		SLATE_ARGUMENT(float, TileWidth)
		SLATE_EVENT(FOnScctMapPicked, OnPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Shows only the maps that offer a mode key (empty: every map). */
	void SetModeFilter(const FString& ModeKey);

	/** Draws a random map among those shown (never the selected one when there is another) and picks it. Returns it, or empty. */
	FString PickRandom();

	/** The maps that loaded a picture (for logs). */
	int32 GetPictureCount() const { return Pictures.Num(); }

private:
	void Refill();
	const FSlateBrush* PictureOf(const FScctHostMap& Map);

	TArray<FScctHostMap> Maps;
	TAttribute<FString> Selected;
	FOnScctMapPicked OnPicked;
	float TileWidth = 132.0f;
	FString ModeFilter;
	TSharedPtr<SWrapBox> Tiles;
	TSharedPtr<SWrapBox> FilterRow;

	/** A map's picture: its texture (kept from garbage collection while the picker lives) and the brush that draws it. */
	struct FPicture
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
	};
	TMap<FString, FPicture> Pictures;
	TSet<FString> MissingPictures;
};
