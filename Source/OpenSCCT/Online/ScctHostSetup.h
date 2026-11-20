#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctHostApi.h"

/** A map a game can be hosted on: installed with converted content, and with an imported level here. */
struct FScctHostMap
{
	/** The map's folder name (AquaE). */
	FString Id;
	/** The name the menus show (ENHANCED AQUARIUS). */
	FString Name;
	/** The map list picture (shared/maps/images/<Map>/menu.png), empty when the map has none. */
	FString Picture;
	/** Its modes as mode keys (Story, DiskHunt, Deathmatch, MercDeathMatch, Extraction, Sabotage, Neutralization). */
	TArray<FString> Modes;

	/** Story when offered, otherwise the map's first available mode. */
	FString DefaultMode() const;
};

/** One row of the Create Session page, as the bridge's model describes it for the current setup. */
struct FScctSetupRow
{
	enum class EKind : uint8 { Number, Check, Choice };

	/** The row's name in edits ("TimeLimit", "Revenge", "Mode"). */
	FString Row;
	EKind Kind = EKind::Number;
	FString Caption;
	/** What the row shows ("Inf." for an unlimited value, "Yes", a mode name). */
	FString Shown;
	int32 Value = 0;
	int32 Min = 0;
	int32 Max = 0;
	bool bVisible = true;
	TArray<FString> Choices;
};

/** One line of the Alternate rules page. */
struct FScctAlternateRow
{
	enum class EKind : uint8 { Ability, Gadget, Slots };

	EKind Kind = EKind::Ability;
	/** 0 spies, 1 mercenaries, 2 both (the cinematic camera). */
	int32 Side = 0;
	/** The ability's bit, or the gadget's option. */
	int32 Id = 0;
	FString Caption;
	/** On or allowed (1/0), or the slot count. */
	int32 Value = 0;
	/** Slots: the most the side may get now. */
	int32 Most = 0;
};

/**
 * Create Session's rules for the menus (Runtime/ScctHostApi.h SetupText; the bridge runs OpenSCCT's own
 * CreateSessionModel). The page keeps the match as an options line, the one FScctHostRequest::SetupOptions and the
 * lobby's SetSetupOptions take, and asks here for its rows, for the change of one row and for the maps.
 */
namespace ScctHostSetup
{
	/** The bridge answers (its host table is version 2 or later). */
	OPENSCCT_API bool IsAvailable();

	/** A SetupText answer; empty when the bridge is not up. */
	OPENSCCT_API FString Text(EScctSetupText Kind, const FString& Options, const FString& Argument = FString(), int32 MaxPlayers = 0);

	/** Reload the map's default game mode and rules, keeping the session-wide options. */
	OPENSCCT_API FString MapDefaults(const FScctHostMap& Map, const FString& Options, int32 MaxPlayers = 0);

	/** The hostable maps that have an imported level here, in the catalog's order. */
	OPENSCCT_API TArray<FScctHostMap> Maps();

	/** The page's rows for a setup line, with the voice and bot hints. */
	OPENSCCT_API TArray<FScctSetupRow> Rows(const FString& Setup, int32 MaxPlayers, FString& OutVoiceHint, FString& OutBotHint);

	/** The Alternate rules of a setup line. */
	OPENSCCT_API TArray<FScctAlternateRow> AlternateRows(const FString& Setup, int32 MaxPlayers);

	/** A value of an options line ("?MapName=AquaE?GameMode=5": Option(Line, "MapName") is "AquaE"); empty when missing. */
	OPENSCCT_API FString Option(const FString& Line, const TCHAR* Key);

	/** The menu name of a mode key ("DiskHunt" is "Disk Hunt"). */
	OPENSCCT_API FString ModeLabel(const FString& Key);

	/** The mode key of a GameMode number (5 is "Story"). */
	OPENSCCT_API FString ModeKeyOf(int32 GameMode);

	/** The characters a session password may hold, at most MaxPasswordLength of them (LobbySetup.PasswordCharacters). */
	OPENSCCT_API FString FilterPassword(const FString& Text);
	constexpr int32 MaxPasswordLength = 16;
}
