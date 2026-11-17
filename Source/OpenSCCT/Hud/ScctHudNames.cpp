#include "Hud/ScctHudSubsystem.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctTeammateNamesApi.h"
#include "Settings/ScctSettings.h"

// The teammate names of UScctHudSubsystem: the Godot client's HudTeammateNames and its options (OptionsMenu's "Show
// teammate names through walls as Spy / as Mercenary", "Show HP beside teammate names", "Team nametag opacity (%)").
// Who gets a label comes from the bridge's teammate table (the Godot rules: living teammates of the player's own pawn
// type with a player name, no spy clones, none in a Merc Deathmatch); the label goes on the pawn as Unreal draws it.

#define LOCTEXT_NAMESPACE "ScctHudNames"

namespace
{
	const FName ShowTeammateNamesKeys[2] = { TEXT("Hud.ShowTeammateNamesSpy"), TEXT("Hud.ShowTeammateNamesMerc") };
	const FName TeammateHealthKey(TEXT("Hud.TeammateHealth"));
	const FName TeammateNamesOpacityKey(TEXT("Hud.TeammateNamesOpacity"));

	/** Where the names' switches were kept before they were options of the Hud page. */
	const TCHAR* const OldNamesSection = TEXT("OpenSCCT.Hud");
	const TCHAR* const OldNamesKeys[2] = { TEXT("TeammateNamesSpy"), TEXT("TeammateNamesMerc") };

	/** A name sits this far over the top of its pawn's collision (the Godot client's 18 units). */
	constexpr float TeammateNameLift = 18.0f;

	FScctSettingDesc NamesOption(FName Key, int32 Order, const FText& Label, const FText& Tooltip, EScctSettingType Type, const TCHAR* Default)
	{
		FScctSettingDesc Desc;
		Desc.Key = Key;
		Desc.Page = TEXT("Hud");
		Desc.Group = LOCTEXT("Group", "Teammate names");
		Desc.Label = Label;
		Desc.Tooltip = Tooltip;
		Desc.Type = Type;
		Desc.Default = Default;
		Desc.Order = Order;
		return Desc;
	}
}

void UScctHudSubsystem::InitNames()
{
	FScctSettings::Register(NamesOption(ShowTeammateNamesKeys[0], 30, LOCTEXT("Spy", "Show teammate names through walls as Spy"),
		LOCTEXT("SpyTip", "Names over your fellow spies, seen through walls. The Toggle teammate names key (N) flips it while you play a spy."),
		EScctSettingType::Bool, TEXT("false")));
	FScctSettings::Register(NamesOption(ShowTeammateNamesKeys[1], 31, LOCTEXT("Merc", "Show teammate names through walls as Mercenary"),
		LOCTEXT("MercTip", "Names over your fellow mercenaries, seen through walls, with a mark while one is knocked out or carries a backpack. The Toggle teammate names key (N) flips it while you play a mercenary."),
		EScctSettingType::Bool, TEXT("false")));
	FScctSettings::Register(NamesOption(TeammateHealthKey, 32, LOCTEXT("Health", "Show HP beside teammate names"),
		LOCTEXT("HealthTip", "The teammate's health as the server reports it, after the name."), EScctSettingType::Bool, TEXT("false")));
	FScctSettingDesc Opacity = NamesOption(TeammateNamesOpacityKey, 33, LOCTEXT("Opacity", "Team nametag opacity (%)"),
		LOCTEXT("OpacityTip", "How strongly the names are drawn, so they stay behind the crosshair and the objective markers."), EScctSettingType::Int, TEXT("50"));
	Opacity.Min = 10.0f;
	Opacity.Max = 100.0f;
	Opacity.Step = 1.0f;
	FScctSettings::Register(Opacity);

	// The switches an earlier build kept under [OpenSCCT.Hud] become the options' values once.
	bool bMoved = false;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		bool bOld = false;
		if (GConfig->GetBool(OldNamesSection, OldNamesKeys[Side], bOld, GGameUserSettingsIni))
		{
			FScctSettings::SetBool(ShowTeammateNamesKeys[Side], bOld);
			GConfig->RemoveKey(OldNamesSection, OldNamesKeys[Side], GGameUserSettingsIni);
			bMoved = true;
		}
	}
	if (bMoved)
	{
		FScctSettings::Save();
	}
}

bool UScctHudSubsystem::AreTeammateNamesOn(int32 Side) const
{
	Side = FMath::Clamp(Side, 0, 1);
	return TeammateNamesForced[Side] >= 0 ? TeammateNamesForced[Side] != 0 : FScctSettings::GetBool(ShowTeammateNamesKeys[Side]);
}

void UScctHudSubsystem::ToggleTeammateNames()
{
	// For the side played now, as the Godot client's key does, and kept with the user's settings.
	const int32 Side = FMath::Clamp(Model->LocalTeam, 0, 1);
	const bool bOn = !AreTeammateNamesOn(Side);
	TeammateNamesForced[Side] = -1;
	FScctSettings::SetBool(ShowTeammateNamesKeys[Side], bOn);
	FScctSettings::Save();
	UE_LOG(LogOpenSCCT, Log, TEXT("HUD: teammate names for the %s are now %s (%s)"), Side == 1 ? TEXT("mercs") : TEXT("spies"), bOn ? TEXT("on") : TEXT("off"),
		*ShowTeammateNamesKeys[Side].ToString());
}

void UScctHudSubsystem::ProjectNames()
{
	FScctHudModel& M = *Model;
	M.NameLabels.Reset();
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	const UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	const int32 Side = FMath::Clamp(M.LocalTeam, 0, 1);
	// Only for a living player with a side of his own whose HUD is up (a spectator's page has its own names).
	if (Player == nullptr || Session == nullptr || !AreTeammateNamesOn(Side) || !M.bHasState || !M.bHasRound || M.State.Health <= 0
		|| (M.State.Flags & (EScctHudFlags::Spectating | EScctHudFlags::GameEnded | EScctHudFlags::HideHud)) != 0)
	{
		LastNameCount = -1;
		return;
	}

	const FScctTeammateNamesApi* Names = ScctFeature<FScctTeammateNamesApi>();
	if (Names == nullptr)
	{
		if (!bNamesTableMissingLogged)
		{
			bNamesTableMissingLogged = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("HUD: the bridge has no teammate names table (its reason is in the log above); no names are drawn"));
		}
		return;
	}
	FScctTeammateNameRow Rows[32];
	const int32 Count = FMath::Clamp(Names->GetTeammates(Rows, UE_ARRAY_COUNT(Rows)), 0, static_cast<int32>(UE_ARRAY_COUNT(Rows)));

	const FScctBridgePawn* Own = nullptr;
	for (const FScctBridgePawn& Pawn : Session->GetPawns())
	{
		if ((Pawn.Flags & EScctPawnFlags::Local) != 0)
		{
			Own = &Pawn;
			break;
		}
	}
	const FVector OwnAt = Own != nullptr ? FVector(Own->X, Own->Y, Own->Z) : FVector(M.State.X, M.State.Y, M.State.Z);
	const bool bHealth = FScctSettings::GetBool(TeammateHealthKey);
	const float Opacity = FMath::Clamp(FScctSettings::GetInt(TeammateNamesOpacityKey), 10, 100) / 100.0f;

	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GetWorld()), 0.01f);
	const FVector2f Size = FVector2f(ViewportSize) / Scale;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctTeammateNameRow& Row = Rows[Index];
		const FString& Name = M.Text(Row.NameId);
		if (Name.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}
		// The pawn where this frame draws it (between ticks), else where the last tick had it.
		FVector At(Row.X, Row.Y, Row.Z);
		float Height = Row.CollisionHeight;
		for (const FScctBridgePawn& Pawn : Session->GetPawns())
		{
			if (Pawn.ActorId == Row.ActorId)
			{
				At = FVector(Pawn.X, Pawn.Y, Pawn.Z);
				Height = Pawn.CollisionHeight;
				break;
			}
		}

		FVector2D Screen;
		if (!Player->ProjectWorldLocationToScreen(At + FVector(0.0f, 0.0f, Height + TeammateNameLift), Screen, false))
		{
			continue;
		}
		const FVector2f Point = FVector2f(Screen) / Scale;
		if (Point.X < 0.0f || Point.Y < 0.0f || Point.X > Size.X || Point.Y > Size.Y)
		{
			continue;
		}
		FScctHudNameLabel& Label = M.NameLabels.AddDefaulted_GetRef();
		Label.Name = bHealth ? FString::Printf(TEXT("%s (%d HP)"), *Name, Row.Health) : Name;
		Label.Screen = Point;
		Label.Metres = static_cast<float>(FVector::Dist(At, OwnAt)) * 0.01f;
		Label.Opacity = Opacity;
		Label.Flags = Row.Flags;
	}

	// A line when the labels on screen or their marks change, to check screenshots against.
	int32 Signature = M.NameLabels.Num();
	for (const FScctHudNameLabel& Label : M.NameLabels)
	{
		Signature = Signature * 31 + Label.Flags + 1;
	}
	if (Signature != LastNameCount)
	{
		LastNameCount = Signature;
		FString Listed;
		for (const FScctHudNameLabel& Label : M.NameLabels)
		{
			Listed += FString::Printf(TEXT(" '%s' %.0f m flags %d at (%.0f, %.0f);"), *Label.Name, Label.Metres, Label.Flags, Label.Screen.X, Label.Screen.Y);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("HUD: teammate names: %d teammates, %d on screen at %.0f%% opacity:%s"), Count, M.NameLabels.Num(), Opacity * 100.0f, *Listed);
	}
}

#undef LOCTEXT_NAMESPACE
