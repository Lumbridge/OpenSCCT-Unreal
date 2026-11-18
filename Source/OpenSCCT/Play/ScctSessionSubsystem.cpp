#include "Play/ScctSessionSubsystem.h"

#include "Demo/ScctDemoSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Engine/GameInstance.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctPoseAim.h"
#include "Play/ScctViewPawn.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctOfflineMatchApi.h"
#include "UnrealClient.h"

namespace
{
	constexpr int32 MaxPawns = 64;
	constexpr int32 MaxBones = 128;

	/** Copies Text into a fixed field of the bridge's structs, cut to fit and always zero terminated. */
	template <int32 Capacity>
	void CopyText(TCHAR (&Field)[Capacity], const FString& Text)
	{
		FCString::Strncpy(Field, *Text, Capacity);
	}

	template <typename TTable>
	void LogFeatureTable(const TCHAR* Label)
	{
		FString Error;
		if (const TTable* Table = ScctFeature<TTable>(&Error))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Feature check: %s version %d"), Label, Table->Version);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Error, TEXT("Feature check: %s missing: %s"), Label, *Error);
		}
	}
}

bool UScctSessionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UScctSessionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctSessionSubsystem, STATGROUP_Tickables);
}

bool UScctSessionSubsystem::ConnectBridge()
{
	if (Api != nullptr)
	{
		return true;
	}

	FString Error;
	Api = FOpenSCCTModule::Get().GetBridge(Error);
	if (Api == nullptr)
	{
		Fail(Error, EScctPlayErrorKind::StartFailed, LastOptions.Map);
		return false;
	}

	// Asking for the play table puts the local player's gear on every level the bridge loads from now on (a local
	// match or an online round), so it is asked for as soon as the bridge is there.
	ScctFeature<FScctPlayApi>();
	return true;
}

bool UScctSessionSubsystem::FollowBridgeSessions(bool bFollow)
{
	bFollowBridge = bFollow;
	return ConnectBridge();
}

bool UScctSessionSubsystem::StartLocalMatch(const FString& Map, const FString& Mode, bool bMerc, int32 Bots)
{
	FScctLocalMatchOptions Options;
	Options.Map = Map;
	Options.Mode = Mode;
	Options.bMerc = bMerc;
	Options.Bots = Bots;
	return StartMatch(Options);
}

bool UScctSessionSubsystem::StartMatch(const FScctLocalMatchOptions& Options)
{
	Failure.Reset();
	LastOptions = Options;
	RestartTimer = -1.0f;
	if (!ConnectBridge())
	{
		return false;
	}

	// The bridge would end the session before on its own; ending it here first keeps the order features rely
	// on (stopped, changed, started).
	StopMatch();

	FScctBridgeStartOptions Start = {};
	Start.Size = sizeof(Start);
	Start.bMerc = Options.bMerc ? 1 : 0;
	Start.Bots = FMath::Max(Options.Bots, 0);
	Start.Flags = (Options.bSidearms ? EScctStartFlags::Sidearms : 0)
		| (Options.bRifles ? EScctStartFlags::Rifles : 0)
		| (Options.bOfflineRules ? EScctStartFlags::OfflineRules : 0)
		| (Options.bResponsiveSmoke ? EScctStartFlags::ResponsiveSmoke : 0)
		| (Options.bUnlimitedGear ? EScctStartFlags::UnlimitedGear : 0);
	Start.Difficulty = FMath::Clamp(Options.Difficulty, 0, 4);
	CopyText(Start.Map, Options.Map);
	CopyText(Start.Mode, Options.Mode);
	CopyText(Start.PlayerName, Options.PlayerName);
	Start.TimeLimitPlusOne = FMath::Max(Options.TimeLimit, -1) + 1;
	Start.SpyLivesPlusOne = FMath::Max(Options.SpyLives, -1) + 1;
	Start.MercLivesPlusOne = FMath::Max(Options.MercLives, -1) + 1;
	Start.RespawnPlusOne = FMath::Max(Options.RespawnSeconds, -1) + 1;
	Start.MatchPlayers = FMath::Clamp(Options.MatchPlayers, 0, 24);

	// The match only allows sidearms; which one the local merc carries is his own gear, asked of this machine when
	// the server hands out the kit.
	if (const FScctPlayApi* Play = ScctFeature<FScctPlayApi>())
	{
		Play->SetSidearm(Options.Sidearm);
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Starting %s (%s) as %s with %d bots (difficulty %d, sidearms %d (own sidearm %d), rifles %d, practice rules %d, unlimited gear %d, responsive smoke %d, match players %d, name '%s'; own rules: time %d, spy lives %d, merc lives %d, respawn %d; -1 keeps the map's)"),
		*Options.Map, *Options.Mode, Options.bMerc ? TEXT("merc") : TEXT("spy"), Start.Bots, Start.Difficulty,
		Options.bSidearms ? 1 : 0, Options.Sidearm, Options.bRifles ? 1 : 0, Options.bOfflineRules ? 1 : 0, Options.bUnlimitedGear ? 1 : 0,
		Options.bResponsiveSmoke ? 1 : 0, Start.MatchPlayers, *Options.PlayerName,
		Options.TimeLimit, Options.SpyLives, Options.MercLives, Options.RespawnSeconds);

	// The player's demo-saving choice as it is now, so the match records (or not) on it (Demo/ScctDemoSubsystem.h).
	UScctDemoSubsystem::PushChoice();

	const int32 NewId = Api->StartLocalEx(&Start);
	if (NewId == 0)
	{
		Fail(FOpenSCCTModule::Get().GetBridgeError(), EScctPlayErrorKind::StartFailed, Options.Map);
		// A failed start has no session to stop, and the bridge stays Failed until told otherwise. Nothing is in
		// its slot now (it ended the session before as it started), so this only clears that state.
		Api->Stop();
		return false;
	}

	FScctBridgeSessionInfo Info = {};
	if (Api->GetSession(&Info) != NewId)
	{
		Fail(FString::Printf(TEXT("The bridge started session %d but now holds session %d."), NewId, Info.Id), EScctPlayErrorKind::StartFailed, Options.Map);
		return false;
	}
	ChangeSession(Info, true);

	// What the simulation's rules were started with, so the options chosen can be checked against what reached them.
	if (const FScctOfflineMatchApi* Offline = ScctFeature<FScctOfflineMatchApi>())
	{
		TCHAR GameOptions[1024] = {};
		Offline->GetGameOptions(GameOptions, UE_ARRAY_COUNT(GameOptions));
		UE_LOG(LogOpenSCCT, Log, TEXT("Offline match game options: %s"), GameOptions);
	}

	// -ScctRestartAfter=S: the unattended check of a second match in the same world, once.
	float RestartAfter = 0.0f;
	if (!bRestarted && FParse::Value(FCommandLine::Get(), TEXT("ScctRestartAfter="), RestartAfter) && RestartAfter > 0.0f)
	{
		RestartTimer = RestartAfter;
	}
	return true;
}

bool UScctSessionSubsystem::PresentBackdrop(int32 NewSessionId)
{
	if (NewSessionId == 0 || !ConnectBridge())
	{
		return false;
	}
	FScctBridgeSessionInfo Info = {};
	if (Api->GetSession(&Info) != NewSessionId)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu backdrop: session %d is not the one the bridge holds (%d); not presented"), NewSessionId, Info.Id);
		return false;
	}
	RestartTimer = -1.0f;
	ChangeSession(Info, true);
	return SessionId == NewSessionId;
}

void UScctSessionSubsystem::StopMatch()
{
	if (Api != nullptr && SessionId != 0)
	{
		// Only this session: if another has taken the slot since, it is left alone.
		Api->StopSession(SessionId);
	}
	ChangeSession(FScctBridgeSessionInfo{}, false);
}

void UScctSessionSubsystem::Deinitialize()
{
	if (bStartedHere)
	{
		StopMatch();
	}
	else
	{
		// A session this world only presented (an online round) goes on without it.
		ChangeSession(FScctBridgeSessionInfo{}, false);
	}
	Super::Deinitialize();
}

void UScctSessionSubsystem::ChangeSession(const FScctBridgeSessionInfo& NewInfo, bool bNewStartedHere)
{
	const int32 OldId = SessionId;
	if (NewInfo.Id == OldId)
	{
		return;
	}

	if (OldId != 0)
	{
		OnMatchStopped.Broadcast();
	}

	DestroyProxies();
	View = {};
	PawnCount = 0;
	LocalActorId = 0;
	TicksThisFrame = 0;

	SessionId = NewInfo.Id;
	SessionInfo = NewInfo;
	if (SessionId != 0)
	{
		LastPresentedMap = SessionInfo.Map;
	}
	bStartedHere = bNewStartedHere && SessionId != 0;
	UE_LOG(LogOpenSCCT, Log, TEXT("Session %d -> %d"), OldId, SessionId);
	OnSessionChanged.Broadcast(OldId, SessionId);
	if (SessionId == 0)
	{
		return;
	}

	PawnBuffer.SetNumUninitialized(MaxPawns);
	PoseBuffer.SetNumUninitialized(MaxBones);
	for (int32 Kind = 0; Kind < UE_ARRAY_COUNT(Skeletons); ++Kind)
	{
		Skeletons[Kind].SetNumUninitialized(MaxBones);
		const int32 Count = Api->GetSkeleton(Kind, Skeletons[Kind].GetData(), MaxBones);
		Skeletons[Kind].SetNum(FMath::Clamp(Count, 0, MaxBones));
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Session %d plays: map %s, %s, local player is a %s"), SessionId, SessionInfo.Map,
		(SessionInfo.Flags & EScctSessionFlags::Remote) != 0 ? TEXT("round on a remote server") : TEXT("local match"),
		IsBackdrop() ? TEXT("watcher only (the menu's backdrop)") : (SessionInfo.Flags & EScctSessionFlags::Merc) != 0 ? TEXT("merc") : TEXT("spy"));
	if (FParse::Param(FCommandLine::Get(), TEXT("ScctCheckFeatures")))
	{
		LogFeatureTables();
	}
	OnMatchStarted.Broadcast();
}

void UScctSessionSubsystem::CheckSession()
{
	FScctBridgeSessionInfo Info = {};
	Api->GetSession(&Info);
	if (Info.Id == SessionId)
	{
		if (SessionId != 0)
		{
			SessionInfo = Info;
		}
		return;
	}

	if (SessionId != 0)
	{
		if (Info.Id == 0 && Info.State == static_cast<int32>(EScctBridgeState::Failed) && IsBackdrop())
		{
			// Nobody asked for the match behind the menu: its failure is a line in the log, not an error page.
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu backdrop: session %d stopped on an error: %s"), SessionId, *FOpenSCCTModule::Get().GetBridgeError());
			Api->StopSession(SessionId);
			Info.State = static_cast<int32>(EScctBridgeState::Idle);
		}
		else if (Info.Id == 0 && Info.State == static_cast<int32>(EScctBridgeState::Failed))
		{
			// The bridge has already stopped the failed session; this shows why, once, and clears the failure
			// (StopSession does that for the session that failed and for no other).
			Fail(FOpenSCCTModule::Get().GetBridgeError(),
				IsRemoteSession() ? EScctPlayErrorKind::Disconnected : EScctPlayErrorKind::ServerStopped, SessionInfo.Map);
			Api->StopSession(SessionId);
			Info.State = static_cast<int32>(EScctBridgeState::Idle);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Session %d is over (the bridge now holds session %d)"), SessionId, Info.Id);
		}
	}

	// A session this world did not start is presented only when it was asked to follow the bridge.
	ChangeSession(bFollowBridge ? Info : FScctBridgeSessionInfo{}, false);
}

void UScctSessionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickAutoShots(DeltaTime);
	if (RestartTimer > 0.0f)
	{
		RestartTimer -= DeltaTime;
		if (RestartTimer <= 0.0f)
		{
			bRestarted = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctRestartAfter: starting the match again (session %d, %d pawn bodies)"), SessionId, Proxies.Num());
			StartMatch(FScctLocalMatchOptions(LastOptions));
		}
	}

	// The order below is the contract written at the top of the header; keep the two together.
	TicksThisFrame = 0;
	if (Api != nullptr)
	{
		OnBeforeTicks.Broadcast(DeltaTime);
		CheckSession();
		if (bFollowBridge)
		{
			CheckVisitFailure();
		}
		TickForcedDisconnect(DeltaTime);

		if (SessionId != 0)
		{
			const int32 Ticks = Api->UpdateSession(SessionId);
			TicksThisFrame = FMath::Max(Ticks, 0);
			if (Ticks < 0 || Api->GetState() != static_cast<int32>(EScctBridgeState::Playing))
			{
				// Replaced, or failed inside its ticks.
				CheckSession();
			}
		}

		if (SessionId != 0)
		{
			Api->GetView(&View);
			ViewOverride.ExecuteIfBound(View);
		}
	}

	// The camera is placed here, in the subsystem's tick: the engine caches the camera right after it, so a
	// pawn that moved itself in a later tick group would be drawn one frame behind the world.
	ApplyTourCamera();
	ApplyViewToPlayer();
	if (SessionId == 0)
	{
		return;
	}

	UpdatePawns();
	if (Api->GetState() != static_cast<int32>(EScctBridgeState::Playing))
	{
		// Reading the view, the pawns or a pose failed and the bridge ended the session: the features are not
		// handed a frame of a match that is gone.
		CheckSession();
		if (SessionId == 0)
		{
			return;
		}
	}
	OnFrame.Broadcast(DeltaTime);
}

void UScctSessionSubsystem::UpdatePawns()
{
	PawnCount = FMath::Clamp(Api->GetPawns(PawnBuffer.GetData(), PawnBuffer.Num()), 0, PawnBuffer.Num());
	LocalActorId = 0;

	// -ScctShowLocalBody: the screenshot tour looks at the local player from outside, where the body belongs.
	static const bool bShowLocalBody = FParse::Param(FCommandLine::Get(), TEXT("ScctShowLocalBody"));
	if (bShowLocalBody)
	{
		for (int32 Index = 0; Index < PawnCount; ++Index)
		{
			PawnBuffer[Index].Flags &= ~EScctPawnFlags::FirstPerson;
		}
	}

	// The local spy's body aims at the view this frame draws (the mouse turn not yet simulated included), handed over
	// before any pose is read.
	int32 AimingActorId = 0;
	for (int32 Index = 0; Index < PawnCount; ++Index)
	{
		if ((PawnBuffer[Index].Flags & EScctPawnFlags::Local) != 0)
		{
			AimingActorId = PawnBuffer[Index].ActorId;
		}
	}
	ScctPoseAim::FeedDrawnView(*GetWorld(), AimingActorId);

	TSet<int32, DefaultKeyFuncs<int32>, TInlineSetAllocator<MaxPawns>> Seen;
	for (int32 Index = 0; Index < PawnCount; ++Index)
	{
		const FScctBridgePawn& Pawn = PawnBuffer[Index];
		Seen.Add(Pawn.ActorId);
		if ((Pawn.Flags & EScctPawnFlags::Local) != 0)
		{
			LocalActorId = Pawn.ActorId;
		}

		TObjectPtr<AScctPawnProxy>& Proxy = Proxies.FindOrAdd(Pawn.ActorId);
		if (!IsValid(Proxy))
		{
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Parameters.ObjectFlags |= RF_Transient;
			Proxy = GetWorld()->SpawnActor<AScctPawnProxy>(FVector(Pawn.X, Pawn.Y, Pawn.Z), FRotator::ZeroRotator, Parameters);
		}
		if (IsValid(Proxy))
		{
			const int32 Bones = FMath::Clamp(Api->GetPawnPose(Pawn.ActorId, PoseBuffer.GetData(), PoseBuffer.Num()), 0, PoseBuffer.Num());
			Proxy->Present(Pawn, GetSkeleton(Pawn.Kind), TConstArrayView<FScctBridgeBone>(PoseBuffer.GetData(), Bones));
		}
	}
	ScctPoseAim::LogAimProbe(*GetWorld(), AimingActorId, GetWorld()->GetDeltaSeconds());

	for (auto It = Proxies.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			if (IsValid(It.Value()))
			{
				It.Value()->Destroy();
			}
			It.RemoveCurrent();
		}
	}
}

void UScctSessionSubsystem::DestroyProxies()
{
	for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Proxies)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	Proxies.Reset();
}

TConstArrayView<FScctBridgeBoneInfo> UScctSessionSubsystem::GetSkeleton(int32 Kind) const
{
	return Kind >= 0 && Kind < UE_ARRAY_COUNT(Skeletons)
		? TConstArrayView<FScctBridgeBoneInfo>(Skeletons[Kind])
		: TConstArrayView<FScctBridgeBoneInfo>();
}

AScctPawnProxy* UScctSessionSubsystem::FindProxy(int32 ActorId) const
{
	const TObjectPtr<AScctPawnProxy>* Proxy = Proxies.Find(ActorId);
	return Proxy != nullptr && IsValid(*Proxy) ? Proxy->Get() : nullptr;
}

void UScctSessionSubsystem::ApplyViewToPlayer()
{
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (AScctViewPawn* ViewPawn = Player != nullptr ? Cast<AScctViewPawn>(Player->GetPawn()) : nullptr)
	{
		ViewPawn->ApplyView(View);
	}
}

void UScctSessionSubsystem::Fail(const FString& Reason, EScctPlayErrorKind Kind, const FString& Map, bool bRejected)
{
	// The bridge's errors are whole exception texts; the first line says what went wrong and the rest is for the log.
	FString FirstLine = Reason;
	int32 LineEnd = INDEX_NONE;
	if (FirstLine.FindChar(TEXT('\n'), LineEnd))
	{
		FirstLine.LeftInline(LineEnd);
	}
	FirstLine.TrimStartAndEndInline();
	// An exception's first line starts with its type ("System.IO.FileNotFoundException: ..."): the player reads the message.
	int32 TypeEnd = FirstLine.Find(TEXT("Exception: "));
	if (TypeEnd != INDEX_NONE && !FirstLine.Left(TypeEnd).Contains(TEXT(" ")))
	{
		FirstLine.RightChopInline(TypeEnd + 11);
	}
	if (FirstLine.IsEmpty())
	{
		FirstLine = TEXT("Unknown error");
	}

	Failure = FirstLine;
	UE_LOG(LogOpenSCCT, Error, TEXT("OpenSCCT match failed: %s"), *Reason);

	// The page that shows it is the match overlay's (Menu/SScctPlayErrorPage.h), in every build: the engine's
	// on-screen debug messages are compiled out of Shipping.
	PlayError.Kind = Kind;
	PlayError.Map = Map;
	PlayError.Reason = FirstLine;
	PlayError.bRejected = bRejected;
	++PlayError.Serial;
	static const TCHAR* const KindNames[] = { TEXT("none"), TEXT("start failed"), TEXT("disconnected"), TEXT("server stopped") };
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctPlayError: %s on '%s'%s: %s (error %d)"), KindNames[static_cast<int32>(Kind)], *Map,
		bRejected ? TEXT(" (rejected)") : TEXT(""), *FirstLine, PlayError.Serial);
	OnPlayError.Broadcast(PlayError);
}

void UScctSessionSubsystem::CheckVisitFailure()
{
	const UGameInstance* Game = GetWorld()->GetGameInstance();
	const UScctOnlineSubsystem* Visit = Game != nullptr ? Game->GetSubsystem<UScctOnlineSubsystem>() : nullptr;
	const bool bFailed = Visit != nullptr && Visit->GetState() == EScctOnlineState::Failed;
	if (!bFailed)
	{
		bVisitFailureSeen = false;
		SerialBeforeVisitFailure = PlayError.Serial;
		return;
	}
	if (bVisitFailureSeen)
	{
		return;
	}
	bVisitFailureSeen = true;
	if (PlayError.Serial != SerialBeforeVisitFailure && PlayError.Kind == EScctPlayErrorKind::Disconnected)
	{
		// The round's own failure (CheckSession) already said it.
		return;
	}

	// Only a level opened for the visit's round shows it here; the front end shows the visit's notice itself.
	if (!GetWorld()->URL.HasOption(UScctOnlineSubsystem::OnlineOption))
	{
		return;
	}
	const FString Map = SessionId != 0 ? FString(SessionInfo.Map) : (!Visit->GetMapToLoad().IsEmpty() ? Visit->GetMapToLoad() : LastPresentedMap);
	const bool bRejected = Visit->HasFlag(EScctOnlineStatusFlags::Kicked);
	const FString Reason = Visit->GetNotice().IsEmpty() ? FString(TEXT("Connection closed")) : Visit->GetNotice();
	if (SessionId != 0)
	{
		// The round is gone with the connection; the world stops presenting it (the visit is the bridge's to end).
		ChangeSession(FScctBridgeSessionInfo{}, false);
	}
	Fail(Reason, EScctPlayErrorKind::Disconnected, Map, bRejected);
}

void UScctSessionSubsystem::TickForcedDisconnect(float DeltaTime)
{
	if (!bForceDisconnectRead)
	{
		bForceDisconnectRead = true;
		float Seconds = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctForceDisconnect="), Seconds) && Seconds > 0.0f)
		{
			ForceDisconnectIn = Seconds;
		}
	}
	if (ForceDisconnectIn < 0.0f || SessionId == 0)
	{
		return;
	}
	ForceDisconnectIn -= DeltaTime;
	if (ForceDisconnectIn >= 0.0f)
	{
		return;
	}

	// What a lost connection leaves behind: the session is gone and the player is told, nothing else changes.
	ForceDisconnectIn = -1.0f;
	const FString Map = SessionInfo.Map;
	UE_LOG(LogOpenSCCT, Log, TEXT("-ScctForceDisconnect: ending session %d as a lost connection would"), SessionId);
	StopMatch();
	Fail(TEXT("Connection closed (-ScctForceDisconnect)"), EScctPlayErrorKind::Disconnected, Map);
}

void UScctSessionSubsystem::LogFeatureTables() const
{
	LogFeatureTable<FScctControlsApi>(TEXT("controls"));
	LogFeatureTable<FScctEventsApi>(TEXT("events"));
	LogFeatureTable<FScctHudApi>(TEXT("HUD"));
	LogFeatureTable<FScctWorldApi>(TEXT("level state"));
	LogFeatureTable<FScctEquipmentApi>(TEXT("equipment"));
	LogFeatureTable<FScctOnlineApi>(TEXT("online"));
	LogFeatureTable<FScctVoiceApi>(TEXT("voice"));
	LogFeatureTable<FScctProjectionsApi>(TEXT("projections"));
	LogFeatureTable<FScctSmokeApi>(TEXT("responsive smoke"));
	LogFeatureTable<FScctSoftBodyApi>(TEXT("soft bodies"));
}

void UScctSessionSubsystem::TickAutoShots(float DeltaTime)
{
	if (ShotsLeft < 0)
	{
		// First tick: read the request once.
		ShotsLeft = 0;
		FParse::Value(FCommandLine::Get(), TEXT("ScctShots="), ShotsLeft);
		float Every = 5.0f;
		FParse::Value(FCommandLine::Get(), TEXT("ScctShotEvery="), Every);
		ShotTimer = Every;

		FString Cameras;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctCams="), Cameras, false))
		{
			TArray<FString> Entries;
			Cameras.ParseIntoArray(Entries, TEXT(";"));
			for (const FString& Entry : Entries)
			{
				TArray<FString> Numbers;
				Entry.ParseIntoArray(Numbers, TEXT(","));
				if (Numbers.Num() >= 5)
				{
					const FVector Location(FCString::Atof(*Numbers[0]), FCString::Atof(*Numbers[1]), FCString::Atof(*Numbers[2]));
					const FRotator Rotation(FCString::Atof(*Numbers[4]), FCString::Atof(*Numbers[3]), 0.0);
					TourCameras.Add(FTransform(Rotation, Location));
				}
			}
			bTourView = TourCameras.Num() > 0;
		}
	}

	if (bAdvanceTour)
	{
		// The shot requested last tick has been taken from the old point.
		bAdvanceTour = false;
		++TourIndex;
	}

	if (QuitTimer >= 0.0f)
	{
		QuitTimer -= DeltaTime;
		if (QuitTimer < 0.0f)
		{
			FPlatformMisc::RequestExit(false);
		}
		return;
	}
	if (ShotsLeft <= 0)
	{
		return;
	}

	ShotTimer -= DeltaTime;
	if (ShotTimer > 0.0f)
	{
		return;
	}

	FString Directory = FPaths::ProjectSavedDir() / TEXT("ScctShots");
	FParse::Value(FCommandLine::Get(), TEXT("ScctShotDir="), Directory);
	// Numbered through the whole run, not per level: a run that comes back to a map (menu, match, menu, match)
	// keeps every picture instead of writing over the first visit's.
	static int32 RunShotIndex = 0;
	const FString File = Directory / FString::Printf(TEXT("%s_%02d.png"), *GetWorld()->GetMapName(), RunShotIndex++);
	// -ScctShotUI: the screenshot includes the HUD and everything else Slate draws over the picture.
	static const bool bShowUI = FParse::Param(FCommandLine::Get(), TEXT("ScctShotUI"));
	FScreenshotRequest::RequestScreenshot(File, bShowUI, false);
	bAdvanceTour = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("Screenshot %s (view valid %d at %.0f %.0f %.0f, session %d)"), *File, View.Valid, View.X, View.Y, View.Z, SessionId);

	float Every = 5.0f;
	FParse::Value(FCommandLine::Get(), TEXT("ScctShotEvery="), Every);
	ShotTimer = Every;
	if (--ShotsLeft == 0)
	{
		// Leave the last request time to be written.
		QuitTimer = 2.0f;
	}
}

void UScctSessionSubsystem::ApplyTourCamera()
{
	if (!bTourView || !TourCameras.IsValidIndex(TourIndex))
	{
		return;
	}

	// The camera sits at the next shot's point for the whole wait, so lighting and exposure settle there.
	const FTransform& Camera = TourCameras[TourIndex];
	const FRotator Rotation = Camera.Rotator();
	View.X = Camera.GetLocation().X;
	View.Y = Camera.GetLocation().Y;
	View.Z = Camera.GetLocation().Z;
	View.Pitch = Rotation.Pitch;
	View.Yaw = Rotation.Yaw;
	View.Roll = 0.0f;
	View.Fov = 75.0f;
	View.Valid = 1;
	View.SpyFirstPerson = 0;
}
