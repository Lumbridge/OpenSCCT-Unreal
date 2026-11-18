#include "Menu/SScctHelpFeedbackPage.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Menu/ScctMenuDriver.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
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

	const FHelpFeedbackLink HelpFeedbackReportLinks[] = {
		{ EScctReportLink::BugReport, TEXT("REPORT A BUG"), TEXT("Opens the OpenSCCT website's bug report form in your browser (Discord sign-in), with the details below filled in, and the screenshot attached when it is captured and ATTACH is on.") },
		{ EScctReportLink::FeatureRequest, TEXT("SUGGEST A FEATURE"), TEXT("Opens a feature request on GitHub in your browser (GitHub sign-in).") },
		{ EScctReportLink::Issues, TEXT("BROWSE EXISTING ISSUES"), TEXT("The bugs and requests already reported, on GitHub.") },
	};

	const FHelpFeedbackLink HelpFeedbackProjectLinks[] = {
		{ EScctReportLink::Documentation, TEXT("DOCUMENTATION"), TEXT("How to install, play, host and convert the content: the project's documentation in your browser.") },
		{ EScctReportLink::Contributing, TEXT("CONTRIBUTION GUIDE"), TEXT("How to help: reporting, testing, translating and code.") },
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
			Report = ScctHelpFeedback::ReportText();
			Status = TEXT("Capture the game view first if a picture shows the problem: REPORT A BUG then attaches it.");
			TArray<FString> Lines;
			Report.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctHelpFeedback: report: %s"), *Line);
			}

			const TSharedRef<SVerticalBox> Feedback = SNew(SVerticalBox);
			Feedback->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				ScctUi::Text(FText::FromString(TEXT("Links open your browser. Bug reports use the OpenSCCT website and Discord sign-in; feature requests and pull requests use GitHub sign-in.")),
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
			for (const FHelpFeedbackLink& Link : HelpFeedbackReportLinks)
			{
				const int32 Which = Link.Link;
				const TSharedRef<SScctButton> Button = SNew(SScctButton)
					.Look(Which == EScctReportLink::BugReport ? EScctButtonLook::Primary : EScctButtonLook::Secondary)
					.FillWidth(true)
					.Text(FText::FromString(Link.Caption))
					.Description(FText::FromString(Link.Description))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Which] { Which == EScctReportLink::BugReport ? ReportBug() : OpenLink(Which, FString()); }));
				First = First.IsValid() ? First : TSharedPtr<SWidget>(Button);
				Feedback->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Button ];
			}
			Feedback->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 6.0f)
			[
				SNew(SScctButton)
				.FillWidth(true)
				.Text(FText::FromString(TEXT("CAPTURE GAME SCREENSHOT")))
				.Description(FText::FromString(TEXT("Saves a picture of the game view without the menus (in Saved/Screenshots). It is only sent when you press REPORT A BUG with ATTACH on, and you can remove it in the form before publishing.")))
				.OnPressed(FSimpleDelegate::CreateSP(this, &SScctHelpFeedbackPage::CaptureScreenshot))
			];
			Feedback->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SScctToggle)
				.Label(FText::FromString(TEXT("Attach the screenshot to the bug report")))
				.Description(FText::FromString(TEXT("On: REPORT A BUG uploads the captured screenshot to the OpenSCCT website first, and the form opens with it attached.")))
				.Get([this] { return bAttach; })
				.Set([this](bool bValue) { bAttach = bValue; })
			];
			Feedback->AddSlot().AutoHeight()
			[
				SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true)
				.Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(Status); }))
			];

			const TSharedRef<SVerticalBox> Details = SNew(SVerticalBox);
			Details->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				ScctUi::Text(FText::FromString(TEXT("These details are filled into the report form for you to review, with your position and movement state when you report from a map. No logs, account details or server addresses are attached.")),
					ScctUi::Small(), FSlateColor(ScctHudStyle::TextDim()), true)
			];
			Details->AddSlot().MaxHeight(230.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true)
					.Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(Report); }))
				]
			];
			Details->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(SScctButton)
				.Text(FText::FromString(TEXT("COPY DETAILS")))
				.Description(FText::FromString(TEXT("Copies the details to the clipboard, to paste into a report or a message.")))
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
					.OnPressed(FSimpleDelegate::CreateLambda([this, Which] { OpenLink(Which, FString()); }))
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
						+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Card(TEXT("Tell us about it"), Feedback) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)[ ScctUi::Card(TEXT("Report details"), Details) ]
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

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SScctMenuPage::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			TickScreenshot();
			TickUpload();
		}

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

		void OpenLink(int32 Link, const FString& Capture)
		{
			const FScctReportApi* Api = ScctFeature<FScctReportApi>();
			const FString Url = Api == nullptr ? FString()
				: HelpFeedbackReadText([Api, Link, &Capture](TCHAR* Buffer, int32 Capacity) { return Api->GetUrl(Link, Capture.IsEmpty() ? nullptr : *Capture, Buffer, Capacity); });
			if (Url.IsEmpty())
			{
				Say(TEXT("Could not make the link: the bridge has no report table."));
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

		void ReportBug()
		{
			if (!bAttach || Png.IsEmpty())
			{
				OpenLink(EScctReportLink::BugReport, FString());
				return;
			}
			if (bUploading)
			{
				return;
			}
			if (!CanOpenBrowser())
			{
				// The upload is the player's own choice, made with a click: never in a run nobody watches.
				Say(TEXT("The screenshot is not uploaded in an unattended run."), false);
				OpenLink(EScctReportLink::BugReport, FString());
				return;
			}
			const FScctReportApi* Api = ScctFeature<FScctReportApi>();
			if (Api == nullptr || Api->BeginUpload(Png.GetData(), Png.Num()) == 0)
			{
				Say(TEXT("Could not attach the screenshot. Try REPORT A BUG again, or turn ATTACH off to go on without it."));
				return;
			}
			bUploading = true;
			Say(TEXT("Attaching the screenshot…"), false);
		}

		void TickUpload()
		{
			if (!bUploading)
			{
				return;
			}
			const FScctReportApi* Api = ScctFeature<FScctReportApi>();
			if (Api == nullptr)
			{
				bUploading = false;
				return;
			}
			TCHAR Buffer[256] = {};
			const int32 State = Api->PollUpload(Buffer, UE_ARRAY_COUNT(Buffer));
			Buffer[UE_ARRAY_COUNT(Buffer) - 1] = TEXT('\0');
			if (State == EScctReportUpload::Running)
			{
				return;
			}
			bUploading = false;
			if (State == EScctReportUpload::Done)
			{
				Say(TEXT("Screenshot attached. Review it in the browser before publishing."), false);
				OpenLink(EScctReportLink::BugReport, Buffer);
			}
			else
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHelpFeedback: the screenshot upload failed: %s"), Buffer);
				Say(TEXT("Could not attach the screenshot. Try REPORT A BUG again, or turn ATTACH off to go on without it."));
			}
		}

		void CaptureScreenshot()
		{
			if (bCapturing)
			{
				return;
			}
			const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir());
			IFileManager::Get().MakeDirectory(*Folder, true);
			ShotFile = Folder / FString::Printf(TEXT("report-%s.png"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
			// The scene alone: the menus and the HUD are not in it.
			FScreenshotRequest::RequestScreenshot(ShotFile, false, false);
			bCapturing = true;
			CaptureWait = 0;
			LastSize = -1;
			Say(TEXT("Capturing the game view…"), false);
		}

		void TickScreenshot()
		{
			if (!bCapturing)
			{
				return;
			}
			++CaptureWait;
			const int64 Size = IFileManager::Get().FileSize(*ShotFile);
			if (Size <= 0 || Size != LastSize)
			{
				LastSize = Size;
				if (CaptureWait > 600)
				{
					bCapturing = false;
					Say(TEXT("Could not save the screenshot. You can still report the bug without it."));
				}
				return;
			}
			bCapturing = false;
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *ShotFile))
			{
				Say(TEXT("Could not save the screenshot. You can still report the bug without it."));
				return;
			}
			int32 Width = 0, Height = 0;
			FImage Image;
			if (FImageUtils::DecompressImage(Bytes.GetData(), Bytes.Num(), Image))
			{
				Width = Image.SizeX;
				Height = Image.SizeY;
			}
			// The website takes up to 2 MB: a bigger picture is made smaller until it fits.
			while (Bytes.Num() > ScctReportMaxScreenshotBytes && Width > 320 && Image.SizeX > 0)
			{
				FImage Colours;
				Image.CopyTo(Colours, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
				const int32 NewWidth = Width * 3 / 4, NewHeight = Height * 3 / 4;
				TArray<FColor> Smaller;
				Smaller.SetNumUninitialized(NewWidth * NewHeight);
				const TArrayView64<FColor> Source = Colours.AsBGRA8();
				FImageUtils::ImageResize(Width, Height, TArrayView<const FColor>(Source.GetData(), static_cast<int32>(Source.Num())), NewWidth, NewHeight, TArrayView<FColor>(Smaller), false);
				TArray64<uint8> Compressed;
				FImageUtils::PNGCompressImageArray(NewWidth, NewHeight, TArrayView64<const FColor>(Smaller.GetData(), Smaller.Num()), Compressed);
				Bytes = TArray<uint8>(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
				Width = NewWidth;
				Height = NewHeight;
				Image = FImage(Width, Height, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
				FMemory::Memcpy(Image.RawData.GetData(), Smaller.GetData(), Smaller.Num() * sizeof(FColor));
			}
			if (Bytes.Num() > ScctReportMaxScreenshotBytes)
			{
				Say(TEXT("The screenshot is too big to attach. You can still report the bug without it."));
				return;
			}
			Png = MoveTemp(Bytes);
			bAttach = true;
			Say(FString::Printf(TEXT("Screenshot ready (%d x %d, %.1f MB, %s). REPORT A BUG attaches it; you can remove it in the form before publishing."),
				Width, Height, Png.Num() / (1024.0f * 1024.0f), *FPaths::GetCleanFilename(ShotFile)));
		}

		void CopyDetails()
		{
			FPlatformApplicationMisc::ClipboardCopy(*Report);
			Say(TEXT("The details are copied to the clipboard."));
		}

		FString Report;
		FString Status;
		FString ShotFile;
		TArray<uint8> Png;
		bool bAttach = false;
		bool bCapturing = false;
		bool bUploading = false;
		int32 CaptureWait = 0;
		int64 LastSize = -1;
	};
}

FString ScctHelpFeedback::ReportText()
{
	const FScctReportApi* Api = ScctFeature<FScctReportApi>();
	const FString Details = Api == nullptr ? FString(TEXT("Source: OpenSCCT game\n(the bridge's report table is not available)"))
		: HelpFeedbackReadText([Api](TCHAR* Buffer, int32 Capacity) { return Api->GetDetails(Buffer, Capacity); });
	return Details + TEXT("\n") + HelpFeedbackSettingsSummary();
}

TSharedRef<SScctMenuPage> ScctHelpFeedback::MakePage(const FScctMenuHost& Host)
{
	return SNew(SScctHelpFeedbackPage, Host);
}
