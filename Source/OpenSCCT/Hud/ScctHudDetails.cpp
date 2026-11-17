#include "Engine/World.h"
#include "Hud/ScctHudSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Runtime/ScctHudTutorialApi.h"
#include "Settings/ScctSettings.h"

// The HUD subsystem's part for the marker and message details: the player's Hud.MarkerFadeWhileMoving option, the
// tutorial message's table (whether the map is a tutorial; the -ScctHudTutorial message raised through the game's own
// SHud.AddTutoMapMessage), log lines when the tutorial message, the held briefings or the markers' fade change, and the
// timed command Tutorial 1/0. The painting is in ScctHudElementsMatch.cpp (markers, banner),
// ScctHudElementsTutorial.cpp (the box) and ScctHudElementsMessages.cpp (the kill feed's icons and cause line).

#define LOCTEXT_NAMESPACE "ScctHudDetails"

void UScctHudSubsystem::InitDetails()
{
	FScctSettingDesc Fade;
	Fade.Key = TEXT("Hud.MarkerFadeWhileMoving");
	Fade.Page = TEXT("Hud");
	Fade.Group = LOCTEXT("Group", "Objective markers");
	Fade.Label = LOCTEXT("FadeLabel", "Fade markers while moving");
	Fade.Tooltip = LOCTEXT("FadeTip", "On: as in the original, the spy's Enhanced Reality markers dim while moving, their brackets close in and their labels fade, and come back on standing still. Off: they stay at full strength.");
	Fade.Type = EScctSettingType::Bool;
	Fade.Default = TEXT("true");
	Fade.Order = 30;
	FScctSettings::Register(Fade);

	// -ScctHudTutorial=<text>[|At]
	FString Switch;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctHudTutorial="), Switch, false))
	{
		FString Text = Switch, At;
		if (Switch.Split(TEXT("|"), &Text, &At, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && At.IsNumeric())
		{
			TutorialSwitchAt = FCString::Atof(*At);
		}
		else
		{
			Text = Switch;
		}
		TutorialSwitchText = Text.TrimStartAndEnd();
	}
}

void UScctHudSubsystem::ReadDetails()
{
	FScctHudModel& M = *Model;
	M.bMarkerFadeWhileMoving = FScctSettings::GetBool(TEXT("Hud.MarkerFadeWhileMoving"));

	const FScctHudTutorialApi* Tutorial = ScctFeature<FScctHudTutorialApi>();
	M.bTutorialTable = Tutorial != nullptr;
	FScctHudTutorialState State = {};
	const bool bHud = Tutorial != nullptr && Tutorial->GetState(&State) != 0;
	M.bTutorialMap = bHud && State.TutorialMap != 0;

	// The unattended message, once the player's HUD is up and the match has run At seconds.
	if (!TutorialSwitchText.IsEmpty() && !bTutorialSwitchDone && bHud && M.bHasState && M.Clock >= TutorialSwitchAt)
	{
		bTutorialSwitchDone = true;
		const int32 Stored = Tutorial->ShowMessage(*TutorialSwitchText);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: tutorial -ScctHudTutorial at %.1f s %s (offline %d): \"%s\""), M.Clock,
			Stored != 0 ? TEXT("stored by SHud.AddTutoMapMessage") : TEXT("refused by SHud.AddTutoMapMessage"), State.Offline, *TutorialSwitchText);
	}
}

bool UScctHudSubsystem::RunDetailsCommand(const FString& Name, int32 Argument)
{
	if (!Name.Equals(TEXT("Tutorial"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	const FScctHudTutorialApi* Tutorial = ScctFeature<FScctHudTutorialApi>();
	if (Tutorial == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctHudCommands: Tutorial refused: the bridge has no tutorial table"));
		return true;
	}
	const FString Text = Argument != 0 ? (TutorialSwitchText.IsEmpty() ? FString(TEXT("Tutorial message check.")) : TutorialSwitchText) : FString();
	const int32 Stored = Tutorial->ShowMessage(*Text);
	UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: Tutorial %d %s"), Argument,
		Stored != 0 ? (Argument != 0 ? TEXT("raised") : TEXT("cleared")) : TEXT("refused (no HUD, or not an offline session)"));
	return true;
}

void UScctHudSubsystem::LogDetails()
{
	const FScctHudModel& M = *Model;
	const int32 TutorialId = M.IsTutorialUp() ? M.State.TutorialId : 0;
	if (TutorialId != LastLoggedTutorialId)
	{
		LastLoggedTutorialId = TutorialId;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: tutorial message %s%s%s at %.1f s"), TutorialId != 0 ? TEXT("shown: \"") : TEXT("cleared"),
			TutorialId != 0 ? *M.Text(TutorialId) : TEXT(""), TutorialId != 0 ? TEXT("\"") : TEXT(""), M.Clock);
	}

	int32 Held = 0;
	for (const FScctHudLine& Line : M.Headlines)
	{
		Held += M.IsTutorialUp() && Line.Category == EScctHudMessageCategory::Game ? 1 : 0;
	}
	if ((Held > 0) != bLastLoggedHeldBriefings)
	{
		bLastLoggedHeldBriefings = Held > 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: tutorial %s at %.1f s"),
			Held > 0 ? *FString::Printf(TEXT("holds %d briefing line(s) of the game message queue"), Held) : TEXT("no longer holds briefing lines"), M.Clock);
	}

	// The fade in three steps (standing, ramping, moving), for checking screenshots against, with where each marker is
	// then: on the screen or behind, how far, and how far above or below (the height arrow shows past 2 m).
	const int32 FadeStep = M.MarkerFade >= 0.98f ? 2 : (M.MarkerFade <= 0.01f ? 0 : 1);
	if (FadeStep != LastLoggedFadeStep && M.bHasState && M.State.Side == 0)
	{
		LastLoggedFadeStep = FadeStep;
		for (const FScctHudMarker& Marker : M.Markers)
		{
			const FScctHudObjective& Objective = Marker.Objective;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: marker '%s' kind %d role %d flags 0x%x at (%.0f %.0f %.0f), %.0f uu away, rise %.0f, %s"),
				*M.Text(Objective.NameId), Objective.Kind, Objective.Role, Objective.Flags, Objective.X, Objective.Y, Objective.Z, Marker.Distance,
				Objective.Z - M.State.Z, Marker.bInFront ? *FString::Printf(TEXT("on the screen at (%.0f %.0f)"), Marker.Screen.X, Marker.Screen.Y) : TEXT("behind"));
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: marker fade %.2f (%s, speed %.0f uu/s, option %s) at %.1f s"), M.MarkerFade,
			FadeStep == 2 ? TEXT("standing: full markers") : (FadeStep == 0 ? TEXT("moving: faded markers") : TEXT("ramping")),
			M.State.Speed, M.bMarkerFadeWhileMoving ? TEXT("on") : TEXT("off"), M.Clock);
	}
}

#undef LOCTEXT_NAMESPACE
