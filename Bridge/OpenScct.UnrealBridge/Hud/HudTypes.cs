using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Hud;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctHudApi.h mirrors them field for field;
// change both together and bump HudFeature.ApiVersion. Every field is a 4-byte int or float (or a fixed array of
// them, or of UTF-16 characters at the end), so the layout has no padding on either side.
//
// Text crosses as ids: a field named ...Id is an id of the session's text table (HudTextTable), read once with
// GetText and kept by the caller. Id 0 is the empty text. Messages carry their text inline instead, because every
// message is new text.

/// <summary>Bits of <see cref="HudState.Flags"/>.</summary>
public static class HudFlags
{
    /// <summary>The player has a live pawn (false while dead or spectating).</summary>
    public const int HasPawn = 1 << 0;

    /// <summary>HUD.bHideHUD, the knock-out screen while it is dark, or the cinematic camera's Hide HUD.</summary>
    public const int HideHud = 1 << 1;

    // The per-element switches of HUD (bLifeOff, bGadgetOff, bTimeOff, bAMMOOff): a map or tutorial hides the element.
    public const int LifeOff = 1 << 2;
    public const int GadgetOff = 1 << 3;
    public const int TimeOff = 1 << 4;
    public const int AmmoOff = 1 << 5;

    /// <summary>SBasePawn.bPoisonned.</summary>
    public const int Poisoned = 1 << 6;

    /// <summary>Pawn.bIsCrouched (the merc's posture icon).</summary>
    public const int Crouched = 1 << 7;

    /// <summary>SBasePawn.bSleeping: knocked out.</summary>
    public const int Sleeping = 1 << 8;

    /// <summary>SBasePawn.bLyingOnGround.</summary>
    public const int LyingOnGround = 1 << 9;

    /// <summary>The spy's cloak is requested or still fading.</summary>
    public const int CloakEngaged = 1 << 10;

    /// <summary>SPlayerController.bFastSpeed; the spy's sneak icon shows while it is clear.</summary>
    public const int FastSpeed = 1 << 11;

    /// <summary>The merc has a weapon (the weapon fields are set).</summary>
    public const int HasWeapon = 1 << 12;

    public const int HasSidearm = 1 << 13;

    /// <summary>The merc reticle shows: weapon drawn, not scoped, laser off, bNoReticule clear.</summary>
    public const int ShowReticle = 1 << 14;

    /// <summary>The local merc's shot hit a pawn in the last 0.4 s (red reticle).</summary>
    public const int ReticleHit = 1 << 15;

    /// <summary>The merc's laser sight is on (it replaces the reticle).</summary>
    public const int LaserOn = 1 << 16;

    /// <summary>HUD.EnhancedRealityLevel != 0: the spy's objective markers are drawn.</summary>
    public const int EnhancedReality = 1 << 17;

    /// <summary>The interaction list shows (two or more usable entries, awake, able to interact).</summary>
    public const int ShowInteractions = 1 << 18;

    /// <summary>Use is held: the list's selection shows.</summary>
    public const int InteractionFocus = 1 << 19;

    /// <summary>The player is spectating (dead and waiting, or a visitor); see <see cref="HudRound"/>.</summary>
    public const int Spectating = 1 << 20;

    /// <summary>SPlayerController.bGameEnded: the end banner shows.</summary>
    public const int GameEnded = 1 << 21;

    /// <summary>SHud.bShowConnectedPlayers.</summary>
    public const int ShowPlayers = 1 << 22;

    /// <summary>The pawn carries the disk (SBasePawn.CarriedFlag).</summary>
    public const int CarryingFlag = 1 << 23;

    /// <summary><see cref="HudState.SpotX"/> and SpotY hold the merc's aim point.</summary>
    public const int SpotValid = 1 << 24;

    /// <summary>Scoped merc: SPlayerController.bHoldBreath (the sights stay centred).</summary>
    public const int HoldBreath = 1 << 25;

    /// <summary>Binoculars: the merc's shot hit a pawn less than 0.4 s ago (red sights).</summary>
    public const int BinocularHit = 1 << 26;

    /// <summary><see cref="HudState.BinocularDistance"/> and the Aim fields are set (scoped merc).</summary>
    public const int BinocularAim = 1 << 27;

    /// <summary>The selected gadget cannot work on this level (the camera network on a map without cameras).</summary>
    public const int GadgetUnavailable = 1 << 28;

    /// <summary>Binoculars: a voice chat target is set, the Hack prompt shows.</summary>
    public const int BinocularHackPrompt = 1 << 29;
}

/// <summary>Bits of <see cref="HudState.EffectFlags"/>: what is disturbing the view or the HUD right now.</summary>
public static class HudEffectFlags
{
    /// <summary>The merc page is perturbed (SHud.bPerturbation or the damage perturbation).</summary>
    public const int Perturbed = 1 << 0;

    /// <summary>SBasePawn.bStickyShocking: the shocker is on the pawn.</summary>
    public const int StickyShocking = 1 << 1;

    /// <summary>SBasePawn.bStickyShockedInTheHead.</summary>
    public const int StickyShockHead = 1 << 2;

    /// <summary>The pawn stands in a rain volume (droplets on the merc's visor).</summary>
    public const int Wet = 1 << 3;

    /// <summary>The extinguisher foam is splashing onto the merc's visor.</summary>
    public const int FoamSplash = 1 << 4;

    /// <summary>The foam lies on the visor (see <see cref="HudState.FoamStopTime"/> and FoamStaySeconds).</summary>
    public const int FoamOn = 1 << 5;

    /// <summary>The merc HUD is shut down (EMP); it reboots <see cref="HudState.ShutDownTime"/> + 12 s.</summary>
    public const int ShutDown = 1 << 6;

    /// <summary>A knock-out wash is running (the screen fades to black and back).</summary>
    public const int SleepScreen = 1 << 7;

    /// <summary>The merc's shock protection is active.</summary>
    public const int ShockImmune = 1 << 8;

    /// <summary>The protection comes from the pawn's state (no deadline) rather than the timer.</summary>
    public const int ShockImmuneByState = 1 << 9;

    /// <summary>The gas mask is on the face.</summary>
    public const int GasMaskWorn = 1 << 10;

    /// <summary>The gas mask's air is nearly out (fast breathing).</summary>
    public const int GasMaskLowAir = 1 << 11;

    /// <summary>The camera network view shows its Alert box.</summary>
    public const int CamNetworkAlert = 1 << 12;
}

/// <summary>
/// The local player's HUD for the frame (<c>GetState</c>): what the spy page, the merc page, the binocular pages
/// and the gadget views of the original show, as numbers. Simulation units (1 uu = 1 Unreal cm), seconds, degrees.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudState
{
    public const int ArrowCount = 6;
    public const int SfxSlots = 8;

    /// <summary>1 when the player's HUD is open (the PRI has a team or only spectates). 0: draw no HUD.</summary>
    public int Valid;

    /// <summary>0 spy, 1 merc.</summary>
    public int Side;

    /// <summary><see cref="HudFlags"/>.</summary>
    public int Flags;

    /// <summary><see cref="HudEffectFlags"/>.</summary>
    public int EffectFlags;

    /// <summary>Level time in seconds; every other time in the HUD structs is on this clock.</summary>
    public float TimeSeconds;

    public int Health;

    /// <summary>PRI.NumLives; 100 or more reads as infinite.</summary>
    public int Lives;

    /// <summary>Pawn location (the radar and marker distances are measured from here).</summary>
    public float X, Y, Z;

    /// <summary>|Velocity| in uu/s (the spy's markers fade while he moves).</summary>
    public float Speed;

    /// <summary>Controller yaw in degrees (the radar turns with it).</summary>
    public float Yaw;

    /// <summary>The controller's state name (s_PlayerWalking, s_Binoculars_Def...), as the simulation names it.</summary>
    public int StateNameId;

    // ---- spy energy (the cloak and vision gauge) ----
    public float EnergyLevel;
    public int EnergyLevelMax;

    // ---- merc weapon ----
    /// <summary>-1 none, 0 rifle, 1 Uzi, 2 shotgun.</summary>
    public int WeaponKind;

    public int WeaponNameId;
    public int ClipAmmo;

    /// <summary>SWeaponDefense.Ammo: every round left, the clip's included (a reload fills the clip and leaves this as it is).</summary>
    public int Ammo;

    /// <summary>SWeaponDefense.ROFMode: 0 single, 1 burst, 2 auto.</summary>
    public int FireMode;

    /// <summary>The fire mode's name from the weapon (content text).</summary>
    public int FireModeNameId;

    // ---- crosshair ----
    /// <summary>
    /// Spread frame of the reticle as the original computes it: trunc(10 x accuracy / (MaxDispersion - MinDispersion)).
    /// The rifle runs from 2 (at rest) to 12 (full spread).
    /// </summary>
    public int ReticleFrame;

    /// <summary>SWeaponDefense.GetAccuracy(): the current dispersion, between MinDispersion and MaxDispersion.</summary>
    public float Accuracy;

    public float MinDispersion, MaxDispersion;

    /// <summary>
    /// The merc's aim point (SHud.SpotReticule) on a 640 x 480 canvas, when <see cref="HudFlags.SpotValid"/>;
    /// else draw at the centre.
    /// </summary>
    public float SpotX, SpotY;

    /// <summary>SHud.ReticuleMessage while it lasts ("Reloading"...).</summary>
    public int ReticleMessageId;

    /// <summary>The name the laser sight locked on (SHud.MyTargetName), or 0.</summary>
    public int TargetNameId;

    /// <summary>HUD.BallisticValue: the throw charge, 0 when not throwing.</summary>
    public float Ballistic;

    // ---- gadgets ----
    /// <summary>Entries <c>GetGadgets</c> returns.</summary>
    public int GadgetCount;

    /// <summary>Index of the selected gadget in <c>GetGadgets</c>, or -1.</summary>
    public int SelectedGadget;

    /// <summary>STeamInfo.iNBGadgets: gadget slots of the team's loadout.</summary>
    public int TeamGadgetSlots;

    // ---- vision ----
    /// <summary>PlayerController.RendMap: 0 or 5 normal, 34 night, 36 thermal, 37 motion tracking, 38 EMF, 50 binoculars.</summary>
    public int VisionMode;

    // ---- views that replace the player page ----
    /// <summary>0 none, 1 sticky cam (spy), 2 camera network (merc).</summary>
    public int GadgetView;

    /// <summary>The camera network's camera name, or 0.</summary>
    public int CamNetworkNameId;

    /// <summary>Lines <c>GetMenu</c> returns: the sticky cam or mine menu is open when above 0.</summary>
    public int MenuLineCount;

    /// <summary>0 none, 1 spy binoculars, 2 merc scope.</summary>
    public int BinocularMode;

    /// <summary>SPlayerController.fBinocularVisionFOV: the zoom step, 30, 20 or 10.</summary>
    public float BinocularFov;

    /// <summary>Breath left for the scoped merc, 5 s when full.</summary>
    public float BreathTime;

    /// <summary>The merc's range finder in uu (0 when nothing is hit); see <see cref="HudFlags.BinocularAim"/>.</summary>
    public float BinocularDistance;

    /// <summary>The scoped merc's aim point in the world, 1000 uu out.</summary>
    public float AimX, AimY, AimZ;

    /// <summary>Targets <c>GetBinocularTargets</c> returns (spy binoculars).</summary>
    public int BinocularTargetCount;

    /// <summary>The Hack prompt's text from the content; show it while <see cref="HudFlags.BinocularHackPrompt"/> is set.</summary>
    public int HackPromptId;

    /// <summary>SHud.BinocularsAttLock, BinocularAttLocked and DropZone: the spy binoculars' lock texts.</summary>
    public int LockTextId, LockedTextId, DropZoneTextId;

    // ---- prompts ----
    /// <summary>Entries <c>GetInteractions</c> returns.</summary>
    public int InteractionCount;

    /// <summary>Index of the selected entry in <c>GetInteractions</c>, or -1.</summary>
    public int SelectedInteraction;

    /// <summary>
    /// What Use does now, as the interaction's own text: the selected entry, or the only usable one. 0 when there is
    /// nothing to use.
    /// </summary>
    public int UsePromptId;

    /// <summary>The spy's voice prompt: 0 none, 1 Speak (a grabbed or downed enemy), 2 Hack (listen to a tracked enemy).</summary>
    public int SpeakPrompt;

    /// <summary>The prompt's text from the content, or 0.</summary>
    public int SpeakPromptId;

    /// <summary>0 none, 1 offer a ladder, 2 climb on the mate, 3 offer a hoist from the ledge, 4 be boosted up.</summary>
    public int CoopIcon;

    // ---- meters ----
    /// <summary>Seconds of the merc's shock protection left, and the same as a fraction of its length.</summary>
    public float ShockImmunityRemaining, ShockImmunityFraction;

    /// <summary>Seconds of gas mask air left, or -1 while the mask gauge is not shown.</summary>
    public int GasMaskAirSeconds;

    /// <summary>The heart beat sensor's cone: 0 hidden, 1 idle, 2 scanning, 3 detected.</summary>
    public int HeartCone;

    /// <summary>
    /// The merc's sound arrows (a noise was heard from that side): seconds each still shows, 0 when off. Order: left,
    /// right, top, bottom, forward, backward.
    /// </summary>
    public fixed float ArrowSeconds[ArrowCount];

    // ---- damage feedback ----
    /// <summary>Hits on the local player so far; a change is a new hit.</summary>
    public int DamageCount;

    /// <summary>SDamageType of the last hit.</summary>
    public int LastDamageType;

    /// <summary>Level time of the last hit.</summary>
    public float LastDamageTime;

    // ---- screen effects (SHud.AddVisualSfx slots) ----
    /// <summary>PlayerController.CrossEyedFactor: double vision, 0 to 1 (gas, smoke).</summary>
    public float CrossEyedFactor;

    /// <summary>PlayerController.RemanenceBlendFactor: the flash-bang after-image, 0 to 1.</summary>
    public float RemanenceBlendFactor;

    /// <summary>Flash after-image captures asked for so far; a change means "freeze the current frame as the after-image".</summary>
    public int RemanenceCaptures;

    /// <summary>
    /// SFXType per slot: 0 none, 1 black wash, 2 cross-eye, 3 smoke, 4 red wash, 5 white wash, then the flash and the
    /// knock-out screen as the simulation numbers them (see ScctHudApi.h).
    /// </summary>
    public fixed int SfxType[SfxSlots];

    /// <summary>The slot's value now: a wash's alpha 0 to 255, a factor 0 to 1.</summary>
    public fixed float SfxValue[SfxSlots];

    /// <summary>Merc HUD shutdowns so far (a change starts one) and the level time of the last.</summary>
    public int ShutDowns;
    public float ShutDownTime;

    /// <summary>Flash-bang white-outs on the merc so far (a change restarts it) and the level time of the last.</summary>
    public int Flashes;
    public float FlashTime;

    /// <summary>Foam: the whole second it settled, and how many seconds it stays before it clears.</summary>
    public int FoamStopTime, FoamStaySeconds;

    // ---- knock-out, tutorial ----
    /// <summary>SHud.TextSleeping: the knock-out screen's text.</summary>
    public int SleepingTextId;

    /// <summary>Seconds until the player gets up.</summary>
    public int SleepSeconds;

    /// <summary>SHud.sTutoMap: the tutorial message, or 0.</summary>
    public int TutorialId;

    // ---- countdown boxes; -1 when not shown ----
    /// <summary>Seconds of the most urgent alarm.</summary>
    public int AlarmSeconds;

    /// <summary>Seconds of the hacking panel.</summary>
    public int HackCountdown;

    /// <summary>Seconds of the first two objective deadlines.</summary>
    public int ObjectiveCountdown0, ObjectiveCountdown1;

    // ---- deathmatch respawn weapon ----
    /// <summary>The weapon's 64 x 64 tile in SHudTextures.Icones_Weapons, or -1 when no pick is offered.</summary>
    public int RespawnWeaponIconU, RespawnWeaponIconV;

    /// <summary>The class of the weapon offered.</summary>
    public int RespawnWeaponClassId;

    // ---- marker texts ----
    /// <summary>HUD.StrDistanceUnit and fDistanceUnit: the unit's name and units per uu for marker distances.</summary>
    public int DistanceUnitId;
    public float DistanceScale;

    /// <summary>SHud.ObjectiveHackingSpy ("under hacking") and ObjectiveSecuredSpy ("access denied") for the markers.</summary>
    public int HackingTextId, SecuredTextId;
}

/// <summary>Bits of <see cref="HudGadget.Flags"/>.</summary>
public static class HudGadgetFlags
{
    public const int Selected = 1;

    /// <summary>Cannot work on this level.</summary>
    public const int Unavailable = 2;
}

/// <summary>One gadget of the pawn's inventory (<c>GetGadgets</c>), in the order the player cycles them.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudGadget
{
    /// <summary>The simulation's class name (SGadStickyCam, SGadFlashBang...): what to pick art by.</summary>
    public int ClassId;

    /// <summary>Inventory.ItemName: the gadget's name from the content.</summary>
    public int NameId;

    /// <summary>The original's icon material name in the Options package, and its tile.</summary>
    public int IconNameId;
    public int IconU, IconV;

    /// <summary>Charges left; below 0 means unlimited (no number shown).</summary>
    public int Charge;

    /// <summary><see cref="HudGadgetFlags"/>.</summary>
    public int Flags;

    /// <summary>The held mine hack's progress 0 to 100 (mine hack gadget), else 0.</summary>
    public int MineHackPercent;

    /// <summary>Seconds until the gadget recharges (clone gadget), else 0.</summary>
    public float RechargeRemaining;
}

/// <summary>One usable entry of the interaction menu (<c>GetInteractions</c>), in slot order.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudInteraction
{
    /// <summary>The interaction's text from the content (SInteractObject.Description).</summary>
    public int DescriptionId;

    /// <summary>The interaction's class name (SComputerInteraction, SDoorInteraction...).</summary>
    public int ClassId;

    /// <summary>1 when this is the selected entry.</summary>
    public int Selected;
}

/// <summary>A line of the sticky cam or mine menu (<c>GetMenu</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudMenuLine
{
    public int TextId;

    /// <summary>0 normal, 1 selected, 2 unavailable (greyed), 3 untouched (leave the page's own text and colour).</summary>
    public int Look;
}

/// <summary>Bits of <see cref="HudObjective.Flags"/>.</summary>
public static class HudObjectiveFlags
{
    /// <summary>Being hacked, taken or armed right now.</summary>
    public const int Triggering = 1;

    public const int Locked = 2;

    /// <summary>The objective is done.</summary>
    public const int Taken = 4;

    /// <summary>Drop zone drawn in the alert colour (a merc, or a spy carrying the disk).</summary>
    public const int Alert = 8;

    /// <summary>Binocular target: the one the spy has locked.</summary>
    public const int Marked = 16;
}

/// <summary>
/// An objective marker in the world (<c>GetObjectives</c>: the spy's Enhanced Reality markers; also
/// <c>GetBinocularTargets</c>). Project <see cref="X"/>, Y, Z to the screen to place it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudObjective
{
    public int NameId;

    /// <summary>0 terminal, 1 disk, 2 drop zone, 3 bomb spot, 4 Disk Hunt disk, 5 other.</summary>
    public int Kind;

    /// <summary>0 a trigger of an objective, 1 the objective itself, 2 a free drop zone, 3 a Disk Hunt disk.</summary>
    public int Role;

    /// <summary><see cref="HudObjectiveFlags"/>.</summary>
    public int Flags;

    /// <summary>Hack progress 0 to 99, or -1 when the marker has none.</summary>
    public int Percent;

    public float X, Y, Z;
}

/// <summary>An objective of the mission status list (<c>GetObjectiveLines</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudObjectiveLine
{
    /// <summary>The objective's description for the player's side, from the map.</summary>
    public int TextId;

    public int Done;
}

/// <summary>A radar blip (<c>GetRadar</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudBlip
{
    /// <summary>0 teammate, 1 enemy, 2 alarm (merc), 3 malfunctioning game object (the spy's orange markers).</summary>
    public int Kind;

    public float X, Y, Z;
}

/// <summary>
/// A message the original HUD is showing right now (<c>GetShownMessages</c>): the lines of SHud's stacks, the head of
/// the game message queue and the console lines. For a HUD that wants the original's own pacing; a new HUD reads
/// <c>DrainMessages</c> instead. A line whose text is one space is the original's blank spacer line.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudShownMessage
{
    /// <summary><see cref="HudMessageCategory"/> (Game, System, State, Info, Chat, TeamChat or Console).</summary>
    public int Category;

    public int TextId;

    /// <summary>Seconds it still shows.</summary>
    public float Remaining;

    public int Priority;
}

/// <summary>The local match (<c>GetRound</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudRound
{
    /// <summary>1 once the game replication info has arrived.</summary>
    public int Valid;

    /// <summary>EGameMode: 1 Extraction, 2 Sabotage, 3 Neutralization, 4 Disk Hunt, 5 Story (GM_Multi), 6 Deathmatch, 7 Merc Deathmatch.</summary>
    public int GameMode;

    /// <summary>Minutes; 0 for no limit.</summary>
    public int TimeLimit;

    /// <summary>Seconds played.</summary>
    public int ElapsedTime;

    /// <summary>Seconds left, or -1 without a time limit.</summary>
    public int RemainingSeconds;

    /// <summary>GRI.bMatchLaunching as replicated (the server sets it when the match launches).</summary>
    public int Launching;

    /// <summary>1 once the round ended for the local player.</summary>
    public int Ended;

    /// <summary>GRI.WinningTeam: 0 spies, 1 mercs, 255 not decided.</summary>
    public int WinningTeam;

    /// <summary>Rounds played in a row on this server and each side's wins among them (GRI.iGamesInARow, iScoresInARow).</summary>
    public int GamesInARow;
    public fixed int ScoresInARow[2];

    /// <summary>LevelInfo.NbPointsAttack and NbPointsDefense: the match score, spies then mercs.</summary>
    public fixed int TeamScore[2];

    /// <summary>GRI.TotalTeamLives, spies then mercs; 1000 or more reads as infinite.</summary>
    public fixed int TeamLives[2];

    /// <summary>Players on each team.</summary>
    public fixed int TeamSize[2];

    public int NumObjectives;

    /// <summary>Objectives the spies still have to do.</summary>
    public int ObjectivesLeft;

    /// <summary>Disk Hunt: disks on the map and disks the spies need.</summary>
    public int NumDisks, NumDisksToWin;

    /// <summary>What the page's counter shows: 0 objectives left, 1 team lives (deathmatch), 2 disks taken of disks to win.</summary>
    public int CounterMode;

    // ---- spectator page ----
    /// <summary>1 for a visitor (PRI.bOnlySpectator) rather than a dead player.</summary>
    public int Visitor;

    /// <summary>Seconds to the respawn (1 to 49), 0 when not shown.</summary>
    public int RespawnIn;

    /// <summary>1 when no life is left.</summary>
    public int NoRespawn;

    /// <summary>The watched player's name, or 0.</summary>
    public int WatchedNameId;

    /// <summary>1 when the "switch player" hint applies.</summary>
    public int ShowSwitchPlayer;

    /// <summary>The team's tip of the moment, from the content.</summary>
    public int TipId;

    /// <summary>The map's title, from the content.</summary>
    public int MapTitleId;
}

/// <summary>Bits of <see cref="HudScoreRow.Flags"/>.</summary>
public static class HudScoreFlags
{
    public const int Local = 1;
    public const int Spectator = 2;

    /// <summary>A live pawn of this player is known on this machine. Enemies out of sight are not replicated, so clear means "not known alive".</summary>
    public const int Alive = 4;

    /// <summary>No life left (NumLives 0 or less).</summary>
    public const int OutOfLives = 8;

    /// <summary>The player carries the disk (PRI.bHasFlag).</summary>
    public const int HasFlag = 16;
}

/// <summary>A scoreboard row (<c>GetScoreboard</c>): spies, then mercs, then spectators.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudScoreRow
{
    public int PlayerId;
    public int NameId;

    /// <summary>0 spies, 1 mercs.</summary>
    public int Team;

    /// <summary><see cref="HudScoreFlags"/>.</summary>
    public int Flags;

    /// <summary>The page's score column: objectives taken for a spy outside deathmatch, else kills.</summary>
    public int ObjOrKills;

    public int Kills;
    public int Deaths;
    public int Score;

    /// <summary>100 or more reads as infinite.</summary>
    public int Lives;

    /// <summary>Milliseconds.</summary>
    public int Ping;

    /// <summary>OpenSCCT's packed player rank (0 for guests and bots).</summary>
    public int Rank;
}

/// <summary><see cref="HudMessage.Category"/>.</summary>
public static class HudMessageCategory
{
    /// <summary>Match activity (the kill feed): <see cref="HudMessage.Kind"/> says which.</summary>
    public const int Activity = 0;

    /// <summary>The game message queue: mission briefings, objective headlines, a teammate's death.</summary>
    public const int Game = 1;

    /// <summary>SHud.MessageStack: objective and alarm lines.</summary>
    public const int System = 2;

    /// <summary>The auxiliary stack: the player's own state (goggles, refill done, mask ready).</summary>
    public const int State = 3;

    /// <summary>The info stack: the zone the player entered.</summary>
    public const int Info = 4;

    /// <summary>The message under the reticle.</summary>
    public const int Reticle = 5;

    /// <summary>A chat line to everyone.</summary>
    public const int Chat = 6;

    /// <summary>A chat line to the team.</summary>
    public const int TeamChat = 7;

    /// <summary>Another console line (ClientMessage, TeamMessage of another type).</summary>
    public const int Console = 8;
}

/// <summary>Bits of <see cref="HudMessage.Flags"/>.</summary>
public static class HudMessageFlags
{
    /// <summary>The local player is the actor, the victim or the one who triggered it.</summary>
    public const int InvolvesLocal = 1;

    public const int Headshot = 2;
}

/// <summary>
/// One new HUD message (<c>DrainMessages</c>), with its final text. Activity messages also carry who did what to
/// whom, so a kill feed can be drawn from the parts.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudMessage
{
    public const int TextCapacity = 256;

    /// <summary><see cref="HudMessageCategory"/>.</summary>
    public int Category;

    /// <summary>Activity only (else -1): 0 kill, 1 death, 2 suicide, 3 resign, 4 sleep, 5 knockout, 6 join, 7 leave, 8 match, 9 last life.</summary>
    public int Kind;

    /// <summary>Activity only: how (HudActivityCause; see ScctHudApi.h), 0 none.</summary>
    public int Cause;

    /// <summary>Activity only: 0 none, else what set the cause off (CombatDeathTrigger: a shot, a chain reaction...).</summary>
    public int Trigger;

    /// <summary><see cref="HudMessageFlags"/>.</summary>
    public int Flags;

    /// <summary>The message's switch in its class (the original's number for it), or -1.</summary>
    public int Switch;

    /// <summary>Level time it arrived.</summary>
    public float Time;

    /// <summary>Seconds the original shows it for (0 when the original has no fixed time).</summary>
    public float Duration;

    public int Priority;

    /// <summary>The actor (killer, or who joined, or the chat's sender): PRI.PlayerID, team (-1 unknown) and name; 0 / -1 / 0 when none.</summary>
    public int ActorPlayerId, ActorTeam, ActorNameId;

    public int VictimPlayerId, VictimTeam, VictimNameId;

    /// <summary>Who set off what killed the victim (shot the barrel), when it was someone else.</summary>
    public int TriggeredByPlayerId, TriggeredByTeam, TriggeredByNameId;

    /// <summary>The final text, UTF-16, zero terminated (cut at 255 characters).</summary>
    public fixed char Text[TextCapacity];
}
