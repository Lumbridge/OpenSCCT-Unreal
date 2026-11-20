#pragma once

#include "CoreMinimal.h"

// The HUD interface of the C# bridge: everything the HUD and the scoreboard show, as data. These mirror
// Bridge/OpenScct.UnrealBridge/Hud/HudTypes.cs and HudExports.cs field for field; change both together and bump the
// version. Fetch the table once with
//     Host.GetFunction(TEXT("OpenScct.UnrealBridge.HudExports"), TEXT("GetApi"), Error)   // int32(__stdcall*)(FScctHudApi*)
// and check the returned version against ScctHudApiVersion.
//
// How to read it:
//  - The simulation ticks at 60 Hz and the HUD data changes only on a tick, so every getter returns the state of the
//    last tick. Call them once a frame after FScctBridgeApi::Update.
//  - Call GetState once right after StartLocal returns, before the first Update: the first HUD call of a session is
//    what starts receiving the server's messages, and the first ones (the mission briefing, the joins) arrive at once.
//  - Units are the simulation's: 1 uu = 1 Unreal cm, seconds, degrees. World positions use the same axes as
//    FScctBridgePawn.
//  - Text crosses as ids. A field named ...Id is an id of the session's text table: read it once with GetText and
//    cache the FString by id (a text keeps its id for the whole session; ids start again with a new session). Id 0 is
//    the empty text. All text comes from the player's own game content, in its language.
//  - Messages (DrainMessages) carry their text inline.
//  - Actor ids (FScctHudState::TargetActorId, FScctHudScoreRow::PawnActorId) are FScctBridgePawn::ActorId values.
//  - The HUD's own key commands go through SetOption (they touch only the HUD, so FScctBridgeApi::Command does not
//    know them): EScctHudOption::SwitchEnhancedReality is the original's F9.
//  - Every struct is made of 4-byte ints and floats (and UTF-16 characters at the end), so there is no padding.

constexpr int32 ScctHudApiVersion = 2;

/** Bits of FScctHudState::Flags. */
namespace EScctHudFlags
{
	constexpr int32 HasPawn = 1 << 0;             // the player has a live pawn (clear while dead or spectating)
	constexpr int32 HideHud = 1 << 1;             // draw no HUD: bHideHUD, the knock-out screen while dark, cinematic camera
	constexpr int32 LifeOff = 1 << 2;             // the map or tutorial hides the health element
	constexpr int32 GadgetOff = 1 << 3;           // ... the gadget box
	constexpr int32 TimeOff = 1 << 4;             // ... the clock
	constexpr int32 AmmoOff = 1 << 5;             // ... the ammunition
	constexpr int32 Poisoned = 1 << 6;
	constexpr int32 Crouched = 1 << 7;            // the merc's posture icon
	constexpr int32 Sleeping = 1 << 8;            // knocked out
	constexpr int32 LyingOnGround = 1 << 9;
	constexpr int32 CloakEngaged = 1 << 10;       // the spy's cloak is requested or still fading
	constexpr int32 FastSpeed = 1 << 11;          // the spy's sneak icon shows while this is clear
	constexpr int32 HasWeapon = 1 << 12;          // the weapon fields are set
	constexpr int32 HasSidearm = 1 << 13;
	constexpr int32 ShowReticle = 1 << 14;        // weapon drawn, not scoped, not reloading, laser off
	constexpr int32 ReticleHit = 1 << 15;         // the local merc's shot hit a pawn in the last 0.4 s (red reticle)
	constexpr int32 LaserOn = 1 << 16;            // the laser sight replaces the reticle
	constexpr int32 EnhancedReality = 1 << 17;    // the spy's objective markers are drawn
	constexpr int32 ShowInteractions = 1 << 18;   // the interaction list shows (two or more usable entries)
	constexpr int32 InteractionFocus = 1 << 19;   // Use is held: the list's selection shows
	constexpr int32 Spectating = 1 << 20;         // dead and waiting, or a visitor; see FScctHudRound
	constexpr int32 GameEnded = 1 << 21;          // the end banner shows; FScctHudRound::WinningTeam says who won
	constexpr int32 ShowPlayers = 1 << 22;        // the player's "show connected players" toggle
	constexpr int32 CarryingFlag = 1 << 23;       // the pawn carries the disk
	constexpr int32 SpotValid = 1 << 24;          // SpotX / SpotY hold the merc's aim point
	constexpr int32 HoldBreath = 1 << 25;         // scoped merc holding his breath (sights stay centred)
	constexpr int32 BinocularHit = 1 << 26;       // scoped merc's shot hit a pawn less than 0.4 s ago (red sights)
	constexpr int32 BinocularAim = 1 << 27;       // BinocularDistance and Aim are set (scoped merc)
	constexpr int32 GadgetUnavailable = 1 << 28;  // the selected gadget cannot work on this level
	constexpr int32 BinocularHackPrompt = 1 << 29; // binoculars: show HackPromptId
	constexpr int32 TargetFriend = 1 << 30;       // the laser sight's target (TargetActorId) is on the player's side: friend colour and mark
}

/** Bits of FScctHudState::EffectFlags: what is disturbing the view or the HUD right now. */
namespace EScctHudEffectFlags
{
	constexpr int32 Perturbed = 1 << 0;          // the merc HUD is perturbed (hit, jammed)
	constexpr int32 StickyShocking = 1 << 1;     // the shocker is on the pawn
	constexpr int32 StickyShockHead = 1 << 2;    // ... on the head
	constexpr int32 Wet = 1 << 3;                // in a rain volume (droplets on the merc's visor)
	constexpr int32 FoamSplash = 1 << 4;         // extinguisher foam is splashing onto the merc's visor
	constexpr int32 FoamOn = 1 << 5;             // the foam lies on the visor (FoamStopTime, FoamStaySeconds)
	constexpr int32 ShutDown = 1 << 6;           // the merc HUD is shut down (EMP); reboots at ShutDownTime + 12 s
	constexpr int32 SleepScreen = 1 << 7;        // a knock-out wash is running
	constexpr int32 ShockImmune = 1 << 8;        // the merc's shock protection is active
	constexpr int32 ShockImmuneByState = 1 << 9; // ... from the pawn's state (no deadline) rather than the timer
	constexpr int32 GasMaskWorn = 1 << 10;
	constexpr int32 GasMaskLowAir = 1 << 11;     // nearly out of air (fast breathing)
	constexpr int32 CamNetworkAlert = 1 << 12;   // the camera network view shows its Alert box
}

/** FScctHudState::SfxType: the original's screen-effect kinds (SBase.SFXType). */
namespace EScctHudSfx
{
	constexpr int32 None = 0;
	constexpr int32 Black = 1;      // black wash, value = alpha 0..255
	constexpr int32 CrossEye = 2;   // double vision, value 0..1
	constexpr int32 Smoke = 3;      // double vision from smoke, value 0..1
	constexpr int32 Red = 4;        // red wash (the spy was hit), value = alpha 0..255
	constexpr int32 White = 5;      // white wash, value = alpha 0..255
	constexpr int32 Flash = 6;      // flash-bang after-image, value 1..0
	constexpr int32 Sleep = 7;      // the knock-out screen (black with SleepingTextId and SleepSeconds), value = alpha 0..255
}

constexpr int32 ScctHudArrowCount = 6;
constexpr int32 ScctHudSfxSlots = 8;

/**
 * The local player's HUD for the frame (GetState): what the original's spy page, merc page, binocular pages and
 * gadget views show, as numbers.
 */
struct FScctHudState
{
	int32 Valid;               // 1 when the player's HUD is open; 0: draw no HUD
	int32 Side;                // 0 spy, 1 merc
	int32 Flags;               // EScctHudFlags
	int32 EffectFlags;         // EScctHudEffectFlags
	float TimeSeconds;         // level time; every other time in the HUD structs is on this clock
	int32 Health;
	int32 Lives;               // 100 or more reads as infinite
	float X, Y, Z;             // pawn location (radar and marker distances are measured from here)
	float Speed;               // uu/s (the spy's markers fade while he moves)
	float Yaw;                 // controller yaw in degrees (the radar turns with it)
	int32 StateNameId;         // the controller's state as the simulation names it (s_PlayerWalking...)

	// Spy energy (the cloak and vision gauge).
	float EnergyLevel;
	int32 EnergyLevelMax;

	// Merc weapon.
	int32 WeaponKind;          // -1 none, 0 rifle, 1 SMG, 2 shotgun
	int32 WeaponNameId;
	int32 ClipAmmo;
	int32 Ammo;                // every round left, the clip's included
	int32 FireMode;            // 0 single, 1 burst, 2 auto
	int32 FireModeNameId;

	// Crosshair.
	int32 ReticleFrame;        // the original's spread frame, trunc(10 x Accuracy / (Max - Min)); the rifle runs 2 to 12
	float Accuracy;            // current dispersion, between MinDispersion and MaxDispersion
	float MinDispersion, MaxDispersion;
	float SpotX, SpotY;        // the merc's aim point on a 640 x 480 canvas when SpotValid; else the centre
	int32 ReticleMessageId;    // the message under the reticle while it lasts, or 0
	int32 TargetNameId;        // the player the laser sight locked on, or 0 (see TargetActorId below)
	float Ballistic;           // the throw charge, 0 when not throwing

	// Gadgets.
	int32 GadgetCount;         // rows GetGadgets returns
	int32 SelectedGadget;      // index in GetGadgets, or -1
	int32 TeamGadgetSlots;     // gadget slots of the team's loadout

	// Vision.
	int32 VisionMode;          // 0 or 5 normal, 34 night, 36 thermal, 37 motion tracking, 38 EMF, 50 binoculars

	// Views that replace the player page.
	int32 GadgetView;          // 0 none, 1 sticky cam (spy), 2 camera network (merc)
	int32 CamNetworkNameId;    // the camera's name, or 0
	int32 MenuLineCount;       // rows GetMenu returns: the sticky cam or mine menu is open when above 0
	int32 BinocularMode;       // 0 none, 1 spy binoculars, 2 merc scope
	float BinocularFov;        // the zoom step: 30, 20 or 10 degrees
	float BreathTime;          // breath left for the scoped merc, 5 s when full
	float BinocularDistance;   // the merc's range finder in uu, 0 when nothing is hit (BinocularAim)
	float AimX, AimY, AimZ;    // the scoped merc's aim point in the world, 1000 uu out (BinocularAim)
	int32 BinocularTargetCount; // rows GetBinocularTargets returns
	int32 HackPromptId;        // the Hack prompt's text; show while BinocularHackPrompt
	int32 LockTextId, LockedTextId, DropZoneTextId; // the spy binoculars' lock texts

	// Prompts.
	int32 InteractionCount;    // rows GetInteractions returns
	int32 SelectedInteraction; // index in GetInteractions, or -1
	int32 UsePromptId;         // what Use does now, as the interaction's own text; 0 when there is nothing to use
	int32 SpeakPrompt;         // the spy's voice prompt: 0 none, 1 Speak, 2 Hack
	int32 SpeakPromptId;       // its text
	int32 CoopIcon;            // 0 none, 1 offer a ladder, 2 climb on the mate, 3 offer a hoist, 4 be boosted up

	// Meters.
	float ShockImmunityRemaining, ShockImmunityFraction; // seconds left, and the same as a fraction of its length
	int32 GasMaskAirSeconds;   // seconds of air left, or -1 while the mask gauge is not shown
	int32 HeartCone;           // heart beat sensor: 0 hidden, 1 idle, 2 scanning, 3 detected
	float ArrowSeconds[ScctHudArrowCount]; // the merc's sound arrows (a noise from that side): seconds each still shows; left, right, top, bottom, forward, backward

	// Damage feedback.
	int32 DamageCount;         // hits on the local player so far; a change is a new hit
	int32 LastDamageType;      // the simulation's SDamageType of the last hit
	float LastDamageTime;

	// Screen effects.
	float CrossEyedFactor;     // double vision 0..1 (gas, smoke)
	float RemanenceBlendFactor; // flash-bang after-image 0..1
	int32 RemanenceCaptures;   // a change means: freeze the current frame as the after-image
	int32 SfxType[ScctHudSfxSlots];  // EScctHudSfx per slot
	float SfxValue[ScctHudSfxSlots]; // the slot's value now
	int32 ShutDowns;           // merc HUD shutdowns so far (a change starts one)
	float ShutDownTime;        // level time of the last, -1 when none
	int32 Flashes;             // flash-bang white-outs on the merc so far (a change restarts it)
	float FlashTime;           // level time of the last, -1 when none
	int32 FoamStopTime, FoamStaySeconds; // the whole second the foam settled, and seconds it stays before clearing

	// Knock-out, tutorial.
	int32 SleepingTextId;      // the knock-out screen's text
	int32 SleepSeconds;        // seconds until the player gets up
	int32 TutorialId;          // the tutorial message, or 0

	// Countdown boxes in seconds; -1 when not shown.
	int32 AlarmSeconds;
	int32 HackCountdown;
	int32 ObjectiveCountdown0, ObjectiveCountdown1;

	// Deathmatch respawn weapon pick.
	int32 RespawnWeaponIconU, RespawnWeaponIconV; // the weapon's 64 x 64 tile in SHudTextures.Icones_Weapons, or -1
	int32 RespawnWeaponClassId; // the class of the weapon offered

	// Marker texts.
	int32 DistanceUnitId;      // the distance unit's name
	float DistanceScale;       // units per uu
	int32 HackingTextId, SecuredTextId; // "under hacking" and "access denied" for the markers

	// Version 2.
	int32 LocationNameId;      // the name of the place the viewed pawn is in (the readout above the radar), or 0
	int32 TargetActorId;       // the pawn the merc's laser sight holds (FScctBridgePawn::ActorId), or 0
	float TargetLockTime;      // level time that lock began; the original brackets the target from then
	float TargetNameDelay;     // seconds after TargetLockTime before TargetNameId is typed over the bracket
	int32 CamNetworkDisabledSeconds; // camera network view: seconds until the shot camera works again (blank the feed), 0 while it works
};

namespace EScctHudGadgetFlags
{
	constexpr int32 Selected = 1;
	constexpr int32 Unavailable = 2; // cannot work on this level
}

/** One gadget of the pawn's inventory (GetGadgets), in the order the player cycles them. */
struct FScctHudGadget
{
	int32 ClassId;         // the simulation's class name (SGadStickyCam, SGadFragGrenade...): pick art by it
	int32 NameId;          // the gadget's name from the content
	int32 IconNameId;      // the original's icon material name
	int32 IconU, IconV;    // and its tile
	int32 Charge;          // charges left; below 0 means unlimited (show no number)
	int32 Flags;           // EScctHudGadgetFlags
	int32 MineHackPercent; // the held mine hack's progress 0..100, else 0
	float RechargeRemaining; // seconds until it recharges, else 0
};

/** One usable entry of the interaction menu (GetInteractions), in slot order. */
struct FScctHudInteraction
{
	int32 DescriptionId; // the interaction's text from the content
	int32 ClassId;       // its class name (SComputerInteraction, SLeaveInteraction...)
	int32 Selected;      // 1 for the selected entry
};

/** A line of the sticky cam or mine menu (GetMenu). */
struct FScctHudMenuLine
{
	int32 TextId;
	int32 Look; // 0 normal, 1 selected, 2 unavailable (greyed), 3 untouched (the original leaves the line as authored)
};

namespace EScctHudObjectiveFlags
{
	constexpr int32 Triggering = 1; // being hacked, taken or armed right now
	constexpr int32 Locked = 2;
	constexpr int32 Taken = 4;      // the objective is done
	constexpr int32 Alert = 8;      // drop zone in the alert colour (a merc, or a spy carrying the disk)
	constexpr int32 Marked = 16;    // binocular target: the one the spy has locked
}

/** An objective marker in the world (GetObjectives, GetBinocularTargets). Project X, Y, Z to the screen to place it. */
struct FScctHudObjective
{
	int32 NameId;
	int32 Kind;    // 0 terminal, 1 disk, 2 drop zone, 3 bomb spot, 4 Disk Hunt disk, 5 other
	int32 Role;    // 0 a trigger of an objective, 1 the objective itself, 2 a free drop zone, 3 a Disk Hunt disk
	int32 Flags;   // EScctHudObjectiveFlags
	int32 Percent; // hack progress 0..99, or -1 when the marker has none
	float X, Y, Z;
};

/** An objective of the mission status list (GetObjectiveLines). */
struct FScctHudObjectiveLine
{
	int32 TextId; // the objective's description for the player's side, from the map
	int32 Done;
};

/** A radar blip (GetRadar), as a world location. */
struct FScctHudBlip
{
	int32 Kind; // 0 teammate, 1 enemy, 2 alarm (merc), 3 malfunctioning game object (spy)
	float X, Y, Z;
};

/** FScctHudMessage::Category and FScctHudShownMessage::Category. */
namespace EScctHudMessageCategory
{
	constexpr int32 Activity = 0; // match activity (the kill feed); Kind says which
	constexpr int32 Game = 1;     // the game message queue: mission briefings, objective headlines
	constexpr int32 System = 2;   // objective and alarm lines
	constexpr int32 State = 3;    // the player's own state (goggles, refill done, mask ready)
	constexpr int32 Info = 4;     // the zone the player entered
	constexpr int32 Reticle = 5;  // the message under the reticle
	constexpr int32 Chat = 6;     // a chat line to everyone
	constexpr int32 TeamChat = 7; // a chat line to the team
	constexpr int32 Console = 8;  // another console line
}

/**
 * A message the original HUD is showing right now (GetShownMessages): its stacks, the head of the game message queue
 * and the console lines, with the original's own pacing. A new HUD reads DrainMessages instead. A line whose text is
 * one space is the original's blank spacer line.
 */
struct FScctHudShownMessage
{
	int32 Category;  // Game, System, State, Info, Chat, TeamChat or Console
	int32 TextId;
	float Remaining; // seconds it still shows
	int32 Priority;
};

/** The match (GetRound). */
struct FScctHudRound
{
	int32 Valid;            // 1 once the game replication info has arrived
	int32 GameMode;         // 1 Extraction, 2 Sabotage, 3 Neutralization, 4 Disk Hunt, 5 Story, 6 Deathmatch, 7 Merc Deathmatch
	int32 TimeLimit;        // minutes; 0 for no limit
	int32 ElapsedTime;      // seconds played
	int32 RemainingSeconds; // seconds left, or -1 without a time limit
	int32 Launching;        // GRI.bMatchLaunching as replicated
	int32 Ended;            // 1 once the round ended for the local player
	int32 WinningTeam;      // 0 spies, 1 mercs, 255 not decided
	int32 GamesInARow;      // rounds played in a row on this server
	int32 ScoresInARow[2];  // each side's wins among them, spies then mercs
	int32 TeamScore[2];     // the match score, spies then mercs
	int32 TeamLives[2];     // team lives, spies then mercs; 1000 or more reads as infinite
	int32 TeamSize[2];      // players on each team
	int32 NumObjectives;
	int32 ObjectivesLeft;   // objectives the spies still have to do
	int32 NumDisks, NumDisksToWin; // Disk Hunt
	int32 CounterMode;      // what the page's counter shows: 0 objectives left, 1 team lives, 2 disks taken of disks to win

	// The spectator page (EScctHudFlags::Spectating).
	int32 Visitor;          // 1 for a visitor rather than a dead player
	int32 RespawnIn;        // seconds to the respawn (1 to 49), 0 when not shown
	int32 NoRespawn;        // 1 when no life is left
	int32 WatchedNameId;    // the watched player's name, or 0
	int32 ShowSwitchPlayer; // 1 when the "switch player" hint applies
	int32 TipId;            // the team's tip of the moment, from the content
	int32 MapTitleId;       // the map's title, from the content

	// Version 2.
	int32 WatchedPlayerId;  // FScctHudScoreRow::PlayerId of the watched player (valid while WatchedNameId is not 0)
};

namespace EScctHudScoreFlags
{
	constexpr int32 Local = 1;
	constexpr int32 Spectator = 2;
	constexpr int32 Alive = 4;      // a live pawn of this player is known here; enemies out of sight are not replicated, so clear means "not known alive"
	constexpr int32 OutOfLives = 8;
	constexpr int32 HasFlag = 16;   // carries the disk
}

/** A scoreboard row (GetScoreboard): spies, then mercs, then spectators. */
struct FScctHudScoreRow
{
	int32 PlayerId;
	int32 NameId;
	int32 Team;       // 0 spies, 1 mercs
	int32 Flags;      // EScctHudScoreFlags
	int32 ObjOrKills; // the original's score column: objectives taken for a spy outside deathmatch, else kills
	int32 Kills;
	int32 Deaths;
	int32 Score;
	int32 Lives;      // 100 or more reads as infinite
	int32 Ping;       // milliseconds
	int32 Rank;       // OpenSCCT's packed player rank (0 for guests and bots)

	// Version 2.
	int32 PawnActorId; // FScctBridgePawn::ActorId of the player's pawn on this machine, or 0 when none is replicated here
};

/** FScctHudMessage::Kind of an Activity message. */
enum class EScctHudActivity : int32
{
	Kill, Death, Suicide, Resign, Sleep, Knockout, Join, Leave, Match, LastLife,
};

/** FScctHudMessage::Cause of an Activity message: how it happened. */
enum class EScctHudCause : int32
{
	None, Firearm, Headshot, Explosion, Charge, DropAttack, Elbow, Grab, NeckBreak, Choke,
	Fall, Crush, Poison, StickyShock, StickyBlast, Smoke, StickyCamera, Other,
	ProximityMine, LaserMine, PoisonMine, FragGrenade, BarrelExplosion, ExplosiveProp,
	MolotovExplosion, MolotovFire, Fire, Hazard, LedgePull,
	Rifle, Shotgun, Smg, Sniper, Pistol, Revolver,
	GasExplosion, ExplosiveExtinguisher, SodaExplosion,
	Taser, ShockCollision,
};

namespace EScctHudMessageFlags
{
	constexpr int32 InvolvesLocal = 1; // the local player is the actor, the victim or the one who triggered it
	constexpr int32 Headshot = 2;
	constexpr int32 ClearsFeed = 4;    // the match restarted: empty the kill feed, then show this line
}

constexpr int32 ScctHudMessageTextCapacity = 256;

/**
 * One new HUD message (DrainMessages) with its final text. Activity messages also carry who did what to whom, so a
 * kill feed can be drawn from the parts.
 */
struct FScctHudMessage
{
	int32 Category; // EScctHudMessageCategory
	int32 Kind;     // EScctHudActivity for an Activity message, else -1
	int32 Cause;    // EScctHudCause (Activity only)
	int32 Trigger;  // Activity only: 0 none, 1 set off by a shot, 2 by a chain reaction
	int32 Flags;    // EScctHudMessageFlags
	int32 Switch;   // the original's number of the message in its class, or -1
	float Time;     // level time it arrived
	float Duration; // seconds the original shows it for
	int32 Priority;
	int32 ActorPlayerId, ActorTeam, ActorNameId;    // the killer, who joined, or the chat's sender; 0 / -1 / 0 when none
	int32 VictimPlayerId, VictimTeam, VictimNameId;
	int32 TriggeredByPlayerId, TriggeredByTeam, TriggeredByNameId; // who set off what killed the victim, when someone else
	TCHAR Text[ScctHudMessageTextCapacity]; // the final text, zero terminated (cut at 255 characters)
};

/** FScctHudApi::SetOption's options. */
namespace EScctHudOption
{
	constexpr int32 DistanceInFeet = 0; // marker distances in feet (1) or metres (0)
	constexpr int32 ShowPlayers = 1;    // the player's "show connected players" toggle
	constexpr int32 SwitchEnhancedReality = 2; // each call flips the objective markers on or off (the original's F9); Value is ignored
}

/**
 * The HUD's entry points. The list getters write up to Capacity rows and return the full count, which can be more
 * than Capacity. Every entry returns 0 while no session runs.
 */
struct FScctHudApi
{
	int32 Version;
	int32 (__cdecl* GetState)(FScctHudState* State);                              // 1 when there is a HUD to draw
	int32 (__cdecl* GetRound)(FScctHudRound* Round);                              // 1 once the match state is known
	int32 (__cdecl* GetGadgets)(FScctHudGadget* Rows, int32 Capacity);
	int32 (__cdecl* GetInteractions)(FScctHudInteraction* Rows, int32 Capacity);
	int32 (__cdecl* GetMenu)(FScctHudMenuLine* Rows, int32 Capacity);
	int32 (__cdecl* GetObjectives)(FScctHudObjective* Rows, int32 Capacity);
	int32 (__cdecl* GetBinocularTargets)(FScctHudObjective* Rows, int32 Capacity);
	int32 (__cdecl* GetObjectiveLines)(FScctHudObjectiveLine* Rows, int32 Capacity);
	int32 (__cdecl* GetRadar)(FScctHudBlip* Rows, int32 Capacity);
	int32 (__cdecl* GetShownMessages)(FScctHudShownMessage* Rows, int32 Capacity);
	int32 (__cdecl* GetScoreboard)(FScctHudScoreRow* Rows, int32 Capacity);
	int32 (__cdecl* DrainMessages)(FScctHudMessage* Messages, int32 Capacity);    // returns the count written; call again while it equals Capacity
	int32 (__cdecl* GetText)(int32 Id, TCHAR* Buffer, int32 Capacity);            // returns the text's full length
	int32 (__cdecl* Localize)(const TCHAR* File, const TCHAR* Section, const TCHAR* Key, TCHAR* Buffer, int32 Capacity); // a string of the game's .int files; returns its length, 0 when missing
	void (__cdecl* SetViewport)(int32 Width, int32 Height);                       // the view's size in pixels (its aspect places the merc's aim point)
	void (__cdecl* SetOption)(int32 Option, int32 Value);                         // EScctHudOption
};

// The sizes the C# side has (HudProbe prints them); a mismatch means the two files drifted apart.
static_assert(sizeof(TCHAR) == 2, "The bridge writes UTF-16");
static_assert(sizeof(FScctHudState) == 448, "FScctHudState must mirror HudState");
static_assert(sizeof(FScctHudRound) == 120, "FScctHudRound must mirror HudRound");
static_assert(sizeof(FScctHudGadget) == 36, "FScctHudGadget must mirror HudGadget");
static_assert(sizeof(FScctHudInteraction) == 12, "FScctHudInteraction must mirror HudInteraction");
static_assert(sizeof(FScctHudMenuLine) == 8, "FScctHudMenuLine must mirror HudMenuLine");
static_assert(sizeof(FScctHudObjective) == 32, "FScctHudObjective must mirror HudObjective");
static_assert(sizeof(FScctHudObjectiveLine) == 8, "FScctHudObjectiveLine must mirror HudObjectiveLine");
static_assert(sizeof(FScctHudBlip) == 16, "FScctHudBlip must mirror HudBlip");
static_assert(sizeof(FScctHudShownMessage) == 16, "FScctHudShownMessage must mirror HudShownMessage");
static_assert(sizeof(FScctHudScoreRow) == 48, "FScctHudScoreRow must mirror HudScoreRow");
static_assert(sizeof(FScctHudMessage) == 584, "FScctHudMessage must mirror HudMessage");
static_assert(sizeof(FScctHudApi) == 136, "FScctHudApi must mirror HudApi");
