#pragma once

#include "CoreMinimal.h"
#include "Online/ScctHostSetup.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SScctMapPicker;
class UScctOnlineSubsystem;
class UTexture2D;
struct FSlateBrush;

/**
 * The lobby's Session Options (the Godot client's LobbyRoomScreen.Settings and SessionRowsView), shown by the menus'
 * lobby page (Menu/SScctMenuLobby.cpp) in place of the lobby while it is open: the map picker and every row of the
 * setup in GAME and RULES tabs with the Alternate rules page, drawn from the server's setup line
 * (EScctOnlineText::SetupOptions) through the bridge's CreateSessionModel. Whoever may change the setup (CanControl)
 * edits a draft: APPLY sends only the values that differ from the server's (SetSetupOptions; the server un-readies the
 * players), DISCARD goes back to the server's. Everyone else sees the same rows read-only.
 *
 * The lobby page's own steppers change the common values at once; this page is for the rest and for changing several
 * values together.
 */
class SScctSessionOptions : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctSessionOptions) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		SLATE_ARGUMENT(TWeakPtr<class SScctDialogLayer>, Dialogs)
		/** APPLY sent, or DISCARD / CLOSE pressed: the owner shows the lobby again. */
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SScctSessionOptions() override;

	/** The page is about to be shown: the draft follows the server's setup, the rows are made again. */
	void Open();

	/** Unattended: puts an options line into the draft (see -ScctLobbyApply in Menu/SScctMenuLobby.cpp). */
	void FillDraft(const FString& Line);

	/** APPLY: sends the draft's changed values. False when nothing was sent. */
	bool Apply();

	/** DISCARD: the draft goes back to the server's setup. */
	void Discard();

	/** The draft has values the server does not have yet. */
	bool IsDirty() const { return !Draft.IsEmpty() && Draft != Baseline; }

	/** The server's setup is known (the lobby's first setup line came in). */
	bool HasServerSetup() const { return !ServerSetup.IsEmpty(); }

	/** RANDOM MAP: a map that offers the mode being set up, with its own rules (SetMatch). */
	void RandomMap();
	bool HasMaps() const { return !Maps.IsEmpty(); }

	/** The hostable maps with a level here (pictures, names, modes), and one of them by its code. */
	const FScctHostMap* FindMap(const FString& Id) const;

private:
	enum class ETab : uint8 { Game, Rules };

	void HandleChanged();
	bool CanControl() const;

	/** The server's setup changed (or was first read): the baseline follows it, and so does an untouched draft. True when it changed. */
	bool SyncSetup();
	void Edit(const FString& Change);
	void EditAlternate(const FString& Change);
	void SetDraft(const FString& Line);
	int32 PlayersOf(const FString& Line) const;

	/** The names whose values differ between the draft and the server's setup, as an options line. */
	FString ChangedOptions(TArray<FString>* OutNames = nullptr) const;

	void Rebuild();
	void RefreshOptions();
	TSharedRef<SWidget> BuildOptions();
	TSharedRef<SWidget> BuildRows();
	TSharedRef<SWidget> MapPicture(float Width);
	void LoadPicture(const FString& MapId);

	/** The map the server's setup plays, and the one the page shows (the draft's for a controller). */
	FString ServerMap() const;
	FString ShownMap() const;

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	FSimpleDelegate OnClose;
	TWeakPtr<SScctDialogLayer> Dialogs;
	FDelegateHandle ChangedHandle;
	TSharedPtr<SBox> Body;
	int32 SeenRevision = -1;
	bool bAlternateOpen = false;
	bool bControlShown = false;
	ETab Tab = ETab::Game;

	// The server's setup normalized by the model, and the controller's draft of the next one.
	FString ServerSetup;
	FString Baseline;
	FString Draft;
	/** The server's setup changed under a draft with changes of its own. */
	bool bDraftConflict = false;
	/** APPLY was sent: the next setup from the server replaces the draft (it may clamp a value). */
	bool bAwaitingEcho = false;
	TArray<FScctSetupRow> Rows;
	FString VoiceHint, BotHint, DraftDescription;
	TSharedPtr<SBox> RowsBox;
	TSharedPtr<SScctMapPicker> Picker;

	TArray<FScctHostMap> Maps;

	/** The shown map's picture, kept from garbage collection while the page lives. */
	FString PictureMap;
	TStrongObjectPtr<UTexture2D> PictureTexture;
	TSharedPtr<FSlateBrush> PictureBrush;
};
