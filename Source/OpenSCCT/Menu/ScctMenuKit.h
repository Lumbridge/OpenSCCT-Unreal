#pragma once

#include "CoreMinimal.h"
#include "Hud/ScctHudStyle.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SBox;
class SEditableTextBox;
class SScrollBox;
class SVerticalBox;

// The menus' widget set, in one visual language with the HUD (Hud/ScctHudStyle.h; docs/DESIGN.md "Menus").
//
// Every control here can be reached and worked with the mouse, the keyboard and a controller:
//   - arrows, D-pad and left stick move the focus (Slate's own navigation between focusable widgets);
//   - Enter, Space and the controller's A press the focused control;
//   - Left and Right change the value of a stepper, a slider or a switch;
//   - pointing at a control focuses it, so there is one highlight, not a hover and a focus that disagree;
//   - Escape and the controller's B are not handled here: they bubble to the page host, which goes back.
// A control tells the page what it is for (its description) when it takes the focus, and makes the content's own
// interface sounds.
namespace ScctUi
{
	// ---- measures (Slate units; the menus are laid out for 1600 x 900 and scaled up from there) -----------------

	constexpr float Unit = 8.0f;
	constexpr float RowHeight = 44.0f;
	constexpr float Corner = 4.0f;
	constexpr float PanelCorner = 8.0f;

	// ---- type scale -------------------------------------------------------------------------------------------

	FSlateFontInfo Display(float Size = 64.0f); // the wordmark and a results headline
	FSlateFontInfo H1();                        // a page's title, 30
	FSlateFontInfo H2();                        // a panel's title, 20
	FSlateFontInfo Body(bool bBold = false);    // 15
	FSlateFontInfo Small();                     // 13
	FSlateFontInfo Caps(float Size = 12.0f);    // spaced capitals: labels, buttons, table heads

	// ---- colours ----------------------------------------------------------------------------------------------

	/** The menus' own accent (no side picked): the spies' cyan. */
	FLinearColor Accent();
	/** The dark of the panels. */
	FLinearColor Ink(float Alpha = 1.0f);
	FLinearColor White(float Alpha);

	// ---- plumbing every control uses --------------------------------------------------------------------------

	void Play(EScctUiSound Sound);

	/**
	 * How a page, the in-match menu and the dialogs answer the arrows and the pad: the nearest control that way on
	 * screen (Slate's own search), and when there is none there, the next or previous control in the page's order,
	 * so the focus never sticks.
	 */
	FNavigationReply Navigate(const FNavigationEvent& Event);

	/** Navigate's search by itself: the control an arrow or the pad would go to from the focused one (none: null). */
	TSharedPtr<SWidget> NavigationTarget(EUINavigation Type);

	/** The description of the control that has the focus; a page's help pane shows it. */
	void Describe(const FText& Title, const FText& Body);
	const FText& GetDescriptionTitle();
	const FText& GetDescriptionBody();

	/** True after the last input came from a controller: the key hints then show its buttons. */
	bool IsPadActive();
	void NoteInput(const FKey& Key);

	/** Whether Key moves a value down (Left, the D-pad's or the stick's left) or up; 0 for any other key. */
	int32 AdjustDirection(const FKey& Key);

	inline FText ToText(const FString& String) { return FText::FromString(String); }

	// ---- plain pieces -----------------------------------------------------------------------------------------

	/** A glass panel: blurred background, dark tint, hairline edge. */
	TSharedRef<SWidget> Panel(const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(28.0f), float Tint = 0.72f);

	TSharedRef<SWidget> Text(const TAttribute<FText>& InText, const FSlateFontInfo& Font, const TAttribute<FSlateColor>& Colour = FSlateColor(ScctHudStyle::Text()), bool bWrap = false);
	TSharedRef<SWidget> Label(const FString& InText, const FLinearColor& Colour = ScctHudStyle::TextDim(), float Size = 12.0f);
	TSharedRef<SWidget> Rule(float Alpha = 0.1f);

	/** A picture that fills its box and keeps its shape (cropped, not squashed); a dark plate when Brush is null. */
	TSharedRef<SWidget> Picture(const FSlateBrush* Brush, float Width, float Height);

	/** "[ENTER] Select": the key (or, with a controller, PadKey) in a small box and what it does. */
	TSharedRef<SWidget> KeyHint(const FString& Key, const FString& PadKey, const FString& What);

	/** A small heading between groups of rows. */
	TSharedRef<SWidget> Section(const FString& Title);

	/**
	 * Content drawn larger on a small screen. Unreal scales the interface by the picture's height (one unit is one
	 * pixel at 1080p), which makes 12-point type six pixels high at 720p; this gives up to a quarter back, so the
	 * menus are laid out for 1600 x 900 units and have more air at 1080p and above.
	 */
	TSharedRef<SWidget> ScaleForSmallScreens(const TSharedRef<SWidget>& Content);

	/** The keyboard is in a text field: letter keys are text, not shortcuts. */
	bool IsTyping();

	/** A one-line text field in the menus' look. */
	TSharedRef<SEditableTextBox> TextBox(const FString& Hint, const FString& Initial = FString(), bool bPassword = false, FOnTextCommitted OnCommitted = FOnTextCommitted());
}

/** How a button is drawn. */
enum class EScctButtonLook : uint8
{
	Primary,   // the page's main action: filled with the accent
	Secondary, // any other action: glass
	Menu,      // an entry of a vertical menu: large type, an accent bar when lit
	Chip,      // a small choice of a row of choices; lit in its colour when selected
	Tab,       // a tab of a tab bar: underlined when selected
	Danger,    // leaves or deletes
};

/**
 * Something an unattended script can find by its caption and press (Menu/ScctMenuDriver.h). Buttons and option
 * rows register themselves while they exist.
 */
class IScctDriven
{
public:
	virtual ~IScctDriven() = default;
	virtual FString GetDrivenLabel() const = 0;
	virtual void DrivenPress() = 0;
	virtual TSharedRef<SWidget> GetDrivenWidget() = 0;
	virtual void SetDrivenHover(bool bHover) {}

	static IScctDriven* Find(const FString& Label);
	/** The control on screen made of this widget, if there is one. */
	static IScctDriven* FindByWidget(const SWidget* Widget);

protected:
	void Register();
	void Unregister();
};

/** The menus' button. */
class SScctButton : public SButton, public IScctDriven
{
public:
	SLATE_BEGIN_ARGS(SScctButton) : _SubTextVisibility(EVisibility::Visible), _Look(EScctButtonLook::Secondary), _Selected(false), _Colour(ScctUi::Accent()), _MinWidth(0.0f), _FillWidth(false), _Compact(false) {}
		SLATE_ATTRIBUTE(FText, Text)
		/** A second, smaller line under the caption (Menu look). */
		SLATE_ATTRIBUTE(FText, SubText)
		/** Whether that second line shows (a long column hides it when it would not fit otherwise). */
		SLATE_ATTRIBUTE(EVisibility, SubTextVisibility)
		SLATE_ARGUMENT(EScctButtonLook, Look)
		SLATE_ATTRIBUTE(bool, Selected)
		SLATE_ARGUMENT(FLinearColor, Colour)
		SLATE_ARGUMENT(float, MinWidth)
		SLATE_ARGUMENT(bool, FillWidth)
		/** Less space above and below the caption (Menu look), for a long column of entries such as the main menu's. */
		SLATE_ARGUMENT(bool, Compact)
		/** What the page's help pane says while the button has the focus. */
		SLATE_ARGUMENT(FText, Description)
		SLATE_EVENT(FSimpleDelegate, OnPressed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SScctButton() override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	// IScctDriven
	virtual FString GetDrivenLabel() const override { return Caption.Get().ToString(); }
	virtual void DrivenPress() override { HandleClicked(); }
	virtual TSharedRef<SWidget> GetDrivenWidget() override { return AsShared(); }
	virtual void SetDrivenHover(bool bHover) override { bDrivenHover = bHover; }

private:
	FReply HandleClicked();
	bool IsLit() const;

	TAttribute<FText> Caption;
	TAttribute<bool> Selected;
	EScctButtonLook Look = EScctButtonLook::Secondary;
	FLinearColor Colour;
	FText DescriptionText;
	FSimpleDelegate OnPressedDelegate;
	bool bDrivenHover = false;
	mutable float Lit = 0.0f;
	mutable float Press = 0.0f;
};

/**
 * A row of a settings list: its caption on the left and a control on the right. The row is the focusable thing;
 * the subclasses draw the control and say what Left, Right and a press do.
 */
class SScctOptionRow : public SCompoundWidget, public IScctDriven
{
public:
	virtual ~SScctOptionRow() override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// IScctDriven
	virtual FString GetDrivenLabel() const override { return Caption.ToString(); }
	virtual void DrivenPress() override { Press(); }
	virtual TSharedRef<SWidget> GetDrivenWidget() override { return AsShared(); }

	/** The width of the control's part of a row. */
	static constexpr float ControlWidth = 260.0f;

protected:
	/** Builds the row around the subclass's control (which may be SNullWidget when the subclass paints it). */
	void BuildRow(const FText& InCaption, const FText& InDescription, const TSharedRef<SWidget>& Control);

	/** Left (-1) or Right (+1) on the row. True when the row took it. */
	virtual bool Adjust(int32 Direction) { return false; }

	/** Enter, A or a click on the row. */
	virtual void Press() {}

	/** A click at X of the control's part, 0 at its left edge and 1 at its right. */
	virtual void ClickControl(float Alpha) { Press(); }

	/** The control's rectangle within the row. */
	FSlateRect GetControlRect(const FGeometry& Geometry) const;

	bool IsLit() const;

	FText Caption;
	FText DescriptionText;
	mutable float Lit = 0.0f;
};

/** One of a few named values, changed with Left and Right: "<  HIGH  >". */
class SScctStepper : public SScctOptionRow
{
public:
	SLATE_BEGIN_ARGS(SScctStepper) : _Wrap(true) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FText, Description)
		SLATE_ARGUMENT(TArray<FString>, Options)
		/** The options, asked again whenever the row is drawn, when they can change (instead of Options). */
		SLATE_ARGUMENT(TFunction<TArray<FString>()>, GetOptions)
		SLATE_ARGUMENT(TFunction<int32()>, Get)
		SLATE_ARGUMENT(TFunction<void(int32)>, Set)
		SLATE_ARGUMENT(bool, Wrap)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual bool Adjust(int32 Direction) override;
	virtual void Press() override { Adjust(1); }
	virtual void ClickControl(float Alpha) override { Adjust(Alpha < 0.5f ? -1 : 1); }

private:
	TArray<FString> CurrentOptions() const;

	TArray<FString> Options;
	TFunction<TArray<FString>()> GetOptions;
	TFunction<int32()> Get;
	TFunction<void(int32)> Set;
	bool bWrap = true;
};

/** A number on a track, changed with Left and Right or by dragging. */
class SScctSlider : public SScctOptionRow
{
public:
	SLATE_BEGIN_ARGS(SScctSlider) : _Min(0.0f), _Max(100.0f), _Step(5.0f) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FText, Description)
		SLATE_ARGUMENT(float, Min)
		SLATE_ARGUMENT(float, Max)
		SLATE_ARGUMENT(float, Step)
		SLATE_ARGUMENT(TFunction<float()>, Get)
		SLATE_ARGUMENT(TFunction<void(float)>, Set)
		/** The value as the row shows it ("80 %", "Inf."); the plain number when not given. */
		SLATE_ARGUMENT(TFunction<FString(float)>, Format)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

protected:
	virtual bool Adjust(int32 Direction) override;

private:
	void SetFromPointer(const FGeometry& Geometry, const FPointerEvent& MouseEvent);
	FSlateRect GetTrackRect(const FGeometry& Geometry) const;

	float Min = 0.0f;
	float Max = 100.0f;
	float Step = 5.0f;
	TFunction<float()> Get;
	TFunction<void(float)> Set;
	TFunction<FString(float)> Format;
	bool bDragging = false;
};

/** A switch. */
class SScctToggle : public SScctOptionRow
{
public:
	SLATE_BEGIN_ARGS(SScctToggle) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FText, Description)
		SLATE_ARGUMENT(TFunction<bool()>, Get)
		SLATE_ARGUMENT(TFunction<void(bool)>, Set)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual bool Adjust(int32 Direction) override;
	virtual void Press() override;

private:
	TFunction<bool()> Get;
	TFunction<void(bool)> Set;
	mutable float Shown = -1.0f;
};

/**
 * A binding: what the row's action is bound to, and the capture of a new key or pad button. A press starts the
 * capture; the next key, mouse button, wheel notch or pad button is handed to OnCaptured (by its Unreal key);
 * Escape gives up, Delete and Backspace clear.
 */
class SScctKeyRow : public SScctOptionRow
{
public:
	SLATE_BEGIN_ARGS(SScctKeyRow) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FText, Description)
		/** What it is bound to now, as the row shows it ("W", "LeftMouse / F"); empty when unbound. */
		SLATE_ARGUMENT(TFunction<FString()>, GetKeys)
		/** A key was pressed during the capture. An invalid key means "clear". */
		SLATE_ARGUMENT(TFunction<void(const FKey&)>, OnCaptured)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;

	bool IsCapturing() const { return bCapturing; }

protected:
	virtual void Press() override;

private:
	void Finish(const FKey& Key, bool bCommit);

	TFunction<FString()> GetKeys;
	TFunction<void(const FKey&)> OnCaptured;
	bool bCapturing = false;
};

/** A row with a text field. */
class SScctTextRow : public SScctOptionRow
{
public:
	SLATE_BEGIN_ARGS(SScctTextRow) : _MaxLength(32) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FText, Description)
		SLATE_ARGUMENT(FString, Hint)
		SLATE_ARGUMENT(FString, Initial)
		SLATE_ARGUMENT(int32, MaxLength)
		SLATE_ARGUMENT(TFunction<void(const FString&)>, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	virtual void Press() override;

private:
	TSharedPtr<SEditableTextBox> Box;
};

/** A tab bar. Q and E, Page Up and Page Down and the controller's shoulders change the tab (the page host asks Step). */
class SScctTabBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctTabBar) {}
		SLATE_ARGUMENT(TArray<FString>, Tabs)
		SLATE_ARGUMENT(int32, Initial)
		SLATE_EVENT(FSimpleDelegate, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	int32 GetTab() const { return Tab; }
	void SetTab(int32 NewTab);
	void Step(int32 Direction) { SetTab((Tab + Direction + Count) % FMath::Max(Count, 1)); }

private:
	int32 Tab = 0;
	int32 Count = 0;
	FSimpleDelegate OnChanged;
};

/**
 * A focusable entry of a list with any content: a table's row, a map of the map list. It is lit while it has the
 * focus and marked while it is the selected one.
 */
class SScctListItem : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctListItem) : _Selected(false), _Padding(FMargin(14.0f, 0.0f)) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(bool, Selected)
		SLATE_ARGUMENT(FMargin, Padding)
		/** The item took the focus (the list selects it). */
		SLATE_EVENT(FSimpleDelegate, OnFocused)
		/** Enter, A or a double click. */
		SLATE_EVENT(FSimpleDelegate, OnActivated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TAttribute<bool> Selected;
	FSimpleDelegate OnFocused;
	FSimpleDelegate OnActivated;
	mutable float Lit = 0.0f;
};

/** A column of a table. */
struct FScctTableColumn
{
	FString Title;
	/** A share of the width that is left (0: Width is fixed). */
	float Fill = 0.0f;
	float Width = 100.0f;
	bool bRight = false;
	bool bNumeric = false;
};

/** A row of a table. */
struct FScctTableRow
{
	TArray<FString> Cells;
	/** What the caller knows the row by. */
	int32 Id = 0;
	bool bDim = false;
	/** Cells drawn in the accent rather than in white (a bit per column). */
	uint32 AccentCells = 0;
	FLinearColor Accent = FLinearColor::White;
};

/**
 * A list with columns. A click on a column's head sorts by it (again: the other way round). Up and Down move
 * through the rows; Enter, A or a double click activates one.
 */
class SScctTable : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnRow, int32 /*Id*/);

	SLATE_BEGIN_ARGS(SScctTable) : _SortColumn(INDEX_NONE), _SortDescending(false) {}
		SLATE_ARGUMENT(TArray<FScctTableColumn>, Columns)
		SLATE_ARGUMENT(int32, SortColumn)
		SLATE_ARGUMENT(bool, SortDescending)
		SLATE_ARGUMENT(FText, EmptyText)
		SLATE_EVENT(FOnRow, OnSelected)
		SLATE_EVENT(FOnRow, OnActivated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Replaces the rows. The selection stays on the row with the same Id when it is still there. */
	void SetRows(const TArray<FScctTableRow>& InRows);
	int32 GetSelectedId() const { return SelectedId; }
	void Select(int32 Id);
	void SetEmptyText(const FText& InText) { EmptyText = InText; }
	void Activate(int32 Id) { OnActivated.ExecuteIfBound(Id); }

private:
	void Rebuild();
	void SortBy(int32 Column);

	TArray<FScctTableColumn> Columns;
	TArray<FScctTableRow> Rows;
	TSharedPtr<SVerticalBox> RowBox;
	TSharedPtr<SScrollBox> Scroll;
	FOnRow OnSelected;
	FOnRow OnActivated;
	FText EmptyText;
	int32 SortColumn = INDEX_NONE;
	bool bSortDescending = false;
	int32 SelectedId = INDEX_NONE;
};

/** A ring that turns while something is waited for. */
class SScctSpinner : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScctSpinner) : _Size(22.0f) {}
		SLATE_ARGUMENT(float, Size)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { Size = InArgs._Size; SetCanTick(false); }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float Size = 22.0f;
};

/** A button of a dialog. */
struct FScctDialogButton
{
	FString Label;
	EScctButtonLook Look = EScctButtonLook::Secondary;
	/** Run after the dialog closed. With a text field: gets what was typed. */
	TFunction<void(const FString&)> OnPressed;
};

/** What a dialog asks. */
struct FScctDialog
{
	FString Title;
	FString Body;
	/** Read each frame in place of Body when set (a download's progress). */
	TFunction<FString()> LiveBody;
	TArray<FScctDialogButton> Buttons;
	/** A text field under the body (a password, a name). */
	bool bTextField = false;
	bool bPassword = false;
	FString TextHint;
	/** Seconds after which the last button is pressed by itself (keep these video settings?); 0 never. */
	float Timeout = 0.0f;
	/** The button that has the focus when the dialog opens. */
	int32 FocusButton = 0;
	/** The button Escape and B press; negative: the last one. */
	int32 CancelButton = -1;
};

/**
 * The layer dialogs and toasts are shown on. A dialog dims and blurs everything under the layer and holds the
 * focus until it is answered; a toast is a line that slides in at the top right and goes by itself.
 */
class SScctDialogLayer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctDialogLayer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	/** Only while a dialog is up: closed, it is never where the arrows take the focus. */
	virtual bool SupportsKeyboardFocus() const override { return bOpen; }

	void Show(const FScctDialog& Dialog);
	bool IsOpen() const { return bOpen; }
	/** Presses the dialog's button at Index (the last one for a negative index). */
	void Answer(int32 Index);

	void Toast(const FString& Message, const FLinearColor& Colour = ScctUi::Accent());

	/** The widget to give the focus back to when a dialog closes. */
	TWeakPtr<SWidget> FocusAfter;
	/** The control that had the focus when the dialog opened: it has it again when the dialog closes. */
	TWeakPtr<SWidget> FocusBefore;

private:
	struct FToast
	{
		TSharedPtr<SWidget> Widget;
		float Age = 0.0f;
	};

	FScctDialog Current;
	TSharedPtr<SBox> DialogBox;
	TSharedPtr<SVerticalBox> ToastBox;
	TSharedPtr<SEditableTextBox> TextField;
	TArray<FToast> Toasts;
	bool bOpen = false;
	float Shown = 0.0f;
	float TimeLeft = 0.0f;
};
