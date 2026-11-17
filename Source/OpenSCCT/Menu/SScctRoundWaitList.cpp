#include "Menu/SScctRoundWaitList.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

void SScctRoundWaitList::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;

	static const FSlateColorBrush Wash(FLinearColor::White);
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bShowing ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))

		// The loading picture over the whole screen, darkened so the list reads over its bright places.
		+ SOverlay::Slot()
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFill)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return PictureBrush.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
			[
				SNew(SImage).Image_Lambda([this] { return PictureBrush.Get(); })
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(&Wash).ColorAndOpacity(FLinearColor(0.0f, 0.004f, 0.01f, 0.45f))
		]

		// Loading_Menu: the title a fifth of the way down, the list under it.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 140.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.MinDesiredWidth(520.0f)
			[
				Ui::Glass(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 20.0f, 200))
						.ColorAndOpacity(ScctHudStyle::Text())
						.Text_Lambda([this] { return Title; })
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(Lines, SVerticalBox)
					], FMargin(30.0f, 18.0f))
			]
		]
	];
}

void SScctRoundWaitList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const UScctOnlineSubsystem* Visit = Online.Get();
	TArray<FScctRoundWaitLine> Wait;
	const bool bWaiting = Visit != nullptr && Visit->GetRoundWaitList(Wait) && Wait.Num() > 0;
	if (!bWaiting)
	{
		if (bShowing)
		{
			bShowing = false;
			ShownKey.Reset();
			UE_LOG(LogOpenSCCT, Log, TEXT("Round waiting list: closed (state %d)"), Visit != nullptr ? Visit->GetStatus().State : -1);
		}
		return;
	}

	// The server's pick among the map's loading pictures: every player of the session sees the same one. Asked once
	// as the list opens (the round's map and the seed do not change while it loads).
	if (!bShowing)
	{
		const FString Picture = Visit->GetRoundText(EScctRoundText::LoadingPicture);
		if (Picture != PicturePath)
		{
			ShowPicture(Picture);
		}
	}

	int32 Ready = 0;
	int32 Expected = 0;
	FString Key;
	for (const FScctRoundWaitLine& Line : Wait)
	{
		const EScctRoundWaitState State = static_cast<EScctRoundWaitState>(Line.State);
		Ready += State == EScctRoundWaitState::Ready ? 1 : 0;
		Expected += State != EScctRoundWaitState::TimedOut ? 1 : 0;
		Key += FString::Printf(TEXT("|%s%d"), Line.Name, Line.State);
	}
	Key = FString::Printf(TEXT("%d/%d"), Ready, Expected) + Key;
	bShowing = true;
	if (Key == ShownKey)
	{
		return;
	}
	ShownKey = Key;
	Title = Ui::ToText(FString::Printf(TEXT("Waiting for players (%d / %d)"), Ready, Expected));
	Lines->ClearChildren();
	for (const FScctRoundWaitLine& Line : Wait)
	{
		const EScctRoundWaitState State = static_cast<EScctRoundWaitState>(Line.State);
		const FString Name = Line.Name;
		// Loading_Menu's own captions (GUI.int): ReadyText, LoadingText, ConnectingText, TimedOutText.
		const FString Text = State == EScctRoundWaitState::Ready ? FString::Printf(TEXT(" -%s is ready"), *Name)
			: State == EScctRoundWaitState::Loading ? FString::Printf(TEXT(" -%s is connecting"), *Name)
			: State == EScctRoundWaitState::Joining ? FString::Printf(TEXT(" -%s Loading..."), *Name)
			: FString::Printf(TEXT(" -%s: Player timed out..."), *Name);
		const FLinearColor Colour = State == EScctRoundWaitState::Ready ? ScctHudStyle::Accent(Line.Team == 1 ? 1 : 0)
			: State == EScctRoundWaitState::TimedOut ? ScctHudStyle::TextFaint() : ScctHudStyle::Text();
		Lines->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)[ Ui::Text(Ui::ToText(Text), 13.0f, FSlateColor(Colour)) ];
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Round waiting list: %s; %s"), *Title.ToString(), *Key);

	// -ScctShotWaitList: one screenshot each time the list changes (it is up for a moment only on a fast machine, too
	// short for the -ScctShots interval), into -ScctShotDir.
	static const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("ScctShotWaitList"));
	if (bShot)
	{
		FString Directory = FPaths::ProjectSavedDir() / TEXT("ScctShots");
		FParse::Value(FCommandLine::Get(), TEXT("ScctShotDir="), Directory);
		static int32 ShotIndex = 0;
		FScreenshotRequest::RequestScreenshot(Directory / FString::Printf(TEXT("WaitList_%02d.png"), ShotIndex++), true, false);
	}
}

void SScctRoundWaitList::ShowPicture(const FString& Path)
{
	PicturePath = Path;
	PictureBrush.Reset();
	PictureTexture.Reset();
	if (Path.IsEmpty() || !FPaths::FileExists(Path))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Round waiting list: no loading picture (%s)"), Path.IsEmpty() ? TEXT("the map has none") : *Path);
		return;
	}
	UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Path);
	if (Texture == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Round waiting list: the loading picture could not be read (%s)"), *Path);
		return;
	}
	PictureTexture.Reset(Texture);
	PictureBrush = MakeShared<FSlateBrush>();
	PictureBrush->SetResourceObject(Texture);
	PictureBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	UE_LOG(LogOpenSCCT, Log, TEXT("Round waiting list: loading picture %s (%dx%d, seed %d)"), *FPaths::GetCleanFilename(Path), Texture->GetSizeX(), Texture->GetSizeY(),
		Online.IsValid() ? Online->GetStatus().LoadingPictureSeed : -1);
}
