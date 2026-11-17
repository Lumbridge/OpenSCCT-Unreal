#include "Menu/SScctLobbyPage.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Math/RandomStream.h"
#include "Menu/SScctMapPicker.h"
#include "Menu/ScctMenuStyle.h"
#include "Menu/ScctSetupRowsView.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace Ui = ScctMenuStyle;

namespace
{
	constexpr float LobbyWidth = 1120.0f;
	constexpr float OptionsWidth = 1260.0f;
	constexpr float OptionsHeight = 540.0f;

	/** FScctOnlinePlayer::MapState. */
	constexpr int32 MapUnknown = 0;
	constexpr int32 MapMissing = 2;
	constexpr int32 MapDiffers = 3;

	/** An options line as its pairs, in order. */
	TArray<TPair<FString, FString>> Pairs(const FString& Line)
	{
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("?"), true);
		TArray<TPair<FString, FString>> Out;
		for (const FString& Part : Parts)
		{
			FString Key, Value;
			if (Part.Split(TEXT("="), &Key, &Value))
			{
				Out.Emplace(Key, Value);
			}
		}
		return Out;
	}

	/** Sets Key in an options line, replacing its value or adding it at the end. */
	FString WithOption(const FString& Line, const FString& Key, const FString& Value)
	{
		TArray<TPair<FString, FString>> List = Pairs(Line);
		bool bFound = false;
		for (TPair<FString, FString>& Pair : List)
		{
			if (Pair.Key.Equals(Key, ESearchCase::IgnoreCase))
			{
				Pair.Value = Value;
				bFound = true;
			}
		}
		if (!bFound)
		{
			List.Emplace(Key, Value);
		}
		FString Out;
		for (const TPair<FString, FString>& Pair : List)
		{
			Out += TEXT("?") + Pair.Key + TEXT("=") + Pair.Value;
		}
		return Out;
	}

	/** What a setup value is called on the page (LobbySetup.ToOptions's names); the Alternate rules' many names are one. */
	FString CaptionOfOption(const FString& Key)
	{
		static const TMap<FString, FString> Captions = {
			{ TEXT("MapName"), TEXT("Map") }, { TEXT("GameMode"), TEXT("Game mode") }, { TEXT("TimeLimit"), TEXT("Game length") },
			{ TEXT("ScoreLimit"), TEXT("Score limit") }, { TEXT("LivesATT"), TEXT("Spy lives") }, { TEXT("LivesDEF"), TEXT("Mercenary lives") },
			{ TEXT("TimesRespawnsATT"), TEXT("Respawn") }, { TEXT("SvmDisks"), TEXT("Active disks") }, { TEXT("SvmDisksToWin"), TEXT("Disks to take") },
			{ TEXT("SvmDisksMax"), TEXT("Disks") }, { TEXT("Revenge"), TEXT("Revenge") }, { TEXT("GamesInARow"), TEXT("Games in a row") },
			{ TEXT("MaxPlayers"), TEXT("Player slots") }, { TEXT("SvmVoice"), TEXT("Voice chat") }, { TEXT("SvmVoiceLobby"), TEXT("Voice in lobby") },
			{ TEXT("SvmRandomMap"), TEXT("Random map") }, { TEXT("SvmBal"), TEXT("Auto balance") }, { TEXT("BalanceTeams"), TEXT("Auto balance") },
			{ TEXT("SvmBots"), TEXT("Bots") }, { TEXT("SvmBotsPerSide"), TEXT("Bots per side") }, { TEXT("SvmSpyBots"), TEXT("Spy bots") },
			{ TEXT("SvmMercBots"), TEXT("Mercenary bots") }, { TEXT("SvmBotSkill"), TEXT("Bot skill") }, { TEXT("SvmPr"), TEXT("Practice") },
			{ TEXT("SvmSidearms"), TEXT("Sidearms") }, { TEXT("SvmRifles"), TEXT("Additional rifles") }, { TEXT("SvmResponsiveSmoke"), TEXT("Responsive smoke") },
		};
		const FString* Found = Captions.Find(Key);
		return Found != nullptr ? *Found : FString(TEXT("Alternate rules"));
	}

	bool OnSide(const FScctOnlinePlayer& Player)
	{
		return Player.Team == 0 || Player.Team == 1;
	}
}

void SScctLobbyPage::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;
	Maps = ScctHostSetup::Maps();
	for (const FString& Map : UScctOnlineSubsystem::FindImportedMaps())
	{
		ImportedMaps.Add(Map.ToLower());
	}

	ChildSlot[ SAssignNew(Body, SBox) ];
	if (UScctOnlineSubsystem* Visit = Online.Get())
	{
		ChangedHandle = Visit->OnChanged.AddSP(this, &SScctLobbyPage::HandleChanged);
	}
	SyncSetup();
	bControlShown = CanControl();

	// Unattended, once per run (the page is built again each time the lobby comes back after a round).
	static bool bSwitchesRead = false;
	if (!bSwitchesRead)
	{
		bSwitchesRead = true;
		const TCHAR* CommandLine = FCommandLine::Get();
		bOptionsOpen = FParse::Param(CommandLine, TEXT("ScctLobbyOptions"));
		bTeamChat = FParse::Param(CommandLine, TEXT("ScctLobbyTeamChat"));
		FString Manage;
		if (FParse::Value(CommandLine, TEXT("ScctLobbyManage="), Manage, false) && !Manage.IsEmpty())
		{
			if (!Manage.Split(TEXT(","), &PendingManage, &PendingManageAction))
			{
				PendingManage = Manage;
			}
		}
		if (FParse::Value(CommandLine, TEXT("ScctLobbyApply="), PendingApply, false) && !PendingApply.IsEmpty())
		{
			ApplyAfter = 4.0f;
			FParse::Value(CommandLine, TEXT("ScctLobbyApplyAfter="), ApplyAfter);
			FParse::Value(CommandLine, TEXT("ScctLobbyApplyWhen="), ApplyWhen);
		}
	}
	Rebuild();
	if (bOptionsOpen)
	{
		RefreshOptions();
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby shown (%s), %d hostable map(s) here%s"), CanControl() ? TEXT("may change the setup") : TEXT("setup read-only"),
		Maps.Num(), bOptionsOpen ? TEXT(", Session Options open") : TEXT(""));
}

SScctLobbyPage::~SScctLobbyPage()
{
	if (UScctOnlineSubsystem* Visit = Online.Get())
	{
		Visit->OnChanged.Remove(ChangedHandle);
	}
}

void SScctLobbyPage::HandleChanged()
{
	// Raised from the visit's frame, before Slate lays the page out: rows made here are measured before they are
	// drawn (made in Tick, which runs while painting, they would be drawn unmeasured, on top of each other, on every
	// frame that brings a new revision).
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || Visit->GetRevision() == SeenRevision)
	{
		return;
	}
	SeenRevision = Visit->GetRevision();
	if (ManagedPlayer >= 0 && (Visit->FindPlayer(ManagedPlayer) == nullptr || !Visit->CanModerate()))
	{
		// The player left (or was kicked), or this player may no longer moderate: the bar has nothing to act on.
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby moderation bar for '%s' closed (%s)"), *ManagedName, Visit->FindPlayer(ManagedPlayer) == nullptr ? TEXT("the player is gone") : TEXT("no longer the host or an admin"));
		ManagedPlayer = -1;
		Asking = EModerate::None;
	}
	const bool bSetupChanged = SyncSetup();
	LoadPicture(ServerMap());
	if (CanControl() != bControlShown)
	{
		// Control came or went (the host left, an admin logged in): the page is drawn again for the new role.
		bControlShown = CanControl();
		Rebuild();
		return;
	}
	if (bOptionsOpen)
	{
		if (bSetupChanged)
		{
			RefreshOptions();
		}
	}
	else
	{
		RefillLobby();
	}
}

bool SScctLobbyPage::CanControl() const
{
	return Online.IsValid() && Online->HasFlag(EScctOnlineStatusFlags::CanControl);
}

const FScctHostMap* SScctLobbyPage::FindMap(const FString& Id) const
{
	return Maps.FindByPredicate([&Id](const FScctHostMap& Map) { return Map.Id.Equals(Id, ESearchCase::IgnoreCase); });
}

FString SScctLobbyPage::ServerMap() const
{
	return Online.IsValid() ? Online->GetSetupMap() : FString();
}

FString SScctLobbyPage::ShownMap() const
{
	return CanControl() && !Draft.IsEmpty() ? ScctHostSetup::Option(Draft, TEXT("MapName")) : ServerMap();
}

int32 SScctLobbyPage::PlayersOf(const FString& Line) const
{
	// The lobby's player slots are the setup's own: the model keeps them rather than following the map's.
	return FMath::Max(0, FCString::Atoi(*ScctHostSetup::Option(Line, TEXT("MaxPlayers"))));
}

void SScctLobbyPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return;
	}

	// Unattended: the moderation bar for a named player once he is in and this player may moderate.
	if (!PendingManage.IsEmpty() && Visit->CanModerate())
	{
		const FScctOnlinePlayer* Target = Visit->GetPlayers().FindByPredicate([this](const FScctOnlinePlayer& Player) { return PendingManage.Equals(Player.Name, ESearchCase::IgnoreCase); });
		if (Target != nullptr)
		{
			Manage(Target->Id);
			const bool bVoiceBanned = (Target->Flags & EScctOnlinePlayerFlags::VoiceBanned) != 0;
			const EModerate Action = PendingManageAction.Equals(TEXT("kick"), ESearchCase::IgnoreCase) ? EModerate::Kick
				: PendingManageAction.Equals(TEXT("ban"), ESearchCase::IgnoreCase) ? EModerate::Ban
				: PendingManageAction.Equals(TEXT("voiceban"), ESearchCase::IgnoreCase) ? (bVoiceBanned ? EModerate::VoiceUnban : EModerate::VoiceBan)
				: EModerate::None;
			if (Action != EModerate::None)
			{
				Ask(Action);
			}
			PendingManage.Reset();
		}
	}

	// Unattended: the draft is filled once the server's setup is in and this player may change it, APPLY a moment later.
	if (!PendingApply.IsEmpty() && CanControl() && !ServerSetup.IsEmpty())
	{
		FillDraft(PendingApply);
		PendingApply.Reset();
	}
	if (ApplyAfter >= 0.0f && PendingApply.IsEmpty() && IsDirty())
	{
		// Counted on the clock from when enough players are in (a loaded machine draws few frames).
		const double Now = FPlatformTime::Seconds();
		if (ApplyAt < 0.0 && Visit->GetStatus().PlayerCount >= ApplyWhen)
		{
			ApplyAt = Now + ApplyAfter;
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyApply: %d player(s) in the lobby, APPLY in %.0f s"), Visit->GetStatus().PlayerCount, ApplyAfter);
		}
		if (ApplyAt >= 0.0 && Now >= ApplyAt)
		{
			ApplyAfter = -1.0f;
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyApply presses APPLY"));
			Apply();
		}
	}
}

// ---- the setup and its draft ------------------------------------------------------------------------------------

bool SScctLobbyPage::SyncSetup()
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	const FString Now = Visit != nullptr ? Visit->GetSetupOptions() : FString();
	if (Now == ServerSetup)
	{
		return false;
	}
	ServerSetup = Now;

	// Through the model, as the draft is: values it would only reformat are not changes. A map the model does not know
	// (not installed here) would be swapped for another, so that setup is taken as the server wrote it.
	FString Normalized = Now;
	if (!Now.IsEmpty() && FindMap(ScctHostSetup::Option(Now, TEXT("MapName"))) != nullptr)
	{
		const FString Line = ScctHostSetup::Text(EScctSetupText::Normalize, Now, FString(), PlayersOf(Now));
		Normalized = Line.IsEmpty() ? Now : Line;
	}

	const bool bHadChanges = IsDirty();
	Baseline = Normalized;
	if (bAwaitingEcho || !bHadChanges)
	{
		Draft = Baseline;
		bDraftConflict = false;
	}
	else if (Draft != Baseline)
	{
		bDraftConflict = true;
	}
	bAwaitingEcho = false;
	return true;
}

void SScctLobbyPage::SetDraft(const FString& Line)
{
	if (!Line.IsEmpty())
	{
		Draft = Line;
	}
	RefreshOptions();
}

void SScctLobbyPage::Edit(const FString& Change)
{
	SetDraft(ScctHostSetup::Text(EScctSetupText::Edit, Draft, Change, PlayersOf(Draft)));
}

void SScctLobbyPage::EditAlternate(const FString& Change)
{
	SetDraft(ScctHostSetup::Text(EScctSetupText::EditAlternate, Draft, Change, PlayersOf(Draft)));
}

FString SScctLobbyPage::ChangedOptions(TArray<FString>* OutNames) const
{
	TMap<FString, FString> Before;
	for (const TPair<FString, FString>& Pair : Pairs(Baseline))
	{
		Before.Add(Pair.Key, Pair.Value);
	}

	FString Out;
	for (const TPair<FString, FString>& Pair : Pairs(Draft))
	{
		const FString* Old = Before.Find(Pair.Key);
		if (Old == nullptr || !Old->Equals(Pair.Value, ESearchCase::CaseSensitive))
		{
			Out += TEXT("?") + Pair.Key + TEXT("=") + Pair.Value;
			if (OutNames != nullptr)
			{
				OutNames->AddUnique(CaptionOfOption(Pair.Key));
			}
		}
	}
	return Out;
}

void SScctLobbyPage::FillDraft(const FString& Line)
{
	SyncSetup();
	FString Next = Draft.IsEmpty() ? Baseline : Draft;

	// The map and mode first: they load their own rules, which the other values then change.
	FString Mode;
	for (const TPair<FString, FString>& Pair : Pairs(Line))
	{
		if (Pair.Key.Equals(TEXT("MapName"), ESearchCase::IgnoreCase))
		{
			Next = ScctHostSetup::Text(EScctSetupText::Edit, Next, TEXT("Map ") + Pair.Value, PlayersOf(Next));
		}
		else if (Pair.Key.Equals(TEXT("Mode"), ESearchCase::IgnoreCase))
		{
			Mode = Pair.Value;
		}
		else if (Pair.Key.Equals(TEXT("GameMode"), ESearchCase::IgnoreCase))
		{
			Mode = Pair.Value.IsNumeric() ? ScctHostSetup::ModeKeyOf(FCString::Atoi(*Pair.Value)) : Pair.Value;
		}
	}
	if (!Mode.IsEmpty())
	{
		Next = ScctHostSetup::Text(EScctSetupText::Edit, Next, TEXT("Mode ") + Mode, PlayersOf(Next));
	}
	for (const TPair<FString, FString>& Pair : Pairs(Line))
	{
		const FString& Key = Pair.Key;
		if (Key.Equals(TEXT("MapName"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("Mode"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("GameMode"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (Key.Equals(TEXT("Tab"), ESearchCase::IgnoreCase))
		{
			Tab = Pair.Value.Equals(TEXT("game"), ESearchCase::IgnoreCase) ? ETab::Game : ETab::Rules;
			bAlternateOpen = Pair.Value.Equals(TEXT("alternate"), ESearchCase::IgnoreCase);
			continue;
		}
		Next = WithOption(Next, Key, Pair.Value);
	}
	Next = ScctHostSetup::Text(EScctSetupText::Normalize, Next, FString(), PlayersOf(Next));
	if (!Next.IsEmpty())
	{
		Draft = Next;
	}
	ShowOptions(true);

	TArray<FString> Names;
	const FString Changes = ChangedOptions(&Names);
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyApply put into the Session Options draft: %s (%s): %s"), *FString::Join(Names, TEXT(", ")), *DraftDescription, *Changes);
}

bool SScctLobbyPage::Apply()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || !CanControl() || !IsDirty())
	{
		return false;
	}

	TArray<FString> Names;
	const FString Changes = ChangedOptions(&Names);
	if (!Visit->SetSetupOptions(Changes))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: Session Options could not send the setup (the visit is not in the lobby)"));
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: Session Options APPLY sent %d change(s): %s"), Names.Num(), *FString::Join(Names, TEXT(", ")));
	bAwaitingEcho = true;
	bDraftConflict = false;
	ShowOptions(false);
	return true;
}

void SScctLobbyPage::Discard()
{
	Draft = Baseline;
	bDraftConflict = false;
	ShowOptions(false);
}

void SScctLobbyPage::RandomMap()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || !CanControl())
	{
		return;
	}

	// A map that offers the mode being set up, never the current one; it comes with its own rules (SetMatch).
	const FString ModeKey = ScctHostSetup::ModeKeyOf(Visit->GetStatus().Mode);
	TArray<FString> Candidates;
	for (const FScctHostMap& Map : Maps)
	{
		if (!Map.Id.Equals(ServerMap(), ESearchCase::IgnoreCase) && Map.Modes.ContainsByPredicate([&ModeKey](const FString& Mode) { return Mode.Equals(ModeKey, ESearchCase::IgnoreCase); }))
		{
			Candidates.Add(Map.Id);
		}
	}
	if (Candidates.IsEmpty())
	{
		return;
	}
	static FRandomStream Stream(FPlatformTime::Cycles());
	const FString Drawn = Candidates[Stream.RandRange(0, Candidates.Num() - 1)];
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby RANDOM MAP drew %s of %d map(s) offering %s"), *Drawn, Candidates.Num(), *ModeKey);
	Visit->SetMatch(Drawn, -1);
}

// ---- what the lobby says about the launch -----------------------------------------------------------------------

FString SScctLobbyPage::MapLine(bool& bProblem) const
{
	bProblem = false;
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return FString();
	}

	const FString Map = ServerMap();
	const FScctHostMap* Entry = FindMap(Map);
	const FString Title = Entry != nullptr ? Entry->Name : Map;
	TArray<FString> Missing, Differs, Checking;
	int32 LocalState = -1;
	for (const FScctOnlinePlayer& Player : Visit->GetPlayers())
	{
		if (Player.Id == Visit->GetStatus().LocalId)
		{
			LocalState = Player.MapState;
			continue;
		}
		if (!OnSide(Player) || (Player.Flags & EScctOnlinePlayerFlags::Bot) != 0)
		{
			continue;
		}
		switch (Player.MapState)
		{
		case MapMissing: Missing.Add(Player.Name); break;
		case MapDiffers: Differs.Add(Player.Name); break;
		case MapUnknown: Checking.Add(Player.Name); break;
		default: break;
		}
	}

	TArray<FString> Lines;
	if (LocalState == MapMissing)
	{
		Lines.Add(FString::Printf(TEXT("You don't have %s, so you cannot be ready and the match cannot start with you on a side. This game cannot download maps yet: WATCH lets the others play."), *Map));
	}
	else if (LocalState == MapDiffers)
	{
		Lines.Add(FString::Printf(TEXT("Your copy of %s differs from the host's, so you cannot be ready and the match cannot start with you on a side. WATCH lets the others play."), *Map));
	}
	else if (!Map.IsEmpty() && !ImportedMaps.Contains(Map.ToLower()))
	{
		Lines.Add(FString::Printf(TEXT("%s has no imported level in this game: a round on it would sit you out until it is over."), *Map));
	}

	if (!Missing.IsEmpty() || !Differs.IsEmpty())
	{
		FString Who;
		if (!Missing.IsEmpty())
		{
			Who += FString::Printf(TEXT("%s %s %s."), *FString::Join(Missing, TEXT(", ")), Missing.Num() == 1 ? TEXT("does not have") : TEXT("do not have"), *Map);
		}
		if (!Differs.IsEmpty())
		{
			Who += FString::Printf(TEXT("%s%s %s a different version of %s."), Who.IsEmpty() ? TEXT("") : TEXT(" "), *FString::Join(Differs, TEXT(", ")),
				Differs.Num() == 1 ? TEXT("has") : TEXT("have"), *Map);
		}
		Lines.Add(Who + TEXT(" The match cannot start until they have the host's map or watch."));
	}
	bProblem = !Lines.IsEmpty();

	if (!Checking.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Checking %s with %s..."), *Map, *FString::Join(Checking, TEXT(", "))));
	}
	if (Lines.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Everyone on a side has %s."), *Title));
	}
	return FString::Join(Lines, TEXT(" "));
}

FString SScctLobbyPage::LaunchProblem() const
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return FString();
	}
	if (Visit->HasFlag(EScctOnlineStatusFlags::PreparingBots))
	{
		return TEXT("The server is preparing its bots for this map.");
	}

	const FString Map = ServerMap();
	TArray<FString> NotReady;
	int32 OnSides = 0;
	for (const FScctOnlinePlayer& Player : Visit->GetPlayers())
	{
		if (!OnSide(Player))
		{
			continue;
		}
		++OnSides;
		if (Player.MapState == MapMissing)
		{
			return FString::Printf(TEXT("%s does not have %s."), Player.Name, *Map);
		}
		if (Player.MapState == MapDiffers)
		{
			return FString::Printf(TEXT("%s has a different version of %s."), Player.Name, *Map);
		}
		if (Player.MapState == MapUnknown)
		{
			return FString::Printf(TEXT("%s has not finished checking %s."), Player.Name, *Map);
		}
		if ((Player.Flags & (EScctOnlinePlayerFlags::Ready | EScctOnlinePlayerFlags::Host | EScctOnlinePlayerFlags::Bot)) == 0)
		{
			NotReady.Add(Player.Name);
		}
	}
	if (OnSides == 0)
	{
		return TEXT("Nobody is on a side.");
	}
	if (!NotReady.IsEmpty())
	{
		return FString::Printf(TEXT("Waiting for %s to be ready."), *FString::Join(NotReady, TEXT(", ")));
	}
	return Visit->HasFlag(EScctOnlineStatusFlags::CanStart) ? FString() : FString(TEXT("Not everyone on a side is ready."));
}

// ---- the picture ------------------------------------------------------------------------------------------------

void SScctLobbyPage::LoadPicture(const FString& MapId)
{
	if (MapId.Equals(PictureMap, ESearchCase::IgnoreCase))
	{
		return;
	}
	PictureMap = MapId;
	PictureBrush.Reset();
	PictureTexture.Reset();

	const FScctHostMap* Entry = FindMap(MapId);
	if (Entry == nullptr || Entry->Picture.IsEmpty() || !FPaths::FileExists(Entry->Picture))
	{
		return;
	}
	UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Entry->Picture);
	if (Texture == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: the picture of %s could not be read (%s)"), *MapId, *Entry->Picture);
		return;
	}
	PictureTexture.Reset(Texture);
	PictureBrush = MakeShared<FSlateBrush>();
	PictureBrush->SetResourceObject(Texture);
	PictureBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	PictureBrush->DrawAs = ESlateBrushDrawType::Image;
}

TSharedRef<SWidget> SScctLobbyPage::MapPicture(float Width)
{
	static const FSlateColorBrush Back(FLinearColor::White);
	return SNew(SBox).WidthOverride(Width).HeightOverride(FMath::RoundToFloat(Width * 0.75f)).Clipping(EWidgetClipping::ClipToBounds)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SImage).Image(&Back).ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.05f)) ]
		// No picture (a map without one, or one this game does not have): its code where it would be.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(PictureBrush.IsValid() ? FString() : PictureMap); }), 12.0f, FSlateColor(ScctHudStyle::TextFaint()))
		]
		+ SOverlay::Slot()
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFill)
			[
				SNew(SImage)
				.Image(TAttribute<const FSlateBrush*>::CreateLambda([this] { return PictureBrush.Get(); }))
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return PictureBrush.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
			]
		]
	];
}

// ---- the pages --------------------------------------------------------------------------------------------------

void SScctLobbyPage::ShowOptions(bool bOpen)
{
	if (bOpen)
	{
		SyncSetup();
	}
	bOptionsOpen = bOpen;
	Rebuild();
	if (bOpen)
	{
		RefreshOptions();
	}
}

void SScctLobbyPage::Rebuild()
{
	ChatRows.Reset();
	ChatBox.Reset();
	AdminLine.Reset();
	RowsBox.Reset();
	Picker.Reset();
	for (TSharedPtr<SVerticalBox>& Rows3 : TeamRows)
	{
		Rows3.Reset();
	}
	LoadPicture(ServerMap());
	Body->SetContent(bOptionsOpen ? BuildOptions() : BuildLobby());
	SeenRevision = Online.IsValid() ? Online->GetRevision() : -1;
	if (!bOptionsOpen)
	{
		RefillLobby();
	}
}

TSharedRef<SWidget> SScctLobbyPage::BuildLobby()
{
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	const auto HasFlag = [Weak](int32 Flag) { return Weak.IsValid() && Weak->HasFlag(Flag); };
	const auto ShownWith = [HasFlag](int32 Flag)
	{
		return TAttribute<EVisibility>::CreateLambda([HasFlag, Flag] { return HasFlag(Flag) ? EVisibility::Visible : EVisibility::Collapsed; });
	};
	const auto TeamButton = [Weak](const TCHAR* LabelText, int32 Team, const FLinearColor& Colour)
	{
		return Ui::Button(Ui::ToText(LabelText),
			FOnClicked::CreateLambda([Weak, Team] { if (Weak.IsValid()) { Weak->SetTeam(Team); } return FReply::Handled(); }),
			TAttribute<bool>::CreateLambda([Weak, Team] { return Weak.IsValid() && Weak->GetStatus().LocalTeam == Team; }), Colour, 110.0f);
	};
	const auto TeamColumn = [this](const TCHAR* Name, int32 Team)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Ui::Label(Name, ScctHudStyle::Accent(Team)) ]
			+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(TeamRows[Team], SVerticalBox) ];
	};
	// The local player's map state: without the host's map the server refuses "ready".
	const auto LocalHasMap = [Weak]
	{
		const UScctOnlineSubsystem* Visit = Weak.Get();
		if (Visit == nullptr)
		{
			return false;
		}
		const FScctOnlinePlayer* Local = Visit->GetPlayers().FindByPredicate([Visit](const FScctOnlinePlayer& Player) { return Player.Id == Visit->GetStatus().LocalId; });
		return Local == nullptr || (Local->MapState != MapMissing && Local->MapState != MapDiffers);
	};
	const bool bControl = CanControl();

	// The setup's own buttons, beside what they change: the rules, and for the controller a random map.
	const TSharedRef<SHorizontalBox> SetupButtons = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(bControl ? TEXT("SESSION OPTIONS") : TEXT("MATCH SETTINGS")), FOnClicked::CreateLambda([this] { ShowOptions(true); return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this] { return IsDirty(); }))
		];
	if (bControl)
	{
		SetupButtons->AddSlot().AutoWidth()
		[
			SNew(SBox).IsEnabled(!Maps.IsEmpty())
			[
				Ui::Button(Ui::ToText(TEXT("RANDOM MAP")), FOnClicked::CreateLambda([this] { RandomMap(); return FReply::Handled(); }), false, ScctHudStyle::Good())
			]
		];
	}

	ChatBox = Ui::TextBox(TEXT("Say something to the lobby"), FString(), false, FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type Commit)
	{
		if (Commit == ETextCommit::OnEnter)
		{
			SendChat();
		}
	}));
	ChatBox->SetHintText(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(bTeamChat ? TEXT("Say something to your team (Tab: everyone)") : TEXT("Say something to the lobby (Tab: team)")); }));
	// Tab switches the channel while typing, as Godot's ChatInput does (it would otherwise move the focus away).
	ChatBox->SetOnKeyDownHandler(FOnKeyDown::CreateSP(this, &SScctLobbyPage::HandleChatKey));

	// The channel beside the line: ALL or TEAM in the local side's colour.
	const TSharedRef<SWidget> ChatLine = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			Ui::Button(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(bTeamChat ? TEXT("TEAM") : TEXT("ALL")); }),
				FOnClicked::CreateLambda([this] { ToggleChannel(); return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this] { return bTeamChat; }),
				ScctHudStyle::Accent(FMath::Clamp(Online.IsValid() ? Online->GetStatus().LocalTeam : 0, 0, 1)), 54.0f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)[ ChatBox.ToSharedRef() ];

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// The setup: its map's picture beside the server, the rules, the map line and what the lobby waits for.
	Box->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 22.0f, 0.0f)[ MapPicture(208.0f) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				Ui::Title(TAttribute<FText>::CreateLambda([Weak] { return Weak.IsValid() ? Ui::ToText(Weak->GetServerName().ToUpper()) : FText::GetEmpty(); }), 24.0f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([this]
				{
					const FScctHostMap* Entry = FindMap(ServerMap());
					return Ui::ToText(Entry != nullptr ? FString::Printf(TEXT("%s  (%s)"), *Entry->Name, *Entry->Id) : ServerMap());
				}), 15.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Bold)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([Weak] { return Weak.IsValid() ? Ui::ToText(Weak->GetSetupDescription()) : FText::GetEmpty(); }),
					11.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([this] { bool bProblem = false; return Ui::ToText(MapLine(bProblem)); }), 11.5f,
					TAttribute<FSlateColor>::CreateLambda([this] { bool bProblem = false; MapLine(bProblem); return FSlateColor(bProblem ? ScctHudStyle::Danger() : ScctHudStyle::Good()); }),
					ScctHudStyle::EWeight::Regular, true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(ScctHudStyle::Label(10.0f))
				.ColorAndOpacity(Ui::Accent())
				.Text(TAttribute<FText>::CreateLambda([Weak]
				{
					const UScctOnlineSubsystem* Visit = Weak.Get();
					if (Visit == nullptr)
					{
						return FText::GetEmpty();
					}
					if (Visit->GetStatus().Countdown > 0.0f)
					{
						return FText::FromString(FString::Printf(TEXT("STARTING IN %d"), FMath::CeilToInt32(Visit->GetStatus().Countdown)));
					}
					if (Visit->HasFlag(EScctOnlineStatusFlags::PreparingBots))
					{
						return Ui::ToText(TEXT("THE SERVER IS PREPARING ITS BOTS"));
					}
					return Ui::ToText(Visit->HasFlag(EScctOnlineStatusFlags::CanStart) ? TEXT("EVERYONE IS READY") : TEXT("WAITING FOR PLAYERS TO BE READY"));
				}))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ SetupButtons ]
		]
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 16.0f)[ Ui::Rule() ];

	Box->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		// The two sides, and whoever watches.
		+ SHorizontalBox::Slot().FillWidth(0.56f).Padding(0.0f, 0.0f, 28.0f, 0.0f)
		[
			SNew(SBox).MinDesiredHeight(240.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 18.0f, 0.0f)[ TeamColumn(TEXT("Spies"), 0) ]
					+ SHorizontalBox::Slot().FillWidth(1.0f)[ TeamColumn(TEXT("Mercenaries"), 1) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
				[
					SAssignNew(TeamRows[2], SVerticalBox)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ ModerationBar() ]
			]
		]
		// The chat.
		+ SHorizontalBox::Slot().FillWidth(0.44f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Ui::Label(TEXT("Chat")) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(200.0f)
				[
					SAssignNew(ChatRows, SScrollBox)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)[ ChatLine ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return ChatNotice.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; }))
				[
					Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(ChatNotice); }), 10.5f, FSlateColor(ScctHudStyle::Danger()))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)[ AdminBox() ]
		]
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 18.0f, 0.0f, 16.0f)[ Ui::Rule() ];

	const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ TeamButton(TEXT("SPIES"), 0, ScctHudStyle::Accent(0)) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ TeamButton(TEXT("MERCENARIES"), 1, ScctHudStyle::Accent(1)) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 22.0f, 0.0f)[ TeamButton(TEXT("WATCH"), 255, ScctHudStyle::Accent(2)) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SBox).IsEnabled(TAttribute<bool>::CreateLambda([HasFlag, LocalHasMap] { return HasFlag(EScctOnlineStatusFlags::LocalReady) || LocalHasMap(); }))
			[
				Ui::Button(TAttribute<FText>::CreateLambda([HasFlag] { return Ui::ToText(HasFlag(EScctOnlineStatusFlags::LocalReady) ? TEXT("READY") : TEXT("NOT READY")); }),
					FOnClicked::CreateLambda([Weak, HasFlag] { if (Weak.IsValid()) { Weak->SetReady(!HasFlag(EScctOnlineStatusFlags::LocalReady)); } return FReply::Handled(); }),
					TAttribute<bool>::CreateLambda([HasFlag] { return HasFlag(EScctOnlineStatusFlags::LocalReady); }), ScctHudStyle::Good(), 130.0f)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SBox)
			.Visibility(ShownWith(EScctOnlineStatusFlags::CanLaunch))
			// Off while the server would refuse it (the reason is under the buttons); the same request cancels a countdown.
			.IsEnabled(TAttribute<bool>::CreateLambda([Weak, HasFlag] { return HasFlag(EScctOnlineStatusFlags::CanStart) || (Weak.IsValid() && Weak->GetStatus().Countdown > 0.0f); }))
			[
				Ui::Button(TAttribute<FText>::CreateLambda([Weak] { return Ui::ToText(Weak.IsValid() && Weak->GetStatus().Countdown > 0.0f ? TEXT("CANCEL START") : TEXT("START")); }),
					FOnClicked::CreateLambda([Weak] { if (Weak.IsValid()) { Weak->Launch(); } return FReply::Handled(); }))
			]
		]
;
	Buttons->AddSlot().FillWidth(1.0f).HAlign(HAlign_Right)
	[
		Ui::Button(Ui::ToText(TEXT("LEAVE")), FOnClicked::CreateLambda([Weak] { if (Weak.IsValid()) { Weak->Leave(); } return FReply::Handled(); }), false, Ui::Accent(), 110.0f)
	];
	Box->AddSlot().AutoHeight()[ Buttons ];

	// Why START is off, and a reminder of a draft not applied.
	Box->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
	[
		Ui::Text(TAttribute<FText>::CreateLambda([this, Weak]
		{
			const UScctOnlineSubsystem* Visit = Weak.Get();
			FString Line;
			if (Visit != nullptr && Visit->HasFlag(EScctOnlineStatusFlags::CanLaunch) && Visit->GetStatus().Countdown <= 0.0f)
			{
				const FString Problem = LaunchProblem();
				Line = Problem.IsEmpty() ? FString() : TEXT("START is off: ") + Problem;
			}
			if (IsDirty())
			{
				Line += (Line.IsEmpty() ? TEXT("") : TEXT("  ")) + FString(TEXT("Unapplied changes: open SESSION OPTIONS to review them."));
			}
			return Ui::ToText(Line);
		}), 11.0f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
	];
	return SNew(SBox).WidthOverride(LobbyWidth)[ Ui::Glass(Box) ];
}

void SScctLobbyPage::RefillLobby()
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || !TeamRows[0].IsValid() || !ChatRows.IsValid())
	{
		return;
	}

	for (const TSharedPtr<SVerticalBox>& Rows3 : TeamRows)
	{
		Rows3->ClearChildren();
	}
	// The Rank column only while somebody in the lobby has a rank to show (LobbyRoomScreen.Layout).
	const bool bRanks = Visit->GetPlayers().ContainsByPredicate([](const FScctOnlinePlayer& Player) { return (Player.Rank & 0xFF) != 0; });
	bool bAnyWatching = false;
	for (const FScctOnlinePlayer& Player : Visit->GetPlayers())
	{
		if (!OnSide(Player))
		{
			if (!bAnyWatching)
			{
				bAnyWatching = true;
				TeamRows[2]->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Ui::Label(TEXT("Watching"), ScctHudStyle::TextFaint()) ];
			}
			TeamRows[2]->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 9.0f)[ PlayerRow(Player, bRanks) ];
			continue;
		}
		TeamRows[Player.Team]->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 9.0f)[ PlayerRow(Player, bRanks) ];
	}

	// The chat is short (the subsystem keeps the last lines only), so it is written out again whole.
	const TArray<FScctOnlineChatEntry>& Chat = Visit->GetChat();
	ChatRows->ClearChildren();
	for (const FScctOnlineChatEntry& Line : Chat)
	{
		ChatRows->AddSlot().Padding(0.0f, 0.0f, 0.0f, 5.0f)
		[
			Line.bSystem
				? Ui::Text(Ui::ToText(Line.Text), 11.0f, FSlateColor(ScctHudStyle::TextFaint()), ScctHudStyle::EWeight::Regular, true)
				// A team line is marked and written in its side's colour (LobbyRoomScreen: "(Team) Name: text").
				: StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, Line.bTeamOnly ? 6.0f : 0.0f, 0.0f)
					[
						Line.bTeamOnly ? Ui::Label(TEXT("(Team)"), ScctHudStyle::Accent(Line.Team)) : SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						Ui::Text(Ui::ToText(Line.Sender), 11.5f, FSlateColor(ScctHudStyle::Accent(Line.Team)), ScctHudStyle::EWeight::Bold)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						Ui::Text(Ui::ToText(Line.Text), 11.5f, FSlateColor(Line.bTeamOnly ? ScctHudStyle::Accent(Line.Team) : ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
					])
		];
	}
	if (Chat.Num() != ChatLinesShown)
	{
		ChatLinesShown = Chat.Num();
		ChatRows->ScrollToEnd();
	}
}

void SScctLobbyPage::SendChat()
{
	if (!Online.IsValid() || !ChatBox.IsValid())
	{
		return;
	}
	const FString Text = ChatBox->GetText().ToString().TrimStartAndEnd();
	if (Text.IsEmpty())
	{
		return;
	}
	const int32 LocalTeam = Online->GetStatus().LocalTeam;
	if (bTeamChat && LocalTeam != 0 && LocalTeam != 1)
	{
		// The server would send it to nobody: the line stays in the box.
		ChatNotice = TEXT("Join a team before sending team chat.");
		return;
	}
	ChatNotice.Reset();
	Online->SendChat(Text, bTeamChat);
	if (bTeamChat)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby chat sent to the team only"));
	}
	ChatBox->SetText(FText::GetEmpty());
	// Enter takes the focus away; the next line is usually typed straight after.
	FSlateApplication::Get().SetKeyboardFocus(ChatBox);
}

void SScctLobbyPage::ToggleChannel()
{
	bTeamChat = !bTeamChat;
	ChatNotice.Reset();
}

FReply SScctLobbyPage::HandleChatKey(const FGeometry& Geometry, const FKeyEvent& Key)
{
	if (Key.GetKey() == EKeys::Tab && !Key.IsRepeat())
	{
		ToggleChannel();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ---- players and moderation -------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctLobbyPage::PlayerRow(const FScctOnlinePlayer& Player, bool bRanks)
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	const bool bLocal = Visit != nullptr && Player.Id == Visit->GetStatus().LocalId;
	const bool bBot = (Player.Flags & EScctOnlinePlayerFlags::Bot) != 0;
	const bool bReady = (Player.Flags & EScctOnlinePlayerFlags::Ready) != 0;
	const bool bWatching = !OnSide(Player);
	FString State = bBot ? TEXT("BOT")
		: (Player.Flags & EScctOnlinePlayerFlags::InGame) != 0 ? TEXT("IN GAME")
		: (Player.Flags & EScctOnlinePlayerFlags::Loading) != 0 ? TEXT("LOADING")
		: bWatching ? TEXT("WATCHING")
		: bReady ? TEXT("READY") : TEXT("NOT READY");
	if (!bBot)
	{
		State += FString::Printf(TEXT("   %d MS"), Player.PingMs);
	}
	// The map check (OnlineLobby.Members' note): without the host's map the player holds the launch.
	const TCHAR* MapNote = bWatching ? nullptr : Player.MapState == MapMissing ? TEXT("NO MAP") : Player.MapState == MapDiffers ? TEXT("OTHER MAP VERSION") : nullptr;

	// Under the name: the host and admin markers, the state and ping, the map check and a voice ban.
	const TSharedRef<SHorizontalBox> Notes = SNew(SHorizontalBox);
	if ((Player.Flags & EScctOnlinePlayerFlags::Host) != 0)
	{
		Notes->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Ui::Label(TEXT("Host"), Ui::Accent()) ];
	}
	if ((Player.Flags & EScctOnlinePlayerFlags::Admin) != 0)
	{
		Notes->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Ui::Label(TEXT("Admin"), Ui::Accent()) ];
	}
	Notes->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Ui::Label(State, bReady || bBot ? ScctHudStyle::Good() : ScctHudStyle::TextFaint()) ];
	if (MapNote != nullptr)
	{
		Notes->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Ui::Label(MapNote, ScctHudStyle::Danger()) ];
	}
	if ((Player.Flags & EScctOnlinePlayerFlags::VoiceBanned) != 0)
	{
		Notes->AddSlot().AutoWidth()[ Ui::Label(TEXT("Voice banned"), ScctHudStyle::Danger()) ];
	}

	// The name line: the rank slot (kept empty for a player without a rank while others have one), the name, and for
	// the host or an admin the way to this person's moderation bar.
	const FString Rank = Visit != nullptr ? Visit->RankText(Player.Rank) : FString();
	FString RankTag = Rank, RankTitle;
	Rank.Split(TEXT(" · "), &RankTag, &RankTitle);
	const TSharedRef<SHorizontalBox> NameLine = SNew(SHorizontalBox);
	if (bRanks)
	{
		NameLine->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(52.0f).ToolTipText(Ui::ToText(Rank))
			[
				Ui::Label(RankTag, ScctHudStyle::TextDim())
			]
		];
	}
	FString Name = Player.Name;
	if (bLocal)
	{
		Name += TEXT(" (you)");
	}
	NameLine->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
	[
		Ui::Text(Ui::ToText(Name), 13.5f, FSlateColor(bLocal ? ScctHudStyle::Accent(FMath::Clamp(Player.Team, 0, 2)) : ScctHudStyle::Text()),
			bLocal ? ScctHudStyle::EWeight::Bold : ScctHudStyle::EWeight::Regular)
	];
	if (Visit != nullptr && Visit->CanModerate() && !bLocal && !bBot)
	{
		const int32 Id = Player.Id;
		NameLine->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			Ui::Button(Ui::ToText(TEXT("MANAGE")), FOnClicked::CreateLambda([this, Id] { Manage(ManagedPlayer == Id ? -1 : Id); return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this, Id] { return ManagedPlayer == Id; }))
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ NameLine ]
		+ SVerticalBox::Slot().AutoHeight().Padding(bRanks ? 60.0f : 0.0f, 2.0f, 0.0f, 0.0f)[ Notes ];
}

void SScctLobbyPage::Manage(int32 PlayerId)
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	const FScctOnlinePlayer* Target = Visit != nullptr ? Visit->FindPlayer(PlayerId) : nullptr;
	ManagedPlayer = Target != nullptr ? PlayerId : -1;
	ManagedName = Target != nullptr ? FString(Target->Name) : FString();
	Asking = EModerate::None;
	if (Target != nullptr)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby moderation bar open for '%s' (player %d)"), *ManagedName, PlayerId);
	}
}

void SScctLobbyPage::Ask(EModerate Action)
{
	Asking = ManagedPlayer >= 0 ? Action : EModerate::None;
	static const TCHAR* const Names[] = { TEXT("nothing"), TEXT("kick"), TEXT("ban"), TEXT("voice ban"), TEXT("voice unban") };
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby asks whether to %s '%s'"), Names[static_cast<int32>(Asking)], *ManagedName);
}

void SScctLobbyPage::Answer(bool bYes)
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (bYes && Visit != nullptr && ManagedPlayer >= 0)
	{
		switch (Asking)
		{
		case EModerate::Kick: Visit->Kick(ManagedPlayer); break;
		case EModerate::Ban: Visit->Ban(ManagedPlayer); break;
		case EModerate::VoiceBan: Visit->SetVoiceBanned(ManagedPlayer, true); break;
		case EModerate::VoiceUnban: Visit->SetVoiceBanned(ManagedPlayer, false); break;
		default: break;
		}
		// A kick or a ban takes the player away; a voice ban leaves the bar open on the player, now with VOICE UNBAN.
		if (Asking == EModerate::Kick || Asking == EModerate::Ban)
		{
			ManagedPlayer = -1;
		}
	}
	Asking = EModerate::None;
}

TSharedRef<SWidget> SScctLobbyPage::ModerationBar()
{
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	const auto VoiceBanned = [this, Weak]
	{
		const FScctOnlinePlayer* Target = Weak.IsValid() ? Weak->FindPlayer(ManagedPlayer) : nullptr;
		return Target != nullptr && (Target->Flags & EScctOnlinePlayerFlags::VoiceBanned) != 0;
	};
	const auto Shown = [this](bool bQuestion)
	{
		return TAttribute<EVisibility>::CreateLambda([this, bQuestion] { return (Asking != EModerate::None) == bQuestion ? EVisibility::Visible : EVisibility::Collapsed; });
	};

	// The actions for the managed player, then (in the same place) the question that confirms one.
	const TSharedRef<SWidget> Actions = SNew(SHorizontalBox)
		.Visibility(Shown(false))
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(ManagedName); }), 13.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Bold)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(TEXT("KICK")), FOnClicked::CreateLambda([this] { Ask(EModerate::Kick); return FReply::Handled(); }), false, ScctHudStyle::Danger())
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(TEXT("BAN")), FOnClicked::CreateLambda([this] { Ask(EModerate::Ban); return FReply::Handled(); }), false, ScctHudStyle::Danger())
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			Ui::Button(TAttribute<FText>::CreateLambda([VoiceBanned] { return Ui::ToText(VoiceBanned() ? TEXT("VOICE UNBAN") : TEXT("VOICE BAN")); }),
				FOnClicked::CreateLambda([this, VoiceBanned] { Ask(VoiceBanned() ? EModerate::VoiceUnban : EModerate::VoiceBan); return FReply::Handled(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Ui::Button(Ui::ToText(TEXT("CLOSE")), FOnClicked::CreateLambda([this] { Manage(-1); return FReply::Handled(); }))
		];

	// LobbyRoomScreen.AskKick's questions.
	const TSharedRef<SWidget> Question = SNew(SHorizontalBox)
		.Visibility(Shown(true))
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([this]
			{
				switch (Asking)
				{
				case EModerate::Ban: return Ui::ToText(FString::Printf(TEXT("Do you want to ban %s from this server?"), *ManagedName));
				case EModerate::VoiceBan: return Ui::ToText(FString::Printf(TEXT("Do you want to ban %s from voice chat?"), *ManagedName));
				case EModerate::VoiceUnban: return Ui::ToText(FString::Printf(TEXT("Do you want to let %s use voice chat again?"), *ManagedName));
				default: return Ui::ToText(FString::Printf(TEXT("Do you want to kick %s?"), *ManagedName));
				}
			}), 12.5f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(TEXT("YES")), FOnClicked::CreateLambda([this] { Answer(true); return FReply::Handled(); }), true, ScctHudStyle::Danger(), 70.0f)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Ui::Button(Ui::ToText(TEXT("NO")), FOnClicked::CreateLambda([this] { Answer(false); return FReply::Handled(); }), false, Ui::Accent(), 70.0f)
		];

	return SNew(SBox)
		.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return ManagedPlayer >= 0 ? EVisibility::Visible : EVisibility::Collapsed; }))
		[
			Ui::Glass(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Ui::Label(TEXT("Manage player"), ScctHudStyle::TextFaint()) ]
				+ SVerticalBox::Slot().AutoHeight()[ Actions ]
				+ SVerticalBox::Slot().AutoHeight()[ Question ],
				FMargin(14.0f, 10.0f), 0.5f)
		];
}

// ---- the admin box ----------------------------------------------------------------------------------------------

bool SScctLobbyPage::ShowsAdminBox() const
{
	// A dedicated server has admins who log in; the player's own server has its host's console.
	return Online.IsValid() && (Online->HasFlag(EScctOnlineStatusFlags::Dedicated) || Online->IsHosting());
}

TSharedRef<SWidget> SScctLobbyPage::AdminBox()
{
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	const auto Commands = [Weak] { return Weak.IsValid() && (Weak->IsLocalAdmin() || Weak->IsHosting()); };
	AdminLine = Ui::TextBox(TEXT("Admin password"), FString(), true, FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type Commit)
	{
		if (Commit == ETextCommit::OnEnter)
		{
			SendAdmin();
		}
	}));
	// Before the login the line is the password (never shown); afterwards the console's command line.
	AdminLine->SetIsPassword(TAttribute<bool>::CreateLambda([Commands] { return !Commands(); }));
	AdminLine->SetHintText(TAttribute<FText>::CreateLambda([Commands]
	{
		return Ui::ToText(Commands() ? TEXT("Command: status, players, kick <name>, map <map>, say <text>...") : TEXT("Admin password"));
	}));

	return SNew(SBox)
		.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return ShowsAdminBox() ? EVisibility::Visible : EVisibility::Collapsed; }))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Font(ScctHudStyle::Label(9.0f))
				.ColorAndOpacity(ScctHudStyle::TextDim())
				.Text(TAttribute<FText>::CreateLambda([Weak]
				{
					return Ui::ToText(!Weak.IsValid() ? FString()
						: Weak->IsHosting() ? FString(TEXT("HOST CONSOLE  (answers appear in the chat)"))
						: Weak->IsLocalAdmin() ? FString(TEXT("ADMIN CONSOLE  (answers appear in the chat)"))
						: FString(TEXT("ADMIN LOGIN")));
				}))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 6.0f, 0.0f)[ AdminLine.ToSharedRef() ]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					Ui::Button(TAttribute<FText>::CreateLambda([Commands] { return Ui::ToText(Commands() ? TEXT("SEND") : TEXT("LOG IN")); }),
						FOnClicked::CreateLambda([this] { SendAdmin(); return FReply::Handled(); }), false, Ui::Accent(), 70.0f)
				]
			]
		];
}

void SScctLobbyPage::SendAdmin()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || !AdminLine.IsValid())
	{
		return;
	}
	const FString Line = AdminLine->GetText().ToString().TrimStartAndEnd();
	if (Line.IsEmpty())
	{
		return;
	}
	if (Visit->IsLocalAdmin() || Visit->IsHosting())
	{
		Visit->AdminCommand(Line);
	}
	else
	{
		Visit->AdminLogin(Line);
	}
	AdminLine->SetText(FText::GetEmpty());
	FSlateApplication::Get().SetKeyboardFocus(AdminLine);
}

// ---- Session Options --------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctLobbyPage::BuildOptions()
{
	const bool bControl = CanControl();
	const TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
	Left->AddSlot().AutoHeight()
	[
		Ui::Title(TAttribute<FText>::CreateLambda([this]
		{
			const FString Map = ShownMap();
			const FScctHostMap* Entry = FindMap(Map);
			return Ui::ToText(Entry != nullptr ? FString::Printf(TEXT("%s  (%s)"), *Entry->Name, *Entry->Id) : Map);
		}), 18.0f)
	];
	Left->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 12.0f)
	[
		Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(DraftDescription); }), 10.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
	];
	if (bControl && !Maps.IsEmpty())
	{
		// The controller picks from the maps this game can show; a pick loads that map's rules into the draft.
		Left->AddSlot().AutoHeight()
		[
			SNew(SBox).HeightOverride(OptionsHeight - 80.0f)
			[
				SAssignNew(Picker, SScctMapPicker)
				.Maps(Maps)
				.TileWidth(124.0f)
				.Selected(TAttribute<FString>::CreateLambda([this] { return ShownMap(); }))
				.OnPicked(FOnScctMapPicked::CreateLambda([this](const FString& Map) { Edit(TEXT("Map ") + Map); }))
			]
		];
	}
	else
	{
		Left->AddSlot().AutoHeight()[ MapPicture(420.0f) ];
	}

	return SNew(SBox).WidthOverride(OptionsWidth)
	[
		Ui::Glass(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Ui::Title(Ui::ToText(bControl ? TEXT("SESSION OPTIONS") : TEXT("MATCH SETTINGS")), 24.0f) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 16.0f)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([this, bControl]
				{
					return Ui::ToText(!bControl ? FString(TEXT("These are the host's current match settings."))
						: bDraftConflict ? FString(TEXT("The match changed while you were editing. Review your changes before applying."))
						: IsDirty() ? FString(TEXT("You have unapplied changes. APPLY updates the match for everyone and un-readies the players."))
						: FString(TEXT("Choose the map and rules, then apply your changes. Changing map or mode loads its starting rules.")));
				}), 11.5f, TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(bDraftConflict ? ScctHudStyle::Danger() : ScctHudStyle::TextDim()); }),
					ScctHudStyle::EWeight::Regular, true)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.54f).Padding(0.0f, 0.0f, 26.0f, 0.0f)[ Left ]
				+ SHorizontalBox::Slot().FillWidth(0.46f)[ SAssignNew(RowsBox, SBox).HeightOverride(OptionsHeight) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f)[ Ui::Rule() ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					Ui::Text(TAttribute<FText>::CreateLambda([this]
					{
						TArray<FString> Names;
						ChangedOptions(&Names);
						return Ui::ToText(IsDirty() ? TEXT("Changed: ") + FString::Join(Names, TEXT(", ")) : FString());
					}), 11.5f, FSlateColor(Ui::Accent()), ScctHudStyle::EWeight::Regular, true)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					Ui::Button(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(IsDirty() && CanControl() ? TEXT("DISCARD") : TEXT("CLOSE")); }),
						FOnClicked::CreateLambda([this] { Discard(); return FReply::Handled(); }))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.Visibility(bControl ? EVisibility::Visible : EVisibility::Collapsed)
					.IsEnabled(TAttribute<bool>::CreateLambda([this] { return IsDirty(); }))
					[
						Ui::Button(Ui::ToText(TEXT("APPLY")), FOnClicked::CreateLambda([this] { Apply(); return FReply::Handled(); }), true, Ui::Accent(), 150.0f)
					]
				]
			])
	];
}

void SScctLobbyPage::RefreshOptions()
{
	const FString& Shown = CanControl() ? Draft : Baseline;
	VoiceHint.Reset();
	BotHint.Reset();
	Rows = ScctHostSetup::Rows(Shown, PlayersOf(Shown), VoiceHint, BotHint);
	DraftDescription = ScctHostSetup::Text(EScctSetupText::Describe, Shown, FString(), PlayersOf(Shown));
	if (!CanControl() && Online.IsValid())
	{
		// The server's own words for its setup (the model may not know a map this game does not have).
		DraftDescription = Online->GetSetupDescription();
	}
	const FScctSetupRow* Alternate = Rows.FindByPredicate([](const FScctSetupRow& Row) { return Row.Row == TEXT("Alternate"); });
	if (Alternate == nullptr || Alternate->Value == 0)
	{
		bAlternateOpen = false;
	}
	if (RowsBox.IsValid())
	{
		RowsBox->SetContent(BuildRows());
	}
}

TSharedRef<SWidget> SScctLobbyPage::BuildRows()
{
	// Rows are edited only on a map the model knows; on another the controller picks a map first.
	const bool bEditable = CanControl() && FindMap(ShownMap()) != nullptr;
	const FString& Shown = CanControl() ? Draft : Baseline;
	const ScctSetupRowsView::FOnEdit OnEdit = [this](const FString& Change) { Edit(Change); };
	const ScctSetupRowsView::FOnEdit OnEditAlternate = [this](const FString& Change) { EditAlternate(Change); };
	const auto FindRow = [this](const TCHAR* Name) { return Rows.FindByPredicate([Name](const FScctSetupRow& Row) { return Row.Row == Name; }); };
	const auto AddRows = [&](const TSharedRef<SVerticalBox>& Box, std::initializer_list<const TCHAR*> Names)
	{
		for (const TCHAR* Name : Names)
		{
			Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Row(FindRow(Name), OnEdit, bEditable) ];
		}
	};

	if (bAlternateOpen)
	{
		const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				Ui::Button(Ui::ToText(TEXT("DONE")), FOnClicked::CreateLambda([this] { bAlternateOpen = false; RefreshOptions(); return FReply::Handled(); }), true, Ui::Accent(), 110.0f)
			];
		if (bEditable)
		{
			Buttons->AddSlot().AutoWidth()
			[
				Ui::Button(Ui::ToText(TEXT("STOCK VALUES")), FOnClicked::CreateLambda([this] { EditAlternate(TEXT("reset")); return FReply::Handled(); }))
			];
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ Ui::Label(TEXT("Alternate rules"), Ui::Accent()) ]
			+ SVerticalBox::Slot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(TEXT("Which abilities and gadgets each team can use."))) ]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot().Padding(0.0f, 0.0f, 14.0f, 0.0f)
				[
					ScctSetupRowsView::Alternate(ScctHostSetup::AlternateRows(Shown, PlayersOf(Shown)), OnEditAlternate, bEditable)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ Buttons ];
	}

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (CanControl() && !bEditable)
	{
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(TEXT("This game does not have the setup's map: pick one of the maps on the left to change the rules."))) ];
	}
	if (Tab == ETab::Game)
	{
		AddRows(Box, { TEXT("Mode"), TEXT("TimeLimit"), TEXT("Voice") });
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(VoiceHint)) ];
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Heading(TEXT("Players & bots")) ];
		AddRows(Box, { TEXT("MaxPlayers"), TEXT("Bots"), TEXT("BotsPerSide"), TEXT("SpyBots"), TEXT("MercBots"), TEXT("BotSkill") });
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(BotHint)) ];
	}
	else
	{
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Heading(TEXT("Round & objectives")) ];
		AddRows(Box, { TEXT("SpyLives"), TEXT("MercLives"), TEXT("Respawn"), TEXT("ActiveDisks"), TEXT("DisksToTake") });
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(TEXT("Set lives to 0 for unlimited respawns."))) ];
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Heading(TEXT("Teams & rotation")) ];
		AddRows(Box, { TEXT("Revenge"), TEXT("GamesInARow"), TEXT("RandomMap"), TEXT("AutoBalance"), TEXT("VoiceInLobby") });
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Heading(TEXT("Extra rules")) ];
		AddRows(Box, { TEXT("Practice"), TEXT("Sidearms"), TEXT("Rifles"), TEXT("ResponsiveSmoke"), TEXT("Alternate") });
		const FScctSetupRow* Alternate = FindRow(TEXT("Alternate"));
		Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f).HAlign(HAlign_Left)
		[
			SNew(SBox).IsEnabled(Alternate != nullptr && Alternate->Value != 0)
			[
				Ui::Button(Ui::ToText(bEditable ? TEXT("CONFIGURE ALTERNATE RULES") : TEXT("VIEW ALTERNATE RULES")),
					FOnClicked::CreateLambda([this] { bAlternateOpen = true; RefreshOptions(); return FReply::Handled(); }))
			]
		];
	}

	const auto TabButton = [this](const TCHAR* Label, ETab Target)
	{
		return Ui::Button(Ui::ToText(Label), FOnClicked::CreateLambda([this, Target] { Tab = Target; RefreshOptions(); return FReply::Handled(); }), Tab == Target, Ui::Accent(), 110.0f);
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ TabButton(TEXT("GAME"), ETab::Game) ]
			+ SHorizontalBox::Slot().AutoWidth()[ TabButton(TEXT("RULES"), ETab::Rules) ]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(0.0f, 0.0f, 14.0f, 0.0f)[ Box ]
		];
}
