#include "Menu/SScctHelpFeedbackPage.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Menu/ScctMenuDriver.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Runtime/ScctReportApi.h"
#include "Settings/ScctSettingsConsole.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	template <typename TRead>
	FString HelpFeedbackReadText(TRead&& Read)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(2048);
		const int32 Length = Read(Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Read(Buffer.GetData(), Buffer.Num());
		}
		Buffer.Last() = TEXT('\0');
		return FString(Buffer.GetData());
	}

	FString HelpFeedbackSetting(const TCHAR* Key)
	{
		FString Value, Error;
		return FScctSettingsConsole::Get(Key, Value, Error) ? Value : FString(TEXT("?"));
	}

	/** The settings summary under the bridge's details: what decides how the game draws and runs. */
	FString HelpFeedbackSettingsSummary()
	{
		const FString Fps = HelpFeedbackSetting(TEXT("Performance.MaxFps"));
		return FString::Printf(TEXT("Display: %s %s, vertical sync %s, frame limit %s\nGraphics: %s quality, render scale %s%%, anti-aliasing %s, renderer %s"),
			*HelpFeedbackSetting(TEXT("Display.WindowMode")), *HelpFeedbackSetting(TEXT("Display.Resolution")),
			HelpFeedbackSetting(TEXT("Display.VSync")) == TEXT("true") ? TEXT("on") : TEXT("off"), Fps == TEXT("0") ? TEXT("none") : *Fps,
			*HelpFeedbackSetting(TEXT("Graphics.Quality")), *HelpFeedbackSetting(TEXT("Graphics.RenderScale")),
			*HelpFeedbackSetting(TEXT("Graphics.AntiAliasing")), *FApp::GetGraphicsRHI());
	}

	/** The project's links (BridgeReportLink's order) with their captions and what they open. */
	struct FHelpFeedbackLink
	{
		int32 Link;
		const TCHAR* Caption;
		const TCHAR* Description;
	};

	const FHelpFeedbackLink HelpFeedbackLinks[] = {
		{ EScctReportLink::FeatureRequest, TEXT("SUGGEST A FEATURE"), TEXT("Opens a feature request on GitHub in your browser (GitHub sign-in).") },
		{ EScctReportLink::Issues, TEXT("BROWSE EXISTING ISSUES"), TEXT("Browse the project’s issue list on GitHub.") },
	};

	const FHelpFeedbackLink HelpFeedbackProjectLinks[] = {
		{ EScctReportLink::Documentation, TEXT("DOCUMENTATION"), TEXT("How to install, play, host and convert the content: the project's documentation in your browser.") },
		{ EScctReportLink::Contributing, TEXT("CONTRIBUTION GUIDE"), TEXT("How to contribute testing, translations and code.") },
		{ EScctReportLink::Development, TEXT("DEVELOPER GUIDE"), TEXT("Building OpenSCCT and finding your way around its code.") },
		{ EScctReportLink::PullRequests, TEXT("VIEW PULL REQUESTS"), TEXT("The changes others have proposed, on GitHub.") },
		{ EScctReportLink::NewPullRequest, TEXT("OPEN A PULL REQUEST"), TEXT("Propose a change of your own: make it on a branch or a fork first.") },
	};

	class SScctHelpFeedbackPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctHelpFeedbackPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			GameDetails = ScctHelpFeedback::GameDetails();
			TArray<FString> Lines;
			GameDetails.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctHelpFeedback: %s"), *Line);
			}

			const TSharedRef<SVerticalBox> Feedback = SNew(SVerticalBox);
			Feedback->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				ScctUi::Text(FText::FromString(TEXT("Feature requests and pull requests open in your browser and require a GitHub account.")),
					ScctUi::Small(), FSlateColor(ScctHudStyle::TextDim()), true)
			];
			if (Host.bInMatch)
			{
				Feedback->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					ScctUi::Text(FText::FromString(TEXT("The match goes on while this page is open.")), ScctUi::Small(), FSlateColor(ScctHudStyle::Danger()), true)
				];
			}
			TSharedPtr<SWidget> First;
			for (const FHelpFeedbackLink& Link : HelpFeedbackLinks)
			{
				const int32 Which = Link.Link;
				const TSharedRef<SScctButton> Button = SNew(SScctButton)
					.Look(Which == EScctReportLink::FeatureRequest ? EScctButtonLook::Primary : EScctButtonLook::Secondary)
					.FillWidth(true)
					.Text(FText::FromString(Link.Caption))
					.Description(FText::FromString(Link.Description))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Which] { OpenLink(Which); }));
				First = First.IsValid() ? First : TSharedPtr<SWidget>(Button);
				Feedback->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Button ];
			}
			Feedback->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 6.0f)
			[
				SNew(SScctButton)
				.FillWidth(true)
				.Text(FText::FromString(TEXT("CAPTURE GAME SCREENSHOT")))
				.Description(FText::FromString(TEXT("Save the game view without menus to Saved/Screenshots.")))
				.OnPressed(FSimpleDelegate::CreateSP(this, &SScctHelpFeedbackPage::CaptureScreenshot))
			];
			Feedback->AddSlot().AutoHeight()
			[
				SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true)
				.Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(Status); }))
			];

			const TSharedRef<SVerticalBox> Details = SNew(SVerticalBox);
			Details->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				ScctUi::Text(FText::FromString(TEXT("View and copy your current game and display settings. During a match, the details include your position and movement state.")),
					ScctUi::Small(), FSlateColor(ScctHudStyle::TextDim()), true)
			];
			Details->AddSlot().MaxHeight(230.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true)
					.Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(GameDetails); }))
				]
			];
			Details->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(SScctButton)
				.Text(FText::FromString(TEXT("COPY DETAILS")))
				.Description(FText::FromString(TEXT("Copies these details to the clipboard.")))
				.OnPressed(FSimpleDelegate::CreateSP(this, &SScctHelpFeedbackPage::CopyDetails))
			];

			const TSharedRef<SVerticalBox> Project = SNew(SVerticalBox);
			for (const FHelpFeedbackLink& Link : HelpFeedbackProjectLinks)
			{
				const int32 Which = Link.Link;
				Project->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(SScctButton)
					.FillWidth(true)
					.Text(FText::FromString(Link.Caption))
					.Description(FText::FromString(Link.Description))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Which] { OpenLink(Which); }))
				];
			}
			Project->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				ScctUi::Text(FText::FromString(TEXT("To contribute code, make your changes on a branch or a fork first, then open a pull request on GitHub.")),
					ScctUi::Small(), FSlateColor(ScctHudStyle::TextDim()), true)
			];

			ChildSlot
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(640.0f).Padding(0.0f, 0.0f, 24.0f, 0.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot().Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Card(TEXT("Feedback"), Feedback) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)[ ScctUi::Card(TEXT("Game details"), Details) ]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(520.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Card(TEXT("Project"), Project) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)[ ScctUi::Card(FString(), ScctUi::DescriptionPane(), FMargin(26.0f, 24.0f)) ]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 16.0f, 0.0f, 0.0f)
					[
						SNew(SScctButton)
						.Text(FText::FromString(TEXT("BACK")))
						.Description(FText::FromString(Host.bInMatch ? TEXT("Back to the menu.") : TEXT("Back to the main menu.")))
						.OnPressed(FSimpleDelegate::CreateLambda([this] { if (Host.Back) { Host.Back(); } }))
					]
				]
			];
			DefaultFocus = First;
		}

		virtual FString GetTitle() const override { return TEXT("Help & feedback"); }
		virtual FString GetKicker() const override { return ScctUi::BuildLabel(); }

	private:
		/** Nobody can look at a browser in a scripted or unattended run: the address goes to the log instead. */
		static bool CanOpenBrowser()
		{
			return !FApp::IsUnattended() && !FScctMenuDriver::IsScripted();
		}

		void Say(const FString& Message, bool bToast = true)
		{
			Status = Message;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHelpFeedback: %s"), *Message);
			if (bToast && Host.Dialogs.IsValid())
			{
				Host.Dialogs->Toast(Message);
			}
		}

		void OpenLink(int32 Link)
		{
			const FScctReportApi* Api = ScctFeature<FScctReportApi>();
			const FString Url = Api == nullptr ? FString()
				: HelpFeedbackReadText([Api, Link](TCHAR* Buffer, int32 Capacity) { return Api->GetUrl(Link, Buffer, Capacity); });
			if (Url.IsEmpty())
			{
				Say(TEXT("This link is unavailable."));
				return;
			}
			if (!CanOpenBrowser())
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctHelpFeedback: would open %s (not opened in an unattended or scripted run)"), *Url);
				Status = TEXT("Link logged (not opened in an unattended run).");
				return;
			}
			FString Error;
			FPlatformProcess::LaunchURL(*Url, nullptr, &Error);
			if (Error.IsEmpty())
			{
				Say(TEXT("Opened in your browser."), false);
			}
			else
			{
				// The address is copied so it can still be pasted into a browser.
				FPlatformApplicationMisc::ClipboardCopy(*Url);
				Say(FString::Printf(TEXT("Could not open your browser (%s); the address is copied to the clipboard."), *Error));
			}
		}

		void CaptureScreenshot()
		{
			if (FScreenshotRequest::IsScreenshotRequested())
			{
				return;
			}
			FScreenshotRequest::RequestScreenshot(false, true);
			FScreenshotRequest::OnScreenshotRequestProcessed().AddSP(this, &SScctHelpFeedbackPage::ScreenshotSaved, FScreenshotRequest::GetFilename());
		}

		void ScreenshotSaved(FString Filename)
		{
			FScreenshotRequest::OnScreenshotRequestProcessed().RemoveAll(this);
			if (IFileManager::Get().FileSize(*Filename) > 0)
			{
				Say(FString::Printf(TEXT("Screenshot saved to %s."), *FPaths::ConvertRelativePathToFull(Filename)));
			}
			else
			{
				Say(TEXT("Could not save the screenshot."));
			}
		}

		void CopyDetails()
		{
			FPlatformApplicationMisc::ClipboardCopy(*GameDetails);
			Say(TEXT("The details are copied to the clipboard."));
		}

		FString GameDetails;
		FString Status;
	};
}

FString ScctHelpFeedback::GameDetails()
{
	const FScctReportApi* Api = ScctFeature<FScctReportApi>();
	const FString Details = Api == nullptr ? FString(TEXT("Source: OpenSCCT game\n(game details unavailable)"))
		: HelpFeedbackReadText([Api](TCHAR* Buffer, int32 Capacity) { return Api->GetDetails(Buffer, Capacity); });
	return Details + TEXT("\n") + HelpFeedbackSettingsSummary();
}

TSharedRef<SScctMenuPage> ScctHelpFeedback::MakePage(const FScctMenuHost& Host)
{
	return SNew(SScctHelpFeedbackPage, Host);
}
