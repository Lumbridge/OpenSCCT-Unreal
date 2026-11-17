#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UScctOnlineSubsystem;
class UTexture2D;
struct FSlateBrush;

/**
 * While a network round waits for its players to load (the Godot client's RoundStartList, the original's
 * Loading_Menu list): the round map's loading picture (the one the server's seed picks, so every player sees the
 * same), "Waiting for players (n / m)" and a line per player: "is ready" once that player's game reports waiting to
 * play, "is connecting" while it loads, "Loading..." before that player is on the map here, and "Player timed out..." for one the server
 * dropped for not loading in time. The lines come from the bridge's round table (Runtime/ScctRoundApi.h); it only
 * shows, it never takes the mouse.
 */
class SScctRoundWaitList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctRoundWaitList) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The list is on screen. */
	bool IsShowing() const { return bShowing; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void ShowPicture(const FString& Path);

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TSharedPtr<SVerticalBox> Lines;
	FText Title;
	bool bShowing = false;
	FString ShownKey;

	FString PicturePath;
	TStrongObjectPtr<UTexture2D> PictureTexture;
	TSharedPtr<FSlateBrush> PictureBrush;
};
