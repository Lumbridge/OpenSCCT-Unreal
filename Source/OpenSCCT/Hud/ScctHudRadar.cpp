#include "Engine/World.h"
#include "Hud/ScctHudSubsystem.h"
#include "Hud/ScctHudTextures.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctHudRadarApi.h"
#include "Settings/ScctSettings.h"

// The HUD subsystem's part for the radar: the player's radar options (registered in the settings registry), the heart
// sensor's cone from the bridge's radar table, the radar's line in the snapshot log, and the timed commands Heart and
// Weapon an unattended check uses to raise the cone. ReadSnapshot reads the blips (GetRadar); the painting is in
// ScctHudElementsRadar.cpp.

#define LOCTEXT_NAMESPACE "ScctHudRadar"

namespace
{
	/** The content's pictures the radar draws with; each missing one is drawn as plain shapes instead. */
	const TCHAR* const RadarTextures[] = { TEXT("fond_radar"), TEXT("HUDConfig"), TEXT("ret_center"), TEXT("zoom_rond01") };
}

void UScctHudSubsystem::InitRadar()
{
	FScctSettingDesc Scale;
	Scale.Key = TEXT("Hud.RadarScale");
	Scale.Page = TEXT("Hud");
	Scale.Group = LOCTEXT("Group", "Radar");
	Scale.Label = LOCTEXT("ScaleLabel", "Radar size");
	Scale.Tooltip = LOCTEXT("ScaleTip", "The size of the radar at the top left, and of the place name above it.");
	Scale.Type = EScctSettingType::Float;
	Scale.Default = TEXT("1");
	Scale.Min = 0.5f;
	Scale.Max = 2.0f;
	Scale.Step = 0.05f;
	Scale.Order = 10;
	FScctSettings::Register(Scale);

	FScctSettingDesc Rotate;
	Rotate.Key = TEXT("Hud.RadarRotate");
	Rotate.Page = TEXT("Hud");
	Rotate.Group = LOCTEXT("Group", "Radar");
	Rotate.Label = LOCTEXT("RotateLabel", "Radar turns with the view");
	Rotate.Tooltip = LOCTEXT("RotateTip", "On: the way you face is always up, as in the original. Off: the radar keeps the map's north up and a mark at its centre shows the way you face.");
	Rotate.Type = EScctSettingType::Bool;
	Rotate.Default = TEXT("true");
	Rotate.Order = 11;
	FScctSettings::Register(Rotate);
}

void UScctHudSubsystem::ReadRadar()
{
	FScctHudModel& M = *Model;
	M.RadarScale = FMath::Clamp(FScctSettings::GetFloat(TEXT("Hud.RadarScale")), 0.5f, 2.0f);
	M.bRadarRotate = FScctSettings::GetBool(TEXT("Hud.RadarRotate"));

	// The cone and its clip's frame come from the bridge's sensor driver (which also plays its sounds). Without that
	// table the HUD state's cone is drawn with a wave on the HUD's own clock.
	M.HeartSensor = FScctHudHeartSensor{};
	if (const FScctHudRadarApi* Radar = ScctFeature<FScctHudRadarApi>())
	{
		Radar->GetHeartSensor(&M.HeartSensor);
	}
	else if (M.bHasState && M.State.HeartCone > 1)
	{
		M.HeartSensor.Cone = M.State.HeartCone;
		M.HeartSensor.LoopFrames = M.State.HeartCone == 3 ? 18 : 20;
		M.HeartSensor.Frame = FMath::Fmod(M.Clock * 10.0f, static_cast<float>(M.HeartSensor.LoopFrames));
	}
	else if (M.bHasState)
	{
		M.HeartSensor.Cone = M.State.HeartCone;
	}

	if (M.HeartSensor.Cone != LastHeartCone)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: heart cone %d -> %d (frame %.1f of %d, scan pings %d, heartbeats %d, sounds raised %d)"),
			LastHeartCone, M.HeartSensor.Cone, M.HeartSensor.Frame, M.HeartSensor.LoopFrames, M.HeartSensor.CheckSounds,
			M.HeartSensor.DetectedSounds, M.HeartSensor.SoundsRaised);
		LastHeartCone = M.HeartSensor.Cone;
	}

	// Once a session, with the player's HUD up: say which of the radar's pictures the content lacks.
	if (!bRadarTextureMissingLogged && M.bHasState)
	{
		bRadarTextureMissingLogged = true;
		if (ScctFeature<FScctHudRadarApi>() == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHud: the bridge has no radar table; the heart cone's wave runs on the HUD's clock and its sounds are not played"));
		}
		for (const TCHAR* Name : RadarTextures)
		{
			if (UScctHudTextures::Find(TEXT("Hud"), Name) == nullptr)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHud: the content has no Hud.%s; the radar draws that part as plain shapes"), Name);
			}
		}
	}
}

bool UScctHudSubsystem::RunRadarCommand(const FString& Name, int32 Argument)
{
	if (Name.Equals(TEXT("Heart"), ESearchCase::IgnoreCase))
	{
		const FScctHudRadarApi* Radar = ScctFeature<FScctHudRadarApi>();
		const bool bSelected = Argument != 0 && Radar != nullptr && Radar->SelectHeartSensor() != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: Heart %s"), bSelected ? TEXT("selected") : TEXT("not selected (no sensor, no pawn or no radar table)"));
		return true;
	}
	if (Name.Equals(TEXT("RadarRotate"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("RadarScale"), ESearchCase::IgnoreCase))
	{
		// The player's options for this run only (set, not saved): RadarRotate 0/1, RadarScale in percent.
		if (Name.Equals(TEXT("RadarRotate"), ESearchCase::IgnoreCase))
		{
			FScctSettings::SetBool(TEXT("Hud.RadarRotate"), Argument != 0);
		}
		else
		{
			FScctSettings::SetFloat(TEXT("Hud.RadarScale"), Argument / 100.0f);
		}
		return true;
	}
	if (Name.Equals(TEXT("Weapon"), ESearchCase::IgnoreCase))
	{
		// The weapon key goes down or comes up, as under a finger.
		if (AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			Player->HoldScriptedAction(TEXT("weapon"), Argument != 0);
		}
		return true;
	}
	return false;
}

void UScctHudSubsystem::LogRadar()
{
	const FScctHudModel& M = *Model;
	int32 Kinds[4] = {};
	FString Rows;
	for (const FScctHudBlip& Blip : M.Radar)
	{
		++Kinds[FMath::Clamp(Blip.Kind, 0, 3)];
		// Each row as kind:distance/height in radar units (44 world units each; the dish reaches 49).
		if (Rows.Len() < 200)
		{
			Rows += FString::Printf(TEXT(" k%d:%.0f/%.0f"), Blip.Kind, FVector2f(Blip.X - M.State.X, Blip.Y - M.State.Y).Size() / 44.0f, (Blip.Z - M.State.Z) / 44.0f);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("HUD radar: %d rows (%d teammates, %d enemies, %d alarms, %d malfunctioning objects)%s yaw %.0f | heart cone %d frame %.1f/%d scan pings %d heartbeats %d raised %d | scale %.2f rotate %d"),
		M.Radar.Num(), Kinds[0], Kinds[1], Kinds[2], Kinds[3], *Rows, M.State.Yaw, M.HeartSensor.Cone, M.HeartSensor.Frame, M.HeartSensor.LoopFrames,
		M.HeartSensor.CheckSounds, M.HeartSensor.DetectedSounds, M.HeartSensor.SoundsRaised, M.RadarScale, M.bRadarRotate ? 1 : 0);
}

#undef LOCTEXT_NAMESPACE
