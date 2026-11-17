#include "Hud/ScctQuickChat.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/ScctChatSubsystem.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudSubsystem.h"
#include "Hud/SScctQuickChatPanel.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctGameMode.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Widgets/Layout/SSafeZone.h"

namespace
{
	/** Over the chat (20), under the match menu (30). */
	constexpr int32 QuickChatZOrder = 21;

	const FScctQuickChatCategory QuickChatCategories[] =
	{
		{ TEXT("Attack"), { TEXT("Attack the objective!"), TEXT("I'm moving in."), TEXT("Cover me!"), TEXT("Follow me!") } },
		{ TEXT("Defend"), { TEXT("Defend the objective!"), TEXT("Hold this position."), TEXT("Watch our flank!"), TEXT("Fall back!") } },
		{ TEXT("Information"), { TEXT("Enemy spotted!"), TEXT("Enemy missing!"), TEXT("Need help!"), TEXT("On my way!") } },
		{ TEXT("Responses"), { TEXT("Yes!"), TEXT("No!"), TEXT("Thanks!"), TEXT("Sorry!") } },
	};

	/** The choice a key picks (0..3), or -1: 1-4 on the number row or the keypad, the D-pad up, right, down, left. */
	int32 QuickChatChoiceOf(const FKey& Key)
	{
		static const FKey Keys[][3] =
		{
			{ EKeys::One, EKeys::NumPadOne, EKeys::Gamepad_DPad_Up },
			{ EKeys::Two, EKeys::NumPadTwo, EKeys::Gamepad_DPad_Right },
			{ EKeys::Three, EKeys::NumPadThree, EKeys::Gamepad_DPad_Down },
			{ EKeys::Four, EKeys::NumPadFour, EKeys::Gamepad_DPad_Left },
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Keys); ++Index)
		{
			for (const FKey& Candidate : Keys[Index])
			{
				if (Key == Candidate)
				{
					return Index;
				}
			}
		}
		return -1;
	}

	/** The keys that go back a step: Esc, Backspace, and the controller's B, Back and Start (as Godot's menu). */
	bool QuickChatIsBackKey(const FKey& Key)
	{
		return Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right
			|| Key == EKeys::Gamepad_Special_Left || Key == EKeys::Gamepad_Special_Right;
	}

	/** The keyboard key of a choice, for the unattended presses. */
	FKey QuickChatDigitKey(int32 Index)
	{
		static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
		return Digits[FMath::Clamp(Index, 0, 3)];
	}
}

/**
 * Sees every key before Slate routes it (and so before the viewport hands it to the player's input): the keys the open
 * menu reads are eaten here, the rest go on to the game as usual.
 */
class FScctQuickChatInput : public IInputProcessor
{
public:
	explicit FScctQuickChatInput(UScctQuickChatSubsystem* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		UScctQuickChatSubsystem* Subsystem = Owner.Get();
		return Subsystem != nullptr && Subsystem->HandleKeyDown(InKeyEvent.GetKey(), InKeyEvent.IsRepeat());
	}

	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		UScctQuickChatSubsystem* Subsystem = Owner.Get();
		return Subsystem != nullptr && Subsystem->HandleKeyUp(InKeyEvent.GetKey());
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("ScctQuickChat"); }

private:
	TWeakObjectPtr<UScctQuickChatSubsystem> Owner;
};

const FScctQuickChatCategory& FScctQuickChatState::Category(int32 Index)
{
	return QuickChatCategories[FMath::Clamp(Index, 0, NumChoices - 1)];
}

bool FScctQuickChatState::Open(double Now)
{
	if (bOpen || Now < NextSend)
	{
		return false;
	}
	bOpen = true;
	CategoryIndex = -1;
	Expires = Now + TimeoutSeconds;
	return true;
}

TOptional<FString> FScctQuickChatState::Choose(int32 Index, double Now)
{
	if (!bOpen || Expire(Now) || Index < 0 || Index >= NumChoices)
	{
		return {};
	}
	if (CategoryIndex < 0)
	{
		CategoryIndex = Index;
		Expires = Now + TimeoutSeconds;
		return {};
	}
	FString Message = Category(CategoryIndex).Messages[Index];
	NextSend = Now + CooldownSeconds;
	Close();
	return Message;
}

void FScctQuickChatState::Back()
{
	if (CategoryIndex >= 0)
	{
		CategoryIndex = -1;
	}
	else
	{
		Close();
	}
}

bool FScctQuickChatState::Expire(double Now)
{
	if (!bOpen || Now < Expires)
	{
		return false;
	}
	Close();
	return true;
}

void FScctQuickChatState::Close()
{
	bOpen = false;
	CategoryIndex = -1;
}

bool UScctQuickChatSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UScctQuickChatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctQuickChatSubsystem, STATGROUP_Tickables);
}

void UScctQuickChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UScctHudSubsystem>();
	Collection.InitializeDependency<UScctChatSubsystem>();

	FString Debug;
	// Not stopping at the comma: the value is C[,P].
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctQuickChat="), Debug, /*bShouldStopOnSeparator*/ false))
	{
		FString CategoryText = Debug, PhraseText;
		Debug.Split(TEXT(","), &CategoryText, &PhraseText);
		DebugCategory = FCString::Atoi(*CategoryText);
		DebugPhrase = PhraseText.IsEmpty() ? 0 : FCString::Atoi(*PhraseText);
		if (DebugCategory < 1 || DebugCategory > 4 || DebugPhrase < 0 || DebugPhrase > 4)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctQuickChat: '%s' is not a category and phrase (1-4, then 1-4)"), *Debug);
			DebugCategory = 0;
		}
		FParse::Value(FCommandLine::Get(), TEXT("ScctQuickChatAt="), DebugAt);
		FParse::Value(FCommandLine::Get(), TEXT("ScctQuickChatHold="), DebugHold);
		DebugHold = FMath::Max(DebugHold, 0.1f);
	}
}

void UScctQuickChatSubsystem::Deinitialize()
{
	if (CommandSource.IsValid())
	{
		CommandSource->OnLocalCommand.Remove(CommandHandle);
	}
	CommandSource.Reset();
	RemovePanel();
	Super::Deinitialize();
}

bool UScctQuickChatSubsystem::EnsurePanel()
{
	if (ViewportContent.IsValid())
	{
		return true;
	}
	// Only a level that plays a match has the quick chat.
	UWorld* World = GetWorld();
	if (World == nullptr || Cast<AScctGameMode>(World->GetAuthGameMode()) == nullptr)
	{
		return false;
	}
	APlayerController* Player = World->GetFirstPlayerController();
	UGameViewportClient* Viewport = World->GetGameViewport();
	if (Player == nullptr || !Player->IsLocalController() || Viewport == nullptr || !FSlateApplication::IsInitialized())
	{
		return false;
	}

	TWeakObjectPtr<UScctQuickChatSubsystem> WeakThis(this);
	SAssignNew(Panel, SScctQuickChatPanel)
		.State(&State)
		.LocalTeam_Lambda([WeakThis]()
		{
			const UScctHudSubsystem* Hud = WeakThis.IsValid() ? WeakThis->GetWorld()->GetSubsystem<UScctHudSubsystem>() : nullptr;
			return Hud != nullptr ? Hud->GetModel().LocalTeam : 0;
		})
		.Clock_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->Clock : 0.0; });
	ViewportContent = SNew(SSafeZone).IsTitleSafe(true)[ Panel.ToSharedRef() ];
	Viewport->AddViewportWidgetContent(ViewportContent.ToSharedRef(), QuickChatZOrder);

	InputProcessor = MakeShared<FScctQuickChatInput>(this);
	FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor, 0);
	return true;
}

void UScctQuickChatSubsystem::RemovePanel()
{
	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}
	InputProcessor.Reset();
	if (ViewportContent.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameViewportClient* Viewport = World->GetGameViewport())
			{
				Viewport->RemoveViewportWidgetContent(ViewportContent.ToSharedRef());
			}
		}
	}
	ViewportContent.Reset();
	Panel.Reset();
}

bool UScctQuickChatSubsystem::IsBlocked() const
{
	const AScctPlayerController* Player = CommandSource.Get();
	const UScctChatSubsystem* Chat = GetWorld()->GetSubsystem<UScctChatSubsystem>();
	return Player == nullptr || Player->IsMenuOpen() || (Chat != nullptr && Chat->IsOpen());
}

void UScctQuickChatSubsystem::Tick(float DeltaTime)
{
	Clock += DeltaTime;
	if (!EnsurePanel())
	{
		return;
	}

	// QuickChat comes from the player's bindings.
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player != nullptr && CommandSource.Get() != Player)
	{
		if (CommandSource.IsValid())
		{
			CommandSource->OnLocalCommand.Remove(CommandHandle);
		}
		CommandSource = Player;
		CommandHandle = Player->OnLocalCommand.AddUObject(this, &UScctQuickChatSubsystem::HandleLocalCommand);
	}

	if (State.IsOpen())
	{
		if (State.Expire(Clock))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctQuickChat: the menu closed by itself after %.0f s without a choice"), FScctQuickChatState::TimeoutSeconds);
		}
		else if (IsBlocked())
		{
			Close(TEXT("a menu or the chat took the keys"));
		}
	}
	RunUnattended(DeltaTime);
}

void UScctQuickChatSubsystem::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	if (!bPressed || Name != TEXT("QuickChat"))
	{
		return;
	}
	if (State.IsOpen())
	{
		Close(TEXT("QuickChat was pressed again"));
	}
	else
	{
		Open();
	}
}

bool UScctQuickChatSubsystem::Open()
{
	if (!EnsurePanel() || State.IsOpen())
	{
		return State.IsOpen();
	}
	if (IsBlocked())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctQuickChat: not opened, a menu or the chat has the keys"));
		return false;
	}
	if (!State.Open(Clock))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctQuickChat: not opened, a line went out less than %.1f s ago"), FScctQuickChatState::CooldownSeconds);
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctQuickChat: the menu opened on the categories (Attack, Defend, Information, Responses)"));
	return true;
}

void UScctQuickChatSubsystem::Close(const TCHAR* Why)
{
	if (!State.IsOpen())
	{
		return;
	}
	State.Close();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctQuickChat: the menu closed (%s)"), Why);
}

void UScctQuickChatSubsystem::Choose(int32 Index)
{
	if (!State.IsOpen())
	{
		return;
	}
	const int32 CategoryBefore = State.GetCategory();
	const TOptional<FString> Message = State.Choose(Index, Clock);
	if (State.IsOpen())
	{
		if (CategoryBefore < 0 && State.GetCategory() >= 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctQuickChat: category %d, %s, its phrases are listed"), Index + 1, FScctQuickChatState::Category(Index).Name);
		}
		return;
	}
	if (!Message.IsSet())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctQuickChat: the menu closed by itself after %.0f s without a choice"), FScctQuickChatState::TimeoutSeconds);
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctQuickChat: phrase %d of %s, \"%s\", goes to the team"), Index + 1, FScctQuickChatState::Category(CategoryBefore).Name, **Message);
	UScctChatSubsystem* Chat = GetWorld()->GetSubsystem<UScctChatSubsystem>();
	if (Chat == nullptr || !Chat->Send(*Message, /*bTeam*/ true))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctQuickChat: \"%s\" could not be sent"), **Message);
	}
}

void UScctQuickChatSubsystem::Back()
{
	if (!State.IsOpen())
	{
		return;
	}
	const bool bFromPhrases = State.GetCategory() >= 0;
	State.Back();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctQuickChat: %s"), bFromPhrases ? TEXT("back to the categories") : TEXT("the menu closed (back)"));
}

bool UScctQuickChatSubsystem::HandleKeyDown(const FKey& Key, bool bRepeat)
{
	if (!State.IsOpen())
	{
		return false;
	}
	const int32 Choice = QuickChatChoiceOf(Key);
	const bool bBack = QuickChatIsBackKey(Key);
	if (Choice < 0 && !bBack)
	{
		// Every other key is the game's: the player keeps moving, looking and firing.
		return false;
	}
	EatenKeys.Add(Key);
	if (!bRepeat)
	{
		if (Choice >= 0)
		{
			Choose(Choice);
		}
		else
		{
			Back();
		}
	}
	return true;
}

bool UScctQuickChatSubsystem::HandleKeyUp(const FKey& Key)
{
	return EatenKeys.Remove(Key) > 0;
}

void UScctQuickChatSubsystem::RunUnattended(float DeltaTime)
{
	if (DebugCategory <= 0 || DebugStep > 2)
	{
		return;
	}
	if (!bDebugStarted)
	{
		const UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
		if (Session == nullptr || !Session->IsPlaying() || Session->FindLocalProxy() == nullptr || !CommandSource.IsValid())
		{
			return;
		}
		bDebugStarted = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctQuickChat: the match began; the menu opens in %.1f s"), DebugAt);
	}
	DebugClock += DeltaTime;
	const float StepAt = DebugAt + DebugHold * DebugStep;
	if (DebugClock < StepAt)
	{
		return;
	}

	// The presses go through Slate as a keyboard's, so they meet the preprocessor that eats them.
	auto Press = [this](const FKey& Key)
	{
		if (!FSlateApplication::IsInitialized())
		{
			HandleKeyDown(Key, false);
			HandleKeyUp(Key);
			return;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		Slate.ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		const bool bEaten = EatenKeys.Contains(Key);
		Slate.ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctQuickChat: pressed %s (%s)"), *Key.ToString(), bEaten ? TEXT("taken by the menu, the game never saw it") : TEXT("passed on to the game"));
	};
	switch (DebugStep++)
	{
	case 0:
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctQuickChat: opening the menu at %.1f s"), DebugClock);
		Open();
		break;
	case 1:
		Press(QuickChatDigitKey(DebugCategory - 1));
		break;
	case 2:
		if (DebugPhrase > 0)
		{
			Press(QuickChatDigitKey(DebugPhrase - 1));
		}
		break;
	}
}
