#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Runtime/ScctGearMenuApi.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SOverlay;
struct FSlateBrush;

/**
 * The Equipment page (gaps 43 and 130): the player's gear per side, as the Godot client's GearMenu lays it out.
 *
 *   Tabs        SPY and MERCENARY; each keeps its own draft until APPLY or CANCEL
 *   Your kit    the primary weapon (the merc picks his; the spy's Shocker is fixed), the merc's sidearm, and the
 *               four gadget slots (slots the host does not give are greyed). Clicking one shows its catalogue.
 *   Catalogue   the cards the slot can take: the gadget, weapon or sidearm's picture, name and small line; a card
 *               the player's level or the host's rules lock is greyed and says why. Picking a gadget another slot
 *               holds swaps the two.
 *   Intel       the card under the mouse (else the slot's): picture, name, a rifle's role, the description, why it
 *               is locked, and its stats (damage at range, magazine, rate of fire, reload, sights; a gadget's uses)
 *   Buttons     CANCEL (asks when something would be lost), RESET LOADOUT (the side's fixed kit, in the draft),
 *               SWITCH TO MERC / SPY, APPLY LOADOUT
 *
 * The rules and texts are the bridge's (Runtime/ScctGearMenuApi.h, the Godot GearMenuModel ported). APPLY saves both
 * drafts as the kit (Saved/Controls/loadout.json) and, in a round, asks the server to put the played side's kit on
 * (ServerChangeGear): its answer is the HUD's message line ("Gear changed.", or at the next spawn, or the wait).
 *
 * It opens over a host overlay, dimming what is behind: from the offline page
 * and the lobby (EQUIPMENT), from the in-match menu (CHANGE GEAR) and on the ChangeGear binding
 * (Menu/ScctMatchCommands.h). Escape is BACK.
 *
 * Unattended: -ScctGear[=spy|merc] opens it (front end, or a match after the menu subsystem's delay);
 * -ScctGearApply=<side>,<gadget>,...[,weapon=<path>][,sidearm=<id>] picks those as clicks do and applies, logging
 * each step and the saved file ("ScctGear: ...").
 */
class SScctGearPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctGearPage)
		: _Side(-1)
		, _Context(EScctGearMenuContext::Menus)
		, _bTakeKeyboard(true)
	{}
		/** 0 spy, 1 merc, -1 the side played in the round (else the spy). */
		SLATE_ARGUMENT(int32, Side)
		/** EScctGearMenuContext. */
		SLATE_ARGUMENT(int32, Context)
		/** In a match the viewport keeps the keyboard (the menu's close keys are read from it): Escape comes through RequestBack. */
		SLATE_ARGUMENT(bool, bTakeKeyboard)
		/** Raised after APPLY saved the kit (the page has closed). */
		SLATE_EVENT(FSimpleDelegate, OnApplied)
		/** Raised whenever the page closes. */
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	/** Opens the page over Host (the one already open there is returned). Null when the bridge has no gear table. */
	static TSharedPtr<SScctGearPage> Open(const TSharedRef<SOverlay>& Host, int32 Side, int32 Context, bool bTakeKeyboard,
		FSimpleDelegate OnApplied = FSimpleDelegate(), FSimpleDelegate OnClosed = FSimpleDelegate());

	/** The page open over Host, if any. */
	static TSharedPtr<SScctGearPage> FindOpen(const TSharedRef<SOverlay>& Host);

	/** A side's saved kit as one line ("Sticky Cam, Smoke G., ..."), for the lobby's and the offline page's summary. */
	static FString SummaryLine(int32 Side);

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	/**
	 * The D-pad and the stick stay on this page (the menu's entries under it are not where they go), and Right from the
	 * loadout goes to the catalogue, Left from the catalogue's first column back to the slot it is for.
	 */
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;

	/** BACK, CANCEL or Escape: closes, after asking when a draft differs from the saved kit. */
	void RequestBack();

	/** The layer the page asks its questions on (the host's, Menu/ScctMenuKit.h); without one CANCEL with changes keeps the page. */
	void SetDialogs(const TSharedPtr<class SScctDialogLayer>& InDialogs) { Dialogs = InDialogs; }

	/** Closes without asking (the drafts are dropped). */
	void Close();

	/**
	 * Unattended: "<spy|merc>,<gadget path>,...[,weapon=<path>][,sidearm=<id>]" picks each through the page's own
	 * handlers (the side's tab, each slot in turn, the weapon and sidearm slots) and presses APPLY. False (logged) when
	 * a pick was refused.
	 */
	bool RunScript(const FString& Script);

	virtual FName GetType() const { return FName(TEXT("SScctGearPage")); }

private:
	void ShowSide(int32 Side);
	void SelectSlot(int32 Category, int32 Slot);
	bool Pick(const FString& Path);
	void ResetSide();
	void ApplyKit();
	void Refresh();
	void SetStatus(const FString& Text, bool bError = false);

	TSharedRef<SWidget> BuildKit();
	TSharedRef<SWidget> BuildCatalogue();
	TSharedRef<SWidget> BuildIntel(const FString& Path);
	TSharedRef<SWidget> KitSlot(const FString& Caption, const FString& Name, int32 Category, int32 Slot, bool bEnabled, const FString& Tooltip);
	TSharedRef<SWidget> Card(const FScctGearOption& Option);
	void ShowIntel(const FString& Path);
	FString SelectedPath() const;

	/** The picture of a piece of gear (its file and tile, from its intel), made once and kept. Null without one. */
	static const FSlateBrush* IconOf(const FScctGearIntel& Intel);

	TWeakPtr<SOverlay> Host;
	TWeakPtr<SWidget> FocusBefore;
	FSimpleDelegate OnApplied;
	FSimpleDelegate OnClosed;
	bool bTakeKeyboard = true;
	TWeakPtr<class SScctDialogLayer> Dialogs;
	bool bFocusTaken = false;
	bool bClosed = false;
	int32 Context = EScctGearMenuContext::Menus;

	/** The side played in the round when the page opened (-1 none): its kit is what APPLY asks the server for. */
	int32 PlayedSide = -1;

	FScctGearMenuState State = {};
	int32 Category = EScctGearCategory::Gadget;
	int32 Slot = 0;
	FString Hovered;
	/** The tile the controller is on ("slot:<category>:<slot>" or "item:<path>"), focused again after a rebuild. */
	FString FocusedTile;
	TMap<FString, TWeakPtr<SWidget>> TileWidgets;
	FString StatusText;
	bool bStatusError = false;

	TSharedPtr<SBox> KitBox;
	TSharedPtr<SBox> CatalogueBox;
	TSharedPtr<SBox> IntelBox;
};
