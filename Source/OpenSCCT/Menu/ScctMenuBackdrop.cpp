#include "Menu/ScctMenuBackdrop.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HDRHelper.h"
#include "Hub/ScctHubSubsystem.h"
#include "Menu/ScctFrontEndGameMode.h"
#include "Menu/ScctUserSettings.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctBackdropApi.h"
#include "ShaderCompiler.h"
#include "Styling/SlateBrush.h"
#include "TextureResource.h"
#include "UnrealClient.h"

namespace
{
	/** How much of the scene the shade covers; the shade reads it every time it is drawn. */
	float GScctMenuBackdropCover = 0.0f;

	/** How opaque the held frame of the scene is; the shade reads it every time it is drawn. */
	float GScctMenuBackdropFreeze = 0.0f;

	/** The brush of the held frame. Its texture belongs to the director that made it. */
	FSlateBrush& ScctMenuBackdropFreezeBrush()
	{
		static FSlateBrush Brush;
		return Brush;
	}

	/** Seconds the scene takes to dip to the shade's ink, and to come back. */
	constexpr double ScctMenuBackdropCoverIn = 0.4;
	constexpr double ScctMenuBackdropCoverOut = 0.6;

	/** Seconds the held frame takes to fade away over the new scene, and the longest it is waited for. */
	constexpr double ScctMenuBackdropDissolve = 1.1;
	constexpr double ScctMenuBackdropFreezeWait = 0.5;

	/** The least time a new match runs under the cover before it is shown: its players stand in the world. */
	constexpr double ScctMenuBackdropWarm = 0.4;

	/** The still's first frames are the level's own to stutter in: nothing of the live backdrop starts in them. */
	constexpr double ScctMenuBackdropSettle = 1.5;

	/** A frame longer than this during a change gets its own line in the log. */
	constexpr double ScctMenuBackdropLongFrameMs = 100.0;

	/** The longest a hidden level waits for its shaders (wall clock seconds) before it is used anyway. */
	constexpr double ScctMenuBackdropShaderWait = 12.0;

	/** A level and a match that are not ready after this long are given up. */
	constexpr double ScctMenuBackdropPrepareLimit = 90.0;

	/** A match that is over stays on screen this long (its end is part of the picture) before the next map. */
	constexpr double ScctMenuBackdropEndLinger = 6.0;

	/** Degrees the held picture sways after its match has stopped. */
	constexpr float ScctMenuBackdropHeldSway = 2.0f;

	const FName ScctMenuBackdropFallbackTag(TEXT("ScctFallback"));

	/** Frames the cover stays after the old level is gone, so the collection of its objects is not in the fade. */
	constexpr int32 ScctMenuBackdropCollectFrames = 4;

	bool ScctMenuBackdropLevelGone(const ULevelStreamingDynamic* Level)
	{
		return !IsValid(Level) || Level->GetLoadedLevel() == nullptr;
	}
}

float UScctMenuBackdrop::GetCover()
{
	return GScctMenuBackdropCover;
}

const FSlateBrush* UScctMenuBackdrop::GetFreeze(float& OutOpacity)
{
	OutOpacity = GScctMenuBackdropFreeze;
	const FSlateBrush& Brush = ScctMenuBackdropFreezeBrush();
	return OutOpacity > 0.0f && IsValid(Brush.GetResourceObject()) ? &Brush : nullptr;
}

void UScctMenuBackdrop::AskFreeze()
{
	bFreezeAsked = true;
	bFreezeTaken = false;
	// The viewport hands a screenshot to whoever listens instead of writing a file: only this one request is
	// listened to, and nothing is asked while somebody else listens.
	if (!bFreezeAllowed || UGameViewportClient::OnScreenshotCaptured().IsBound() || FScreenshotRequest::IsScreenshotRequested())
	{
		return;
	}
	// On an HDR display the viewport's screenshot is a floating point picture that goes to its own listeners (or to
	// a file): it is not asked for, and the change dips to the shade's ink.
	if (IsHDREnabled())
	{
		return;
	}
	FreezeHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UScctMenuBackdrop::HandleFreeze);
	FScreenshotRequest::RequestScreenshot(false);
}

void UScctMenuBackdrop::ForgetFreezeRequest()
{
	if (FreezeHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(FreezeHandle);
		FreezeHandle.Reset();
	}
}

void UScctMenuBackdrop::HandleFreeze(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	ForgetFreezeRequest();
	if (Width <= 0 || Height <= 0 || Bitmap.Num() != Width * Height)
	{
		return;
	}
	if (!IsValid(FreezeTexture) || FreezeTexture->GetSizeX() != Width || FreezeTexture->GetSizeY() != Height)
	{
		FreezeTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!IsValid(FreezeTexture))
		{
			return;
		}
		// The pixels are the screen's own, already in the display's encoding: they are drawn back as they are.
		FreezeTexture->SRGB = false;
		FreezeTexture->NeverStream = true;
		FreezeTexture->Filter = TF_Bilinear;
	}
	FTexturePlatformData* Platform = FreezeTexture->GetPlatformData();
	if (Platform == nullptr || Platform->Mips.Num() == 0)
	{
		return;
	}
	void* Pixels = Platform->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Pixels, Bitmap.GetData(), static_cast<SIZE_T>(Bitmap.Num()) * sizeof(FColor));
	Platform->Mips[0].BulkData.Unlock();
	FreezeTexture->UpdateResource();

	FSlateBrush& Brush = ScctMenuBackdropFreezeBrush();
	Brush.SetResourceObject(FreezeTexture);
	Brush.SetImageSize(FVector2f(static_cast<float>(Width), static_cast<float>(Height)));
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.Tiling = ESlateBrushTileType::NoTile;
	bFreezeTaken = true;
}

void UScctMenuBackdrop::SetCovered(float Amount)
{
	GScctMenuBackdropFreeze = bCoverIsFreeze ? Amount : 0.0f;
	GScctMenuBackdropCover = bCoverIsFreeze ? 0.0f : Amount;
}

void UScctMenuBackdrop::BeginDestroy()
{
	// The shade must not draw a frame whose texture goes with this director.
	ForgetFreezeRequest();
	if (ScctMenuBackdropFreezeBrush().GetResourceObject() == FreezeTexture)
	{
		ScctMenuBackdropFreezeBrush().SetResourceObject(nullptr);
		GScctMenuBackdropFreeze = 0.0f;
	}
	Super::BeginDestroy();
}

void UScctMenuBackdrop::SetPhase(EPhase NewPhase)
{
	Phase = NewPhase;
	PhaseStarted = FPlatformTime::Seconds();
}

bool UScctMenuBackdrop::IsWanted(const UWorld& World, const UScctSessionSubsystem* Session, const TCHAR*& OutWhy) const
{
	const UScctUserSettings* Settings = UScctUserSettings::Get();
	if (ForcedOption == 0)
	{
		OutWhy = TEXT("it is off for this run (-ScctMenuLive=0, or an unattended run without -ScctMenuLive=1)");
		return false;
	}
	if (Settings != nullptr && !Settings->bLiveMenuBackground)
	{
		OutWhy = TEXT("the option Live menu background is off");
		return false;
	}
	if (const UScctOnlineSubsystem* Online = UScctOnlineSubsystem::Get(&World); Online != nullptr && Online->IsVisiting())
	{
		OutWhy = TEXT("an online visit is under way");
		return false;
	}
	if (const UScctHubSubsystem* Hub = UScctHubSubsystem::Get(&World); Hub != nullptr && Hub->IsVisiting())
	{
		OutWhy = TEXT("a hub visit is under way");
		return false;
	}
	if (Session != nullptr && Session->IsPlaying() && !Session->IsBackdrop())
	{
		OutWhy = TEXT("a match plays");
		return false;
	}
	return true;
}

FString UScctMenuBackdrop::PickNext()
{
	if (!bOrderRead)
	{
		bOrderRead = true;
		FString Given;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctMenuLiveMaps="), Given, false))
		{
			Given.ParseIntoArray(Order, TEXT(","));
			bOrderGiven = Order.Num() > 0;
		}
		if (!bOrderGiven)
		{
			Order = UScctOnlineSubsystem::FindPlayableMaps();
			// The drill range is a test hall, not a place a match is played.
			Order.RemoveAll([](const FString& Map) { return Map.Equals(TEXT("TestRange"), ESearchCase::IgnoreCase); });
			for (int32 Index = Order.Num() - 1; Index > 0; --Index)
			{
				Order.Swap(Index, FMath::RandRange(0, Index));
			}
			// The first match plays where the still already stands, so no second level has to load for it.
			if (const int32 Still = Order.IndexOfByKey(ShownMap); Still > 0)
			{
				Order.Swap(0, Still);
			}
		}
		OrderIndex = 0;
	}

	for (int32 Tried = 0; Tried < Order.Num(); ++Tried)
	{
		const FString Map = Order[OrderIndex % Order.Num()];
		OrderIndex = (OrderIndex + 1) % Order.Num();
		if (Refused.Contains(Map) || !FPackageName::DoesPackageExist(UScctOnlineSubsystem::ImportedLevelOf(Map)))
		{
			continue;
		}
		// Another map than the one on screen, unless it is the only one (its match then starts again).
		if (Map == ShownMap && bFirstShown && Order.Num() - Refused.Num() > 1)
		{
			continue;
		}
		return Map;
	}
	return FString();
}

void UScctMenuBackdrop::DropNextLevel()
{
	if (IsValid(NextLevel) && NextLevel != ShownLevel)
	{
		NextLevel->SetShouldBeVisible(false);
		NextLevel->SetIsRequestingUnloadAndRemoval(true);
	}
	NextLevel = nullptr;
	NextMap.Reset();
}

void UScctMenuBackdrop::HoldPicture(AScctFrontEndGameMode& GameMode)
{
	if (bHaveLastView)
	{
		GameMode.SetStillCamera(LastViewLocation, LastViewRotation, ScctMenuBackdropHeldSway);
	}
}

void UScctMenuBackdrop::Suspend(UWorld& World, const FScctBackdropApi* Api, UScctSessionSubsystem* Session, AScctFrontEndGameMode& GameMode, const TCHAR* Why)
{
	bSuspended = true;
	const bool bWasBusy = Phase != EPhase::Idle || (Session != nullptr && Session->IsBackdrop());
	if (Api != nullptr)
	{
		Api->Cancel();
	}
	if (Session != nullptr && Session->IsBackdrop())
	{
		// The picture holds where the match was last seen; only a backdrop match is ever stopped from here.
		HoldPicture(GameMode);
		Session->StopMatch();
	}
	DropNextLevel();
	if (IsValid(ShownLevel) && !ShownLevel->ShouldBeVisible())
	{
		ShownLevel->SetShouldBeVisible(true);
	}
	GScctMenuBackdropCover = 0.0f;
	GScctMenuBackdropFreeze = 0.0f;
	ForgetFreezeRequest();
	SetPhase(EPhase::Idle);
	if (bWasBusy)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop stopped (%s); the picture holds"), Why);
	}
}

void UScctMenuBackdrop::Shutdown(UWorld* World)
{
	if (bShutDown)
	{
		return;
	}
	bShutDown = true;
	GScctMenuBackdropCover = 0.0f;
	GScctMenuBackdropFreeze = 0.0f;
	ForgetFreezeRequest();
	UScctSessionSubsystem* Session = World != nullptr ? World->GetSubsystem<UScctSessionSubsystem>() : nullptr;
	const bool bPlayed = Session != nullptr && Session->IsBackdrop();
	if (Phase == EPhase::Idle && !bPlayed)
	{
		// Nothing was ever asked of the bridge for the backdrop: it is not asked now either.
		return;
	}
	if (const FScctBackdropApi* Api = ScctFeature<FScctBackdropApi>())
	{
		Api->Cancel();
	}
	if (bPlayed)
	{
		Session->StopMatch();
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop shut down as the menu is left (%s)"), bPlayed ? TEXT("its match was stopped") : TEXT("no match was playing"));
	SetPhase(EPhase::Idle);
}

void UScctMenuBackdrop::StartNext(UWorld& World, const FScctBackdropApi& Api)
{
	const double Now = FPlatformTime::Seconds();
	const FString Map = PickNext();
	if (Map.IsEmpty())
	{
		// Nothing to play: no imported map, or every one was refused. The picture stays as it is.
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: no map for a live backdrop (%d known, %d refused); the picture stays as it is"), Order.Num(), Refused.Num());
		NextTryAt = Now + 3600.0;
		return;
	}

	NextMap = Map;
	if (Map == ShownMap && IsValid(ShownLevel))
	{
		NextLevel = ShownLevel;
	}
	else
	{
		bool bStarted = false;
		ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstance(&World, UScctOnlineSubsystem::ImportedLevelOf(Map), FVector::ZeroVector, FRotator::ZeroRotator, bStarted);
		if (!bStarted || Streaming == nullptr)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop: the level of %s could not be streamed in"), *Map);
			Refused.Add(Map);
			NextMap.Reset();
			NextTryAt = Now + 0.5;
			return;
		}
		// Loaded hidden: the picture on screen stays until the exchange under the cover.
		Streaming->SetShouldBeVisible(false);
		NextLevel = Streaming;
	}

	Api.Begin(*Map);
	PrepareStarted = Now;
	bOldDropped = false;
	FramesSinceOldGone = 0;
	HideSeconds = ShowSeconds = WarmSeconds = 0.0;
	LongestLoadingFrameMs = LongestCoveredFrameMs = LongestFadeFrameMs = AdoptMs = 0.0;
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop: %s is being made ready (%s) while %s stays on screen"), *Map,
		NextLevel == ShownLevel ? TEXT("its level is the one shown") : TEXT("its level streams in hidden"),
		ShownMap.IsEmpty() ? TEXT("the level's own scene") : *ShownMap);
	SetPhase(EPhase::Preparing);
}

void UScctMenuBackdrop::TickPreparing(UWorld& World, const FScctBackdropApi& Api)
{
	const double Now = FPlatformTime::Seconds();
	FScctBackdropStatus Status = {};
	Api.GetStatus(&Status);

	const TCHAR* Failure = nullptr;
	FString Reason;
	if (Status.Prepare == EScctBackdropPrepare::Failed)
	{
		TCHAR Text[512] = {};
		Api.GetError(Text, UE_ARRAY_COUNT(Text));
		Reason = Text;
		int32 LineEnd = INDEX_NONE;
		if (Reason.FindChar(TEXT('\n'), LineEnd))
		{
			Reason.LeftInline(LineEnd);
		}
		Failure = *Reason;
	}
	else if (Status.Prepare == EScctBackdropPrepare::Idle)
	{
		Failure = TEXT("its start was dropped (something else started)");
	}
	else if (!IsValid(NextLevel) || NextLevel->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad)
	{
		Failure = TEXT("its level failed to load");
	}
	else if (Now - PrepareStarted > ScctMenuBackdropPrepareLimit)
	{
		Failure = TEXT("it was not ready in time");
	}
	if (Failure != nullptr)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop: %s is not used: %s"), *NextMap, Failure);
		// A dropped start says nothing against the map; everything else does, for this run.
		if (Status.Prepare != EScctBackdropPrepare::Idle)
		{
			Refused.Add(NextMap);
		}
		Api.Cancel();
		DropNextLevel();
		NextTryAt = Now + (Status.Prepare == EScctBackdropPrepare::Idle ? 5.0 : 0.5);
		const UScctSessionSubsystem* Session = World.GetSubsystem<UScctSessionSubsystem>();
		SetPhase(Session != nullptr && Session->IsBackdrop() ? EPhase::Live : EPhase::Idle);
		return;
	}

	bool bLevelReady = NextLevel == ShownLevel;
	if (!bLevelReady && NextLevel->IsLevelLoaded())
	{
		// Uncooked runs compile the map's materials on first sight: until then it draws grey or black.
		const int32 Jobs = GShaderCompilingManager != nullptr ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
		bLevelReady = Jobs == 0 || Now - PrepareStarted >= ScctMenuBackdropShaderWait;
	}
	if (bLevelReady && Status.Prepare == EScctBackdropPrepare::Ready)
	{
		CoverStarted = Now;
		bLiveAfterCover = true;
		bCoverIsFreeze = false;
		bFreezeAsked = false;
		SetPhase(EPhase::CoverIn);
	}
}

void UScctMenuBackdrop::Exchange(UWorld& World, UScctSessionSubsystem& Session)
{
	// The scene is covered. The match shown until now ends first, so one match at most ever reads the world.
	if (Session.IsBackdrop())
	{
		Session.StopMatch();
	}
	if (NextLevel != ShownLevel && IsValid(ShownLevel))
	{
		ShownLevel->SetShouldBeVisible(false);
		SetPhase(EPhase::HideOld);
		return;
	}
	if (IsValid(NextLevel))
	{
		NextLevel->SetShouldBeVisible(true);
	}
	SetPhase(EPhase::ShowNew);
}

void UScctMenuBackdrop::Adopt(UWorld& World, const FScctBackdropApi& Api, UScctSessionSubsystem& Session)
{
	const double Before = FPlatformTime::Seconds();
	const int32 Id = Api.Adopt();
	const bool bPresented = Id != 0 && Session.PresentBackdrop(Id);
	AdoptMs = (FPlatformTime::Seconds() - Before) * 1000.0;
	if (!bPresented)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop: the match on %s could not be adopted (session %d)"), *NextMap, Id);
		// Warm sees that no backdrop session plays and gives the map up.
	}
	SetPhase(EPhase::Warm);
}

void UScctMenuBackdrop::GiveUpOnNext(UWorld& World, const FScctBackdropApi& Api, UScctSessionSubsystem& Session, AScctFrontEndGameMode& GameMode, const TCHAR* Why)
{
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop: %s is given up under the cover (%s); the picture before comes back"), *NextMap, Why);
	Refused.Add(NextMap);
	Api.Cancel();
	if (Session.IsBackdrop())
	{
		Session.StopMatch();
	}
	const bool bOtherLevel = NextLevel != ShownLevel;
	DropNextLevel();
	if (bOtherLevel && IsValid(ShownLevel))
	{
		ShownLevel->SetShouldBeVisible(true);
	}
	HoldPicture(GameMode);
	bLiveAfterCover = false;
	NextTryAt = FPlatformTime::Seconds() + 3.0;
	SetPhase(EPhase::CoverOut);
}

void UScctMenuBackdrop::Tick(AScctFrontEndGameMode& GameMode, float DeltaSeconds, bool bStillSettled, const FString& StillMap, ULevelStreamingDynamic* StillLevel)
{
	UWorld* World = GameMode.GetWorld();
	if (bShutDown || World == nullptr)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (!bSwitchesRead)
	{
		bSwitchesRead = true;
		CreatedAt = Now;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ScctMenuLive="), ForcedOption) && FApp::IsUnattended())
		{
			// Unattended checks of the menu keep the still they were written against.
			ForcedOption = 0;
		}
		int32 Freeze = 1;
		FParse::Value(FCommandLine::Get(), TEXT("ScctMenuLiveFreeze="), Freeze);
		bFreezeAllowed = Freeze != 0;
		double Seconds = 0.0;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctMenuLiveSwapSeconds="), Seconds) && Seconds > 0.0)
		{
			SwapSeconds = FMath::Max(Seconds, 5.0);
		}
	}

	// The still (or the level's own scene) is the first picture; nothing is done before it is up.
	if (!bStillSettled)
	{
		return;
	}
	if (SettledAt == 0.0)
	{
		SettledAt = Now;
		NextTryAt = Now + ScctMenuBackdropSettle;
	}
	if (!bFirstShown && ShownMap.IsEmpty() && Phase == EPhase::Idle && IsValid(StillLevel))
	{
		ShownMap = StillMap;
		ShownLevel = StillLevel;
	}

	UScctSessionSubsystem* Session = World->GetSubsystem<UScctSessionSubsystem>();
	if (Session == nullptr)
	{
		return;
	}
	const TCHAR* Why = TEXT("");
	if (!IsWanted(*World, Session, Why))
	{
		if (!bSuspended)
		{
			// The table is only asked for when something of the backdrop is there to drop.
			const bool bBusy = Phase != EPhase::Idle || Session->IsBackdrop();
			Suspend(*World, bBusy ? ScctFeature<FScctBackdropApi>() : nullptr, Session, GameMode, Why);
		}
		return;
	}
	if (bSuspended)
	{
		// Wanted again: a moment later, so a page that only flickers through a visit does not start a map load.
		bSuspended = false;
		NextTryAt = FMath::Max(NextTryAt, Now + 1.5);
	}
	if (Phase == EPhase::Idle && Now < NextTryAt)
	{
		return;
	}

	const FScctBackdropApi* Api = ScctFeature<FScctBackdropApi>();
	if (Api == nullptr)
	{
		// No bridge, or one without the backdrop: the still is all there is. Asked again now and then.
		NextTryAt = Now + 30.0;
		return;
	}

	// What a change costs the menu: the longest frame of each of its stretches.
	const double FrameMs = FApp::GetDeltaTime() * 1000.0;
	if (FrameMs > ScctMenuBackdropLongFrameMs && Phase != EPhase::Idle && Phase != EPhase::Live)
	{
		static const TCHAR* const PhaseNames[] = { TEXT("idle"), TEXT("making the next map ready"), TEXT("fading out"), TEXT("covered, taking the old level out"),
			TEXT("covered, putting the new level in"), TEXT("covered, the new match's first ticks"), TEXT("fading in"), TEXT("live") };
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop: a frame of %.0f ms %s (%.2f s into it)"), FrameMs, PhaseNames[static_cast<int32>(Phase)], Now - PhaseStarted);
	}
	switch (Phase)
	{
	case EPhase::Preparing:
		LongestLoadingFrameMs = FMath::Max(LongestLoadingFrameMs, FrameMs);
		break;
	case EPhase::CoverIn:
	case EPhase::CoverOut:
		LongestFadeFrameMs = FMath::Max(LongestFadeFrameMs, FrameMs);
		break;
	case EPhase::HideOld:
	case EPhase::ShowNew:
	case EPhase::Warm:
		LongestCoveredFrameMs = FMath::Max(LongestCoveredFrameMs, FrameMs);
		break;
	default:
		break;
	}

	const bool bMatch = Session->IsBackdrop();
	if (bMatch && Session->GetView().Valid != 0)
	{
		const FScctBridgeView& View = Session->GetView();
		bHaveLastView = true;
		LastViewLocation = FVector(View.X, View.Y, View.Z);
		LastViewRotation = FRotator(View.Pitch, View.Yaw, 0.0f);
	}

	const double InPhase = Now - PhaseStarted;
	switch (Phase)
	{
	case EPhase::Idle:
		StartNext(*World, *Api);
		break;

	case EPhase::Preparing:
		TickPreparing(*World, *Api);
		break;

	case EPhase::CoverIn:
		if (!bFreezeAsked)
		{
			// The frame drawn at the end of this tick is the one that is held.
			AskFreeze();
			if (FreezeHandle.IsValid())
			{
				break;
			}
		}
		if (bFreezeTaken)
		{
			// The held frame is the picture on screen: it goes over the scene at once, and nothing is seen to change.
			bFreezeTaken = false;
			bCoverIsFreeze = true;
			SetCovered(1.0f);
			Exchange(*World, *Session);
			break;
		}
		if (FreezeHandle.IsValid())
		{
			if (InPhase < ScctMenuBackdropFreezeWait)
			{
				break;
			}
			// No frame came back: this change dips to the shade's ink instead.
			ForgetFreezeRequest();
			PhaseStarted = Now;
			break;
		}
		SetCovered(static_cast<float>(FMath::Clamp(InPhase / ScctMenuBackdropCoverIn, 0.0, 1.0)));
		if (GScctMenuBackdropCover >= 1.0f)
		{
			Exchange(*World, *Session);
		}
		break;

	case EPhase::HideOld:
		if (!IsValid(ShownLevel) || !ShownLevel->IsLevelVisible() || InPhase > 15.0)
		{
			if (IsValid(NextLevel))
			{
				NextLevel->SetShouldBeVisible(true);
			}
			HideSeconds = InPhase;
			SetPhase(EPhase::ShowNew);
		}
		break;

	case EPhase::ShowNew:
		if (!IsValid(NextLevel))
		{
			GiveUpOnNext(*World, *Api, *Session, GameMode, TEXT("its level went away"));
		}
		else if (NextLevel->IsLevelVisible())
		{
			ShowSeconds = InPhase;
			Adopt(*World, *Api, *Session);
		}
		else if (InPhase > 20.0)
		{
			GiveUpOnNext(*World, *Api, *Session, GameMode, TEXT("its level did not come into the world"));
		}
		break;

	case EPhase::Warm:
	{
		FScctBackdropStatus Status = {};
		Api->GetStatus(&Status);
		if (!bMatch && !bOldDropped)
		{
			GiveUpOnNext(*World, *Api, *Session, GameMode, TEXT("its match is not playing"));
			break;
		}
		const bool bSeen = Status.ViewValid != 0 && Status.PawnsAlive > 0 && Status.TargetActorId != 0 && Session->GetView().Valid != 0;
		if (bSeen && InPhase >= ScctMenuBackdropWarm && !bOldDropped)
		{
			// The new match is there to be seen. The level shown before goes now, still under the cover, so the
			// collection of its objects is not felt in the picture that comes back.
			bOldDropped = true;
			OldDroppedAt = Now;
			SwapFrom = ShownMap;
			if (IsValid(ShownLevel) && ShownLevel != NextLevel)
			{
				OldLevel = ShownLevel;
				OldLevel->SetIsRequestingUnloadAndRemoval(true);
			}
		}
		const bool bOldGone = bOldDropped && (OldLevel == nullptr || ScctMenuBackdropLevelGone(OldLevel) || Now - OldDroppedAt > 3.0);
		FramesSinceOldGone = bOldGone ? FramesSinceOldGone + 1 : 0;
		if (bOldGone && (OldLevel == nullptr || FramesSinceOldGone > ScctMenuBackdropCollectFrames))
		{
			// The level's own stand-in scene would shine into the map (the game mode removes it for a still).
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(ScctMenuBackdropFallbackTag))
				{
					It->Destroy();
				}
			}
			OldLevel = nullptr;
			ShownLevel = NextLevel;
			ShownMap = NextMap;
			NextLevel = nullptr;
			NextMap.Reset();
			WarmSeconds = InPhase;
			SetPhase(EPhase::CoverOut);
		}
		else if (!bOldDropped && InPhase > 12.0)
		{
			GiveUpOnNext(*World, *Api, *Session, GameMode, TEXT("it showed no player in time"));
		}
		break;
	}

	case EPhase::CoverOut:
		// A level coming back into the world after a map was given up is waited for under the cover.
		if (!bLiveAfterCover && IsValid(ShownLevel) && !ShownLevel->IsLevelVisible() && InPhase < 10.0)
		{
			PhaseStarted = Now;
			break;
		}
		// The held frame leaves slowly at both ends, as a dissolve does; the ink goes evenly.
		SetCovered(bCoverIsFreeze
			? static_cast<float>(FMath::SmoothStep(0.0, 1.0, FMath::Clamp(1.0 - InPhase / ScctMenuBackdropDissolve, 0.0, 1.0)))
			: static_cast<float>(FMath::Clamp(1.0 - InPhase / ScctMenuBackdropCoverOut, 0.0, 1.0)));
		if (GScctMenuBackdropCover <= 0.0f && GScctMenuBackdropFreeze <= 0.0f)
		{
			if (!bLiveAfterCover)
			{
				SetPhase(EPhase::Idle);
				break;
			}
			const double Covered = Now - CoverStarted;
			if (!bFirstShown)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop is %s (2 v 2 bots, shown %.1f s after the menu opened; made ready in %.1f s, %s %.2f s: old level out %.2f, new level in %.2f, first ticks %.2f; longest frame %.0f ms while it was made ready, %.0f ms in the fades, %.0f ms under the cover; adopting the match took %.0f ms)"),
					*ShownMap, Now - CreatedAt, CoverStarted - PrepareStarted, bCoverIsFreeze ? TEXT("a held frame dissolved into it over") : TEXT("scene dipped to dark for"), Covered, HideSeconds, ShowSeconds, WarmSeconds, LongestLoadingFrameMs, LongestFadeFrameMs, LongestCoveredFrameMs, AdoptMs);
			}
			else
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Menu: backdrop swap %s -> %s (made ready in %.1f s behind the old match, %s %.2f s: old level out %.2f, new level in %.2f, first ticks %.2f; longest frame %.0f ms while the next map was made ready, %.0f ms in the fades, %.0f ms under the cover; adopting the match took %.0f ms)"),
					SwapFrom.IsEmpty() ? TEXT("the level's own scene") : *SwapFrom, *ShownMap, CoverStarted - PrepareStarted, bCoverIsFreeze ? TEXT("a held frame dissolved into it over") : TEXT("scene dipped to dark for"), Covered, HideSeconds, ShowSeconds, WarmSeconds, LongestLoadingFrameMs, LongestFadeFrameMs, LongestCoveredFrameMs, AdoptMs);
			}
			bFirstShown = true;
			SwapFrom.Reset();
			LiveSince = Now;
			EndedSince = 0.0;
			NextTryAt = 0.0;
			SetPhase(EPhase::Live);
		}
		break;

	case EPhase::Live:
	{
		if (!bMatch)
		{
			// The match is gone (its server stopped, or something took the bridge's slot): the picture holds.
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop: the match on %s is gone; the picture holds and another map is tried"), *ShownMap);
			HoldPicture(GameMode);
			NextTryAt = Now + 2.0;
			SetPhase(EPhase::Idle);
			break;
		}
		FScctBackdropStatus Status = {};
		Api->GetStatus(&Status);
		if (Status.MatchEnded != 0 && EndedSince == 0.0)
		{
			EndedSince = Now;
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: live backdrop: the match on %s is over after %.0f s; the next map follows"), *ShownMap, Now - LiveSince);
		}
		const bool bDue = Now - LiveSince >= SwapSeconds || (EndedSince > 0.0 && Now - EndedSince >= ScctMenuBackdropEndLinger);
		if (bDue && Now >= NextTryAt)
		{
			StartNext(*World, *Api);
		}
		break;
	}
	}

	// A match that went away while the next one was being made ready: the picture holds until the exchange.
	if (!bMatch && bHaveLastView && (Phase == EPhase::Preparing || Phase == EPhase::CoverIn) && bFirstShown && !bHeldForLoss)
	{
		bHeldForLoss = true;
		HoldPicture(GameMode);
	}
	else if (bMatch)
	{
		bHeldForLoss = false;
	}
}
