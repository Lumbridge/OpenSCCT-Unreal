#include "Play/ScctLocalPlayer.h"

#include "Engine/GameViewportClient.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"
#include "HAL/IConsoleManager.h"
#include "Menu/ScctMenuKit.h"
#include "Misc/StringOutputDevice.h"
#include "Play/ScctConsoleCommands.h"
#include "Widgets/SViewport.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
	class FScctConsoleShortcut : public IInputProcessor
	{
	public:
		explicit FScctConsoleShortcut(UGameViewportClient* InViewport) : Viewport(InViewport) {}
		virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& Slate, const FPointerEvent&) override { return CapturesPointer(Slate); }
		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& Slate, const FPointerEvent&) override { return CapturesPointer(Slate); }
		virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& Slate, const FPointerEvent&, const FPointerEvent*) override { return CapturesPointer(Slate); }

		virtual bool HandleKeyDownEvent(FSlateApplication& Slate, const FKeyEvent& Event) override
		{
			UGameViewportClient* Client = Viewport.Get();
			if (!Client || !Client->ViewportConsole || Event.IsControlDown() || Event.IsAltDown() || Event.IsCommandDown())
			{
				return false;
			}
			const TArray<FKey>& Keys = GetDefault<UInputSettings>()->ConsoleKeys;
			bool bToggle = Keys.Contains(Event.GetKey());
#if PLATFORM_WINDOWS
			// Godot uses the physical key left of 1. On a UK keyboard it can report a different virtual key.
			if (!bToggle && Keys.Contains(EKeys::Tilde))
			{
				bToggle = Event.GetKeyCode() == ::MapVirtualKeyW(0x29, MAPVK_VSC_TO_VK_EX);
			}
#endif
			if (!bToggle) return false;
			const TSharedPtr<SViewport> Widget = Client->GetGameViewportWidget();
			if (!Widget || (!Widget->HasKeyboardFocus() && !Widget->HasFocusedDescendants())) return false;

			UConsole* Console = Client->ViewportConsole;
			const auto Focus = Slate.GetKeyboardFocusedWidget();
			if (!Console->ConsoleActive() && (ScctUi::IsTyping() ||
				(Focus && Focus->GetType() == FName(TEXT("SScctKeyRow")) && StaticCastSharedPtr<SScctKeyRow>(Focus)->IsCapturing()))) return false;
			if (Event.IsRepeat()) return true;
			if (Console->ConsoleActive())
			{
				Console->FakeGotoState(NAME_None);
			}
			else
			{
				// The typing state saves menu focus and gives the viewport the keys. Open shows command output too.
				Console->StartTyping(TEXT(""));
				Console->FakeGotoState(TEXT("Open"));
			}
			return true;
		}
	private:
		bool CapturesPointer(FSlateApplication& Slate) const
		{
			const UGameViewportClient* Client = Viewport.Get();
			return Client && Client->ViewportConsole && Client->ViewportConsole->ConsoleActive() &&
				Client->GetWindow() == Slate.GetActiveTopLevelWindow();
		}
		TWeakObjectPtr<UGameViewportClient> Viewport;
	};
}

void UScctLocalPlayer::PlayerAdded(UGameViewportClient* InViewport, FPlatformUserId InUserId)
{
	Super::PlayerAdded(InViewport, InUserId);
	if (InViewport && FSlateApplication::IsInitialized() && !ConsoleShortcut)
	{
		if (!InViewport->ViewportConsole)
		{
			InViewport->ViewportConsole = NewObject<UScctGameConsole>(InViewport);
		}
		ConsoleShortcut = MakeShared<FScctConsoleShortcut>(InViewport);
		FSlateApplication::Get().RegisterInputPreProcessor(ConsoleShortcut, 0);
	}
}

void UScctLocalPlayer::PlayerRemoved()
{
	if (ConsoleShortcut && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(ConsoleShortcut);
	}
	ConsoleShortcut.Reset();
	Super::PlayerRemoved();
}

void UScctGameConsole::ConsoleCommand(const FString& Command)
{
	const FString Line = Command.TrimStartAndEnd();
	HistoryBuffer.Remove(Line);
	HistoryBuffer.Add(Line);
	if (HistoryBuffer.Num() > MAX_HISTORY_ENTRIES) HistoryBuffer.RemoveAt(0);
	OutputText(TEXT("> ") + Line);
	FStringOutputDevice Reply;
	Reply.SetAutoEmitLineTerminator(true);
	UWorld* World = GetOuterUGameViewportClient()->GetWorld();
	if (!ScctConsoleCommands::ExecSettingCommand(*Line, World, Reply) && !ScctConsoleCommands::ExecGameCommand(*Line, World, Reply))
	{
		FString Verb = Line;
		int32 Space;
		if (Verb.FindChar(TEXT(' '), Space)) Verb.LeftInline(Space);
		if (Verb.Equals(TEXT("Settings"), ESearchCase::IgnoreCase) || Verb.Equals(TEXT("Admin"), ESearchCase::IgnoreCase) ||
			Verb.Equals(TEXT("AdminLogin"), ESearchCase::IgnoreCase))
		{
			IConsoleManager::Get().ProcessUserConsoleInput(*Line, Reply, World);
		}
		else ScctConsoleCommands::SayUnknown(*Line, Reply);
	}
	OutputText(Reply.TrimEnd());
}

#if UE_ALLOW_EXEC_COMMANDS
bool UScctLocalPlayer::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (ScctConsoleCommands::ExecSettingCommand(Cmd, InWorld, Ar) || ScctConsoleCommands::ExecGameCommand(Cmd, InWorld, Ar))
	{
		return true;
	}
	if (Super::Exec(InWorld, Cmd, Ar))
	{
		return true;
	}
	if (FString(Cmd).TrimStartAndEnd().IsEmpty())
	{
		return false;
	}
	ScctConsoleCommands::SayUnknown(Cmd, Ar);
	return true;
}
#endif
