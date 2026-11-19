// Maps & Content: the maps and shared packs of OpenSCCT-Content with what this PC has of them, ported from the Godot
// client's game/Scripts/UI/ContentScreen.cs. Download, Update, Retry, Stop and Remove work through the bridge's
// ContentPacks (Online/ScctContentCatalogue.h), so a download started here is the same one the lobby shows, and a
// map that came in is offered by Create Session at once. The Godot client's Linux first-run setup is not ported:
// this client ships for Windows only.

#include "Menu/ScctMenuPage.h"

#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Online/ScctContentCatalogue.h"
#include "Online/ScctContentDownloads.h"
#include "OpenSCCT.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float ContentPictureWidth = 176.0f;
	constexpr float ContentPictureHeight = 99.0f;

	FLinearColor ContentWarnColour() { return FLinearColor(1.0f, 0.62f, 0.2f, 1.0f); }

	FString ContentMegabytes(int64 Bytes)
	{
		return Bytes > 0 ? FString::Printf(TEXT("%.1f MB"), static_cast<double>(Bytes) / 1048576.0) : FString();
	}

	FString ContentCategoryLabel(const FString& Category)
	{
		return Category.IsEmpty() ? FString(TEXT("Other")) : Category.Left(1).ToUpper() + Category.Mid(1);
	}

	bool CanDownloadContent(const FScctContentRow& Row)
	{
		return Row.State == TEXT("available") || Row.State == TEXT("update") || (Row.bShared && Row.State == TEXT("notneeded"));
	}

	/** The line under a row's title: what this PC has, or how its download went. */
	FString ContentStateText(const FScctContentRow& Row)
	{
		if (Row.IsRunning())
		{
			return Row.JobLine.IsEmpty() ? FString(TEXT("Starting")) : Row.JobLine;
		}
		if (Row.HasFailed() && CanDownloadContent(Row))
		{
			const FString Operation = Row.State == TEXT("update") ? TEXT("Update") : TEXT("Download");
			return Row.Job == TEXT("stopped") ? Operation + TEXT(" stopped") : Operation + TEXT(" failed: ") + Row.JobLine;
		}
		const FString Version = Row.Version.IsEmpty() ? FString() : FString::Printf(TEXT(" (%s)"), *Row.Version);
		if (Row.State == TEXT("downloaded")) { return TEXT("Up to date") + Version; }
		if (Row.State == TEXT("update")) { return TEXT("Update available") + Version; }
		if (Row.State == TEXT("ingame")) { return TEXT("In your converted game files"); }
		if (Row.State == TEXT("unlisted")) { return TEXT("Downloaded (no longer listed)"); }
		if (Row.State == TEXT("notneeded")) { return TEXT("Not needed: your converted game files have it"); }
		return TEXT("Not downloaded");
	}

	FLinearColor ContentStateColour(const FScctContentRow& Row)
	{
		if (Row.IsRunning()) { return ScctHudStyle::Text(); }
		if (Row.HasFailed() && CanDownloadContent(Row)) { return ContentWarnColour(); }
		if (Row.State == TEXT("downloaded")) { return ScctUi::Accent(); }
		if (Row.State == TEXT("update")) { return ContentWarnColour(); }
		if (Row.State == TEXT("available")) { return ScctHudStyle::Text(); }
		return ScctHudStyle::TextDim();
	}

	class SScctMenuContent : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMenuContent) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			ScctContentCatalogue::RefreshIndex();
			SeenGeneration = ScctContentDownloads::Generation();

			SearchBox = ScctUi::TextBox(TEXT("Search maps by name or code"));
			DefaultFocus = SearchBox;

			TSharedPtr<SScctButton> FolderButton;
			ChildSlot
			[
				SNew(SHorizontalBox)
				// The catalogue.
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 24.0f, 0.0f)
				[
					ScctUi::Card(TEXT("Map catalogue"),
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ SearchBox.ToSharedRef() ]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 12.0f, 0.0f)
							[
								SNew(SScctStepper)
								.Label(FText::FromString(TEXT("Category")))
								.Description(FText::FromString(TEXT("Only the maps of one category of OpenSCCT-Content: stock, original, enhanced, recovered, ports or community.")))
								.GetOptions([this] { return CategoryOptions(); })
								.Get([this] { return CategoryIndex; })
								.Set([this](int32 Index) { CategoryIndex = Index; Fill(); })
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(SScctToggle)
								.Label(FText::FromString(TEXT("On this PC only")))
								.Description(FText::FromString(TEXT("Only the maps this PC has: downloaded, or in your converted game files.")))
								.Get([this] { return bHereOnly; })
								.Set([this](bool bOn) { bHereOnly = bOn; Fill(); })
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 10.0f, 0.0f, 10.0f)
						[
							SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true)
							.Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(StatusLine()); }))
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SAssignNew(MapScroll, SScrollBox)
							+ SScrollBox::Slot()[ SAssignNew(MapList, SVerticalBox) ]
						],
						FMargin(24.0f, 18.0f))
				]
				// Where the content is, the shared packs and the folder.
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(420.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)
						[
							ScctUi::Card(TEXT("Your library"),
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
								[
									SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true)
									.Text(FText::FromString(TEXT("Download maps and keep your shared game content up to date.")))
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
									.Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(Library); }))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
									[
										SAssignNew(FolderButton, SScctButton)
										.Look(EScctButtonLook::Chip)
										.Text(FText::FromString(TEXT("OPEN FOLDER")))
										.Description(FText::FromString(TEXT("Shows the folder downloaded maps and shared packs go into.")))
										.OnPressed(FSimpleDelegate::CreateSP(this, &SScctMenuContent::OpenFolder))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(SScctButton)
										.Look(EScctButtonLook::Chip)
										.Text(FText::FromString(TEXT("CHECK FOR UPDATES")))
										.Description(FText::FromString(TEXT("Checks the OpenSCCT-Content catalogue. An UPDATE button appears beside each installed map with a different published release.")))
										.OnPressed(FSimpleDelegate::CreateSP(this, &SScctMenuContent::Refresh))
									]
								])
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							ScctUi::Card(TEXT("Shared content"),
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
								[
									SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
									.Text(FText::FromString(TEXT("Characters, sounds and common files used across maps. Required packs download with each map.")))
								]
								+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(SharedList, SVerticalBox) ])
						]					]
				]
			];

			Fill();
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctMenuContent::Watch));
		}

		virtual FString GetTitle() const override { return TEXT("MAPS & CONTENT"); }
		virtual FString GetKicker() const override { return TEXT("OpenSCCT-Content"); }

		virtual void GetHints(TArray<TPair<FString, FString>>& OutKeyAndWhat, TArray<FString>& OutPadKeys) const override
		{
			OutKeyAndWhat.Add({ TEXT("R"), TEXT("Check for updates") });
			OutPadKeys.Add(TEXT("Y"));
		}

		virtual bool HandleShortcut(const FKey& Key) override
		{
			if (!ScctUi::IsTyping() && (Key == EKeys::R || Key == EKeys::F5 || Key == EKeys::Gamepad_FaceButton_Top))
			{
				Refresh();
				return true;
			}
			return false;
		}

		virtual void OnClosing() override
		{
			// Create Session, Play and the lobby read the maps again if anything came in or went.
			if (bChanged)
			{
				ScctContentCatalogue::RefreshMenuMaps();
			}
		}

	private:
		/** Polls the bridge: the rows' state, the search text, and the pictures still to be read. */
		EActiveTimerReturnType Watch(double, float)
		{
			const double Now = FPlatformTime::Seconds();
			const FString Search = SearchBox.IsValid() ? SearchBox->GetText().ToString().TrimStartAndEnd() : FString();
			if (Search != ShownSearch)
			{
				ShownSearch = Search;
				Fill();
			}
			else if (Now >= NextPoll)
			{
				NextPoll = Now + 0.4;
				const int32 Generation = ScctContentDownloads::Generation();
				if (Generation != SeenGeneration)
				{
					SeenGeneration = Generation;
					bChanged = true;
					ScctContentCatalogue::RefreshMenuMaps();
				}
				ScctContentCatalogue::RefreshIndex();
				Read();
				if (Signature() != ShownSignature)
				{
					Fill(false);
				}
			}
			LoadSomePictures();
			return EActiveTimerReturnType::Continue;
		}

		void Read()
		{
			ScctContentCatalogue::GetRows(Rows);
			Summary = ScctContentCatalogue::GetSummary();
			Library = ScctContentCatalogue::GetLibrary();
		}

		/** What the rows were built with: when it moves (a download started, ended or failed, a map came in or went), they are built again. */
		FString Signature() const
		{
			FString Out = Summary;
			for (const FScctContentRow& Row : Rows)
			{
				Out += Row.Key + Row.State + Row.Version + Row.Job + (Row.Picture.IsEmpty() ? TEXT("") : TEXT("p"));
			}
			return Out;
		}

		const FScctContentRow* FindRow(bool bShared, const FString& Key) const
		{
			return Rows.FindByPredicate([bShared, &Key](const FScctContentRow& Row) { return Row.bShared == bShared && Row.Key == Key; });
		}

		TArray<FString> CategoryOptions() const
		{
			TArray<FString> Options = { TEXT("All categories") };
			Options.Append(Categories);
			return Options;
		}

		FString StatusLine() const
		{
			return !ActionStatus.IsEmpty() && FPlatformTime::Seconds() < ActionStatusUntil ? ActionStatus : Summary;
		}

		void ShowStatus(const FString& Text)
		{
			ActionStatus = Text;
			ActionStatusUntil = FPlatformTime::Seconds() + 8.0;
		}

		bool Matches(const FScctContentRow& Row) const
		{
			if (!ShownSearch.IsEmpty() && !Row.Title.Contains(ShownSearch) && !Row.Key.Contains(ShownSearch))
			{
				return false;
			}
			if (CategoryIndex > 0 && Categories.IsValidIndex(CategoryIndex - 1) && ContentCategoryLabel(Row.Category) != Categories[CategoryIndex - 1])
			{
				return false;
			}
			return !bHereOnly || Row.IsHere();
		}

		/** Builds the rows again from the bridge (bRead) or from what was read last. */
		void Fill(bool bRead = true)
		{
			if (bRead)
			{
				Read();
			}
			ShownSignature = Signature();

			// The categories in the index's order of rank, as they first appear.
			for (const FScctContentRow& Row : Rows)
			{
				if (!Row.bShared)
				{
					Categories.AddUnique(ContentCategoryLabel(Row.Category));
				}
			}

			// The focus stays on the same row's first button when the row is built again.
			FString FocusedKey;
			for (const TPair<FString, TWeakPtr<SWidget>>& Pair : RowFocus)
			{
				const TSharedPtr<SWidget> Widget = Pair.Value.Pin();
				if (Widget.IsValid() && Widget->HasKeyboardFocus())
				{
					FocusedKey = Pair.Key;
				}
			}
			RowFocus.Reset();
			PictureSlots.Reset();

			MapList->ClearChildren();
			int32 Shown = 0;
			int32 Maps = 0;
			int32 Downloaded = 0;
			int32 InGame = 0;
			int32 Pictures = 0;
			for (const FScctContentRow& Row : Rows)
			{
				if (Row.bShared)
				{
					continue;
				}
				++Maps;
				Downloaded += Row.IsDownloaded() ? 1 : 0;
				InGame += Row.State == TEXT("ingame") ? 1 : 0;
				Pictures += Row.Picture.IsEmpty() ? 0 : 1;
				if (!Matches(Row))
				{
					continue;
				}
				MapList->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 8.0f, 8.0f)[ BuildRow(Row) ];
				++Shown;
			}
			if (Shown == 0)
			{
				MapList->AddSlot().AutoHeight().Padding(4.0f, 8.0f)
				[
					SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim())
					.Text(FText::FromString(Maps == 0 ? TEXT("No maps yet.") : TEXT("No maps match.")))
				];
			}

			SharedList->ClearChildren();
			int32 SharedCount = 0;
			for (const FScctContentRow& Row : Rows)
			{
				if (Row.bShared)
				{
					SharedList->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ BuildRow(Row) ];
					++SharedCount;
				}
			}
			if (SharedCount == 0)
			{
				SharedList->AddSlot().AutoHeight()
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					.Text(FText::FromString(TEXT("The shared packs are listed once the content list is read.")))
				];
			}

			if (!FocusedKey.IsEmpty())
			{
				if (const TWeakPtr<SWidget>* Again = RowFocus.Find(FocusedKey))
				{
					if (const TSharedPtr<SWidget> Widget = Again->Pin())
					{
						FSlateApplication::Get().SetKeyboardFocus(Widget, EFocusCause::SetDirectly);
					}
				}
			}

			const FString Counts = FString::Printf(TEXT("%d maps listed, %d shown, %d downloaded, %d in the converted game files, %d with a picture, %d shared packs"),
				Maps, Shown, Downloaded, InGame, Pictures, SharedCount);
			if (Counts != LoggedCounts)
			{
				LoggedCounts = Counts;
				UE_LOG(LogOpenSCCT, Log, TEXT("Maps & Content: %s. %s"), *Counts, *Summary);
			}
		}

		TSharedRef<SWidget> BuildRow(const FScctContentRow& Row)
		{
			const bool bShared = Row.bShared;
			const FString Key = Row.Key;
			const FString Title = Row.Title;
			const FString RowId = (bShared ? TEXT("shared:") : TEXT("map:")) + Key;

			// The buttons this row offers now.
			const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
			TSharedPtr<SWidget> First;
			const auto AddButton = [&Buttons, &First, bShared](const FString& Caption, const FString& Description, EScctButtonLook Look, FSimpleDelegate OnPressed)
			{
				TSharedPtr<SScctButton> Made;
				Buttons->AddSlot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(Made, SScctButton).Look(Look).MinWidth(bShared ? 0.0f : 104.0f).Text(FText::FromString(Caption)).Description(FText::FromString(Description)).OnPressed(OnPressed)
				];
				if (!First.IsValid())
				{
					First = Made;
				}
			};
			if (Row.IsRunning())
			{
				AddButton(TEXT("STOP"), FString::Printf(TEXT("Stops the download of %s. A half-made download is never installed."), *Title), EScctButtonLook::Secondary,
					FSimpleDelegate::CreateSP(this, &SScctMenuContent::StopRow, bShared, Key));
			}
			else
			{
				if (CanDownloadContent(Row))
				{
					const bool bUpdate = Row.State == TEXT("update");
					const FString Caption = FString(Row.HasFailed() ? TEXT("RETRY") : bUpdate ? TEXT("UPDATE") : TEXT("DOWNLOAD")) + (bShared ? TEXT(" PACK") : TEXT(""));
					const FString Description = bUpdate
						? FString::Printf(TEXT("Downloads the published release of %s (%s) and replaces the installed copy after verification."), *Title, *ContentMegabytes(Row.Size))
						: FString::Printf(TEXT("Downloads %s (%s) from OpenSCCT-Content.%s"), *Title, *ContentMegabytes(Row.Size),
							bShared ? TEXT("") : TEXT(" Once installed it can be picked in Create Session and played on servers that use it."));
					AddButton(Caption, Description,
						Row.State == TEXT("notneeded") ? EScctButtonLook::Secondary : EScctButtonLook::Primary, FSimpleDelegate::CreateSP(this, &SScctMenuContent::DownloadRow, bShared, Key));
				}
				if (!bShared && Row.IsDownloaded())
				{
					AddButton(TEXT("REMOVE"), FString::Printf(TEXT("Deletes the downloaded %s from this PC. It can be downloaded again."), *Title), EScctButtonLook::Danger,
						FSimpleDelegate::CreateSP(this, &SScctMenuContent::AskRemove, Key, Title));
				}
			}
			if (First.IsValid())
			{
				RowFocus.Add(RowId, First);
			}

			// The map's picture, read a few at a time (LoadSomePictures); a plate with its name until then or without one.
			TSharedPtr<SWidget> PictureWidget;
			if (!bShared)
			{
				TSharedPtr<SBox> PictureSlot;
				SAssignNew(PictureWidget, SBox).WidthOverride(ContentPictureWidth).HeightOverride(ContentPictureHeight)
				[
					SAssignNew(PictureSlot, SBox)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[ ScctUi::Picture(nullptr, ContentPictureWidth, ContentPictureHeight) ]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(8.0f)
						[
							SNew(STextBlock).Font(ScctUi::Caps(11.0f)).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true).Justification(ETextJustify::Center)
							.Text(FText::FromString(Title.ToUpper()))
						]
					]
				];
				if (!Row.Picture.IsEmpty())
				{
					PictureSlots.Add({ PictureSlot, Row.Picture });
				}
			}

			const FString Meta = bShared ? ContentMegabytes(Row.Size)
				: FString::Printf(TEXT("%s   %s   %s"), *ContentCategoryLabel(Row.Category).ToUpper(), *Row.Key, *ContentMegabytes(Row.Size));

			const TSharedRef<SVerticalBox> RowLines = SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(bShared ? ScctUi::Body(true) : ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true)
					.Text(FText::FromString(Title)).ToolTipText(FText::FromString(Row.Detail))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)[ ScctUi::Label(Meta, ScctHudStyle::TextDim(), 10.5f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock).Font(ScctUi::Small()).AutoWrapText(true)
					.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, bShared, Key]
					{
						const FScctContentRow* Now = FindRow(bShared, Key);
						return FSlateColor(Now != nullptr ? ContentStateColour(*Now) : ScctHudStyle::TextDim());
					}))
					.Text(TAttribute<FText>::CreateLambda([this, bShared, Key]
					{
						const FScctContentRow* Now = FindRow(bShared, Key);
						return FText::FromString(Now != nullptr ? ContentStateText(*Now) : FString());
					}))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SBox).HeightOverride(4.0f)
					.Visibility(TAttribute<EVisibility>::CreateLambda([this, bShared, Key]
					{
						const FScctContentRow* Now = FindRow(bShared, Key);
						return Now != nullptr && Now->IsRunning() ? EVisibility::Visible : EVisibility::Collapsed;
					}))
					[
						SNew(SProgressBar).FillColorAndOpacity(ScctUi::Accent())
						.Percent(TAttribute<TOptional<float>>::CreateLambda([this, bShared, Key]
						{
							const FScctContentRow* Now = FindRow(bShared, Key);
							return TOptional<float>(Now != nullptr ? Now->Fraction : 0.0f);
						}))
					]
				];

			const TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
			if (PictureWidget.IsValid())
			{
				Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)[ PictureWidget.ToSharedRef() ];
			}
			Line->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)[ RowLines ];
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(bShared ? 0.0f : 12.0f, 0.0f, 0.0f, 0.0f)[ Buttons ];
			if (bShared)
			{
				// The shared card is narrow: the pack's lines and its button side by side, without a frame.
				return Line;
			}
			return ScctUi::Panel(Line, FMargin(12.0f, 10.0f), 0.5f);
		}

		void LoadSomePictures()
		{
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			int32 Budget = 4;
			while (Menu != nullptr && Budget-- > 0 && PictureSlots.Num() > 0)
			{
				const TPair<TWeakPtr<SBox>, FString> Next = PictureSlots[0];
				PictureSlots.RemoveAt(0);
				const TSharedPtr<SBox> PictureBox = Next.Key.Pin();
				const FSlateBrush* Brush = PictureBox.IsValid() ? Menu->GetPicture(Next.Value) : nullptr;
				if (Brush != nullptr)
				{
					PictureBox->SetContent(ScctUi::Picture(Brush, ContentPictureWidth, ContentPictureHeight));
				}
			}
		}

		void DownloadRow(bool bShared, FString Key)
		{
			const FScctContentRow* Row = FindRow(bShared, Key);
			if (Row == nullptr)
			{
				return;
			}
			const int32 Result = ScctContentCatalogue::Download(*Row);
			ShowStatus(Result == 1 ? FString::Printf(TEXT("Downloading %s..."), *Row->Title)
				: Result == -1 ? FString(TEXT("The content list is still being read: try again in a moment."))
				: Result == 0 ? FString::Printf(TEXT("OpenSCCT-Content no longer lists %s."), *Row->Title)
				: FString::Printf(TEXT("%s could not be downloaded."), *Row->Title));
			ForceRefill();
		}

		void StopRow(bool bShared, FString Key)
		{
			if (const FScctContentRow* Row = FindRow(bShared, Key))
			{
				ScctContentCatalogue::Stop(*Row);
				ShowStatus(FString::Printf(TEXT("Stopped %s."), *Row->Title));
			}
			ForceRefill();
		}

		void AskRemove(FString Key, FString Title)
		{
			const TWeakPtr<SScctMenuContent> Weak = SharedThis(this);
			ScctUi::Confirm(Host.Dialogs, TEXT("Remove map"), FString::Printf(TEXT("Remove %s from this PC?"), *Title), TEXT("REMOVE"), TEXT("CANCEL"),
				[Weak, Key, Title]
				{
					const TSharedPtr<SScctMenuContent> Page = Weak.Pin();
					if (!Page.IsValid())
					{
						return;
					}
					FString Error;
					const int32 Result = ScctContentCatalogue::Remove(Key, Error);
					Page->ShowStatus(Result == 1 ? FString::Printf(TEXT("Removed %s."), *Title) : FString::Printf(TEXT("Could not remove %s: %s"), *Title, *Error));
					if (Result == 1)
					{
						Page->bChanged = true;
						ScctContentCatalogue::RefreshMenuMaps();
					}
					Page->ForceRefill();
				}, true);
		}

		void OpenFolder()
		{
			FString Error;
			if (!ScctContentCatalogue::OpenFolder(Error))
			{
				ShowStatus(TEXT("Could not open the folder: ") + Error);
			}
		}

		void Refresh()
		{
			ActionStatus.Reset();
			ScctContentCatalogue::RefreshIndex(true);
			ForceRefill();
		}

		void ForceRefill()
		{
			Fill();
			NextPoll = 0.0;
		}

		TSharedPtr<SEditableTextBox> SearchBox;
		TSharedPtr<SScrollBox> MapScroll;
		TSharedPtr<SVerticalBox> MapList;
		TSharedPtr<SVerticalBox> SharedList;
		TArray<FScctContentRow> Rows;
		TArray<FString> Categories;
		TMap<FString, TWeakPtr<SWidget>> RowFocus;
		TArray<TPair<TWeakPtr<SBox>, FString>> PictureSlots;
		FString Summary;
		FString Library;
		FString ShownSearch;
		FString ShownSignature;
		FString ActionStatus;
		FString LoggedCounts;
		double ActionStatusUntil = 0.0;
		double NextPoll = 0.0;
		int32 CategoryIndex = 0;
		int32 SeenGeneration = -1;
		bool bHereOnly = false;
		bool bChanged = false;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeContent(const FScctMenuHost& Host)
{
	return SNew(SScctMenuContent, Host);
}
