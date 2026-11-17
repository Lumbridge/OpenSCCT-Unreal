#include "Menu/SScctBindingsPanel.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Menu/SScctConfirm.h"
#include "Menu/SScctOptionsPage.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "Runtime/ScctBindingsApi.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	constexpr float BindingsLabelShare = 0.40f;
	constexpr float SlotWidth = 178.0f;

	/** Reads one of the tables' texts, which are handed over as "copy what fits, return the full length". */
	template <typename TRead>
	FString ReadText(TRead&& Read)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(1024);
		const int32 Length = Read(Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Read(Buffer.GetData(), Buffer.Num());
		}
		Buffer.Last() = TEXT('\0');
		return FString(Buffer.GetData());
	}

	const TCHAR* SideName(bool bMerc) { return bMerc ? TEXT("merc") : TEXT("spy"); }

	FLinearColor SideColour(bool bMerc) { return ScctHudStyle::Accent(bMerc ? 1 : 0); }

	/** The binding name of the modifier an Unreal key is ("Shift" for either Shift key), or empty. */
	FString ModifierName(const FKey& Key)
	{
		if (Key == EKeys::LeftShift || Key == EKeys::RightShift) return TEXT("Shift");
		if (Key == EKeys::LeftControl || Key == EKeys::RightControl) return TEXT("Ctrl");
		if (Key == EKeys::LeftAlt || Key == EKeys::RightAlt) return TEXT("Alt");
		return FString();
	}

	/**
	 * The binding name of an Unreal key or mouse button ("W", "LeftMouse", "MouseWheelUp"): the bridge's name of the
	 * EScctKey that reads it. Empty for a key the bindings cannot read, and for the controller's buttons (they are
	 * not keyboard bindings).
	 */
	FString BindingNameOf(const FKey& Key)
	{
		const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>();
		if (Controls == nullptr || !Key.IsValid())
		{
			return FString();
		}
		const TArray<TPair<FKey, FKey>>& Table = FScctControls::Keys();
		for (int32 Code = 0; Code < Table.Num() && Code < static_cast<int32>(EScctKey::PadA); ++Code)
		{
			if (Table[Code].Key == Key || Table[Code].Value == Key)
			{
				return ReadText([Controls, Code](TCHAR* Buffer, int32 Capacity) { return Controls->GetKeyName(Code, Buffer, Capacity); });
			}
		}
		return FString();
	}

	/** Held Ctrl, Shift and Alt in front of a key, in the order the bridge writes them. */
	FString WithModifiers(const FModifierKeysState& Modifiers, const FString& Key)
	{
		FString Text;
		if (Modifiers.IsControlDown()) Text += TEXT("Ctrl+");
		if (Modifiers.IsShiftDown()) Text += TEXT("Shift+");
		if (Modifiers.IsAltDown()) Text += TEXT("Alt+");
		return Text + Key;
	}

	/** A dimmed layer over the whole Options screen with a glass box in its middle, removed by Close. */
	class SScctBindingModal : public SCompoundWidget
	{
	public:
		virtual bool SupportsKeyboardFocus() const override { return true; }

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			// The keyboard can only be given to a widget that is on screen, which the box is by its first tick. Over
			// a match the viewport has it otherwise, and the capture must hear Escape and every key before it does.
			if (!bFocusTaken && FSlateApplication::IsInitialized())
			{
				bFocusTaken = true;
				FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
			}
		}

		void Show(const TSharedRef<SOverlay>& InHost)
		{
			Host = InHost;
			if (FSlateApplication::IsInitialized())
			{
				FocusBefore = FSlateApplication::Get().GetKeyboardFocusedWidget();
			}
			InHost->AddSlot()[ SharedThis(this) ];
		}

		/** Takes the layer away and gives the keyboard back to whoever had it. */
		void Close()
		{
			if (bClosed)
			{
				return;
			}
			bClosed = true;
			const TSharedRef<SWidget> Self = SharedThis(this);
			if (const TSharedPtr<SOverlay> Overlay = Host.Pin())
			{
				Overlay->RemoveSlot(Self);
			}
			if (const TSharedPtr<SWidget> Before = FocusBefore.Pin(); Before.IsValid() && FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetKeyboardFocus(Before, EFocusCause::SetDirectly);
			}
		}

	protected:
		void Frame(const TSharedRef<SWidget>& Content, float Width)
		{
			static const FSlateColorBrush Dim(FLinearColor::White);
			ChildSlot
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[ SNew(SImage).Image(&Dim).ColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f)) ]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(Width)[ Ui::Glass(Content, FMargin(30.0f, 26.0f), 0.9f) ]
				]
			];
		}

		bool bClosed = false;

	private:
		TWeakPtr<SOverlay> Host;
		TWeakPtr<SWidget> FocusBefore;
		bool bFocusTaken = false;
	};

	/** Waits for the key of one slot (OptionsMenu.BindingCapture.cs). */
	class SScctBindingCapture : public SScctBindingModal
	{
	public:
		SLATE_BEGIN_ARGS(SScctBindingCapture) {}
			SLATE_ARGUMENT(FString, Title)
			SLATE_ARGUMENT(FString, Description)
			SLATE_ARGUMENT(bool, bDoubleTapAllowed)
			SLATE_ARGUMENT(bool, bDoubleTap)
			SLATE_ARGUMENT(bool, bCanClear)
			SLATE_ARGUMENT(FLinearColor, Colour)
			/** Why a captured key cannot be taken ("" when it can): the capture then stays open and says so. */
			SLATE_ARGUMENT(TFunction<FString(const FString&)>, Validate)
			SLATE_ARGUMENT(TFunction<void(const FString&)>, OnCaptured)
			SLATE_ARGUMENT(TFunction<void()>, OnClear)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bDoubleTapAllowed = InArgs._bDoubleTapAllowed;
			bDoubleTap = InArgs._bDoubleTapAllowed && InArgs._bDoubleTap;
			bCanClear = InArgs._bCanClear;
			Validate = InArgs._Validate;
			OnCaptured = InArgs._OnCaptured;
			OnClear = InArgs._OnClear;
			Status = TEXT("Waiting for input");

			const TSharedRef<SWidget> DoubleTapBox = bDoubleTapAllowed
				? StaticCastSharedRef<SWidget>(SNew(SCheckBox)
					.IsFocusable(false)
					.IsChecked_Lambda([this] { return bDoubleTap ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bDoubleTap = State == ECheckBoxState::Checked; })
					.ToolTipText(Ui::ToText(TEXT("Tick this with the mouse, then press the key once. The key's own action still runs on each press.")))
					[
						Ui::Text(Ui::ToText(TEXT("Double tap: trigger on two quick presses")), 12.0f)
					])
				: SNullWidget::NullWidget;
			const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
			Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox).IsEnabled(bCanClear)
				[
					Ui::Button(Ui::ToText(TEXT("CLEAR")), FOnClicked::CreateLambda([this] { Clear(); return FReply::Handled(); }), false, Ui::Accent(), 110.0f)
				]
			];
			Buttons->AddSlot().AutoWidth()
			[
				Ui::Button(Ui::ToText(TEXT("CANCEL")), FOnClicked::CreateLambda([this] { Cancel(); return FReply::Handled(); }), false, Ui::Accent(), 110.0f)
			];

			Frame(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					Ui::Text(Ui::ToText(TEXT("BIND:  ") + InArgs._Title), 17.0f, FSlateColor(InArgs._Colour), ScctHudStyle::EWeight::Regular, true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					Ui::Text(Ui::ToText(InArgs._Description), 12.5f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					Ui::Text(Ui::ToText(TEXT("Press a key or mouse button. Hold Ctrl, Shift or Alt to make a combination. Press and release a modifier alone to bind it. Escape cancels; Delete or Backspace clears this key.")),
						11.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					Ui::Text(Ui::ToText(TEXT("A modifier's own action stays active alongside a combination, such as Shift + R.")),
						11.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
				[
					Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(Status); }), 13.0f, FSlateColor(InArgs._Colour), ScctHudStyle::EWeight::Regular, true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, bDoubleTapAllowed ? 16.0f : 0.0f)[ DoubleTapBox ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[ Buttons ], 560.0f);
		}

		virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
		{
			const FKey Key = InKeyEvent.GetKey();
			if (InKeyEvent.IsRepeat())
			{
				return FReply::Handled();
			}
			if (Key == EKeys::Escape)
			{
				Cancel();
				return FReply::Handled();
			}
			if (Key == EKeys::Delete || Key == EKeys::BackSpace)
			{
				if (InKeyEvent.GetModifierKeys().AnyModifiersDown())
				{
					// With a modifier they are an ordinary combination (Ctrl+Delete).
					Finish(WithModifiers(InKeyEvent.GetModifierKeys(), BindingNameOf(Key)));
				}
				else if (bCanClear)
				{
					Clear();
				}
				else
				{
					Status = TEXT("This slot has no key to clear. Press a key, or Escape to cancel.");
				}
				return FReply::Handled();
			}
			if (Key == EKeys::LeftCommand || Key == EKeys::RightCommand)
			{
				Status = TEXT("Use Ctrl, Shift or Alt for keyboard combinations.");
				return FReply::Handled();
			}

			const FString Modifier = ModifierName(Key);
			if (!Modifier.IsEmpty())
			{
				ModifiersDown.Add(Modifier);
				const FModifierKeysState& Held = InKeyEvent.GetModifierKeys();
				const int32 Count = (Held.IsControlDown() ? 1 : 0) + (Held.IsShiftDown() ? 1 : 0) + (Held.IsAltDown() ? 1 : 0);
				bCombination |= ModifiersDown.Num() > 1 || Count > 1;
				LoneModifier = bCombination ? FString() : Modifier;
				Status = bCombination
					? TEXT("Press a key with these modifiers. A modifier-only combination cannot be bound.")
					: TEXT("Hold the modifier and press a key, or release it to bind the modifier alone.");
				return FReply::Handled();
			}

			const FString Name = BindingNameOf(Key);
			if (Name.IsEmpty())
			{
				Status = FString::Printf(TEXT("%s cannot be bound here."), *Key.GetDisplayName().ToString());
				return FReply::Handled();
			}
			LoneModifier.Reset();
			Finish(WithModifiers(InKeyEvent.GetModifierKeys(), Name));
			return FReply::Handled();
		}

		virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
		{
			const FString Modifier = ModifierName(InKeyEvent.GetKey());
			if (Modifier.IsEmpty())
			{
				return FReply::Handled();
			}
			ModifiersDown.Remove(Modifier);
			if (bCombination)
			{
				if (ModifiersDown.Num() == 0 && !InKeyEvent.GetModifierKeys().AnyModifiersDown())
				{
					bCombination = false;
					Status = TEXT("Waiting for a key, mouse button or single modifier.");
				}
			}
			else if (LoneModifier == Modifier)
			{
				Finish(Modifier);
			}
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			// The box's own buttons and tick box take their clicks before this hears them.
			const FString Name = BindingNameOf(MouseEvent.GetEffectingButton());
			if (!Name.IsEmpty())
			{
				LoneModifier.Reset();
				Finish(WithModifiers(MouseEvent.GetModifierKeys(), Name));
			}
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			return OnMouseButtonDown(MyGeometry, MouseEvent);
		}

		virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			const float Delta = MouseEvent.GetWheelDelta();
			if (Delta != 0.0f)
			{
				Finish(WithModifiers(MouseEvent.GetModifierKeys(), Delta > 0.0f ? TEXT("MouseWheelUp") : TEXT("MouseWheelDown")));
			}
			return FReply::Handled();
		}

	private:
		void Finish(FString Key)
		{
			if (bClosed || Key.IsEmpty())
			{
				return;
			}
			if (bDoubleTap)
			{
				if (Key.Contains(TEXT("MouseWheel")))
				{
					Status = TEXT("A double tap needs a key or mouse button, not the mouse wheel.");
					return;
				}
				Key = TEXT("Double+") + Key;
			}
			if (Validate)
			{
				const FString Error = Validate(Key);
				if (!Error.IsEmpty())
				{
					Status = Error;
					return;
				}
			}
			const TFunction<void(const FString&)> Then = OnCaptured;
			Close();
			if (Then)
			{
				Then(Key);
			}
		}

		void Clear()
		{
			if (!bCanClear)
			{
				return;
			}
			const TFunction<void()> Then = OnClear;
			Close();
			if (Then)
			{
				Then();
			}
		}

		void Cancel()
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Bindings: capture cancelled"));
			Close();
		}

		bool bDoubleTapAllowed = false;
		bool bDoubleTap = false;
		bool bCanClear = false;
		bool bCombination = false;
		TSet<FString> ModifiersDown;
		FString LoneModifier;
		FString Status;
		TFunction<FString(const FString&)> Validate;
		TFunction<void(const FString&)> OnCaptured;
		TFunction<void()> OnClear;
	};

	/** A key that already does something: Replace, Swap, Share or Cancel (OptionsMenu.OpenBindingConflict). */
	class SScctBindingConflict : public SScctBindingModal
	{
	public:
		SLATE_BEGIN_ARGS(SScctBindingConflict) {}
			SLATE_ARGUMENT(FString, Message)
			SLATE_ARGUMENT(bool, bCanSwap)
			SLATE_ARGUMENT(FLinearColor, Colour)
			/** The answer: an EScctBindingCommit, or -1 for Cancel. */
			SLATE_ARGUMENT(TFunction<void(int32)>, OnAnswer)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnAnswer = InArgs._OnAnswer;
			const auto Answer = [this](const TCHAR* Label, int32 Mode, bool bPrimary)
			{
				return Ui::Button(Ui::ToText(Label), FOnClicked::CreateLambda([this, Mode] { Reply(Mode); return FReply::Handled(); }), bPrimary, Ui::Accent(), 110.0f);
			};
			const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
			Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ Answer(TEXT("CANCEL"), -1, false) ];
			Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ Answer(TEXT("SHARE KEY"), static_cast<int32>(EScctBindingCommit::Share), false) ];
			if (InArgs._bCanSwap)
			{
				Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ Answer(TEXT("SWAP"), static_cast<int32>(EScctBindingCommit::Swap), false) ];
			}
			Buttons->AddSlot().AutoWidth()[ Answer(TEXT("REPLACE"), static_cast<int32>(EScctBindingCommit::Replace), true) ];

			Frame(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					Ui::Text(Ui::ToText(TEXT("KEY ALREADY ASSIGNED")), 17.0f, FSlateColor(InArgs._Colour))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 22.0f)
				[
					Ui::Text(Ui::ToText(InArgs._Message), 13.5f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[ Buttons ], 720.0f);
		}

		virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
		{
			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				Reply(-1);
			}
			return FReply::Handled();
		}

	private:
		void Reply(int32 Mode)
		{
			const TFunction<void(int32)> Then = OnAnswer;
			Close();
			if (Then)
			{
				Then(Mode);
			}
		}

		TFunction<void(int32)> OnAnswer;
	};
}

// ---- The model -----------------------------------------------------------------------------------------------------

const FScctBindingsApi* ScctBindings::Api()
{
	// The player's files first: otherwise the draft would start from the defaults and a save write them over the file.
	UScctControlSettings::Get()->EnsureLoaded();
	return ScctFeature<FScctBindingsApi>();
}

TArray<FScctBindingRow> ScctBindings::Rows(bool bMerc)
{
	TArray<FScctBindingRow> Rows;
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		return Rows;
	}
	const FString Text = ReadText([Bindings, bMerc](TCHAR* Buffer, int32 Capacity) { return Bindings->GetRows(bMerc ? 1 : 0, Buffer, Capacity); });
	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, TEXT("\n"));
	for (const FString& Line : Lines)
	{
		TArray<FString> Fields;
		Line.ParseIntoArray(Fields, TEXT("\t"), false);
		if (Fields.Num() < 5)
		{
			continue;
		}
		FScctBindingRow& Row = Rows.AddDefaulted_GetRef();
		Row.Id = Fields[0];
		Row.Category = Fields[1];
		Row.Label = Fields[2];
		Row.Description = Fields[3];
		Row.bDoubleTap = Fields[4] == TEXT("1");
		for (int32 Index = 5; Index < Fields.Num(); ++Index)
		{
			if (!Fields[Index].IsEmpty())
			{
				Row.Keys.Add(Fields[Index]);
			}
		}
	}
	return Rows;
}

FString ScctBindings::DisplayName(const FString& Key)
{
	static const TMap<FString, FString> Names = {
		{ TEXT("LeftMouse"), TEXT("Left Mouse") }, { TEXT("RightMouse"), TEXT("Right Mouse") }, { TEXT("MiddleMouse"), TEXT("Middle Mouse") },
		{ TEXT("MouseX1"), TEXT("Mouse 4") }, { TEXT("MouseX2"), TEXT("Mouse 5") },
		{ TEXT("MouseWheelUp"), TEXT("Wheel Up") }, { TEXT("MouseWheelDown"), TEXT("Wheel Down") },
		{ TEXT("PageUp"), TEXT("Page Up") }, { TEXT("PageDown"), TEXT("Page Down") }, { TEXT("CapsLock"), TEXT("Caps Lock") },
		{ TEXT("NumLock"), TEXT("Num Lock") }, { TEXT("ScrollLock"), TEXT("Scroll Lock") },
		{ TEXT("GreyStar"), TEXT("Num *") }, { TEXT("GreyPlus"), TEXT("Num +") }, { TEXT("GreyMinus"), TEXT("Num -") },
		{ TEXT("GreySlash"), TEXT("Num /") }, { TEXT("NumPadPeriod"), TEXT("Num .") },
		{ TEXT("Semicolon"), TEXT(";") }, { TEXT("EqualSign"), TEXT("=") }, { TEXT("Comma"), TEXT(",") }, { TEXT("Minus"), TEXT("-") },
		{ TEXT("Period"), TEXT(".") }, { TEXT("Slash"), TEXT("/") }, { TEXT("LeftBracket"), TEXT("[") }, { TEXT("Backslash"), TEXT("\\") },
		{ TEXT("RightBracket"), TEXT("]") }, { TEXT("SingleQuote"), TEXT("'") }, { TEXT("Tilde"), TEXT("~") },
	};
	TArray<FString> Parts;
	Key.ParseIntoArray(Parts, TEXT("+"));
	FString Text;
	bool bDouble = false;
	for (const FString& Part : Parts)
	{
		if (Part.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
		{
			bDouble = true;
			continue;
		}
		FString Shown = Part;
		if (const FString* Found = Names.Find(Part))
		{
			Shown = *Found;
		}
		else if (Part.StartsWith(TEXT("NumPad")) && Part.Len() == 7)
		{
			Shown = TEXT("Num ") + Part.Right(1);
		}
		Text += Text.IsEmpty() ? Shown : TEXT(" + ") + Shown;
	}
	return bDouble ? TEXT("Double-tap ") + Text : Text;
}

FString ScctBindings::Check(bool bMerc, const FString& ActionId, const FString& Key)
{
	const FScctBindingsApi* Bindings = Api();
	return Bindings == nullptr ? FString(TEXT("The bridge is not running."))
		: ReadText([&](TCHAR* Buffer, int32 Capacity) { return Bindings->Check(bMerc ? 1 : 0, *ActionId, *Key, Buffer, Capacity); });
}

FString ScctBindings::Conflict(bool bMerc, const FString& ActionId, const FString& OldKey, const FString& Key, bool& bOutAlreadyBound, bool& bOutCanSwap)
{
	bOutAlreadyBound = false;
	bOutCanSwap = false;
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		return FString();
	}
	int32 Flags = 0;
	const FString Text = ReadText([&](TCHAR* Buffer, int32 Capacity) { return Bindings->Conflict(bMerc ? 1 : 0, *ActionId, *OldKey, *Key, &Flags, Buffer, Capacity); });
	bOutAlreadyBound = (Flags & EScctBindingConflict::AlreadyBound) != 0;
	bOutCanSwap = (Flags & EScctBindingConflict::CanSwap) != 0;
	return Text;
}

bool ScctBindings::Commit(bool bMerc, const FScctBindingRow& Row, const FString& OldKey, const FString& Key, int32 Mode, const FString& ConflictWith, FString& OutError)
{
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		OutError = TEXT("The bridge is not running.");
		return false;
	}
	OutError = ReadText([&](TCHAR* Buffer, int32 Capacity) { return Bindings->Commit(bMerc ? 1 : 0, *Row.Id, *OldKey, *Key, Mode, Buffer, Capacity); });
	const bool bDone = OutError.IsEmpty();
	static const TCHAR* const ModeNames[] = { TEXT("share"), TEXT("replace"), TEXT("swap") };
	const FString How = ConflictWith.IsEmpty() ? FString(TEXT("the key was free"))
		: FString::Printf(TEXT("conflict with %s: %s"), *ConflictWith, ModeNames[FMath::Clamp(Mode, 0, 2)]);
	if (bDone)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s %s -> %s (%s%s)"), SideName(bMerc), *Key, *Row.Label, *How,
			OldKey.IsEmpty() ? TEXT(", added as another key") : *FString::Printf(TEXT(", in place of %s"), *OldKey));
	}
	else
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctBind: %s %s -> %s refused: %s"), SideName(bMerc), *Key, *Row.Label, *OutError);
	}
	return bDone;
}

void ScctBindings::Clear(bool bMerc, const FScctBindingRow& Row, const FString& Key)
{
	if (const FScctBindingsApi* Bindings = Api())
	{
		Bindings->Clear(bMerc ? 1 : 0, *Row.Id, *Key);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s %s cleared from %s"), SideName(bMerc), *Key, *Row.Label);
	}
}

void ScctBindings::ResetKeys(bool bMerc)
{
	if (const FScctBindingsApi* Bindings = Api())
	{
		Bindings->ResetKeys(bMerc ? 1 : 0);
		FString Sample;
		for (const FScctBindingRow& Row : Rows(bMerc))
		{
			if (Row.Id == TEXT("use") || Row.Id == TEXT("jump") || Row.Id == TEXT("fire"))
			{
				Sample += FString::Printf(TEXT("%s%s on %s"), Sample.IsEmpty() ? TEXT("") : TEXT(", "), *Row.Label, Row.Keys.Num() > 0 ? *FString::Join(Row.Keys, TEXT(" / ")) : TEXT("nothing"));
			}
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s keys reset to the defaults (%s)"), IsSplit() ? SideName(bMerc) : TEXT("shared"), *Sample);
	}
}

bool ScctBindings::IsSplit()
{
	const FScctBindingsApi* Bindings = Api();
	return Bindings == nullptr || Bindings->GetSplit() != 0;
}

void ScctBindings::SetSplit(bool bSplit)
{
	if (const FScctBindingsApi* Bindings = Api())
	{
		Bindings->SetSplit(bSplit ? 1 : 0);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s"), bSplit ? TEXT("the spy and the merc have a key layout each") : TEXT("both sides play the spy's key layout"));
	}
}

void ScctBindings::Apply()
{
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr || Bindings->HasEdits() == 0)
	{
		return;
	}
	Bindings->Apply();
	UScctControlSettings::Get()->NotifyRebound();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: key edits applied; a running match plays them from its next frame"));
}

void ScctBindings::Save()
{
	const FScctBindingsApi* Bindings = Api();
	if (Bindings != nullptr && Bindings->HasUnsaved() != 0)
	{
		UScctControlSettings::Get()->SaveBindings();
	}
}

void ScctBindings::Discard()
{
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		return;
	}
	const bool bChanged = Bindings->HasUnsaved() != 0;
	Bindings->Discard();
	if (bChanged)
	{
		UScctControlSettings::Get()->NotifyRebound();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: key edits thrown away; the saved keys are in play again"));
	}
}

bool ScctBindings::HasEdits()
{
	const FScctBindingsApi* Bindings = ScctFeature<FScctBindingsApi>();
	return Bindings != nullptr && Bindings->HasEdits() != 0;
}

bool ScctBindings::HasUnsaved()
{
	const FScctBindingsApi* Bindings = ScctFeature<FScctBindingsApi>();
	return Bindings != nullptr && Bindings->HasUnsaved() != 0;
}

bool ScctBindings::OpenCapture(const FString& Title, const FString& Description, const FLinearColor& Colour, bool bCanClear,
	TFunction<FString(const FString&)> Validate, TFunction<void(const FString&)> OnCaptured, TFunction<void()> OnClear)
{
	const TSharedPtr<SOverlay> Host = SScctOptionsPage::ModalHost();
	if (!Host.IsValid())
	{
		return false;
	}
	const TSharedRef<SScctBindingCapture> Capture = SNew(SScctBindingCapture)
		.Title(Title)
		.Description(Description)
		.bDoubleTapAllowed(false)
		.bCanClear(bCanClear)
		.Colour(Colour)
		.Validate(MoveTemp(Validate))
		.OnCaptured(MoveTemp(OnCaptured))
		.OnClear(MoveTemp(OnClear));
	Capture->Show(Host.ToSharedRef());
	return true;
}

void ScctBindings::RunUnattendedTest()
{
	static bool bDone = false;
	FString Steps;
	if (bDone || !FParse::Value(FCommandLine::Get(), TEXT("ScctBindTest="), Steps, false))
	{
		return;
	}
	bDone = true;
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("-ScctBindTest: the bridge's bindings editor is missing"));
		return;
	}

	const FString File = UScctControlSettings::GetBindingsFile();
	const FDateTime Before = IFileManager::Get().GetTimeStamp(*File);
	Bindings->Begin();
	TArray<FString> Entries;
	Steps.ParseIntoArray(Entries, TEXT(";"));
	for (const FString& Entry : Entries)
	{
		TArray<FString> Parts;
		Entry.ParseIntoArray(Parts, TEXT(","));
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
		}
		const bool bMerc = Parts.Num() > 0 && Parts[0].Equals(TEXT("merc"), ESearchCase::IgnoreCase);
		if (Parts.Num() == 2 && Parts[1].Equals(TEXT("reset"), ESearchCase::IgnoreCase))
		{
			ResetKeys(bMerc);
			continue;
		}
		if (Parts.Num() < 3)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctBindTest: '%s' is not <side>,<key>,<action>[,replace|swap|share] or <side>,reset"), *Entry);
			continue;
		}

		const FString& Key = Parts[1];
		const FScctBindingRow* Row = nullptr;
		const TArray<FScctBindingRow> SideRows = Rows(bMerc);
		for (const FScctBindingRow& Each : SideRows)
		{
			if (Each.Id.Equals(Parts[2], ESearchCase::IgnoreCase) || Each.Label.Equals(Parts[2], ESearchCase::IgnoreCase))
			{
				Row = &Each;
				break;
			}
		}
		if (Row == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctBindTest: the %s has no action '%s'"), SideName(bMerc), *Parts[2]);
			continue;
		}

		// As if the player clicked the action's primary key and pressed Key.
		const FString OldKey = Row->Keys.Num() > 0 ? Row->Keys[0] : FString();
		const FString Refused = Check(bMerc, Row->Id, Key);
		if (!Refused.IsEmpty())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctBind: %s %s -> %s refused at capture: %s"), SideName(bMerc), *Key, *Row->Label, *Refused);
			continue;
		}
		bool bAlready = false, bCanSwap = false;
		const FString With = Conflict(bMerc, Row->Id, OldKey, Key, bAlready, bCanSwap);
		int32 Mode = static_cast<int32>(EScctBindingCommit::Share);
		if (!With.IsEmpty())
		{
			const FString Answer = Parts.Num() > 3 ? Parts[3] : FString(TEXT("replace"));
			Mode = Answer.Equals(TEXT("swap"), ESearchCase::IgnoreCase) && bCanSwap ? static_cast<int32>(EScctBindingCommit::Swap)
				: Answer.Equals(TEXT("share"), ESearchCase::IgnoreCase) ? static_cast<int32>(EScctBindingCommit::Share)
				: static_cast<int32>(EScctBindingCommit::Replace);
		}
		FString Error;
		Commit(bMerc, *Row, OldKey, Key, Mode, With, Error);
		for (const FScctBindingRow& After : ScctBindings::Rows(bMerc))
		{
			if (After.Id == Row->Id)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: the %s's %s is now on %s"), SideName(bMerc), *After.Label, *FString::Join(After.Keys, TEXT(" / ")));
			}
		}
	}

	// What APPLY and SAVE do on the Options screen.
	Apply();
	Save();
	const FDateTime After = IFileManager::Get().GetTimeStamp(*File);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s %s (was %s, now %s)"), *File, After != Before ? TEXT("written") : TEXT("unchanged"),
		Before == FDateTime::MinValue() ? TEXT("missing") : *Before.ToString(), After == FDateTime::MinValue() ? TEXT("missing") : *After.ToString());
}

// ---- The panel -----------------------------------------------------------------------------------------------------

void SScctBindingsPanel::RegisterSections()
{
	static bool bRegistered = false;
	if (bRegistered)
	{
		return;
	}
	bRegistered = true;

	for (const bool bSide : { false, true })
	{
		FScctOptionsSection Section;
		Section.Build = [bSide]() -> TSharedRef<SWidget> { return SNew(SScctBindingsPanel).bMerc(bSide); };
		// The two pages share one draft, so each callback is safe to run twice (the second finds nothing to do).
		Section.HasEdits = [] { return ScctBindings::HasEdits(); };
		Section.HasUnsaved = [] { return ScctBindings::HasUnsaved(); };
		Section.Apply = [] { ScctBindings::Apply(); };
		Section.Save = [] { ScctBindings::Save(); };
		Section.Discard = [] { ScctBindings::Discard(); };
		Section.ResetToDefaults = [bSide] { ScctBindings::ResetKeys(bSide); };
		SScctOptionsPage::RegisterSection(bSide ? FName(TEXT("Mercenary")) : FName(TEXT("Spy")), Section);
	}
}

void SScctBindingsPanel::Construct(const FArguments& InArgs)
{
	bMerc = InArgs._bMerc;
	if (const FScctBindingsApi* Bindings = ScctBindings::Api())
	{
		Bindings->Begin();
	}

	ChildSlot
	[
		SAssignNew(List, SVerticalBox)
	];
	Refresh();

	// -ScctBindCapture=<action>[,<key>]: open the capture of the action's primary key on the page shown first, and
	// with a key, press it (a conflict then shows its question). Once per run, for screenshots.
	static bool bCaptureShown = false;
	FString Wanted;
	if (!bCaptureShown && FParse::Value(FCommandLine::Get(), TEXT("ScctBindCapture="), Wanted, false))
	{
		bCaptureShown = true;
		FString ActionId = Wanted, Key;
		Wanted.Split(TEXT(","), &ActionId, &Key);
		for (const FScctBindingRow& Row : ScctBindings::Rows(bMerc))
		{
			if (Row.Id == ActionId.TrimStartAndEnd())
			{
				const FString OldKey = Row.Keys.Num() > 0 ? Row.Keys[0] : FString();
				UE_LOG(LogOpenSCCT, Log, TEXT("-ScctBindCapture: %s%s"), *Row.Label, Key.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", pressing %s"), *Key));
				if (Key.IsEmpty())
				{
					BeginCapture(Row, OldKey);
				}
				else
				{
					Captured(Row, OldKey, Key.TrimStartAndEnd());
				}
			}
		}
	}
}

void SScctBindingsPanel::Refresh()
{
	List->ClearChildren();
	const FLinearColor Colour = SideColour(bMerc);
	if (ScctBindings::Api() == nullptr)
	{
		List->AddSlot().AutoHeight()
		[
			Ui::Text(Ui::ToText(TEXT("The key bindings need the bridge, which is not running.")), 12.0f, FSlateColor(ScctHudStyle::TextDim()))
		];
		return;
	}

	const bool bSplit = ScctBindings::IsSplit();
	List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Ui::Label(bMerc ? TEXT("Mercenary keys") : TEXT("Spy keys"), Colour) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(24.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SCheckBox)
			.IsFocusable(false)
			.IsChecked(bSplit ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			.ToolTipText(Ui::ToText(TEXT("On: the spy and the mercenary each have their own keys. Off: both play the spy's keys, and this page edits them.")))
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
			{
				ScctBindings::SetSplit(State == ECheckBoxState::Checked);
				Status = State == ECheckBoxState::Checked ? TEXT("Each side has its own keys again, starting from the shared ones.") : TEXT("Both sides now play the spy's keys.");
				Refresh();
			})
			[
				Ui::Text(Ui::ToText(TEXT("Separate spy and mercenary layouts")), 12.5f)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Ui::Button(Ui::ToText(TEXT("RESET KEYS")), FOnClicked::CreateLambda([this] { AskReset(); return FReply::Handled(); }), false, Colour, 120.0f)
		]
	];
	if (!bSplit)
	{
		List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			Ui::Text(Ui::ToText(TEXT("Shared spy / mercenary bindings: a key changed here changes for both sides.")), 11.5f, FSlateColor(Colour))
		];
	}
	List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		Ui::Text(TAttribute<FText>::CreateLambda([this]
		{
			return Ui::ToText(Status.IsEmpty() ? FString(TEXT("Click a key to change it, or an empty slot to add one. Hover an action for what it does.")) : Status);
		}), 11.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
	];

	// The column heads.
	List->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 14.0f, 4.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(BindingsLabelShare)[ Ui::Label(TEXT("Action")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(SlotWidth + 8.0f)[ Ui::Label(TEXT("Primary")) ] ]
		+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(SlotWidth)[ Ui::Label(TEXT("Secondary")) ] ]
		+ SHorizontalBox::Slot().FillWidth(1.0f - BindingsLabelShare)
	];

	FString Category;
	for (const FScctBindingRow& Row : ScctBindings::Rows(bMerc))
	{
		if (Row.Category != Category)
		{
			Category = Row.Category;
			List->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 6.0f)[ Ui::Label(Category, Ui::Accent()) ];
		}

		const TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
		Line->AddSlot().FillWidth(BindingsLabelShare).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			Ui::Text(Ui::ToText(Row.Label), 13.0f, FSlateColor(Row.Keys.Num() > 0 ? ScctHudStyle::Text() : ScctHudStyle::TextDim()))
		];
		// Primary and secondary, every further key the action has, and empty slots to add one.
		const int32 Slots = FMath::Max(2, Row.Keys.Num());
		for (int32 Index = 0; Index < Slots; ++Index)
		{
			Line->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(SlotWidth)[ MakeSlot(Row, Row.Keys.IsValidIndex(Index) ? Row.Keys[Index] : FString()) ]
			];
		}
		Line->AddSlot().FillWidth(1.0f - BindingsLabelShare);

		List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 14.0f, 5.0f)
		[
			SNew(SBox).MinDesiredHeight(32.0f).ToolTipText(Ui::ToText(Row.Description))[ Line ]
		];
	}
}

TSharedRef<SWidget> SScctBindingsPanel::MakeSlot(const FScctBindingRow& Row, const FString& Key)
{
	const FScctBindingRow Copy = Row;
	return Ui::Button(Ui::ToText(Key.IsEmpty() ? FString(TEXT("-")) : ScctBindings::DisplayName(Key)),
		FOnClicked::CreateLambda([this, Copy, Key] { BeginCapture(Copy, Key); return FReply::Handled(); }),
		false, SideColour(bMerc), SlotWidth);
}

void SScctBindingsPanel::BeginCapture(const FScctBindingRow& Row, const FString& OldKey)
{
	const TSharedPtr<SOverlay> Host = SScctOptionsPage::ModalHost();
	if (!Host.IsValid())
	{
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Bindings: capturing a key for the %s's %s (slot %s)"), SideName(bMerc), *Row.Label, OldKey.IsEmpty() ? TEXT("empty") : *OldKey);

	const TWeakPtr<SScctBindingsPanel> Weak = SharedThis(this);
	const bool bSide = bMerc;
	const TSharedRef<SScctBindingCapture> Capture = SNew(SScctBindingCapture)
		.Title(Row.Label)
		.Description(Row.Description)
		.bDoubleTapAllowed(Row.bDoubleTap)
		.bDoubleTap(OldKey.StartsWith(TEXT("Double+")))
		.bCanClear(!OldKey.IsEmpty())
		.Colour(SideColour(bMerc))
		.Validate([bSide, Row](const FString& Key) { return ScctBindings::Check(bSide, Row.Id, Key); })
		.OnCaptured([Weak, Row, OldKey](const FString& Key)
		{
			if (const TSharedPtr<SScctBindingsPanel> Panel = Weak.Pin())
			{
				Panel->Captured(Row, OldKey, Key);
			}
		})
		.OnClear([Weak, Row, OldKey]
		{
			if (const TSharedPtr<SScctBindingsPanel> Panel = Weak.Pin())
			{
				ScctBindings::Clear(Panel->bMerc, Row, OldKey);
				Panel->Status = FString::Printf(TEXT("%s no longer on %s."), *Row.Label, *ScctBindings::DisplayName(OldKey));
				Panel->Refresh();
			}
		});
	Capture->Show(Host.ToSharedRef());
}

void SScctBindingsPanel::Captured(const FScctBindingRow& Row, const FString& OldKey, const FString& Key)
{
	bool bAlready = false, bCanSwap = false;
	const FString With = ScctBindings::Conflict(bMerc, Row.Id, OldKey, Key, bAlready, bCanSwap);
	if (bAlready)
	{
		Status = FString::Printf(TEXT("%s is already on %s."), *Row.Label, *ScctBindings::DisplayName(Key));
		Refresh();
		return;
	}

	const auto Bind = [Weak = TWeakPtr<SScctBindingsPanel>(SharedThis(this)), Row, OldKey, Key, With](int32 Mode)
	{
		const TSharedPtr<SScctBindingsPanel> Panel = Weak.Pin();
		if (!Panel.IsValid() || Mode < 0)
		{
			return;
		}
		FString Error;
		Panel->Status = ScctBindings::Commit(Panel->bMerc, Row, OldKey, Key, Mode, With, Error)
			? FString::Printf(TEXT("%s: %s. Apply or save to keep it."), *Row.Label, *ScctBindings::DisplayName(Key))
			: Error;
		Panel->Refresh();
	};
	if (With.IsEmpty())
	{
		Bind(static_cast<int32>(EScctBindingCommit::Share));
		return;
	}

	const TSharedPtr<SOverlay> Host = SScctOptionsPage::ModalHost();
	if (!Host.IsValid())
	{
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Bindings: %s already does %s on the %s's keys; asking"), *Key, *With, SideName(bMerc));
	const TSharedRef<SScctBindingConflict> Ask = SNew(SScctBindingConflict)
		.Message(FString::Printf(TEXT("%s currently performs:\n%s\n\nReplace reassigns this key to %s. Share keeps its current actions as well.%s"),
			*ScctBindings::DisplayName(Key), *With, *Row.Label, bCanSwap ? TEXT(" Swap exchanges it with the key being changed.") : TEXT("")))
		.bCanSwap(bCanSwap)
		.Colour(SideColour(bMerc))
		.OnAnswer(Bind);
	Ask->Show(Host.ToSharedRef());
}

void SScctBindingsPanel::AskReset()
{
	const TSharedPtr<SOverlay> Host = SScctOptionsPage::ModalHost();
	if (!Host.IsValid())
	{
		return;
	}
	const bool bShared = !ScctBindings::IsSplit();
	const TWeakPtr<SScctBindingsPanel> Weak = SharedThis(this);
	SScctConfirm::Open(Host.ToSharedRef(),
		bShared ? TEXT("Put the shared spy and mercenary keys back to the defaults? You can still leave without saving afterwards.")
			: FString::Printf(TEXT("Put the %s keys back to the defaults? You can still leave without saving afterwards."), bMerc ? TEXT("mercenary's") : TEXT("spy's")),
		TEXT("RESET KEYS"), TEXT("CANCEL"), FSimpleDelegate::CreateLambda([Weak]
		{
			if (const TSharedPtr<SScctBindingsPanel> Panel = Weak.Pin())
			{
				ScctBindings::ResetKeys(Panel->bMerc);
				Panel->Status = TEXT("Keys back to the defaults. Apply or save to keep them.");
				Panel->Refresh();
			}
		}));
}
