#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/World.h"
#include "Hud/ScctHudElements.h"
#include "Hud/ScctHudSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctControlAutoplay.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctHudBinocularApi.h"

// The merc's rifle scope is read here too (ReadScope): the content's P_BinocularsDEF page evaluated by the bridge with the
// clip of its rings on each zoom step, the aim point of the drawn moment placed on the page for its sights, the breath,
// the range finder and the player's keys. Its painting is in ScctHudElementsBinoculars.cpp as well.
//
// The HUD subsystem's part for the spy binoculars: the objective targets placed on the screen, the content's
// P_BinocularsATT page evaluated by the bridge for this moment (the frame, sound meter and zoom areas run from the moment
// the binoculars came up, the speak box from when the Hack prompt appeared) with the player's keys in its key help, the
// Use key for the lock hint, the log lines, and the unattended -ScctBinoculars check. The painting is in
// ScctHudElementsBinoculars.cpp.

namespace
{
	/** The display name of the first key the current side has on Command, or empty. */
	FString BoundKeyName(const AScctPlayerController& Player, const TCHAR* Command)
	{
		const TArray<FKey> Keys = Player.FindKeysBoundTo(Command);
		return Keys.Num() > 0 ? Keys[0].GetDisplayName().ToString() : FString();
	}

	/** Copies a string into one of the bridge's fixed buffers, cut to fit and zero ended. */
	template <int32 N>
	void CopyInto(TCHAR (&Into)[N], const FString& From)
	{
		const int32 Count = FMath::Min(From.Len(), N - 1);
		FMemory::Memcpy(Into, *From, Count * sizeof(TCHAR));
		Into[Count] = 0;
	}

	constexpr int32 PageCapacity = 96;
}

void UScctHudSubsystem::InitBinoculars()
{
	// -ScctBinoculars=At[,Zoom+Zoom...[,Lower]]
	FString Switch;
	TArray<FString> Parts;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctBinoculars="), Switch, false))
	{
		Switch.ParseIntoArray(Parts, TEXT(","), false);
		BinocularCheck.bOn = true;
		BinocularCheck.At = Parts.IsValidIndex(0) && !Parts[0].IsEmpty() ? FCString::Atof(*Parts[0]) : BinocularCheck.At;
		if (Parts.IsValidIndex(1))
		{
			TArray<FString> Steps;
			Parts[1].ParseIntoArray(Steps, TEXT("+"));
			for (const FString& Step : Steps)
			{
				BinocularCheck.ZoomAt.Add(FCString::Atof(*Step));
			}
		}
		BinocularCheck.Lower = Parts.IsValidIndex(2) && !Parts[2].IsEmpty() ? FCString::Atof(*Parts[2]) : -1.0f;
		if (Parts.IsValidIndex(3))
		{
			TArray<FString> Steps;
			Parts[3].ParseIntoArray(Steps, TEXT("+"));
			for (const FString& Step : Steps)
			{
				BinocularCheck.UseAt.Add(FCString::Atof(*Step));
			}
		}
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("ScctBinoculars")))
	{
		BinocularCheck.bOn = true;
	}
	BinocularCheck.bAim = FParse::Param(FCommandLine::Get(), TEXT("ScctBinocularsAim"));
}

void UScctHudSubsystem::ProjectBinocularTargets()
{
	FScctHudModel& M = *Model;
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || !M.bHasState || M.State.BinocularMode != 1)
	{
		return;
	}

	// Widget units are viewport pixels over the DPI scale, as for the objective markers.
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GetWorld()), 0.01f);
	const FVector2f Centre = FVector2f(ViewportSize) * 0.5f / Scale;
	FVector ViewLocation;
	FRotator ViewRotation;
	Player->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Eye(M.State.X, M.State.Y, M.State.Z);
	for (const FScctHudObjective& Target : M.BinocularTargets)
	{
		FScctHudMarker& Marker = M.BinocularMarkers.AddDefaulted_GetRef();
		Marker.Objective = Target;
		const FVector Location(Target.X, Target.Y, Target.Z);
		Marker.Distance = static_cast<float>(FVector::Distance(Eye, Location));
		FVector2D Screen;
		Marker.bInFront = Player->ProjectWorldLocationToScreen(Location, Screen, false);
		if (Marker.bInFront)
		{
			Marker.Screen = FVector2f(Screen) / Scale;
			continue;
		}

		// Behind the view the original pushes the point to the opposite edge, so its arrow points round to the side the
		// target is on: here the point goes far out from the centre along the target's side and height.
		const FVector Local = ViewRotation.UnrotateVector(Location - ViewLocation);
		FVector2f Side(static_cast<float>(Local.Y), static_cast<float>(-Local.Z));
		if (Side.IsNearlyZero())
		{
			Side = FVector2f(0.0f, 1.0f);
		}
		Marker.Screen = Centre + Side.GetSafeNormal() * 100000.0f;
	}
}

void UScctHudSubsystem::ReadBinoculars(float DeltaSeconds)
{
	FScctHudModel& M = *Model;
	const FScctHudState& S = M.State;
	const int32 Mode = M.bHasState && (S.Flags & EScctHudFlags::HasPawn) != 0 ? S.BinocularMode : 0;
	const int32 Step = Mode == 1 ? FMath::TruncToInt32(S.BinocularFov * 0.1f) : 0;

	// The page is made anew each time the binoculars come up (spec/hud/binoculars.md rule 1): its clocks start then.
	BinocularAge = Mode == 1 && LastBinocularMode == 1 ? BinocularAge + DeltaSeconds : 0.0f;
	const bool bSpeak = Mode == 1 && (S.Flags & EScctHudFlags::BinocularHackPrompt) != 0;
	SpeakAge = bSpeak ? (SpeakAge < 0.0f ? 0.0f : SpeakAge + DeltaSeconds) : -1.0f;

	// The key help and the lock hint follow the bindings: read on entering and every two seconds while up.
	if (Mode != 0 && (LastBinocularMode != Mode || M.Clock - BinocularKeysAge > 2.0f))
	{
		BinocularKeysAge = M.Clock;
		if (const AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			// The binoculars zoom on the wheel itself whatever the walking bindings make of its notches (the input mapper
			// hands every notch to the zoom), so the key help names the wheel, as the original's does.
			BinocularZoomKeys = EKeys::MouseScrollUp.GetDisplayName().ToString() + TEXT(" / ") + EKeys::MouseScrollDown.GetDisplayName().ToString();
			BinocularExitKey = BoundKeyName(*Player, TEXT("SwitchBinocularVision"));
			M.BinocularUseKey = BoundKeyName(*Player, TEXT("InvButton bUsing"));
			ScopeBreathKey = BoundKeyName(*Player, TEXT("Button bHoldBreath"));
		}
	}
	ReadScope(DeltaSeconds, Mode);

	M.BinocularLayers.Reset();
	const FScctHudBinocularApi* Binoculars = ScctFeature<FScctHudBinocularApi>();
	if (Mode == 1 && Binoculars != nullptr)
	{
		FScctHudBinocularPageInput Input = {};
		Input.Seconds = BinocularAge;
		Input.SpeakSeconds = SpeakAge;
		Input.ZoomStep = Step;
		CopyInto(Input.ZoomKeys, BinocularZoomKeys);
		CopyInto(Input.ExitKey, BinocularExitKey);
		CopyInto(Input.SpeakText, bSpeak ? M.Text(S.HackPromptId) : FString());
		M.BinocularLayers.SetNumUninitialized(PageCapacity);
		const int32 Count = Binoculars->GetSpyPage(&Input, M.BinocularLayers.GetData(), PageCapacity);
		M.BinocularLayers.SetNum(FMath::Clamp(Count, 0, PageCapacity));
		if (Count == 0 && !bBinocularPageMissingLogged)
		{
			bBinocularPageMissingLogged = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHud: the content has no P_BinocularsATT page; the spy binoculars show only their targets"));
		}
	}
	else if (Mode == 1 && !bBinocularPageMissingLogged)
	{
		bBinocularPageMissingLogged = true;
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHud: the bridge has no binocular table; the spy binoculars show only their targets"));
	}

	// A line whenever the page opens or closes, the zoom step or the number of targets changes, or another target comes
	// under the lock.
	const int32 Candidate = Mode == 1 && ViewportSize.X > 0
		? ScctHudElements::BinocularLockCandidate(M, FVector2f(ViewportSize) / FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GetWorld()), 0.01f))
		: -1;
	const FString CandidateName = M.BinocularMarkers.IsValidIndex(Candidate)
		? (M.BinocularMarkers[Candidate].Objective.Kind == 2 ? M.Text(S.DropZoneTextId) : M.Text(M.BinocularMarkers[Candidate].Objective.NameId))
		: FString();
	const bool bMarked = M.BinocularMarkers.IsValidIndex(Candidate) && (M.BinocularMarkers[Candidate].Objective.Flags & EScctHudObjectiveFlags::Marked) != 0;
	const int32 Targets = Mode == 1 ? M.BinocularTargets.Num() : -1;
	const bool bChange = Mode != LastBinocularMode || Step != LastZoomStep || Targets != LastBinocularTargetCount || CandidateName != LastLockCandidate
		|| bMarked != bLastLockMarked;
	bLastLockMarked = bMarked;
	LastBinocularMode = Mode;
	LastZoomStep = Step;
	LastBinocularTargetCount = Targets;
	LastLockCandidate = CandidateName;
	if (bChange)
	{
		LogBinoculars(true);
	}
}

void UScctHudSubsystem::ReadScope(float DeltaSeconds, int32 Mode)
{
	FScctHudModel& M = *Model;
	const FScctHudState& S = M.State;
	M.ScopeLayers.Reset();
	if (Mode != 2)
	{
		return;
	}

	// The page is made anew each time the scope comes up (spec/hud/binoculars.md rule 1): its clocks start then, and the
	// new page stands on zoom step 1, so the scope's first step (3, the 30 degree view) plays the rings' opening clip.
	const int32 Step = FMath::TruncToInt32(S.BinocularFov * 0.1f);
	const bool bEntered = LastBinocularMode != 2;
	if (bEntered)
	{
		ScopeAge = 0.0f;
		ScopeZoomAge = -1.0f;
		ScopeZoomAnimation = 0;
		LastScopeStep = 1;
		ScopeSpeakAge = -1.0f;
		ScopeAlertAge = -1.0f;
	}
	else
	{
		ScopeAge += DeltaSeconds;
		ScopeZoomAge = ScopeZoomAge < 0.0f ? ScopeZoomAge : ScopeZoomAge + DeltaSeconds;
	}
	const bool bStepped = Step != LastScopeStep;
	if (bStepped)
	{
		// A smaller step is a narrower view: the rings close in; a larger one opens them out.
		ScopeZoomAnimation = Step < LastScopeStep ? 1 : 2;
		ScopeZoomAge = 0.0f;
		LastScopeStep = Step;
	}
	const bool bSpeak = (S.Flags & EScctHudFlags::BinocularHackPrompt) != 0;
	ScopeSpeakAge = bSpeak ? (ScopeSpeakAge < 0.0f ? 0.0f : ScopeSpeakAge + DeltaSeconds) : -1.0f;
	const bool bAlert = S.ObjectiveCountdown0 >= 0;
	ScopeAlertAge = bAlert ? (ScopeAlertAge < 0.0f ? 0.0f : ScopeAlertAge + DeltaSeconds) : -1.0f;

	const FScctHudBinocularApi* Binoculars = ScctFeature<FScctHudBinocularApi>();
	if (Binoculars == nullptr || Binoculars->GetMercPage == nullptr)
	{
		UE_CLOG(!bScopePageMissingLogged, LogOpenSCCT, Warning, TEXT("ScctHud: the bridge has no scope page table; the merc's scope shows a plain ring and sights"));
		bScopePageMissingLogged = true;
		return;
	}

	// The aim point (1000 uu out along the aim, at the drawn moment between the last two ticks) seen from the camera of
	// this frame, on the 640 x 480 page as high as the picture: SHud s_H_Binoculars_Def.PostRender keeps it within the
	// middle half of the page's width and the middle 60 percent of its height. The sights sit there, off the centre by
	// the breath's sway and the view's lag behind the aim.
	FVector2f Spot(320.0f, 240.0f);
	float Aim[3] = {};
	const AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player != nullptr && Binoculars->GetMercAim != nullptr && Binoculars->GetMercAim(Aim) != 0)
	{
		const FScctLookView& View = Player->GetLookView();
		if (View.Valid != 0 && View.VerticalFov > 0.5f && View.VerticalFov < 179.0f)
		{
			const FVector Local = FRotator(View.Pitch, View.Yaw, View.Roll).UnrotateVector(FVector(Aim[0], Aim[1], Aim[2]) - FVector(View.X, View.Y, View.Z));
			if (Local.X > 1.0)
			{
				const double Focal = 240.0 / FMath::Tan(FMath::DegreesToRadians(static_cast<double>(View.VerticalFov) * 0.5));
				Spot.X = 320.0f + static_cast<float>(Local.Y / Local.X * Focal);
				Spot.Y = 240.0f - static_cast<float>(Local.Z / Local.X * Focal);
			}
		}
	}
	Spot.X = FMath::Clamp(Spot.X, 160.0f, 480.0f);
	Spot.Y = FMath::Clamp(Spot.Y, 96.0f, 384.0f);
	M.ScopeSightOffset = (S.Flags & EScctHudFlags::HoldBreath) != 0 ? FVector2f::ZeroVector : Spot - FVector2f(320.0f, 240.0f);

	FScctHudScopePageInput Input = {};
	Input.Seconds = ScopeAge;
	Input.ZoomSeconds = ScopeZoomAge;
	Input.ZoomAnimation = ScopeZoomAnimation;
	Input.ZoomStep = Step;
	Input.SpotX = Spot.X;
	Input.SpotY = Spot.Y;
	Input.Flags = ((S.Flags & EScctHudFlags::HoldBreath) != 0 ? EScctHudScopeFlags::HoldBreath : 0)
		| ((S.Flags & EScctHudFlags::BinocularHit) != 0 ? EScctHudScopeFlags::Hit : 0);
	Input.BreathTime = S.BreathTime;
	Input.Distance = S.BinocularDistance;
	Input.DistanceScale = S.DistanceScale;
	Input.SpeakSeconds = ScopeSpeakAge;
	Input.AlertSeconds = ScopeAlertAge;
	CopyInto(Input.ZoomKeys, BinocularZoomKeys);
	CopyInto(Input.ExitKey, BinocularExitKey);
	CopyInto(Input.BreathKey, ScopeBreathKey);
	CopyInto(Input.SpeakText, bSpeak ? M.Text(S.HackPromptId) : FString());
	CopyInto(Input.Message, S.ReticleMessageId != 0 ? M.Text(S.ReticleMessageId) : FString());
	M.ScopeLayers.SetNumUninitialized(PageCapacity);
	const int32 Count = Binoculars->GetMercPage(&Input, M.ScopeLayers.GetData(), PageCapacity);
	M.ScopeLayers.SetNum(FMath::Clamp(Count, 0, PageCapacity));
	if (Count == 0 && !bScopePageMissingLogged)
	{
		bScopePageMissingLogged = true;
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHud: the content has no P_BinocularsDEF page; the merc's scope shows a plain ring and sights"));
	}

	// A line when the scope page comes up and on each zoom step.
	if (bEntered || bStepped)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: scope page %s, fov %.0f zoom %s, rings %s, %d layers, sights at (%.0f,%.0f) of 640 x 480, breath %.1f s, range %.0f uu, keys zoom '%s' exit '%s' breath '%s'"),
			bEntered ? TEXT("up") : TEXT("zoom step"), S.BinocularFov, Step == 1 ? TEXT("x6") : (Step == 2 ? TEXT("x4") : TEXT("x2")),
			ScopeZoomAnimation == 1 ? TEXT("closing in") : TEXT("opening out"), M.ScopeLayers.Num(), Spot.X, Spot.Y, S.BreathTime, S.BinocularDistance,
			*BinocularZoomKeys, *BinocularExitKey, *ScopeBreathKey);
	}
}

void UScctHudSubsystem::LogBinoculars(bool bChange)
{
	const FScctHudModel& M = *Model;
	const FScctHudState& S = M.State;
	if (!bChange && (!M.bHasState || S.BinocularMode != 1))
	{
		return;
	}
	if (S.BinocularMode != 1)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: binoculars page off (binocular mode %d: 0 none, 2 the merc's scope)"), S.BinocularMode);
		return;
	}

	int32 OnScreen = 0, Images = 0, Texts = 0;
	for (const FScctHudMarker& Marker : M.BinocularMarkers)
	{
		OnScreen += Marker.bInFront ? 1 : 0;
	}
	FString Zoom, Hint;
	for (const FScctHudPageLayer& Layer : M.BinocularLayers)
	{
		Images += Layer.Kind == 0 ? 1 : 0;
		Texts += Layer.Kind == 1 ? 1 : 0;
	}
	const int32 Step = FMath::TruncToInt32(S.BinocularFov * 0.1f);
	Zoom = Step == 1 ? TEXT("x6") : (Step == 2 ? TEXT("x4") : TEXT("x2"));
	if (!LastLockCandidate.IsEmpty())
	{
		// Several objectives share a name (six 'Tech Room' bombs on AquaE), so the mark is the one ReadBinoculars found on the
		// candidate itself, not on the first target of that name.
		Hint = M.Text(bLastLockMarked ? S.LockedTextId : S.LockTextId).Replace(TEXT("\\USE"), *M.BinocularUseKey);
	}
	// The targets one by one only on a change; the line with each snapshot keeps to the counts.
	FString Rows;
	for (const FScctHudMarker& Marker : bChange ? M.BinocularMarkers : TArray<FScctHudMarker>())
	{
		Rows += FString::Printf(TEXT("%s'%s' kind %d flags %d %d%% %.0f uu %s (%.0f,%.0f)"), Rows.IsEmpty() ? TEXT("") : TEXT(" | "),
			*M.Text(Marker.Objective.NameId), Marker.Objective.Kind, Marker.Objective.Flags, Marker.Objective.Percent, Marker.Distance,
			Marker.bInFront ? TEXT("at") : TEXT("behind"), Marker.Screen.X, Marker.Screen.Y);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: binoculars up %.1f s, fov %.0f zoom %s, ER %d, %d binocular targets (%d in front), lock candidate '%s' hint '%s', page %d images %d texts, keys zoom '%s' exit '%s' use '%s'%s | %s"),
		BinocularAge, S.BinocularFov, *Zoom, (S.Flags & EScctHudFlags::EnhancedReality) != 0 ? 1 : 0, M.BinocularTargets.Num(), OnScreen,
		*LastLockCandidate, *Hint, Images, Texts, *BinocularZoomKeys, *BinocularExitKey, *M.BinocularUseKey,
		SpeakAge >= 0.0f ? TEXT(", speak box up") : TEXT(""), *Rows);
}

void UScctHudSubsystem::RunBinocularCheck()
{
	FBinocularCheck& Check = BinocularCheck;
	if (!Check.bOn || !Model->bHasState || (Model->State.Flags & EScctHudFlags::HasPawn) == 0)
	{
		return;
	}
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player == nullptr)
	{
		return;
	}
	const float Now = Model->Clock;

	if (Check.Stage == 0 && Now >= Check.At)
	{
		const bool bPressed = Player->TapBoundCommand(TEXT("SwitchBinocularVision"));
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctBinoculars: binocular key at %.1f s%s"), Now, bPressed ? TEXT("") : TEXT(" refused (no key bound)"));
		Check.Stage = 1;
		Check.AimClock = 0.0f;
	}
	if (Check.Stage != 1)
	{
		return;
	}

	if (Check.ZoomAt.IsValidIndex(Check.NextZoom) && Now >= Check.ZoomAt[Check.NextZoom])
	{
		// One notch of the wheel, as Unreal's viewport raises it: the scroll key's press and release.
		++Check.NextZoom;
		FScctControlAutoplay::SendKey(*Player, EKeys::MouseScrollUp, true);
		FScctControlAutoplay::SendKey(*Player, EKeys::MouseScrollUp, false);
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctBinoculars: wheel notch up at %.1f s (fov %.0f before)"), Now, Model->State.BinocularFov);
	}

	// The Use key, which locks the target under the lock panel or unlocks the locked one, held for a fifth of a second.
	if (Check.bUseHeld && Now >= Check.UseReleaseAt)
	{
		Check.bUseHeld = false;
		Player->HoldScriptedAction(TEXT("use"), false);
	}
	if (Check.UseAt.IsValidIndex(Check.NextUse) && Now >= Check.UseAt[Check.NextUse])
	{
		++Check.NextUse;
		Check.bUseHeld = Player->HoldScriptedAction(TEXT("use"), true);
		Check.UseReleaseAt = Now + 0.2f;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctBinoculars: Use key at %.1f s (lock candidate '%s')"), Now, *LastLockCandidate);
	}

	// The nearest target, turned to every half second (the player's own turn would move off it).
	if (Check.bAim && Model->State.BinocularMode == 1 && Now >= Check.AimClock && Model->BinocularTargets.Num() > 0)
	{
		Check.AimClock = Now + 0.5f;
		const FVector Eye(Model->State.X, Model->State.Y, Model->State.Z);
		const FScctHudObjective* Nearest = nullptr;
		double Best = TNumericLimits<double>::Max();
		for (const FScctHudObjective& Target : Model->BinocularTargets)
		{
			const double Distance = FVector::DistSquared(Eye, FVector(Target.X, Target.Y, Target.Z));
			if (Distance < Best)
			{
				Best = Distance;
				Nearest = &Target;
			}
		}
		const FScctHudBinocularApi* Binoculars = ScctFeature<FScctHudBinocularApi>();
		if (Nearest != nullptr && Binoculars != nullptr && Binoculars->AimAt(Nearest->X, Nearest->Y, Nearest->Z) == 0)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctBinocularsAim: no pawn to turn"));
		}
	}

	if (Check.Lower >= 0.0f && Now >= Check.Lower)
	{
		Player->TapBoundCommand(TEXT("SwitchBinocularVision"));
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctBinoculars: binocular key again at %.1f s"), Now);
		Check.Stage = 2;
	}
}
