// The Demos page (an OpenSCCT feature, docs/DEMOS.md), ported from the Godot client's DemosScreen and its Downloads and
// History parts: the demos on this computer (saved and downloaded) with a search, a storage filter and a sort, each one
// with Watch, Keep (a download), Delete and its final scoreboard; the master's recent online demos (signed in) to
// watch or keep; and the matches whose demos carry a scoreboard. The data is the bridge's demo library
// (Bridge/OpenScct.UnrealBridge/Demo/DemoLibrary.cs) through FScctDemoPlayApi; watching is Demo/ScctDemoPlayback.h.

#include "Menu/ScctMenuPage.h"

#include "Brushes/SlateColorBrush.h"
#include "Demo/ScctDemoPlayback.h"
#include "Demo/ScctDemoSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "Menu/ScctMenuStyle.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace DemosUi = ScctMenuStyle;

namespace
{
	constexpr float DemosPageWidth = 1080.0f;

	enum class EDemosTab : uint8 { Local, Online, History };
	enum class EDemosStorage : uint8 { All, Saved, Downloaded };
	enum class EDemosSort : uint8 { Newest, Oldest, Longest, Map };

	const TCHAR* const DemosStorageNames[] = { TEXT("ALL STORAGE"), TEXT("SAVED"), TEXT("DOWNLOADED") };
	const TCHAR* const DemosSortNames[] = { TEXT("NEWEST FIRST"), TEXT("OLDEST FIRST"), TEXT("LONGEST FIRST"), TEXT("BY MAP") };

	/** One demo of a collection as the page shows it. */
	struct FDemosRow
	{
		FScctDemoEntry Entry = {};
		int32 Index = 0;
		FString Path;
		FString FileName;
		FString Map;
		FString Server;
		FString Players;
		FString Version;
		FString ModeName;
		FString Outcome;
	};

	FString DemosReadText(TFunctionRef<int32(TCHAR*, int32)> Get)
	{
		TCHAR Short[512];
		const int32 Length = Get(Short, UE_ARRAY_COUNT(Short));
		if (Length <= 0)
		{
			return FString();
		}
		if (Length < UE_ARRAY_COUNT(Short))
		{
			return FString(Short);
		}
		TArray<TCHAR> Long;
		Long.SetNumZeroed(Length + 1);
		Get(Long.GetData(), Long.Num());
		return FString(Long.GetData());
	}

	FString DemosLibraryText(const FScctDemoPlayApi* Api, EScctDemoLibraryText Kind)
	{
		return Api != nullptr ? DemosReadText([Api, Kind](TCHAR* Buffer, int32 Capacity) { return Api->GetLibraryText(static_cast<int32>(Kind), Buffer, Capacity); }) : FString();
	}

	FString DemosDuration(int32 Seconds)
	{
		Seconds = FMath::Max(Seconds, 0);
		return Seconds >= 3600 ? FString::Printf(TEXT("%d:%02d:%02d"), Seconds / 3600, Seconds / 60 % 60, Seconds % 60) : FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
	}

	/** A Unix time in milliseconds as local "2026-11-17 19:15". */
	FString DemosDate(int64 UnixMs)
	{
		if (UnixMs <= 0)
		{
			return TEXT("unknown date");
		}
		const FDateTime Local = FDateTime::FromUnixTimestamp(UnixMs / 1000) + (FDateTime::Now() - FDateTime::UtcNow());
		return Local.ToString(TEXT("%Y-%m-%d %H:%M"));
	}

	FString DemosBytes(int64 Bytes)
	{
		return Bytes >= 1024 * 1024 ? FString::Printf(TEXT("%.1f MB"), Bytes / (1024.0 * 1024.0)) : FString::Printf(TEXT("%lld KB"), FMath::Max<int64>(1, Bytes / 1024));
	}

	FString DemosMapTitle(const FString& Map)
	{
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		const FString Title = Menu != nullptr ? Menu->GetMapTitle(Map) : FString();
		return Title.IsEmpty() ? Map : Title;
	}

	TSharedRef<SWidget> DemosLine(const FString& Text, float Size = 12.0f, const FLinearColor& Colour = ScctHudStyle::Text(), ScctHudStyle::EWeight Weight = ScctHudStyle::EWeight::Regular)
	{
		return DemosUi::Text(DemosUi::ToText(Text), Size, FSlateColor(Colour), Weight, true);
	}

	/** A card of the list: a dim panel. */
	TSharedRef<SWidget> DemosCard(const TSharedRef<SWidget>& Content)
	{
		static const FSlateColorBrush Panel(FLinearColor::White);
		return SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(&Panel).ColorAndOpacity(FLinearColor(0.02f, 0.035f, 0.05f, 0.55f)) ]
			+ SOverlay::Slot().Padding(14.0f, 12.0f)[ Content ];
	}

	/** The map's picture, or an empty frame of the same size. */
	TSharedRef<SWidget> DemosPicture(const FString& Map)
	{
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		const FSlateBrush* Brush = Menu != nullptr ? Menu->GetMapPicture(Map, false) : nullptr;
		if (Brush != nullptr)
		{
			return ScctUi::Picture(Brush, 168.0f, 94.0f);
		}
		static const FSlateColorBrush Blank(FLinearColor::White);
		return SNew(SBox).WidthOverride(168.0f).HeightOverride(94.0f)[ SNew(SImage).Image(&Blank).ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.05f)) ];
	}

	class SScctDemosMenuPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctDemosMenuPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			FString Tab;
			if (FParse::Value(FCommandLine::Get(), TEXT("ScctDemosTab="), Tab))
			{
				ShowSub(Tab);
			}

			SearchBox = DemosUi::TextBox(TEXT("search map, server, player or file"), FString(), false, FOnTextCommitted(),
				FOnTextChanged::CreateLambda([this](const FText& Text) { Search = Text.ToString().TrimStartAndEnd(); bDirty = true; }));

			const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

			// What is recorded, from the demo settings.
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				DemosUi::Text(TAttribute<FText>::CreateSP(this, &SScctDemosMenuPage::RecordingLine), 12.0f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
			];

			// The tabs.
			const TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
			const auto AddTab = [this, &Tabs](EDemosTab Which)
			{
				Tabs->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					DemosUi::Button(TAttribute<FText>::CreateLambda([this, Which] { return DemosUi::ToText(TabLabel(Which)); }),
						FOnClicked::CreateLambda([this, Which] { SetTab(Which); return FReply::Handled(); }),
						TAttribute<bool>::CreateLambda([this, Which] { return CurrentTab == Which; }))
				];
			};
			AddTab(EDemosTab::Local);
			AddTab(EDemosTab::Online);
			AddTab(EDemosTab::History);
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Tabs ];

			// The search and the filters (the demos on this computer and the history).
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this] { return CurrentTab == EDemosTab::Online ? EVisibility::Collapsed : EVisibility::Visible; })
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 8.0f, 0.0f)[ SearchBox.ToSharedRef() ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					DemosUi::Button(TAttribute<FText>::CreateLambda([this] { return DemosUi::ToText(DemosStorageNames[static_cast<int32>(Storage)]); }),
						FOnClicked::CreateLambda([this] { Storage = static_cast<EDemosStorage>((static_cast<int32>(Storage) + 1) % 3); bDirty = true; return FReply::Handled(); }),
						false, DemosUi::Accent(), 150.0f)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					DemosUi::Button(TAttribute<FText>::CreateLambda([this] { return DemosUi::ToText(DemosSortNames[static_cast<int32>(Sort)]); }),
						FOnClicked::CreateLambda([this] { Sort = static_cast<EDemosSort>((static_cast<int32>(Sort) + 1) % 4); bDirty = true; return FReply::Handled(); }),
						false, DemosUi::Accent(), 150.0f)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					DemosUi::Button(DemosUi::ToText(TEXT("RESET")), FOnClicked::CreateLambda([this]
					{
						Storage = EDemosStorage::All;
						Sort = EDemosSort::Newest;
						Search.Reset();
						SearchBox->SetText(FText::GetEmpty());
						bDirty = true;
						return FReply::Handled();
					}))
				]
			];

			// The count of what is shown.
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				DemosUi::Text(TAttribute<FText>::CreateLambda([this] { return DemosUi::ToText(CountLine); }), 11.5f, FSlateColor(ScctHudStyle::TextFaint()))
			];

			Box->AddSlot().AutoHeight()
			[
				SNew(SBox).MaxDesiredHeight(560.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()[ SAssignNew(ListBox, SBox) ]
				]
			];

			// The library's last message: kept, deleted, downloading, an error.
			Box->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 12.0f)
			[
				DemosUi::Text(TAttribute<FText>::CreateSP(this, &SScctDemosMenuPage::StatusLine),
					12.0f, TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(bStatusIsError ? ScctHudStyle::Danger() : DemosUi::Accent()); }), ScctHudStyle::EWeight::Regular, true)
			];

			Box->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					DemosUi::Button(DemosUi::ToText(TEXT("OPEN FOLDER")), FOnClicked::CreateLambda([this] { OpenFolder(); return FReply::Handled(); }))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					DemosUi::Button(DemosUi::ToText(TEXT("REFRESH")), FOnClicked::CreateLambda([this] { Refresh(); return FReply::Handled(); }))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
				[
					DemosUi::Button(DemosUi::ToText(TEXT("BACK")), FOnClicked::CreateLambda([this] { if (Host.Back) { Host.Back(); } return FReply::Handled(); }))
				]
			];

			ChildSlot
			[
				SNew(SBox).WidthOverride(DemosPageWidth)[ DemosUi::Glass(Box, FMargin(22.0f, 18.0f)) ]
			];

			Refresh();
		}

		virtual FString GetTitle() const override { return TEXT("DEMOS"); }
		virtual FString GetKicker() const override { return TEXT("Recorded matches"); }
		virtual bool HasTabs() const override { return true; }

		virtual void StepTab(int32 Direction) override
		{
			SetTab(static_cast<EDemosTab>((static_cast<int32>(CurrentTab) + (Direction < 0 ? 2 : 1)) % 3));
		}

		virtual void ShowSub(const FString& Sub) override
		{
			if (Sub.Equals(TEXT("online"), ESearchCase::IgnoreCase) || Sub.Equals(TEXT("recent"), ESearchCase::IgnoreCase))
			{
				SetTab(EDemosTab::Online);
			}
			else if (Sub.Equals(TEXT("history"), ESearchCase::IgnoreCase))
			{
				SetTab(EDemosTab::History);
			}
			else
			{
				SetTab(EDemosTab::Local);
			}
		}

		virtual void GetHints(TArray<TPair<FString, FString>>& OutKeyAndWhat, TArray<FString>& OutPadKeys) const override
		{
			OutKeyAndWhat.Add({ TEXT("R"), TEXT("Refresh") });
		}

		virtual bool HandleShortcut(const FKey& Key) override
		{
			if (Key == EKeys::R)
			{
				Refresh();
				return true;
			}
			return false;
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SScctMenuPage::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			const FScctDemoPlayApi* Api = UScctDemoPlayback::Api();
			if (Api == nullptr)
			{
				return;
			}

			FScctDemoLibraryInfo Now = {};
			Api->GetLibraryInfo(&Now);
			if (Now.Revision != Library.Revision || Now.OnlineState != Library.OnlineState || Now.DownloadPercent != Library.DownloadPercent || Now.Flags != Library.Flags)
			{
				Library = Now;
				ReadRows();
				bDirty = true;
			}

			// A download meant for watching finished: play it.
			if ((Now.Flags & EScctDemoLibraryFlags::WatchReady) != 0)
			{
				const FString Path = DemosReadText([Api](TCHAR* Buffer, int32 Capacity) { return Api->TakeWatchPath(Buffer, Capacity); });
				if (!Path.IsEmpty())
				{
					Watch(Path);
				}
			}

			if (bDirty)
			{
				bDirty = false;
				Rebuild();
			}
		}

	private:
		UScctDemoPlayback* Playback() const
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			UGameInstance* Instance = Visit != nullptr ? Visit->GetGameInstance() : nullptr;
			return Instance != nullptr ? Instance->GetSubsystem<UScctDemoPlayback>() : nullptr;
		}

		FString TabLabel(EDemosTab Which) const
		{
			switch (Which)
			{
			case EDemosTab::Local: return FString::Printf(TEXT("ON THIS COMPUTER (%d)"), LocalRows.Num());
			case EDemosTab::Online: return Library.OnlineState == static_cast<int32>(EScctDemoOnlineState::Ready) ? FString::Printf(TEXT("RECENT ONLINE (%d)"), Library.OnlineTotal) : FString(TEXT("RECENT ONLINE"));
			default:
			{
				int32 WithBoard = 0;
				for (const FDemosRow& Row : LocalRows)
				{
					WithBoard += (Row.Entry.Flags & EScctDemoEntryFlags::HasScoreboard) != 0 ? 1 : 0;
				}
				return FString::Printf(TEXT("MATCH HISTORY (%d)"), WithBoard);
			}
			}
		}

		void SetTab(EDemosTab Which)
		{
			CurrentTab = Which;
			if (Which == EDemosTab::Online && Library.OnlineState == static_cast<int32>(EScctDemoOnlineState::NotLoaded))
			{
				if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
				{
					Api->RequestOnline(0);
				}
			}
			bDirty = true;
		}

		FText RecordingLine() const
		{
			const FString Offline = UScctDemoSubsystem::SavesOffline() ? TEXT("on") : TEXT("off");
			const FString Online = UScctDemoSubsystem::SavesOnline() ? TEXT("on") : TEXT("off");
			return DemosUi::ToText(FString::Printf(TEXT("Recording offline matches: %s  ·  online rounds: %s  (Settings, Demos)"), *Offline, *Online));
		}

		FText StatusLine() const
		{
			if (!PageStatus.IsEmpty())
			{
				return DemosUi::ToText(PageStatus);
			}
			FString Text = DemosLibraryText(UScctDemoPlayback::Api(), EScctDemoLibraryText::Status);
			if ((Library.Flags & EScctDemoLibraryFlags::Downloading) != 0 && Library.DownloadPercent >= 0)
			{
				Text = FString::Printf(TEXT("Downloading… %d%%"), Library.DownloadPercent);
			}
			return DemosUi::ToText(Text);
		}

		void Refresh()
		{
			PageStatus.Reset();
			bStatusIsError = false;
			UScctDemoSubsystem::PushChoice();
			if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
			{
				Api->RefreshLocal();
				Api->GetLibraryInfo(&Library);
				if (CurrentTab == EDemosTab::Online && Library.OnlineState != static_cast<int32>(EScctDemoOnlineState::Loading))
				{
					Api->RequestOnline(Library.OnlinePage);
				}
			}
			else
			{
				PageStatus = TEXT("Demos cannot be listed: the simulation did not start (see the log).");
				bStatusIsError = true;
			}
			ReadRows();
			bDirty = true;
		}

		void ReadRows()
		{
			const FScctDemoPlayApi* Api = UScctDemoPlayback::Api();
			bStatusIsError = (Library.Flags & EScctDemoLibraryFlags::StatusIsError) != 0 || (!PageStatus.IsEmpty() && bStatusIsError);
			const auto Read = [Api](EScctDemoCollection Collection, int32 Count, TArray<FDemosRow>& Out)
			{
				Out.Reset();
				if (Api == nullptr || Count <= 0)
				{
					return;
				}
				TArray<FScctDemoEntry> Entries;
				Entries.SetNumZeroed(Count);
				const int32 Got = Api->GetEntries(static_cast<int32>(Collection), Entries.GetData(), Entries.Num());
				for (int32 Index = 0; Index < Got; ++Index)
				{
					FDemosRow& Row = Out.AddDefaulted_GetRef();
					Row.Entry = Entries[Index];
					Row.Index = Index;
					const auto Text = [Api, Collection, Index](EScctDemoEntryText Kind)
					{
						return DemosReadText([&](TCHAR* Buffer, int32 Capacity) { return Api->GetEntryText(static_cast<int32>(Collection), Index, static_cast<int32>(Kind), Buffer, Capacity); });
					};
					Row.Path = Text(EScctDemoEntryText::Path);
					Row.FileName = Text(EScctDemoEntryText::FileName);
					Row.Map = Text(EScctDemoEntryText::Map);
					Row.Server = Text(EScctDemoEntryText::Server);
					Row.Players = Text(EScctDemoEntryText::Players);
					Row.Version = Text(EScctDemoEntryText::GameVersion);
					Row.ModeName = Text(EScctDemoEntryText::ModeName);
					Row.Outcome = Text(EScctDemoEntryText::Outcome);
				}
			};
			Read(EScctDemoCollection::Local, Library.LocalCount, LocalRows);
			Read(EScctDemoCollection::Online, Library.OnlineCount, OnlineRows);
		}

		/** The local rows the search, the storage filter and the sort leave, in order. */
		TArray<const FDemosRow*> Filtered(bool bHistory) const
		{
			TArray<const FDemosRow*> Out;
			for (const FDemosRow& Row : LocalRows)
			{
				const bool bCached = (Row.Entry.Flags & EScctDemoEntryFlags::Cached) != 0;
				if (bHistory && (Row.Entry.Flags & EScctDemoEntryFlags::HasScoreboard) == 0)
				{
					continue;
				}
				if ((Storage == EDemosStorage::Saved && bCached) || (Storage == EDemosStorage::Downloaded && !bCached))
				{
					continue;
				}
				if (!Search.IsEmpty())
				{
					const FString Haystack = Row.Map + TEXT(" ") + DemosMapTitle(Row.Map) + TEXT(" ") + Row.Server + TEXT(" ") + Row.Players + TEXT(" ") + Row.FileName + TEXT(" ") + Row.ModeName;
					if (!Haystack.Contains(Search, ESearchCase::IgnoreCase))
					{
						continue;
					}
				}
				Out.Add(&Row);
			}
			Out.StableSort([this](const FDemosRow& A, const FDemosRow& B)
			{
				switch (Sort)
				{
				case EDemosSort::Oldest: return A.Entry.StartUnixMs < B.Entry.StartUnixMs;
				case EDemosSort::Longest: return A.Entry.DurationSeconds > B.Entry.DurationSeconds;
				case EDemosSort::Map:
				{
					const int32 Order = DemosMapTitle(A.Map).Compare(DemosMapTitle(B.Map), ESearchCase::IgnoreCase);
					return Order != 0 ? Order < 0 : A.Entry.StartUnixMs > B.Entry.StartUnixMs;
				}
				default: return A.Entry.StartUnixMs > B.Entry.StartUnixMs;
				}
			});
			return Out;
		}

		void Rebuild()
		{
			if (!ListBox.IsValid())
			{
				return;
			}
			const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
			const auto Add = [&List](const TSharedRef<SWidget>& Widget)
			{
				List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Widget ];
			};

			switch (CurrentTab)
			{
			case EDemosTab::Local:
			case EDemosTab::History:
			{
				const bool bHistory = CurrentTab == EDemosTab::History;
				const TArray<const FDemosRow*> Rows = Filtered(bHistory);
				CountLine = FString::Printf(TEXT("%d of %d shown  ·  %s"), Rows.Num(), bHistory ? Filtered(true).Num() : LocalRows.Num(),
					*DemosLibraryText(UScctDemoPlayback::Api(), EScctDemoLibraryText::SavedFolder));
				if (bHistory)
				{
					CountLine = FString::Printf(TEXT("%d demos with a final scoreboard  ·  every round you played, recorded or not, is in Match history"), Rows.Num());
					Add(DemosCard(SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							DemosLine(TEXT("The scoreboard of every round you play is saved even without a demo, and your account's rounds on the master are listed with them."), 13.0f, ScctHudStyle::TextDim())
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							DemosUi::Button(DemosUi::ToText(TEXT("MATCH HISTORY")), FOnClicked::CreateLambda([this] { if (Host.Open) { Host.Open(TEXT("history")); } return FReply::Handled(); }))
						]));
				}
				if (Rows.Num() == 0)
				{
					Add(DemosCard(DemosLine(LocalRows.Num() == 0
						? TEXT("No demos yet. Matches are recorded when recording is on in Settings, Demos; downloaded online demos appear here too.")
						: bHistory ? TEXT("No demo here carries a final scoreboard. A round that ends while recording saves one.")
						: TEXT("No demo matches the search and filters. RESET shows them all."), 13.0f, ScctHudStyle::TextDim())));
				}
				for (const FDemosRow* Row : Rows)
				{
					Add(LocalCard(*Row, bHistory));
				}
				break;
			}
			case EDemosTab::Online:
				CountLine = Library.OnlineState == static_cast<int32>(EScctDemoOnlineState::Ready)
					? FString::Printf(TEXT("Page %d of %d  ·  %d recent demos on the master"), Library.OnlinePage + 1, FMath::Max(1, Library.OnlinePages), Library.OnlineTotal)
					: FString();
				BuildOnline(List);
				break;
			}
			ListBox->SetContent(List);
		}

		TSharedRef<SWidget> LocalCard(const FDemosRow& Row, bool bHistory)
		{
			const bool bCached = (Row.Entry.Flags & EScctDemoEntryFlags::Cached) != 0;
			const bool bPlayable = (Row.Entry.Flags & EScctDemoEntryFlags::Playable) != 0;
			const bool bBoard = (Row.Entry.Flags & EScctDemoEntryFlags::HasScoreboard) != 0;
			const int32 Index = Row.Index;
			const FString Path = Row.Path;
			const FString FileName = Row.FileName;

			const TSharedRef<SVerticalBox> Details = SNew(SVerticalBox);
			Details->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				DemosLine(DemosMapTitle(Row.Map).ToUpper() + (bCached ? TEXT("   · DOWNLOADED, EXPIRES AFTER 7 DAYS UNLESS KEPT") : TEXT("")), 15.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Bold)
			];
			Details->AddSlot().AutoHeight()[ DemosLine(FString::Printf(TEXT("%s  ·  %s  ·  %s"), *Row.ModeName, *DemosDuration(Row.Entry.DurationSeconds), *DemosDate(Row.Entry.StartUnixMs)), 12.5f, ScctHudStyle::TextDim()) ];
			Details->AddSlot().AutoHeight()[ DemosLine(FString::Printf(TEXT("%s  ·  %d players: %s"), Row.Server.IsEmpty() ? TEXT("Offline match") : *Row.Server, Row.Entry.PlayerCount, Row.Players.IsEmpty() ? TEXT("none recorded") : *Row.Players), 12.0f, ScctHudStyle::TextDim()) ];
			if (!Row.Outcome.IsEmpty())
			{
				Details->AddSlot().AutoHeight()[ DemosLine(Row.Outcome, 12.0f, DemosUi::Accent()) ];
			}
			Details->AddSlot().AutoHeight()[ DemosLine(FString::Printf(TEXT("%s  ·  %s"), *DemosBytes(Row.Entry.Bytes), *FileName), 11.0f, ScctHudStyle::TextFaint()) ];
			if (!bPlayable)
			{
				Details->AddSlot().AutoHeight()[ DemosLine(FString::Printf(TEXT("Recorded on %s, another game version: this build cannot play it."), Row.Version.IsEmpty() ? TEXT("an unknown version") : *Row.Version), 12.0f, ScctHudStyle::Danger()) ];
			}

			const TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
			const auto Action = [&Actions](const TSharedRef<SWidget>& Button) { Actions->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ Button ]; };
			const TSharedRef<SButton> WatchButton = DemosUi::Button(DemosUi::ToText(TEXT("WATCH")), FOnClicked::CreateLambda([this, Path] { Watch(Path); return FReply::Handled(); }), true);
			WatchButton->SetEnabled(bPlayable);
			Action(WatchButton);
			if (bCached)
			{
				Action(DemosUi::Button(DemosUi::ToText(TEXT("KEEP")), FOnClicked::CreateLambda([this, Index] { Keep(Index); return FReply::Handled(); })));
			}
			Action(DemosUi::Button(DemosUi::ToText(TEXT("DELETE")), FOnClicked::CreateLambda([this, Index, FileName] { AskDelete(Index, FileName); return FReply::Handled(); }), false, ScctHudStyle::Danger()));
			if (bBoard && !bHistory)
			{
				Action(DemosUi::Button(DemosUi::ToText(TEXT("SCOREBOARD")), FOnClicked::CreateLambda([this, Path]
				{
					ShownBoard = ShownBoard == Path ? FString() : Path;
					bDirty = true;
					return FReply::Handled();
				}), TAttribute<bool>::CreateLambda([this, Path] { return ShownBoard == Path; })));
			}
			Details->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[ Actions ];
			if (bBoard && (bHistory || ShownBoard == Path))
			{
				Details->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)[ Scoreboard(Index) ];
			}

			return DemosCard(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 14.0f, 0.0f)[ DemosPicture(Row.Map) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f)[ Details ]);
		}

		/** The demo's final scoreboard as a table, the spies first. */
		TSharedRef<SWidget> Scoreboard(int32 Index) const
		{
			const FScctDemoPlayApi* Api = UScctDemoPlayback::Api();
			TArray<FScctDemoScoreRow> Rows;
			Rows.SetNumZeroed(32);
			const int32 Count = Api != nullptr ? Api->GetScoreboard(Index, Rows.GetData(), Rows.Num()) : -1;
			if (Count < 0)
			{
				return DemosLine(TEXT("This demo carries no scoreboard."), 12.0f, ScctHudStyle::TextDim());
			}
			Rows.SetNum(Count);
			Rows.StableSort([](const FScctDemoScoreRow& A, const FScctDemoScoreRow& B) { return A.Team != B.Team ? A.Team < B.Team : A.Score > B.Score; });

			const TSharedRef<SVerticalBox> Table = SNew(SVerticalBox);
			const auto Line = [&Table](const FString& Name, const FString& Score, const FString& Kills, const FString& Deaths, const FString& Objectives, const FLinearColor& Colour, ScctHudStyle::EWeight Weight)
			{
				const auto Cell = [&](const FString& Text) { return DemosUi::Text(DemosUi::ToText(Text), 12.0f, FSlateColor(Colour), Weight); };
				Table->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[ Cell(Name) ]
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(70.0f).HAlign(HAlign_Right)[ Cell(Score) ] ]
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(70.0f).HAlign(HAlign_Right)[ Cell(Kills) ] ]
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(70.0f).HAlign(HAlign_Right)[ Cell(Deaths) ] ]
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(100.0f).HAlign(HAlign_Right)[ Cell(Objectives) ] ]
				];
			};
			Line(TEXT("PLAYER"), TEXT("SCORE"), TEXT("KILLS"), TEXT("DEATHS"), TEXT("OBJECTIVES"), ScctHudStyle::TextFaint(), ScctHudStyle::EWeight::Bold);
			for (const FScctDemoScoreRow& Row : Rows)
			{
				FString Name = FString::Printf(TEXT("%s · %s"), Row.Name, Row.Team == 1 ? TEXT("Merc") : TEXT("Spy"));
				if ((Row.Flags & EScctDemoScoreFlags::Bot) != 0) { Name += TEXT(" (bot)"); }
				if ((Row.Flags & EScctDemoScoreFlags::LeftEarly) != 0) { Name += TEXT(" (left)"); }
				Line(Name, FString::FromInt(Row.Score), FString::FromInt(Row.Kills), FString::FromInt(Row.Deaths),
					Row.Objectives < 0 ? FString(TEXT("-")) : FString::FromInt(Row.Objectives), ScctHudStyle::Accent(Row.Team == 1 ? 1 : 0), ScctHudStyle::EWeight::Regular);
			}
			return Table;
		}

		void BuildOnline(const TSharedRef<SVerticalBox>& List)
		{
			const auto Add = [&List](const TSharedRef<SWidget>& Widget) { List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Widget ]; };
			const auto Message = [&](const FString& Text, const TSharedPtr<SWidget>& Button = nullptr)
			{
				const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[ DemosLine(Text, 13.0f, ScctHudStyle::TextDim()) ];
				if (Button.IsValid())
				{
					Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 10.0f, 0.0f, 0.0f)[ Button.ToSharedRef() ];
				}
				Add(DemosCard(Box));
			};

			switch (static_cast<EScctDemoOnlineState>(Library.OnlineState))
			{
			case EScctDemoOnlineState::NoMaster:
				Message(TEXT("No master server is set, so there are no online demos to list."));
				return;
			case EScctDemoOnlineState::SignedOut:
				Message(TEXT("Recent online demos need an OpenSCCT account. Sign in on the Account page."),
					DemosUi::Button(DemosUi::ToText(TEXT("ACCOUNT")), FOnClicked::CreateLambda([this] { if (Host.Open) { Host.Open(TEXT("account")); } return FReply::Handled(); })));
				return;
			case EScctDemoOnlineState::NotLoaded:
			case EScctDemoOnlineState::Loading:
				Message(TEXT("Asking the master server for recent demos…"));
				return;
			case EScctDemoOnlineState::Failed:
				Message(TEXT("The recent demos could not be read: ") + DemosLibraryText(UScctDemoPlayback::Api(), EScctDemoLibraryText::OnlineError),
					DemosUi::Button(DemosUi::ToText(TEXT("RETRY")), FOnClicked::CreateLambda([this] { RequestPage(Library.OnlinePage); return FReply::Handled(); })));
				return;
			default:
				break;
			}

			if (OnlineRows.Num() == 0)
			{
				Message(TEXT("The master has no recent demos."));
			}
			const bool bBusy = (Library.Flags & EScctDemoLibraryFlags::Downloading) != 0;
			for (const FDemosRow& Row : OnlineRows)
			{
				const bool bPlayable = (Row.Entry.Flags & EScctDemoEntryFlags::Playable) != 0;
				const bool bExpired = (Row.Entry.Flags & EScctDemoEntryFlags::Expired) != 0;
				const int32 Index = Row.Index;

				const TSharedRef<SVerticalBox> Details = SNew(SVerticalBox);
				Details->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)[ DemosLine(DemosMapTitle(Row.Map).ToUpper(), 15.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Bold) ];
				Details->AddSlot().AutoHeight()[ DemosLine(FString::Printf(TEXT("%s  ·  %s  ·  ended %s"), *Row.ModeName, *DemosDuration(Row.Entry.DurationSeconds), *DemosDate(Row.Entry.StartUnixMs)), 12.5f, ScctHudStyle::TextDim()) ];
				Details->AddSlot().AutoHeight()[ DemosLine(FString::Printf(TEXT("%s  ·  %d players: %s"), *Row.Server, Row.Entry.PlayerCount, *Row.Players), 12.0f, ScctHudStyle::TextDim()) ];
				if (!Row.Outcome.IsEmpty())
				{
					Details->AddSlot().AutoHeight()[ DemosLine(Row.Outcome, 12.0f, DemosUi::Accent()) ];
				}
				Details->AddSlot().AutoHeight()
				[
					DemosLine(FString::Printf(TEXT("%s  ·  %s"), *DemosBytes(Row.Entry.Bytes), bExpired ? TEXT("no longer available") : *(TEXT("available until ") + DemosDate(Row.Entry.ExpiresUnixMs))), 11.0f, ScctHudStyle::TextFaint())
				];
				if (!bPlayable)
				{
					Details->AddSlot().AutoHeight()[ DemosLine(FString::Printf(TEXT("Recorded on %s, another game version: this build cannot play it."), *Row.Version), 12.0f, ScctHudStyle::Danger()) ];
				}
				const TSharedRef<SButton> WatchButton = DemosUi::Button(DemosUi::ToText(TEXT("WATCH")), FOnClicked::CreateLambda([this, Index] { Download(Index, false); return FReply::Handled(); }), true);
				const TSharedRef<SButton> KeepButton = DemosUi::Button(DemosUi::ToText(TEXT("DOWNLOAD & KEEP")), FOnClicked::CreateLambda([this, Index] { Download(Index, true); return FReply::Handled(); }));
				WatchButton->SetEnabled(bPlayable && !bExpired && !bBusy);
				KeepButton->SetEnabled(!bExpired && !bBusy);
				Details->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ WatchButton ]
					+ SHorizontalBox::Slot().AutoWidth()[ KeepButton ]
				];
				Add(DemosCard(SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 14.0f, 0.0f)[ DemosPicture(Row.Map) ]
					+ SHorizontalBox::Slot().FillWidth(1.0f)[ Details ]));
			}

			// The pager.
			const TSharedRef<SButton> Previous = DemosUi::Button(DemosUi::ToText(TEXT("PREVIOUS")), FOnClicked::CreateLambda([this] { RequestPage(Library.OnlinePage - 1); return FReply::Handled(); }));
			const TSharedRef<SButton> Next = DemosUi::Button(DemosUi::ToText(TEXT("NEXT")), FOnClicked::CreateLambda([this] { RequestPage(Library.OnlinePage + 1); return FReply::Handled(); }));
			Previous->SetEnabled(Library.OnlinePage > 0);
			Next->SetEnabled(Library.OnlinePage + 1 < Library.OnlinePages);
			Add(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ Previous ]
				+ SHorizontalBox::Slot().AutoWidth()[ Next ]);
		}

		void RequestPage(int32 Page)
		{
			if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
			{
				Api->RequestOnline(FMath::Clamp(Page, 0, FMath::Max(0, Library.OnlinePages - 1)));
				bDirty = true;
			}
		}

		void Watch(const FString& Path)
		{
			UScctDemoPlayback* Demo = Playback();
			if (Demo == nullptr)
			{
				PageStatus = TEXT("Demos cannot be watched from here.");
				bStatusIsError = true;
				return;
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("Demos page: watching %s"), *Path);
			if (!Demo->Watch(Path))
			{
				PageStatus = Demo->GetError();
				bStatusIsError = true;
			}
		}

		void Keep(int32 Index)
		{
			PageStatus.Reset();
			if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
			{
				Api->Keep(Index);
			}
		}

		void AskDelete(int32 Index, const FString& FileName)
		{
			const TWeakPtr<SWidget> Weak = AsShared();
			ScctUi::Confirm(Host.Dialogs, TEXT("DELETE DEMO"), FString::Printf(TEXT("Delete %s? The file is removed from this computer and cannot be brought back."), *FileName),
				TEXT("DELETE"), TEXT("CANCEL"), [this, Weak, Index]
				{
					if (!Weak.IsValid())
					{
						return;
					}
					PageStatus.Reset();
					if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
					{
						Api->Delete(Index);
					}
				}, true);
		}

		void Download(int32 Index, bool bKeep)
		{
			PageStatus.Reset();
			if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
			{
				Api->Download(Index, bKeep ? 1 : 0);
			}
		}

		void OpenFolder()
		{
			FString Folder = DemosLibraryText(UScctDemoPlayback::Api(), EScctDemoLibraryText::SavedFolder);
			if (Folder.IsEmpty())
			{
				Folder = UScctDemoSubsystem::DemoFolder();
			}
			IFileManager::Get().MakeDirectory(*Folder, true);
			FPlatformProcess::ExploreFolder(*FPaths::ConvertRelativePathToFull(Folder));
		}

		TSharedPtr<SEditableTextBox> SearchBox;
		TSharedPtr<SBox> ListBox;
		FScctDemoLibraryInfo Library = {};
		TArray<FDemosRow> LocalRows;
		TArray<FDemosRow> OnlineRows;
		EDemosTab CurrentTab = EDemosTab::Local;
		EDemosStorage Storage = EDemosStorage::All;
		EDemosSort Sort = EDemosSort::Newest;
		FString Search;
		FString ShownBoard;
		FString CountLine;
		FString PageStatus;
		bool bStatusIsError = false;
		bool bDirty = true;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeDemos(const FScctMenuHost& Host)
{
	return SNew(SScctDemosMenuPage, Host);
}
