#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctHudApi.h"

/**
 * The HUD's own labels. Their text is the original game's, read from the player's content with the bridge's
 * Localize; a label the content does not have stays empty and is simply not drawn.
 */
enum class EScctHudLabel : uint8
{
	Spies,
	Mercenaries,
	Objectives,
	Kills,
	Deaths,
	Lives,
	Ping,
	Infinite,
	MissionStatus,
	NightVision,
	ThermalVision,
	MotionTracking,
	EmfVision,
	YouWin,
	YouLose,
	Finished,
	SpectatorMode,
	Watching,
	RespawnIn,
	NoRespawn,
	Count,
};

/** A message on screen: a kill feed line, a headline or a log line. */
struct FScctHudLine
{
	FString Text;
	int32 Category = 0;      // EScctHudMessageCategory
	int32 Kind = -1;         // EScctHudActivity for the kill feed
	int32 Cause = 0;         // EScctHudCause
	int32 Flags = 0;         // EScctHudMessageFlags
	FString Actor, Victim;   // the two names of a kill, empty when the line has none
	int32 ActorTeam = -1, VictimTeam = -1;
	float Age = 0.0f;        // seconds on screen
	float Life = 5.0f;       // seconds it stays
};

/** An objective marker placed on the screen for this frame. */
struct FScctHudMarker
{
	FScctHudObjective Objective = {};
	FVector2f Screen = FVector2f::ZeroVector; // in the full-screen layer's units
	float Distance = 0.0f;                    // simulation units from the player
	bool bInFront = false;
};

/**
 * Everything the HUD draws from: the bridge's snapshot of the last simulation tick, the messages still on screen
 * and the values that are in motion (a number easing to its new value, a pulse fading out). The HUD subsystem
 * fills and advances it once a frame; the widgets only read it.
 */
class FScctHudModel
{
public:
	// --- The snapshot -------------------------------------------------------------------------------------
	FScctHudState State = {};
	FScctHudRound Round = {};
	bool bHasState = false; // the player's HUD is open
	bool bHasRound = false; // the match state has arrived

	TArray<FScctHudGadget> Gadgets;
	TArray<FScctHudInteraction> Interactions;
	TArray<FScctHudObjective> Objectives;
	TArray<FScctHudObjectiveLine> ObjectiveLines;
	TArray<FScctHudScoreRow> Scores;
	TArray<FScctHudMarker> Markers;

	/** The team of the player at this machine (0 spies, 1 mercs), from the scoreboard, else the side played. */
	int32 LocalTeam = 0;

	// --- Text ---------------------------------------------------------------------------------------------
	/** The text behind one of the snapshot's ...Id fields. Ids belong to one session. */
	const FString& Text(int32 Id) const;
	const FString& Label(EScctHudLabel Which) const { return Labels[static_cast<int32>(Which)]; }

	// --- Messages on screen -------------------------------------------------------------------------------
	TArray<FScctHudLine> Feed;      // match activity, newest last
	TArray<FScctHudLine> Headlines; // briefings and objective lines
	TArray<FScctHudLine> Log;       // the player's own state, zones, chat

	// --- Values in motion ---------------------------------------------------------------------------------
	float Clock = 0.0f;           // seconds since the session began (real time)
	float HealthShown = 0.0f;     // the health bar, easing to State.Health
	float HealthLoss = 0.0f;      // 1 when health just dropped, fading to 0
	float EnergyShown = 0.0f;
	float DamagePulse = 0.0f;     // 1 on a hit, fading to 0
	float HitConfirm = 0.0f;      // 1 while the reticle reports a hit, fading to 0
	float Spread = 0.0f;          // 0 tightest .. 1 widest, eased
	float AmmoPulse = 0.0f;       // 1 when the clip changed
	int32 ClipCapacity = 0;       // the fullest clip seen for the weapon held
	int32 GasMaskCapacity = 0;    // the most air seen in the mask, in seconds: the gauge's full mark
	float GadgetPulse = 0.0f;     // 1 when another gadget was selected
	float PromptShown = 0.0f;     // 0..1 the interaction prompt
	FString PromptText;           // kept while it fades out
	float VisionShown = 0.0f;     // 0..1 the vision mode chip
	int32 VisionMode = 0;         // the mode the chip names (kept while it fades out)
	float ScoreboardShown = 0.0f; // 0..1
	float EndAge = -1.0f;         // seconds since the round ended, negative before
	bool bScoreboardHeld = false;

	/** The flash-bang after-image: the frame frozen at the flash and how much of it covers the picture (UScctScreenEffectsSubsystem fills both). */
	const struct FSlateBrush* AfterImage = nullptr;
	float AfterImageAlpha = 0.0f;

	/** Unattended look check only (-ScctHudCommands "PreviewEnd"): shows the end banner as if this team had won; -1 when off. */
	int32 PreviewWinner = -1;

	/** Unattended look check only (-ScctHudCommands "PreviewDamage"): plays the feedback of a hit on the player. */
	void PreviewDamage() { DamagePulse = 1.0f; HealthLoss = 1.0f; }

	/** Starts again for a new session: nothing of the last match stays on screen. */
	void Reset(const FScctHudApi* InApi);

	/** Moves the values in motion on by one frame and ages the messages. */
	void Advance(float DeltaSeconds);

	/** Puts a drained message where it belongs. */
	void AddMessage(const FScctHudMessage& Message);

	/** The name of a vision mode from the content, or empty for normal sight. */
	const FString& VisionName(int32 Mode) const;

	static bool IsVisionActive(int32 Mode) { return Mode == 34 || Mode == 36 || Mode == 37 || Mode == 38; }

private:
	void LoadLabels();

	const FScctHudApi* Api = nullptr;
	mutable TMap<int32, FString> Texts;
	FString Labels[static_cast<int32>(EScctHudLabel::Count)];

	// What the last frame showed, to notice changes.
	int32 LastHealth = -1;
	int32 LastDamageCount = 0;
	int32 LastClip = -1;
	int32 LastWeaponNameId = 0;
	int32 LastSelectedGadget = -1;
};
