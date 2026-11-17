#include "Menu/ScctLoadingSubsystem.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Hud/ScctHudStyle.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "MoviePlayer.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const FrontEndLevelName = TEXT("L_FrontEnd");

	/** A text of the play table, whatever its length. */
	template <typename TCall>
	FString ReadText(TCall&& Call)
	{
		TCHAR Small[512];
		const int32 Length = Call(Small, static_cast<int32>(UE_ARRAY_COUNT(Small)));
		if (Length < static_cast<int32>(UE_ARRAY_COUNT(Small)))
		{
			return FString(Small);
		}
		TArray<TCHAR> Long;
		Long.SetNumZeroed(Length + 1);
		Call(Long.GetData(), Long.Num());
		return FString(Long.GetData());
	}

	/** The mode as the menus name it, from AScctGameMode's ?Mode= option. */
	FString ModeLabel(const FString& Mode)
	{
		if (Mode.Equals(TEXT("DiskHunt"), ESearchCase::IgnoreCase)) { return TEXT("DISK HUNT"); }
		if (Mode.Equals(TEXT("Deathmatch"), ESearchCase::IgnoreCase)) { return TEXT("DEATHMATCH"); }
		if (Mode.Equals(TEXT("MercDeathMatch"), ESearchCase::IgnoreCase)) { return TEXT("MERC DEATHMATCH"); }
		return TEXT("STORY");
	}

	/**
	 * The page. It is drawn by the movie player's own thread while the game thread loads, so it holds everything it
	 * draws with (its brushes) and binds nothing to game objects.
	 */
	class SScctLoadingPage : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SScctLoadingPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctLoadingPage& Page)
		{
			using namespace ScctHudStyle;
			const FLinearColor Colour = Page.Side == 0 || Page.Side == 1 ? Accent(Page.Side) : Accent(0);
			const bool bPicture = Page.Picture != nullptr;
			if (bPicture)
			{
				PictureBrush.SetResourceObject(Page.Picture);
				PictureBrush.ImageSize = FVector2D(Page.Picture->GetSizeX(), Page.Picture->GetSizeY());
				PictureBrush.DrawAs = ESlateBrushDrawType::Image;
			}

			const TSharedRef<SVerticalBox> Words = SNew(SVerticalBox);
			Words->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SCircularThrobber)
					.Radius(8.0f)
					.NumPieces(8)
					.Period(1.1f)
					.ColorAndOpacity(Colour)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("LOADING")))
					.Font(Label(11.0f))
					.ColorAndOpacity(Colour)
				]
			];
			if (!Page.Title.IsEmpty())
			{
				Words->AddSlot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Page.Title.ToUpper()))
					.Font(Font(EWeight::Light, 46.0f, 180))
					.ColorAndOpacity(Text())
					.AutoWrapText(true)
				];
				Words->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(2.0f, 14.0f, 0.0f, 14.0f)
				[
					SNew(SBox).WidthOverride(120.0f).HeightOverride(2.0f)
					[
						SNew(SImage).Image(&WhiteBrush).ColorAndOpacity(Colour)
					]
				];
			}
			if (!Page.Detail.IsEmpty())
			{
				Words->AddSlot().AutoHeight().Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Page.Detail.ToUpper()))
					.Font(Label(10.5f))
					.ColorAndOpacity(TextDim())
				];
			}
			if (!Page.Tip.IsEmpty())
			{
				Words->AddSlot().AutoHeight().Padding(0.0f, 44.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.BorderImage(&PanelBrush)
					.BorderBackgroundColor(FLinearColor(0.004f, 0.007f, 0.011f, 0.72f))
					.Padding(FMargin(22.0f, 16.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("TIP")))
							.Font(Label(8.5f))
							.ColorAndOpacity(Fade(Colour, 0.9f))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Page.Tip))
							.Font(Font(EWeight::Regular, 13.0f))
							.ColorAndOpacity(Text())
							.AutoWrapText(true)
						]
					]
				];
			}

			const TSharedRef<SOverlay> Layers = SNew(SOverlay);
			Layers->AddSlot()[ SNew(SImage).Image(&WhiteBrush).ColorAndOpacity(FLinearColor(0.004f, 0.006f, 0.009f, 1.0f)) ];
			if (bPicture)
			{
				// The map's own picture, large and dark, behind everything: the page has the map's colours.
				Layers->AddSlot()
				[
					SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFill)
					[
						SNew(SImage).Image(&PictureBrush).ColorAndOpacity(FLinearColor(0.16f, 0.16f, 0.16f, 1.0f))
					]
				];
			}
			Layers->AddSlot()
			[
				SNew(SSafeZone)
				.IsTitleSafe(true)
				[
					SNew(SBox)
					.Padding(FMargin(96.0f, 72.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Bottom)
						[
							SNew(SBox).MaxDesiredWidth(820.0f).HAlign(HAlign_Left)[ Words ]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(64.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(460.0f)
							.HeightOverride(460.0f)
							.Visibility(bPicture ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
							[
								SNew(SBorder)
								.BorderImage(&FrameBrush)
								.BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.22f))
								.Padding(FMargin(1.0f))
								[
									SNew(SImage).Image(&PictureBrush)
								]
							]
						]
					]
				]
			];

			SetVisibility(EVisibility::HitTestInvisible);
			ChildSlot[ Layers ];
		}

	private:
		FSlateBrush PictureBrush;
		FSlateColorBrush WhiteBrush = FSlateColorBrush(FLinearColor::White);
		FSlateRoundedBoxBrush PanelBrush = FSlateRoundedBoxBrush(FLinearColor::White, ScctHudStyle::Corner, FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), 1.0f);
		FSlateRoundedBoxBrush FrameBrush = FSlateRoundedBoxBrush(FLinearColor::Transparent, 0.0f, FLinearColor::White, 1.0f);
	};
}

void UScctLoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (IsRunningDedicatedServer() || IsRunningCommandlet())
	{
		return;
	}

	FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &UScctLoadingSubsystem::HandlePreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UScctLoadingSubsystem::HandlePostLoadMap);
	if (IGameMoviePlayer* Player = GetMoviePlayer())
	{
		// The movie player asks for a page when a level load starts and none was set up for it.
		PrepareHandle = Player->OnPrepareLoadingScreen().AddUObject(this, &UScctLoadingSubsystem::HandlePrepareLoadingScreen);
	}

	// The engine empties a travel's URL before the load begins (UEngine::Browse) and tells the load only the map's
	// name, so the options (side, mode, the online mark) are noted while the travel waits: at the end of the frame
	// it was asked for in (from game code, Slate or a ticker), before the next engine tick starts it.
	FCoreDelegates::OnEndFrame.AddUObject(this, &UScctLoadingSubsystem::NoteTravel);
}

void UScctLoadingSubsystem::NoteTravel()
{
	if (GEngine == nullptr)
	{
		return;
	}
	for (const FWorldContext& Each : GEngine->GetWorldContexts())
	{
		if (Each.WorldType == EWorldType::Game && !Each.TravelURL.IsEmpty())
		{
			PendingTravelURL = Each.TravelURL;
		}
	}
}

void UScctLoadingSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	if (IGameMoviePlayer* Player = GetMoviePlayer())
	{
		Player->OnPrepareLoadingScreen().Remove(PrepareHandle);
	}
	FTSTicker::GetCoreTicker().RemoveTicker(HoldTicker);
	FCoreDelegates::OnEndFrame.RemoveAll(this);
	if (HeldPage.IsValid() && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HeldPage.ToSharedRef());
	}
	HeldPage.Reset();
	Super::Deinitialize();
}

TSharedRef<SWidget> UScctLoadingSubsystem::MakePage(const FScctLoadingPage& Page)
{
	return SNew(SScctLoadingPage, Page);
}

FScctLoadingPage UScctLoadingSubsystem::DescribeLoad(const FWorldContext& Context, const FString& MapName)
{
	FScctLoadingPage Page;
	const FString Level = FPackageName::GetShortName(MapName);
	if (Level.Equals(FrontEndLevelName, ESearchCase::IgnoreCase))
	{
		// On the way back to the menu: nothing to say but that it loads.
		return Page;
	}

	// The options the level is opened with: the travel's URL (noted while it waited), or the command line's for
	// the first level of a run.
	FString Url = !Context.TravelURL.IsEmpty() ? Context.TravelURL : PendingTravelURL;
	PendingTravelURL.Reset();
	if (Url.IsEmpty())
	{
		const TCHAR* CommandLine = FCommandLine::Get();
		FString First;
		if (FParse::Token(CommandLine, First, false) && !First.StartsWith(TEXT("-")))
		{
			Url = First;
		}
	}
	FString Options;
	int32 Question = INDEX_NONE;
	if (Url.FindChar(TEXT('?'), Question))
	{
		Options = Url.Mid(Question);
	}

	FString Map = UGameplayStatics::ParseOption(Options, TEXT("ScctMap"));
	if (Map.IsEmpty())
	{
		Map = Level;
	}
	const UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>();
	// A visit's travel into its round is online whatever was noted of its URL.
	const bool bOnline = UGameplayStatics::HasOption(Options, UScctOnlineSubsystem::OnlineOption)
		|| (Online != nullptr && Online->GetState() != EScctOnlineState::Idle);
	const bool bMerc = bOnline
		? Online != nullptr && Online->GetStatus().LocalTeam == 1
		: UGameplayStatics::ParseOption(Options, TEXT("Side")).Equals(TEXT("merc"), ESearchCase::IgnoreCase);
	const bool bWatching = bOnline && Online != nullptr && Online->GetStatus().LocalTeam != 0 && Online->GetStatus().LocalTeam != 1;

	Page.Side = bWatching ? -1 : (bMerc ? 1 : 0);
	Page.Title = Map;
	const TCHAR* SideName = bWatching ? TEXT("SPECTATOR") : (bMerc ? TEXT("MERCENARY") : TEXT("SPY"));
	Page.Detail = bOnline
		? FString::Printf(TEXT("%s   /   %s"), Online != nullptr && !Online->GetServerName().IsEmpty() ? *Online->GetServerName() : TEXT("ONLINE"), SideName)
		: FString::Printf(TEXT("%s   /   %s"), *ModeLabel(UGameplayStatics::ParseOption(Options, TEXT("Mode"))), SideName);

	// The map's name, picture and the side's tip are the player's own content, read through the bridge.
	FString Error;
	const FScctPlayApi* Play = ScctFeature<FScctPlayApi>(&Error);
	if (Play == nullptr)
	{
		return Page;
	}
	Page.Title = ReadText([&](TCHAR* Buffer, int32 Capacity) { return Play->GetMapTitle(*Map, Buffer, Capacity); });
	if (Page.Title.IsEmpty())
	{
		Page.Title = Map;
	}
	Page.Tip = ReadText([&](TCHAR* Buffer, int32 Capacity) { return Play->GetTip(bMerc ? 1 : 0, -1, Buffer, Capacity); });

	const FString File = ReadText([&](TCHAR* Buffer, int32 Capacity) { return Play->GetLoadingImage(*Map, bMerc ? 1 : 0, Buffer, Capacity); });
	if (!File.IsEmpty())
	{
		TObjectPtr<UTexture2D>& Picture = Pictures.FindOrAdd(File);
		if (Picture == nullptr)
		{
			Picture = FImageUtils::ImportFileAsTexture2D(File);
		}
		Page.Picture = Picture;
	}
	return Page;
}

void UScctLoadingSubsystem::HandlePreLoadMap(const FWorldContext& Context, const FString& MapName)
{
	if (Context.OwningGameInstance != GetGameInstance())
	{
		return;
	}
	Pending = DescribeLoad(Context, MapName);
	bHasPending = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("Loading page for %s: '%s', '%s', picture %s, tip \"%s\""), *MapName, *Pending.Title, *Pending.Detail,
		Pending.Picture != nullptr ? TEXT("yes") : TEXT("none"), *Pending.Tip.Left(120));

	// Set up here as well as on the movie player's own request: whichever of the two hears the load first, the
	// page is there when the player starts.
	HandlePrepareLoadingScreen();
}

void UScctLoadingSubsystem::HandlePrepareLoadingScreen()
{
	IGameMoviePlayer* Player = GetMoviePlayer();
	if (!bHasPending || Player == nullptr || Player->IsMovieCurrentlyPlaying())
	{
		return;
	}

	FLoadingScreenAttributes Screen;
	Screen.WidgetLoadingScreen = MakePage(Pending);
	// It goes when the level has begun play (the match has started by then), not on a key.
	Screen.bAutoCompleteWhenLoadingCompletes = true;
	Screen.bMoviesAreSkippable = false;
	Screen.bWaitForManualStop = false;
	Screen.bAllowEngineTick = false;
	Player->SetupLoadingScreen(Screen);
}

void UScctLoadingSubsystem::HandlePostLoadMap(UWorld* World)
{
	if (!bHasPending || World == nullptr || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	bHasPending = false;

	// -ScctLoadingHold=S: the same page stays over the picture for a while, for an unattended run's screenshot.
	float Hold = 0.0f;
	UGameViewportClient* Viewport = GEngine != nullptr ? GEngine->GameViewport : nullptr;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ScctLoadingHold="), Hold) || Hold <= 0.0f || Viewport == nullptr)
	{
		return;
	}
	if (HeldPage.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(HeldPage.ToSharedRef());
	}
	HeldPage = MakePage(Pending);
	Viewport->AddViewportWidgetContent(HeldPage.ToSharedRef(), 1000);
	UE_LOG(LogOpenSCCT, Log, TEXT("-ScctLoadingHold: the loading page stays up for %.1f s"), Hold);
	FTSTicker::GetCoreTicker().RemoveTicker(HoldTicker);
	HoldTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		if (HeldPage.IsValid() && GEngine != nullptr && GEngine->GameViewport != nullptr)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(HeldPage.ToSharedRef());
		}
		HeldPage.Reset();
		return false;
	}), Hold);
}
