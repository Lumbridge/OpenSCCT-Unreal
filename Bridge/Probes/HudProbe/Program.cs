using System;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.Game.Combat;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Hud;

// HudProbe <content root> [map] [spy|merc] [seconds] [mode] [api]
// With "api" as the sixth argument the whole run goes through the two function tables exactly as Unreal calls them
// (Exports.GetApi and HudExports.GetApi, unmanaged function pointers, raw buffers), instead of the managed methods.
string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
bool merc = args.Length > 2 && args[2].Equals("merc", StringComparison.OrdinalIgnoreCase);
double seconds = args.Length > 3 ? double.Parse(args[3], CultureInfo.InvariantCulture) : 14;
string mode = args.Length > 4 ? args[4] : "Story";

// The struct sizes ScctHudApi.h asserts.
Console.WriteLine($"sizes: HudState {Unsafe.SizeOf<HudState>()}, HudRound {Unsafe.SizeOf<HudRound>()}, HudGadget {Unsafe.SizeOf<HudGadget>()}, " +
    $"HudInteraction {Unsafe.SizeOf<HudInteraction>()}, HudMenuLine {Unsafe.SizeOf<HudMenuLine>()}, HudObjective {Unsafe.SizeOf<HudObjective>()}, " +
    $"HudObjectiveLine {Unsafe.SizeOf<HudObjectiveLine>()}, HudBlip {Unsafe.SizeOf<HudBlip>()}, HudShownMessage {Unsafe.SizeOf<HudShownMessage>()}, " +
    $"HudScoreRow {Unsafe.SizeOf<HudScoreRow>()}, HudMessage {Unsafe.SizeOf<HudMessage>()}, HudApi {Unsafe.SizeOf<HudApi>()}");

Console.WriteLine($"sizes: HudCameraView {Unsafe.SizeOf<HudCameraView>()}, HudGadgetApi {Unsafe.SizeOf<HudGadgetApi>()}");

if (args.Length > 5 && args[5].Equals("api", StringComparison.OrdinalIgnoreCase))
{
    return ApiRun(content, map, mode, merc, seconds);
}

Console.WriteLine($"sizes: HudTextureLayer {Unsafe.SizeOf<HudTextureLayer>()}, HudTextureApi {Unsafe.SizeOf<HudTextureApi>()}");

if (args.Length > 5 && args[5].Equals("menu", StringComparison.OrdinalIgnoreCase))
{
    return MenuRun(content, map, mode, merc, seconds);
}

if (args.Length > 5 && args[5].Equals("icons", StringComparison.OrdinalIgnoreCase))
{
    return IconsRun(content, map, mode, merc, seconds);
}

Console.WriteLine($"sizes: HudHeartSensorState {Unsafe.SizeOf<HudHeartSensorState>()}, HudRadarApi {Unsafe.SizeOf<HudRadarApi>()}");

if (args.Length > 5 && args[5].Equals("radar", StringComparison.OrdinalIgnoreCase))
{
    return RadarRun(content, map, mode, merc, seconds);
}

Console.WriteLine($"sizes: HudBackpackState {Unsafe.SizeOf<HudBackpackState>()}, HudBackpackRow {Unsafe.SizeOf<HudBackpackRow>()}, HudBackpackApi {Unsafe.SizeOf<HudBackpackApi>()}");

if (args.Length > 5 && args[5].Equals("backpack", StringComparison.OrdinalIgnoreCase))
{
    return BackpackRun(content, map, mode, merc, seconds);
}

Console.WriteLine($"sizes: HudPageLayer {Unsafe.SizeOf<HudPageLayer>()}, HudBinocularPageInput {Unsafe.SizeOf<HudBinocularPageInput>()}, HudScopePageInput {Unsafe.SizeOf<HudScopePageInput>()}, HudBinocularApi {Unsafe.SizeOf<HudBinocularApi>()}");

if (args.Length > 5 && args[5].Equals("binoculars", StringComparison.OrdinalIgnoreCase))
{
    return BinocularsRun(content, map, mode, merc, seconds);
}

Console.WriteLine($"sizes: HudMotionView {Unsafe.SizeOf<HudMotionView>()}, HudMotionState {Unsafe.SizeOf<HudMotionState>()}, HudMotionRow {Unsafe.SizeOf<HudMotionRow>()}, " +
    $"HudMotionLock {Unsafe.SizeOf<HudMotionLock>()}, HudMotionApi {Unsafe.SizeOf<HudMotionApi>()}");

if (args.Length > 5 && args[5].Equals("motion", StringComparison.OrdinalIgnoreCase))
{
    return MotionRun(content, map, mode, merc, seconds);
}

Console.WriteLine($"sizes: TeamPingCamera {Unsafe.SizeOf<TeamPingCamera>()}, TeamPingState {Unsafe.SizeOf<TeamPingState>()}, TeamPingRow {Unsafe.SizeOf<TeamPingRow>()}, TeamPingApi {Unsafe.SizeOf<TeamPingApi>()}");

if (args.Length > 5 && args[5].Equals("ping", StringComparison.OrdinalIgnoreCase))
{
    return PingRun(content, map, mode, merc, seconds);
}

BridgeSession.Configure(content);
var load = Stopwatch.StartNew();
using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 3);
// A stand-in for an exec command that raises a HUD notification: the play session runs the player's commands inside
// the client tick, after BridgeSession.BeforeTick and right before the world steps, and what they raise is dropped by
// the step unless the HUD reads it in between. This hook sits where the commands run (it is added before the HUD's).
double lateAt = 4.2;
bool lateRaised = false;
var clock = Stopwatch.StartNew();
session.Session.Client.BeforeWorldTick += _ =>
{
    if (!lateRaised && clock.Elapsed.TotalSeconds > lateAt && session.Session.LocalController is { } local)
    {
        lateRaised = true;
        CombatFx.Notify(local, CombatFx.SetHudPerturbationLength, 2f);
        Console.WriteLine($"   probe: raised SetHudPerturbationLength(2) between ticks at level time {local.Level.TimeSeconds:F2}");
    }
};

// What the HUD's own work after each tick allocates: one handler before the HUD's and one after it.
long tickMark = 0, tickAllocated = 0, tickPeak = 0, updateAllocated = 0;
int updateTicks = 0;
int ticksMeasured = 0, ticksSeen = 0;
session.Ticked += () => tickMark = GC.GetAllocatedBytesForCurrentThread();

// As Unreal must: the HUD is asked for before the first Update, so the first messages are received.
HudFeature hud = HudFeature.Of(session);
session.Ticked += () =>
{
    // The first seconds fill the text table and the row buffers; after that a tick should allocate nothing.
    if (++ticksSeen > 240)
    {
        long bytes = GC.GetAllocatedBytesForCurrentThread() - tickMark;
        tickAllocated += bytes;
        tickPeak = Math.Max(tickPeak, bytes);
        ticksMeasured++;
    }
};
hud.SetViewport(1920, 1080);
Console.WriteLine($"{map} ({mode}, {(merc ? "merc" : "spy")}) loaded in {load.Elapsed.TotalSeconds:F1} s; interface strings {(hud.Strings.Loaded ? "loaded" : "MISSING")}: " +
    $"[HUD] TextChat1 = '{hud.Strings.Get("MagmaDynamic", "HUD", "TextChat1")}', TextChat2 = '{hud.Strings.Get("MagmaDynamic", "HUD", "TextChat2")}'");

var messages = new HudMessage[16];
var gadgets = new HudGadget[16];
var interactions = new HudInteraction[16];
var objectives = new HudObjective[64];
var lines = new HudObjectiveLine[32];
var blips = new HudBlip[64];
var shown = new HudShownMessage[32];
var rows = new HudScoreRow[32];
var input = default(BridgeInput);
var run = Stopwatch.StartNew();
double nextReport = 0;
int ticks = 0, frames = 0, drained = 0, lastClip = -1, lastGadget = -2, lastDamage = 0;
double readMs = 0;
bool fired = false, goggled = false, markers = false, lasered = false;
int lastLaser = 0;
int lastEr = -1, lastLocation = -1, lastTarget = 0, lastFx = 0;
int lastVision = 0;

while (run.Elapsed.TotalSeconds < seconds)
{
    double t = run.Elapsed.TotalSeconds;
    Drive(t, ref input);
    session.SetInput(input);
    if (t > 6.5 && !fired)
    {
        // The press itself is an exec command; the held button keeps an automatic weapon firing.
        fired = true;
        session.Command("Fire", 0);
    }

    if (t > 3 && !goggled)
    {
        // An exec command between two ticks: the goggle's message is raised outside a tick and must still arrive.
        goggled = true;
        session.Command("SwitchHeadset", 1);
    }

    if (merc && t > 7.8 && !lasered)
    {
        // The laser sight: its target name pass runs each tick from here (lock, then expiry).
        lasered = true;
        session.Command("GadLaserSwitch", 0);
    }

    if (t > 5 && !markers)
    {
        // The HUD's own F9 command: the Enhanced Reality flag flips and the original's line is queued.
        markers = true;
        hud.SwitchEnhancedReality();
    }

    long updateMark = GC.GetAllocatedBytesForCurrentThread();
    int ran = session.Update();
    ticks += ran;
    if (ticksSeen > 240)
    {
        updateAllocated += GC.GetAllocatedBytesForCurrentThread() - updateMark;
        updateTicks += ran;
    }

    if (session.Failure is { } failure)
    {
        Console.WriteLine("FAILED: " + failure);
        return 1;
    }

    // Every frame, as Unreal would: the state, the round, every list, and the new messages. Reading must not allocate.
    frames++;
    long reading = Stopwatch.GetTimestamp();
    long before = GC.GetAllocatedBytesForCurrentThread();
    bool valid = hud.GetState(out HudState s);
    hud.GetRound(out HudRound r);
    int gadgetCount = hud.Frame.Gadgets.CopyTo(gadgets);
    int interactionCount = hud.Frame.Interactions.CopyTo(interactions);
    int objectiveCount = hud.Frame.Objectives.CopyTo(objectives);
    int lineCount = hud.Frame.ObjectiveLines.CopyTo(lines);
    int blipCount = hud.Frame.Radar.CopyTo(blips);
    int shownCount = hud.Frame.Shown.CopyTo(shown);
    int rowCount = hud.Frame.Score.CopyTo(rows);
    long readAllocated = GC.GetAllocatedBytesForCurrentThread() - before;
    readMs += Stopwatch.GetElapsedTime(reading).TotalMilliseconds;
    if (frames > 200 && readAllocated != 0)
    {
        Console.WriteLine($"FAILED: reading a frame allocated {readAllocated} bytes");
        return 1;
    }

    int n;
    while ((n = hud.DrainMessages(messages)) > 0)
    {
        for (int i = 0; i < n; i++)
        {
            drained++;
            Console.WriteLine("   message " + Describe(hud, messages[i]));
        }
    }

    // Changes worth a line of their own.
    if (valid && s.ClipAmmo != lastClip && (s.Flags & HudFlags.HasWeapon) != 0)
    {
        Console.WriteLine($"   t={t:F1} clip {s.ClipAmmo} / reserve {s.Ammo}, accuracy {s.Accuracy:F3} (reticle frame {s.ReticleFrame}), reticle message '{hud.Texts.Text(s.ReticleMessageId)}'");
        lastClip = s.ClipAmmo;
    }

    if (valid && s.SelectedGadget != lastGadget)
    {
        string name = s.SelectedGadget >= 0 && s.SelectedGadget < gadgetCount ? hud.Texts.Text(gadgets[s.SelectedGadget].ClassId) : "none";
        Console.WriteLine($"   t={t:F1} selected gadget {s.SelectedGadget}: {name}");
        lastGadget = s.SelectedGadget;
    }

    if (valid && s.VisionMode != lastVision)
    {
        Console.WriteLine($"   t={t:F1} vision mode {lastVision} -> {s.VisionMode}");
        lastVision = s.VisionMode;
    }

    int er = s.Flags & HudFlags.EnhancedReality;
    if (valid && er != lastEr)
    {
        Console.WriteLine($"   t={t:F1} enhanced reality {(er != 0 ? "on" : "off")}");
        lastEr = er;
    }

    if (valid && s.LocationNameId != lastLocation)
    {
        Console.WriteLine($"   t={t:F1} location '{hud.Texts.Text(s.LocationNameId)}'");
        lastLocation = s.LocationNameId;
    }

    if (valid && s.TargetActorId != lastTarget)
    {
        Console.WriteLine($"   t={t:F1} laser target actor {s.TargetActorId} '{hud.Texts.Text(s.TargetNameId)}' friend={(s.Flags & HudFlags.TargetFriend) != 0} locked at {s.TargetLockTime:F1}, name after {s.TargetNameDelay:F1} s");
        lastTarget = s.TargetActorId;
    }

    int laser = s.Flags & HudFlags.LaserOn;
    if (valid && laser != lastLaser)
    {
        Console.WriteLine($"   t={t:F1} laser sight {(laser != 0 ? "on" : "off")}, reticle shown={(s.Flags & HudFlags.ShowReticle) != 0}");
        lastLaser = laser;
    }

    if (valid && s.EffectFlags != lastFx)
    {
        Console.WriteLine($"   t={t:F1} effect flags 0x{lastFx:X} -> 0x{s.EffectFlags:X}{((s.EffectFlags & HudEffectFlags.Perturbed) != 0 ? " (perturbed)" : "")}");
        lastFx = s.EffectFlags;
    }

    if (s.DamageCount != lastDamage)
    {
        Console.WriteLine($"   t={t:F1} hit #{s.DamageCount}: damage type {s.LastDamageType}, health {s.Health}");
        lastDamage = s.DamageCount;
    }

    if (t >= nextReport)
    {
        nextReport += 3;
        Console.WriteLine($"t={t:F1} ticks={ticks} frames={frames} read {readMs / frames:F4} ms/frame, texts {hud.Texts.Count}, hud calls received {hud.Received}, messages drained {drained}");
        Console.WriteLine($"   state: valid={s.Valid} side={s.Side} flags=0x{s.Flags:X} fx=0x{s.EffectFlags:X} state='{hud.Texts.Text(s.StateNameId)}' health={s.Health} lives={s.Lives} " +
            $"at ({s.X:F0},{s.Y:F0},{s.Z:F0}) yaw={s.Yaw:F0} speed={s.Speed:F0} vision={s.VisionMode} energy={s.EnergyLevel:F0}/{s.EnergyLevelMax}");
        if ((s.Flags & HudFlags.HasWeapon) != 0)
        {
            Console.WriteLine($"   weapon: kind {s.WeaponKind} '{hud.Texts.Text(s.WeaponNameId)}' clip {s.ClipAmmo} reserve {s.Ammo} mode {s.FireMode} '{hud.Texts.Text(s.FireModeNameId)}' " +
                $"dispersion {s.MinDispersion:F2}..{s.MaxDispersion:F2} now {s.Accuracy:F3}; reticle shown={(s.Flags & HudFlags.ShowReticle) != 0} spot={((s.Flags & HudFlags.SpotValid) != 0 ? $"({s.SpotX:F1},{s.SpotY:F1})" : "none")}");
        }

        for (int i = 0; i < Math.Min(gadgetCount, gadgets.Length); i++)
        {
            HudGadget g = gadgets[i];
            Console.WriteLine($"   gadget {i}: {hud.Texts.Text(g.ClassId)} '{hud.Texts.Text(g.NameId)}' icon '{hud.Texts.Text(g.IconNameId)}' ({g.IconU},{g.IconV}) charge {g.Charge} flags {g.Flags}");
        }

        Console.WriteLine($"   prompts: {interactionCount} interactions (selected {s.SelectedInteraction}, use '{hud.Texts.Text(s.UsePromptId)}'), speak {s.SpeakPrompt}, coop {s.CoopIcon}; view {s.GadgetView}, binoculars {s.BinocularMode}, heart cone {s.HeartCone}, gas mask {s.GasMaskAirSeconds}");
        for (int i = 0; i < Math.Min(interactionCount, interactions.Length); i++)
        {
            Console.WriteLine($"      interaction {i}: {hud.Texts.Text(interactions[i].ClassId)} '{hud.Texts.Text(interactions[i].DescriptionId)}' selected={interactions[i].Selected}");
        }

        Console.WriteLine($"   round: valid={r.Valid} mode={r.GameMode} map '{hud.Texts.Text(r.MapTitleId)}' limit {r.TimeLimit} min, elapsed {r.ElapsedTime} s, left {r.RemainingSeconds} s, launching={r.Launching} ended={r.Ended} winner={r.WinningTeam}, " +
            $"objectives {r.ObjectivesLeft} left of {r.NumObjectives}, counter mode {r.CounterMode}, {Pairs(r)}");
        for (int i = 0; i < Math.Min(lineCount, lines.Length); i++)
        {
            Console.WriteLine($"   objective line {i}: done={lines[i].Done} '{hud.Texts.Text(lines[i].TextId)}'");
        }

        for (int i = 0; i < Math.Min(objectiveCount, Math.Min(4, objectives.Length)); i++)
        {
            HudObjective o = objectives[i];
            Console.WriteLine($"   marker {i} of {objectiveCount}: '{hud.Texts.Text(o.NameId)}' kind {o.Kind} role {o.Role} flags {o.Flags} percent {o.Percent} at ({o.X:F0},{o.Y:F0},{o.Z:F0})");
        }

        Console.WriteLine($"   radar: {blipCount} blips" + (blipCount > 0 ? $", first kind {blips[0].Kind} at ({blips[0].X:F0},{blips[0].Y:F0},{blips[0].Z:F0})" : ""));
        for (int i = 0; i < Math.Min(shownCount, shown.Length); i++)
        {
            Console.WriteLine($"   shown: category {shown[i].Category} '{hud.Texts.Text(shown[i].TextId)}' for {shown[i].Remaining:F1} s more");
        }

        for (int i = 0; i < Math.Min(rowCount, rows.Length); i++)
        {
            HudScoreRow p = rows[i];
            Console.WriteLine($"   score: #{p.PlayerId} '{hud.Texts.Text(p.NameId)}' team {p.Team} flags {p.Flags} obj/kills {p.ObjOrKills} k {p.Kills} d {p.Deaths} score {p.Score} lives {p.Lives} ping {p.Ping} pawn {p.PawnActorId}");
        }
    }

    Thread.Sleep(4);
}

Console.WriteLine($"done: {ticks} ticks, {frames} frames, {drained} messages drained of {hud.Messages.Total} queued, {hud.Texts.Count} texts, {hud.Received} HUD calls received");
Console.WriteLine($"HUD work per tick after warm-up: {(ticksMeasured > 0 ? (double)tickAllocated / ticksMeasured : 0):F1} bytes allocated on average over {ticksMeasured} ticks (largest {tickPeak}); the whole client tick allocates {(updateTicks > 0 ? (double)updateAllocated / updateTicks : 0):F0} bytes");
return 0;

// The local player's keys over the run: walk forward, cycle gadgets, fire (a merc), reload, then hold Use.
static unsafe void Drive(double t, ref BridgeInput input)
{
    const int Up = 0, NextItem = 5, Fire = 6, Using = 10, Reload = 13;
    input.Buttons[Up] = (byte)(t is > 2 and < 4 ? 1 : 0);
    input.Buttons[NextItem] = (byte)((t is > 4.5 and < 4.6) || (t is > 5.5 and < 5.6) ? 1 : 0);
    input.Buttons[Fire] = (byte)(t is > 6.5 and < 7.5 ? 1 : 0);
    input.Buttons[Reload] = (byte)(t is > 8 and < 8.2 ? 1 : 0);
    input.Buttons[Using] = (byte)(t is > 10 and < 11 ? 1 : 0);
}

// The same run through the native tables: what Unreal's C++ does, call for call.
static unsafe int ApiRun(string content, string map, string mode, bool merc, double seconds)
{
    BridgeApi bridgeTable;
    HudApi hudTable;
    int bridgeVersion = ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApiSized)(&bridgeTable, sizeof(BridgeApi));
    int hudVersion = ((delegate* unmanaged<HudApi*, int>)&HudExports.GetApi)(&hudTable);
    BridgeApi bridge = bridgeTable;
    HudApi api = hudTable;
    Console.WriteLine($"api: bridge table version {bridgeVersion}, HUD table version {hudVersion} (table says {api.Version})");

    HudState state;
    HudRound round;
    char* text = stackalloc char[512];
    string Text(int id)
    {
        int length = api.GetText(id, text, 512);
        return new string(text, 0, Math.Min(length, 511));
    }

    string Error()
    {
        int length = bridge.LastError(text, 512);
        return new string(text, 0, Math.Min(length, 511));
    }

    // Before a session: every entry answers 0 and writes nothing harmful.
    int idleState = api.GetState(&state);
    int idleDrain = api.DrainMessages(null, 0);
    Console.WriteLine($"api: no session: GetState {idleState} (valid {state.Valid}), GetState(null) {api.GetState(null)}, GetRound(null) {api.GetRound(null)}, DrainMessages {idleDrain}, GetText(1) length {api.GetText(1, text, 512)}");

    fixed (char* c = content, m = map, g = mode)
    {
        if (bridge.Configure(c) == 0 || bridge.StartLocal(m, g, merc ? 1 : 0, 3) == 0)
        {
            Console.WriteLine("FAILED: " + Error());
            return 1;
        }
    }

    // As ScctHudApi.h says: one HUD call right after StartLocal, before the first Update.
    api.GetState(&state);
    api.SetViewport(1920, 1080);
    fixed (char* file = "MagmaDynamic", section = "Magma", key = "Inifite_Short")
    {
        int length = api.Localize(file, section, key, text, 512);
        Console.WriteLine($"api: Localize(MagmaDynamic, Magma, Inifite_Short) = '{new string(text, 0, Math.Min(length, 511))}'");
    }

    HudMessage* messages = stackalloc HudMessage[4];
    HudGadget* gadgets = stackalloc HudGadget[16];
    HudScoreRow* rows = stackalloc HudScoreRow[32];
    HudObjectiveLine* lines = stackalloc HudObjectiveLine[32];
    HudObjective* objectives = stackalloc HudObjective[64];
    HudBlip* blips = stackalloc HudBlip[64];
    HudShownMessage* shown = stackalloc HudShownMessage[32];
    var run = Stopwatch.StartNew();
    int ticks = 0, frames = 0, drained = 0;
    double next = 2;
    bool switched = false;
    while (run.Elapsed.TotalSeconds < seconds)
    {
        ticks += bridge.Update();
        if (bridge.GetState() != (int)BridgeState.Playing)
        {
            Console.WriteLine("FAILED: " + Error());
            return 1;
        }

        frames++;
        int valid = api.GetState(&state);
        api.GetRound(&round);
        int gadgetCount = api.GetGadgets(gadgets, 16);
        int rowCount = api.GetScoreboard(rows, 32);
        int lineCount = api.GetObjectiveLines(lines, 32);
        int markerCount = api.GetObjectives(objectives, 64);
        int blipCount = api.GetRadar(blips, 64);
        int shownCount = api.GetShownMessages(shown, 32);
        int n;
        do
        {
            // A buffer of four on purpose: a burst needs several calls, and none may be lost or repeated.
            n = api.DrainMessages(messages, 4);
            for (int i = 0; i < n; i++)
            {
                drained++;
                Console.WriteLine($"   api message: category {messages[i].Category} kind {messages[i].Kind} actor '{Text(messages[i].ActorNameId)}' flags {messages[i].Flags}: \"{new string(messages[i].Text)}\"");
            }
        }
        while (n == 4);

        if (!switched && run.Elapsed.TotalSeconds > 4 && valid != 0)
        {
            switched = true;
            api.SetOption(HudOption.SwitchEnhancedReality, 1);
            api.SetOption(HudOption.DistanceInFeet, 1);
        }

        if (run.Elapsed.TotalSeconds >= next)
        {
            next += 3;
            Console.WriteLine($"api t={run.Elapsed.TotalSeconds:F1}: valid {valid} side {state.Side} flags 0x{state.Flags:X} health {state.Health} lives {state.Lives} state '{Text(state.StateNameId)}' location '{Text(state.LocationNameId)}' " +
                $"unit '{Text(state.DistanceUnitId)}' x {state.DistanceScale:F4}; round mode {round.GameMode} '{Text(round.MapTitleId)}' left {round.RemainingSeconds} s; " +
                $"{gadgetCount} gadgets, {rowCount} score rows, {lineCount} objective lines, {markerCount} markers, {blipCount} blips, {shownCount} shown");
            if (gadgetCount > 0)
            {
                // A buffer smaller than the list: only that many rows are written and the full count comes back.
                HudGadget one;
                int full = api.GetGadgets(&one, 1);
                int cut = api.GetText(one.NameId, text, 4);
                string cutText = new(text);
                Console.WriteLine($"   api gadget 0 of {full}: {Text(one.ClassId)} '{Text(one.NameId)}' charge {one.Charge}; GetText into 4 chars -> '{cutText}' of full length {cut}");
            }

            for (int i = 0; i < Math.Min(rowCount, 32); i++)
            {
                Console.WriteLine($"   api score: #{rows[i].PlayerId} '{Text(rows[i].NameId)}' team {rows[i].Team} flags {rows[i].Flags} lives {rows[i].Lives} pawn {rows[i].PawnActorId}");
            }
        }

        Thread.Sleep(4);
    }

    bridge.Stop();
    int afterStop = api.GetState(&state);
    Console.WriteLine($"api done: {ticks} ticks, {frames} frames, {drained} messages drained; after Stop GetState {afterStop} (valid {state.Valid}), bridge state {bridge.GetState()}");
    return 0;
}

// The gadget menus and the camera page: Alt-fire held from 4 s, the gadget's menu forced at 4.2 s (the spy's sticky
// cam menu, the merc's mine menu), the down key twice to move the selection, Alt-fire let go at 9 s (the spy's
// selected "new cam" then launches a sticky cam and the view goes through it). Every change of the menu rows and of
// the camera view is printed.
static unsafe int MenuRun(string content, string map, string mode, bool merc, double seconds)
{
    BridgeSession.Configure(content);
    using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 3);
    HudFeature hud = HudFeature.Of(session);
    hud.SetViewport(1920, 1080);
    var menu = new HudMenuLine[8];
    var input = default(BridgeInput);
    var run = Stopwatch.StartNew();
    bool forced = false;
    var commandsDone = new System.Collections.Generic.HashSet<double>();
    string lastRows = "", lastView = "";
    int lastState = -1;
    while (run.Elapsed.TotalSeconds < seconds)
    {
        double t = run.Elapsed.TotalSeconds;
        const int Down = 1, AltFire = 7;
        input.Buttons[AltFire] = (byte)(t is > 4 and < 9 ? 1 : 0);
        input.Buttons[Down] = (byte)((t is > 6 and < 6.2) || (t is > 7 and < 7.2) ? 1 : 0);
        // Then the spy fires the sticky cam he holds (the ordinary launch), which puts the view through it.
        const int Fire = 6;
        // The merc instead takes the camera network gadget at 10.5 s and fires it at 11 s.
        input.Buttons[Fire] = (byte)((!merc && t is > 10.5 and < 10.7) || (merc && t is > 11 and < 11.2) ? 1 : 0);
        input.MouseY = !merc && t is > 9.5 and < 10.3 ? -40f : 0f;
        // The spy draws the Shocker with the fire-mode button held (the sticky cam launches from its muzzle).
        const int FireMode = 11;
        input.Buttons[FireMode] = (byte)(!merc && t is > 9.5 and < 10.2 ? 1 : 0);
        foreach ((double at, string command) in new[] { (10.5, "Fire"), (11.0, "Fire"), (12.0, "AltFire"), (13.0, "AltFire") })
        {
            if (t > at && commandsDone.Add(at))
            {
                session.Command(command, 0);
                Console.WriteLine($"t={t:F1} command {command}; loaded {session.Session.LocalController?.SBPawn?.WeaponLoaded}, selected {session.Session.LocalController?.SBPawn?.SelectedGadget?.GetType().Name}");
            }
        }
        if (merc && t > 10.5 && session.Session.LocalController?.SBPawn is { SelectedGadget: not OpenScct.Game.SBase.SGadCamNetwork } mercPawn)
        {
            for (int slot = 0; slot < 8 && mercPawn.SelectedGadget is not OpenScct.Game.SBase.SGadCamNetwork; slot++)
            {
                session.Session.LocalController.SelectGadget(slot);
            }
        }
        session.SetInput(input);
        session.Update();
        if (session.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            return 1;
        }

        while (hud.DrainMessages(new HudMessage[8]) > 0)
        {
        }

        if (!forced && t > 4.2)
        {
            forced = true;
            var held = new HudGadget[16];
            int heldCount = hud.Frame.Gadgets.CopyTo(held);
            Console.WriteLine($"t={t:F1} gadgets: {string.Join(", ", held.Take(heldCount).Select(g => hud.Texts.Text(g.ClassId)))}");
            bool entered = HudGadgetExports.EnterMenu(session.Session.LocalController, merc ? 2 : 1);
            Console.WriteLine($"t={t:F1} ForceMenu({(merc ? "mine" : "sticky")}) -> {(entered ? "in the menu" : "refused")}");
        }

        hud.GetState(out HudState s);
        if (s.StateNameId != lastState)
        {
            lastState = s.StateNameId;
            Console.WriteLine($"t={t:F1} controller state '{hud.Texts.Text(s.StateNameId)}', view {s.GadgetView}, menu lines {s.MenuLineCount}");
        }

        int count = hud.Frame.Menu.CopyTo(menu);
        string rows = string.Join(" | ", Enumerable.Range(0, Math.Min(count, menu.Length)).Select(i =>
            $"'{hud.Texts.Text(menu[i].TextId)}' {(menu[i].Look switch { 0 => "normal", 1 => "SELECTED", 2 => "grey", _ => "untouched" })}"));
        if (rows != lastRows)
        {
            lastRows = rows;
            Console.WriteLine($"t={t:F1} menu rows ({count}): {(count == 0 ? "none" : rows)}");
        }

        HudCameraView cam = HudGadgetExports.ReadCameraView(session.Session.LocalController);
        string view = cam.View == 0 ? "none" : $"view {cam.View} fov {cam.Fov:F0} ({cam.MinFov:F0}..{cam.MaxFov:F0}) gas {cam.GasDoses} ready in {cam.GasReadySeconds:F1} s, alert {((s.EffectFlags & HudEffectFlags.CamNetworkAlert) != 0 ? 1 : 0)}";
        if (view != lastView)
        {
            lastView = view;
            Console.WriteLine($"t={t:F1} camera view: {view}");
        }

        Thread.Sleep(4);
    }

    return 0;
}

// The HUD's pictures: the textures the coop icons and the respawn pick draw from, the coop clips at a few moments, then
// a match where the local pawn is killed on the server at 3 s (Merc Deathmatch: the respawn weapon pick comes up, Use
// at 7 s cycles it) and the CoopIcon and RespawnWeapon fields are printed whenever they change.
static unsafe int IconsRun(string content, string map, string mode, bool merc, double seconds)
{
    BridgeSession.Configure(content);
    foreach ((string package, string name) in new[] { ("SHudTextures", "Icones_Weapons"), ("SHudTextures", "Icones_Gadgets"), ("Hud", "Move_coop"), ("Hud", "Move_coop02"), ("Hud", "heart"), ("Hud", "nothing_here") })
    {
        HudPng.Image? image = HudTextureExports.ReadTexture(package, name, out string? error);
        string lit = image is null ? "" : $", {image.Bgra.Where((_, i) => i % 4 == 3).Count(a => a > 0)} texels not transparent, first opaque BGRA " +
            string.Join(",", image.Bgra.Chunk(4).FirstOrDefault(p => p[3] > 128) ?? []);
        Console.WriteLine($"texture {package}.{name}: {(image is null ? "missing (" + error + ")" : $"{image.Width}x{image.Height}{lit}")}");
    }

    HudTextureLibrary? library = HudTextureLibrary.Current(out _);
    Console.WriteLine(library is not null && library.Material("Hud", "heart", out string tex, out int u0, out int v0, out int u1, out int v1)
        ? $"material Hud.heart: {tex} ({u0},{v0})-({u1},{v1})" : "material Hud.heart: missing");
    HudCoopIcons? icons = HudCoopIcons.Current(out string? coopError);
    Console.WriteLine(icons is null ? $"coop icons: missing ({coopError})" : $"coop icons: bounds {icons.Bounds}");
    for (int icon = 1; icon <= 4 && icons is not null; icon++)
    {
        Console.WriteLine($"   coop {icon} {icons.ClipName(icon)} bounds {icons.BoundsOf(icon)}");
        foreach (float at in new[] { 0f, 0.5f, 1.5f, 4f })
        {
            var layers = HudTextureExports.ReadCoopLayers(icon, at);
            Console.WriteLine($"   coop {icon} {icons.ClipName(icon)} at {at:F1} s: " + string.Join(" | ", layers.Select(l =>
                $"{l.Texture} ({l.U0},{l.V0})-({l.U1},{l.V1}) at ({l.L:F0},{l.T:F0})-({l.R:F0},{l.B:F0}) rgba {l.Red:F2},{l.Green:F2},{l.Blue:F2},{l.Alpha:F2}")));
        }
    }

    using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 3);
    HudFeature hud = HudFeature.Of(session);
    hud.SetViewport(1920, 1080);
    var input = default(BridgeInput);
    var run = Stopwatch.StartNew();
    bool killed = false;
    string last = "";
    while (run.Elapsed.TotalSeconds < seconds)
    {
        double t = run.Elapsed.TotalSeconds;
        const int Use = 10;
        input.Buttons[Use] = (byte)(t is > 7 and < 7.3 ? 1 : 0);
        session.SetInput(input);
        session.Update();
        if (session.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            return 1;
        }

        while (hud.DrainMessages(new HudMessage[8]) > 0)
        {
        }

        if (!killed && t > 3 && session.Session.LocalController?.PlayerReplicationInfo is { } pri && session.Session.Host is { } host)
        {
            killed = true;
            int id = pri.PlayerID;
            void Kill(OpenScct.Server.ServerHost running)
            {
                running.BeforeWorldTick -= Kill;
                Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1} KillOnServer(player {id}) -> {HudTextureExports.KillOnServer(running, id)}");
            }

            host.BeforeWorldTick += Kill;
        }

        hud.GetState(out HudState s);
        hud.GetRound(out HudRound r);
        string now = $"state '{hud.Texts.Text(s.StateNameId)}' mode {r.GameMode} health {s.Health} lives {s.Lives} | CoopIcon {s.CoopIcon} | " +
            $"RespawnWeapon icon ({s.RespawnWeaponIconU},{s.RespawnWeaponIconV}) class '{hud.Texts.Text(s.RespawnWeaponClassId)}'";
        if (now != last)
        {
            last = now;
            Console.WriteLine($"t={t:F1} {now}");
        }

        Thread.Sleep(4);
    }

    return 0;
}

// The radar and the heart sensor: the blips by kind each second, and for a spy the sensor selected at 2 s, the weapon
// drawn (the FireMode button held) from 3 s, the cone, its clip frame and the scan pings and heartbeats it raised,
// checked against the sound records the events feature resolved from them.
static unsafe int RadarRun(string content, string map, string mode, bool merc, double seconds)
{
    BridgeSession.Configure(content);
    using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 3);
    HudFeature hud = HudFeature.Of(session);
    OpenScct.UnrealBridge.Events.EventsFeature events = OpenScct.UnrealBridge.Events.EventsFeature.Of(session);
    HudHeartSensor sensor = HudHeartSensor.Of(session);
    hud.SetViewport(1920, 1080);
    var input = default(BridgeInput);
    var blips = new HudBlip[64];
    var sounds = new BridgeSound[256];
    var run = Stopwatch.StartNew();
    bool selected = false;
    double nextReport = 1;
    int pings = 0, beats = 0, maxCone = 0;
    while (run.Elapsed.TotalSeconds < seconds)
    {
        double t = run.Elapsed.TotalSeconds;
        const int FireMode = 11;
        input.Buttons[FireMode] = (byte)(!merc && t is > 3 and < 3.7 ? 1 : 0);
        session.SetInput(input);
        session.Update();
        if (session.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            return 1;
        }

        while (hud.DrainMessages(new HudMessage[8]) > 0)
        {
        }

        if (!merc && !selected && t > 2)
        {
            selected = true;
            Console.WriteLine($"t={t:F1} SelectHeartSensor -> {HudRadarExports.Select(session.Session.LocalController)}");
        }

        int n;
        while ((n = events.DrainSounds(sounds)) > 0)
        {
            for (int i = 0; i < n; i++)
            {
                string name = OpenScct.UnrealBridge.Events.EventNames.Text(sounds[i].SoundId);
                pings += name.EndsWith("echo_indic1", StringComparison.OrdinalIgnoreCase) ? 1 : 0;
                beats += name.EndsWith("Heart_Beat", StringComparison.OrdinalIgnoreCase) ? 1 : 0;
            }
        }

        hud.GetState(out HudState s);
        HudHeartSensorState h = HudRadarExports.Read(sensor);
        maxCone = Math.Max(maxCone, h.Cone);
        if (t >= nextReport)
        {
            nextReport += 1;
            int count = hud.Frame.Radar.CopyTo(blips);
            int[] kinds = new int[4];
            for (int i = 0; i < count; i++)
            {
                kinds[Math.Clamp(blips[i].Kind, 0, 3)]++;
            }

            string far = string.Join(" ", blips.Take(count).Select(b =>
                $"k{b.Kind}:{MathF.Sqrt((b.X - s.X) * (b.X - s.X) + (b.Y - s.Y) * (b.Y - s.Y)) / 44f:F0}/{(b.Z - s.Z) / 44f:F0}"));
            Console.WriteLine($"t={t:F1} side {s.Side} yaw {s.Yaw:F0} location '{hud.Texts.Text(s.LocationNameId)}' | {count} blips: {kinds[0]} teammates, {kinds[1]} enemies, " +
                $"{kinds[2]} alarms, {kinds[3]} gpo [radar units dist/dz {far}] | HeartCone {s.HeartCone} sensor cone {h.Cone} frame {h.Frame:F1}/{h.LoopFrames} " +
                $"pings {h.CheckSounds} beats {h.DetectedSounds} raised {h.SoundsRaised} resolved echo_indic1 {pings} Heart_Beat {beats}");
        }

        Thread.Sleep(4);
    }

    Console.WriteLine($"radar summary: highest cone {maxCone}, scan pings asked {sensor.CheckSounds}, heartbeats asked {sensor.DetectedSounds}, raised {sensor.SoundsRaised}, " +
        $"sound records resolved: echo_indic1 {pings}, Heart_Beat {beats}");
    return 0;
}

// The merc backpack panels (five bots, so a merc has two merc mates): backpacks given to every merc at 1.2 s, every merc's gadgets and backpack slots on the server at 2 s, the backpack selected at 3 s,
// the panels each second (own share and both mates' shares with icons), and from 8 s for 5 s the mate carrying the
// player's share held in front of him with one of his charges spent, so the refill entry offers that share.
static unsafe int BackpackRun(string content, string map, string mode, bool merc, double seconds)
{
    BridgeSession.Configure(content);
    using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 5);
    HudFeature hud = HudFeature.Of(session);
    hud.SetViewport(1920, 1080);
    var input = default(BridgeInput);
    var rows = new System.Collections.Generic.List<HudBackpackRow>();
    var run = Stopwatch.StartNew();
    bool given = false, listed = false, selected = false, forced = false;
    double nextReport = 1;
    int maxMode = 0, sharePanels = 0;
    while (run.Elapsed.TotalSeconds < seconds)
    {
        double t = run.Elapsed.TotalSeconds;
        session.SetInput(input);
        session.Update();
        if (session.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            return 1;
        }

        while (hud.DrainMessages(new HudMessage[8]) > 0)
        {
        }

        if (!given && t > 1.2 && session.Session.Host is { } giveHost && session.Session.LocalController?.PlayerReplicationInfo is { } me)
        {
            given = true;
            int id = me.PlayerID;
            void Give(OpenScct.Server.ServerHost running)
            {
                running.BeforeWorldTick -= Give;
                Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1} GiveOnServer(player {id}, all mercs) -> {HudBackpackExports.GiveOnServer(running, id, true)} backpacks given");
            }

            giveHost.BeforeWorldTick += Give;
        }

        if (!listed && t > 2.5 && session.Session.Host is { } host)
        {
            listed = true;
            void List(OpenScct.Server.ServerHost running)
            {
                running.BeforeWorldTick -= List;
                foreach (OpenScct.Game.SBase.SBasePawn p in running.World.Level.AllActors<OpenScct.Game.SBase.SBasePawn>())
                {
                    var names = new System.Collections.Generic.List<string>();
                    for (OpenScct.Game.SBase.SGadget? g = p.MyGadget; g is not null; g = g.MyGadget)
                    {
                        names.Add($"{g.GetType().Name}({g.Charge})");
                    }

                    string who = (p.Controller?.PlayerReplicationInfo ?? p.PlayerReplicationInfo)?.PlayerName ?? "?";
                    Console.WriteLine($"   server pawn '{who}' type {p.SPawnType} health {p.Health}: {string.Join(" ", names)} | A '{p.aBackPackOwner_A?.PlayerReplicationInfo?.PlayerName}' " +
                        $"{p.aIDGad_Mate_A0}:{p.aNbChargesGad_Mate_A0} {p.aIDGad_Mate_A1}:{p.aNbChargesGad_Mate_A1} | B '{p.aBackPackOwner_B?.PlayerReplicationInfo?.PlayerName}' {p.aIDGad_Mate_B0}:{p.aNbChargesGad_Mate_B0} {p.aIDGad_Mate_B1}:{p.aNbChargesGad_Mate_B1}");
                }
            }

            host.BeforeWorldTick += List;
        }

        if (!selected && t > 3)
        {
            selected = true;
            Console.WriteLine($"t={t:F1} SelectBackpack -> {HudBackpackExports.Select(session.Session.LocalController)}");
        }

        if (!forced && t > 8 && session.Session.Host is { } h && session.Session.LocalController?.PlayerReplicationInfo is { } pri)
        {
            forced = true;
            Console.WriteLine($"t={t:F1} ForceRefill 5 s for player {pri.PlayerID}");
            HudBackpackExports.Queue(h, pri.PlayerID, 5f);
        }

        if (forced && t >= nextReport && session.Session.LocalController is { } lpc && lpc.SBPawn is { } lp)
        {
            // What the refill entry sees on the client: its focus, whether it can be used, the mate found and where
            // the nearest mate stands.
            var entry = lpc.IManager?.Interactions.Length > 0 ? lpc.IManager.Interactions[0] : null;
            string near = "";
            foreach (OpenScct.Game.SBase.SBasePawn p in lp.XLevel.AllActors<OpenScct.Game.SBase.SBasePawn>())
            {
                if (!ReferenceEquals(p, lp) && p.SPawnType == lp.SPawnType)
                {
                    near += $"'{p.PlayerReplicationInfo?.PlayerName}' {(p.Location - lp.Location).Size():F0} uu yaw {p.Rotation.Yaw & 0xFFFF} (mine {lp.Rotation.Yaw & 0xFFFF}) ";
                }
            }
            string charges = "";
            for (OpenScct.Game.SBase.SGadget? g = lp.MyGadget; g is not null; g = g.MyGadget)
            {
                charges += $"{g.GetType().Name}({g.Charge}) ";
            }

            Console.WriteLine($"   refill entry {entry?.GetType().Name} focus {(entry as OpenScct.Game.SBase.SInteractObject)?.bHasFocus} canUse {entry?.CanUse(lpc)} " +
                $"mate '{(entry as OpenScct.Game.SBase.SMateInteraction)?.Mate?.PlayerReplicationInfo?.PlayerName}' selected {lpc.IManager?.SelectedInteractions} of {lpc.IManager?.NbObjects} | {near} | {charges}");
        }

        if (forced && t < 13 && session.Session.LocalController is { } focusPc)
        {
            HudBackpackExports.FocusRefill(focusPc);
        }

        HudBackpackState s = HudBackpackExports.Read(session.Session.LocalController, hud, rows);
        maxMode = Math.Max(maxMode, s.Mode);
        sharePanels += s.Mode == 2 ? 1 : 0;
        if (t >= nextReport || (s.Mode == 2 && sharePanels == 1))
        {
            nextReport = Math.Floor(t) + 1;
            string text = string.Join(" ", rows.Select(r => $"[p{r.Panel} r{r.Row} id{r.GadgetId} {r.Charges}/{r.Max} {r.TextureName()} ({r.U0},{r.V0})-({r.U1},{r.V1})]"));
            Console.WriteLine($"t={t:F1} backpack mode {s.Mode} rows {s.Rows} panels {s.Panels} labels '{hud.Texts.Text(s.SelfLabelId)}'/'{hud.Texts.Text(s.MateLabelId)}' " +
                $"mates '{hud.Texts.Text(s.MateANameId)}' '{hud.Texts.Text(s.MateBNameId)}' | {s.RowCount} rows {text}");
        }

        Thread.Sleep(4);
    }

    Console.WriteLine($"backpack summary: highest mode {maxMode}, frames with the refill share {sharePanels}");
    return 0;
}

// The spy binoculars: raised at 2 s (the controller's SwitchBinocularVision, as the B key's binding calls it), the
// mode, zoom, lock texts and objective targets each second, the view turned onto the first target at 3 s, and the
// page's layers (goggle frame, sound meter, zoom scale, key help) for each zoom step and at three moments of the meter.
static unsafe int BinocularsRun(string content, string map, string mode, bool merc, double seconds)
{
    BridgeSession.Configure(content);
    HudMagmaPage? page = HudBinocularExports.SpyPage(out string? pageError);
    Console.WriteLine(page is null ? $"spy page: missing ({pageError})" : $"spy page {page.Name}: instances {string.Join(", ", page.Instances)}");
    foreach (int step in new[] { 3, 2, 1 })
    {
        var layers = HudBinocularExports.ReadSpyPage(0.5f, -1f, step, "Mouse Wheel Up / Mouse Wheel Down", "B", "");
        int images = layers.Count(l => !l.IsText);
        string texts = string.Join(" | ", layers.Where(l => l.IsText).Select(l => $"{l.Widget}='{l.Text}' at ({l.L:F0},{l.T:F0}) h {l.Height:F0}"));
        HudMagmaPage.Layer marker = layers.FirstOrDefault(l => l.Widget == "zoomInside");
        Console.WriteLine($"   step {step} ({HudBinocularExports.ZoomLabel(step)}): {layers.Count} layers, {images} images; zoomInside at ({marker.L:F0},{marker.T:F0}); texts {texts}");
    }

    foreach (float at in new[] { 0f, 1.0f, 1.6f, 2.9f })
    {
        var layers = HudBinocularExports.ReadSpyPage(at, -1f, 3, "", "", "");
        string meter = string.Join(" ", layers.Where(l => l.Widget.StartsWith("curseur", StringComparison.Ordinal) || l.Widget.StartsWith("soundAnimation", StringComparison.Ordinal))
            .Select(l => $"{l.Widget}:{l.T:F0}-{l.B:F0}/a{l.Alpha:F2}"));
        Console.WriteLine($"   meter at {at:F1} s: {meter}");
    }

    var speak = HudBinocularExports.ReadSpyPage(1f, 0.3f, 3, "", "", "Hack");
    Console.WriteLine("   speak box: " + string.Join(" | ", speak.Where(l => l.Widget is "_TextSpeakATT" or "Element4").Select(l => $"{l.Widget}='{l.Text}' at ({l.L:F0},{l.T:F0})")));

    // The merc's scope page: at rest, through the rings' clip of a step in, with the breath half used, the sights moved
    // to an aim point and held on the centre, a hit, the reticle message and the speak and alert boxes.
    HudMagmaPage? scope = HudBinocularExports.MercPage(out string? scopeError);
    Console.WriteLine(scope is null ? $"scope page: missing ({scopeError})" : $"scope page {scope.Name}: instances {string.Join(", ", scope.Instances)}");
    if (scope is not null)
    {
        string Box(HudMagmaPage.Layer l) => $"{l.Widget}[{l.Mask}] ({l.L:F0},{l.T:F0})-({l.R:F0},{l.B:F0}) r{l.Rotation:F0} a{l.Alpha:F2}";
        var rest = HudBinocularExports.ReadMercPage(3f, 2f, 2, 3, 320f, 240f, false, false, 5f, 1234f, 0.01f, -1f, -1f, "Mouse Wheel Up / Mouse Wheel Down", "B", "Right Mouse Button", "", "");
        Console.WriteLine($"   at rest: {rest.Count} layers, {rest.Count(l => !l.IsText)} images; texts " +
            string.Join(" | ", rest.Where(l => l.IsText).Select(l => $"{l.Widget}='{l.Text}' at ({l.L:F0},{l.T:F0}) h {l.Height:F0}")));
        Console.WriteLine("   at rest, pictures: " + string.Join(" ", rest.Where(l => !l.IsText).Select(l => l.Widget + "@" + l.Texture).Distinct()));
        foreach ((int clip, float at) in new[] { (1, 0f), (1, 0.1f), (1, 0.3f), (1, 0.95f), (2, 0.1f), (2, 0.2f) })
        {
            var layers = HudBinocularExports.ReadMercPage(3f, at, clip, 2, 320f, 240f, false, false, 5f, 0f, 0.01f, -1f, -1f, "", "", "", "", "");
            Console.WriteLine($"   rings' clip {clip} at {at:F2} s: " + string.Join(" ", layers.Where(l => l.Widget is "border22" or "green_back" or "border").Select(Box)));
        }

        foreach (float breath in new[] { 5f, 2.5f, 0f })
        {
            var layers = HudBinocularExports.ReadMercPage(3f, -1f, 0, 3, 320f, 240f, false, false, breath, 0f, 0.01f, -1f, -1f, "", "", "", "", "");
            Console.WriteLine($"   breath {breath:F1} s: " + string.Join(" ", layers.Where(l => l.Mask != 0).Select(Box)));
        }

        var moved = HudBinocularExports.ReadMercPage(3f, -1f, 0, 3, 300.7f, 250.2f, false, true, 5f, 0f, 0.01f, 0.2f, 0.5f, "", "", "", "Hack", "Reloading");
        Console.WriteLine("   aim (300.7, 250.2), a hit, speak and alert: " + string.Join(" ", moved.Where(l => l.Widget is "sights" or "verticalAxis" or "horizontalAxis" or "sight3").Select(l => Box(l) + $" rgb {l.Red:F2},{l.Green:F2},{l.Blue:F2}")) +
            " | " + string.Join(" | ", moved.Where(l => l.IsText && l.Widget is "messageReticule" or "_TextSpeakDEF" or "messageAlert").Select(l => $"{l.Widget}='{l.Text}' at ({l.L:F0},{l.T:F0})")) +
            $" | {moved.Count} layers");
        var held = HudBinocularExports.ReadMercPage(3f, -1f, 0, 3, 300.7f, 250.2f, true, false, 5f, 0f, 0.01f, -1f, -1f, "", "", "", "", "");
        Console.WriteLine("   breath held: " + string.Join(" ", held.Where(l => l.Widget is "sights" or "verticalAxis" or "horizontalAxis").Select(Box)));
    }

    using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 3);
    HudFeature hud = HudFeature.Of(session);
    hud.SetViewport(1920, 1080);
    var input = default(BridgeInput);
    var targets = new HudObjective[16];
    var run = Stopwatch.StartNew();
    bool raised = false, aimed = false;
    double nextReport = 1;
    int maxTargets = 0;
    while (run.Elapsed.TotalSeconds < seconds)
    {
        double t = run.Elapsed.TotalSeconds;
        // The Use button from 5 to 5.3 s marks the target under the reticle (and again from 8 s, which unmarks it).
        const int Use = 10;
        input.Buttons[Use] = (byte)(t is > 5 and < 5.3 or > 8 and < 8.3 ? 1 : 0);
        session.SetInput(input);
        session.Update();
        if (session.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            return 1;
        }

        while (hud.DrainMessages(new HudMessage[8]) > 0)
        {
        }

        if (!raised && t > 2 && session.Session.LocalController is { } pc)
        {
            raised = true;
            pc.eventSwitchBinocularVision();
            Console.WriteLine($"t={t:F1} SwitchBinocularVision -> state {pc.GetStateName()}");
        }

        hud.GetState(out HudState s);
        int count = hud.Frame.BinocularTargets.CopyTo(targets);
        maxTargets = Math.Max(maxTargets, count);
        if (!aimed && t > 3 && count > 0)
        {
            aimed = true;
            HudObjective first = targets.Take(Math.Min(count, targets.Length))
                .MinBy(o => (o.X - s.X) * (o.X - s.X) + (o.Y - s.Y) * (o.Y - s.Y) + (o.Z - s.Z) * (o.Z - s.Z));
            Console.WriteLine($"t={t:F1} AimAt nearest '{hud.Texts.Text(first.NameId)}' -> {HudBinocularExports.Aim(session.Session.LocalController, new OpenScct.Core.FVector(first.X, first.Y, first.Z))}" +
                $", under the reticle now: '{OpenScct.Game.Equipment.SpyBinoculars.ObjectiveUnderReticle(session.Session.LocalController!)?.TriggerName}'");
        }

        if (t >= nextReport)
        {
            nextReport += 1;
            string rows = string.Join(" | ", targets.Take(Math.Min(count, targets.Length)).Select(o =>
                $"'{hud.Texts.Text(o.NameId)}' kind {o.Kind} flags {o.Flags} {o.Percent}% at {MathF.Sqrt((o.X - s.X) * (o.X - s.X) + (o.Y - s.Y) * (o.Y - s.Y) + (o.Z - s.Z) * (o.Z - s.Z)):F0} uu"));
            string marked = string.Join(", ", targets.Take(Math.Min(count, targets.Length)).Where(o => (o.Flags & HudObjectiveFlags.Marked) != 0)
                .Select(o => hud.Texts.Text(o.NameId)));
            Console.WriteLine($"t={t:F1} marked '{marked}' (SHud.LockedObjective '{(session.Session.LocalController?.myHUD as OpenScct.Game.SBase.SHud)?.LockedObjective?.TriggerName}')");
            Console.WriteLine($"t={t:F1} mode {s.BinocularMode} fov {s.BinocularFov:F0} yaw {s.Yaw:F0} ER {((s.Flags & HudFlags.EnhancedReality) != 0 ? 1 : 0)} | lock '{hud.Texts.Text(s.LockTextId)}' locked '{hud.Texts.Text(s.LockedTextId)}' " +
                $"drop zone '{hud.Texts.Text(s.DropZoneTextId)}' | {count} targets: {rows}");
        }

        Thread.Sleep(4);
    }

    Console.WriteLine($"binocular summary: most targets {maxTargets}");
    return 0;
}

// The merc's motion tracking: the first vision mode switched on at 1.5 s (SwitchHeadset 1, then 2 if that was not
// motion), and each second the goggles' locks with the actors' speeds, then the draw built for a 1920 x 1080 view from the
// merc's eye turned onto the first lock (so a moving pawn is in front of him), with its rows by kind and the boxes.
static unsafe int MotionRun(string content, string map, string mode, bool merc, double seconds)
{
    BridgeSession.Configure(content);
    using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 3);
    HudFeature hud = HudFeature.Of(session);
    hud.SetViewport(1920, 1080);
    var input = default(BridgeInput);
    var rows = new HudMotionRow[256];
    var run = Stopwatch.StartNew();
    double nextReport = 1;
    int switched = 0, summoned = 0, maxLocks = 0, maxDrawn = 0, framesWithRows = 0, cornersInFront = 0, maxFriends = 0, friendMarks = 0, holdFire = 0;
    bool laserOn = false;
    var lockedIds = new System.Collections.Generic.HashSet<int>();
    while (run.Elapsed.TotalSeconds < seconds)
    {
        double t = run.Elapsed.TotalSeconds;
        session.SetInput(input);
        session.Update();
        if (session.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            return 1;
        }

        while (hud.DrainMessages(new HudMessage[8]) > 0)
        {
        }

        hud.GetState(out HudState s);
        if ((switched == 0 && t > 1.5) || (switched == 1 && t > 2.5 && s.VisionMode != 37))
        {
            switched++;
            session.Command("SwitchHeadset", switched);
            Console.WriteLine($"t={t:F1} SwitchHeadset {switched} (vision now {s.VisionMode})");
        }

        // An enemy, then a teammate put in front of the merc on the server (the bots start across the map), then the
        // laser gadget on, so its target (the teammate in front) gets the HUD tracker's friend marks.
        if (summoned < 2 && t > 3 + summoned * 0.4 && session.Session.Host is { } host && session.Session.LocalController?.PlayerReplicationInfo is { } pri)
        {
            bool mate = summoned == 1;
            int id = pri.PlayerID;
            summoned++;
            void Run(OpenScct.Server.ServerHost running)
            {
                running.BeforeWorldTick -= Run;
                Console.WriteLine($"   summon {(mate ? "teammate" : "enemy")} -> server actor {HudMotionExports.SummonOnServer(running, id, mate)}");
            }

            host.BeforeWorldTick += Run;
        }

        if (!laserOn && t > 4.5)
        {
            laserOn = true;
            session.Command("GadLaserSwitch", 0);
            Console.WriteLine($"t={t:F1} GadLaserSwitch");
        }

        var pc = session.Session.LocalController;

        OpenScct.Game.SBase.SMotionTrackingVision? tracker = HudMotionDraw.TrackerOf(pc);
        var view = new HudMotionView { Width = 1920, Height = 1080, FocalX = 960f / MathF.Tan(45f * MathF.PI / 180f), CentreX = 960, CentreY = 540, Page = 1 };
        view.FocalY = view.FocalX;
        if (pc?.Pawn is { } pawn)
        {
            view.X = pawn.Location.X;
            view.Y = pawn.Location.Y;
            view.Z = pawn.Location.Z + pawn.BaseEyeHeight;
            view.Yaw = pc.Rotation.Yaw * 360f / 65536f;
            view.Pitch = pc.Rotation.Pitch * 360f / 65536f;
            // From 4.5 s to 9 s the merc aims at his nearest teammate (the laser's target, for the friend marks) and the
            // view is his own; otherwise the view turns onto the first lock.
            OpenScct.Game.SBase.SBasePawn? mate = null;
            if (t is > 4.5 and < 9 && pc.XLevel is { } lv)
            {
                foreach (OpenScct.Game.SBase.SBasePawn p in lv.AllActors<OpenScct.Game.SBase.SBasePawn>())
                {
                    if (!p.bDeleteMe && !ReferenceEquals(p, pawn) && p is OpenScct.Game.SBase.SPAWNDEFENSE &&
                        (mate is null || (p.Location - pawn.Location).SizeSquared() < (mate.Location - pawn.Location).SizeSquared()))
                    {
                        mate = p;
                    }
                }
            }

            if (mate is not null)
            {
                HudBinocularExports.Aim(pc, mate.Location);
                view.Yaw = pc.Rotation.Yaw * 360f / 65536f;
                view.Pitch = pc.Rotation.Pitch * 360f / 65536f;
            }
            else if (tracker is { LockedObjects.Count: > 0 } && tracker.LockedObjects[0].Actor is { } first)
            {
                float dx = first.Location.X - view.X, dy = first.Location.Y - view.Y, dz = first.Location.Z - view.Z;
                view.Yaw = MathF.Atan2(dy, dx) * 180f / MathF.PI;
                view.Pitch = MathF.Atan2(dz, MathF.Sqrt(dx * dx + dy * dy)) * 180f / MathF.PI;
            }
        }

        int n = HudMotionDraw.Shared.Build(pc, view, hud.TargetNameRan, out HudMotionState ms, rows);
        maxLocks = Math.Max(maxLocks, ms.Locks);
        maxDrawn = Math.Max(maxDrawn, ms.Drawn);
        maxFriends = Math.Max(maxFriends, ms.Friends);
        framesWithRows += n > 0 ? 1 : 0;
        foreach (HudMotionLock l in HudMotionDraw.Shared.Locks)
        {
            lockedIds.Add(l.ActorId);
        }

        for (int i = 0; i < n; i++)
        {
            cornersInFront += rows[i].Kind == 0 ? 1 : 0;
            friendMarks += rows[i].Kind == 2 ? 1 : 0;
            HudMotionRow copy = rows[i];
            holdFire += copy.Kind == 3 && new string(copy.Text).Contains("Hold", StringComparison.OrdinalIgnoreCase) ? 1 : 0;
        }

        if (t >= nextReport)
        {
            nextReport += 1;
            int[] kinds = new int[4];
            for (int i = 0; i < n; i++)
            {
                kinds[Math.Clamp(rows[i].Kind, 0, 3)]++;
            }

            string locks = string.Join(" ", HudMotionDraw.Shared.Locks.Select(l =>
                $"[#{l.ActorId} side {l.Side} friend {l.Friend} laser {l.Laser} front {l.InFront} removing {l.Removing} speed {l.Speed:F0} box ({l.X1:F0},{l.Y1:F0})-({l.X2:F0},{l.Y2:F0})]"));
            string movers = "";
            if (pc?.XLevel is { } level && pc.Pawn is { } own)
            {
                foreach (OpenScct.Game.SBase.SBasePawn p in level.AllActors<OpenScct.Game.SBase.SBasePawn>())
                {
                    if (!p.bDeleteMe && !ReferenceEquals(p, own))
                    {
                        movers += $"#{p.ActorId}:{(p is OpenScct.Game.SBase.SPAWNDEFENSE ? "merc" : "spy")} v{p.Velocity.Size():F0} d{(p.Location - own.Location).Size():F0} ";
                    }
                }
            }
            Console.WriteLine($"t={t:F1} vision {s.VisionMode} tracking {ms.Tracking} locks {ms.Locks} laser {ms.LaserLocks} drawn {ms.Drawn} friends {ms.Friends} | rows {ms.Rows}/{ms.TotalRows} " +
                $"(corners {kinds[0]}, sweeps {kinds[1]}, friend marks {kinds[2]}, texts {kinds[3]}) name {ms.NameRow} scale {ms.FrameScale:F2} | {locks} | pawns {movers}");
            for (int i = 0; i < Math.Min(n, 6); i++)
            {
                HudMotionRow r = rows[i];
                Console.WriteLine($"   row {i}: kind {r.Kind} at ({r.X:F0},{r.Y:F0}) size ({r.XL:F0},{r.YL:F0}) texels ({r.U},{r.V},{r.UL},{r.VL}) colour {r.R},{r.G},{r.B},{r.A}" +
                    (r.Kind == 3 ? $" text '{new string(r.Text)}'" : ""));
            }
        }

        Thread.Sleep(4);
    }

    Console.WriteLine($"motion summary: most locks {maxLocks}, most drawn {maxDrawn}, most friends {maxFriends}, frames with rows {framesWithRows}, corner rows {cornersInFront}, friend mark rows {friendMarks}, Hold Fire rows {holdFire}, actors locked [{string.Join(",", lockedIds)}]");
    return 0;
}

// The team ping through a local match: once a ping can be placed, a tap (generic) aimed through the session's view and,
// after the server's one-second limit, a "check here" and a spy-only kind (refused for a merc); every second the state
// and the team's pings as Unreal reads them.
static int PingRun(string content, string map, string mode, bool merc, double seconds)
{
    BridgeSession.Configure(content);
    using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 3);
    HudFeature hud = HudFeature.Of(session);
    TeamPingFeature pings = TeamPingFeature.Of(session);
    var input = default(BridgeInput);
    var rows = new TeamPingRow[8];
    var run = Stopwatch.StartNew();
    double nextReport = 1, firstSendAt = -1;
    int step = 0, lastContext = 0, mostRows = 0, ownRows = 0;
    while (run.Elapsed.TotalSeconds < seconds)
    {
        double t = run.Elapsed.TotalSeconds;
        session.SetInput(input);
        session.Update();
        if (session.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            return 1;
        }

        while (hud.DrainMessages(new HudMessage[8]) > 0)
        {
        }

        TeamPingState state = pings.State();
        if (state.Context != lastContext)
        {
            Console.WriteLine($"t={t:F1} context {lastContext} -> {state.Context} team {state.Team} supported {state.Supported}");
            lastContext = state.Context;
        }

        var view = session.Session.View;
        var camera = new TeamPingCamera
        {
            X = view.Location.X, Y = view.Location.Y, Z = view.Location.Z,
            Pitch = view.Rotation.Pitch * 360f / 65536f, Yaw = view.Rotation.Yaw * 360f / 65536f, Roll = view.Rotation.Roll * 360f / 65536f,
        };
        (OpenScct.Server.TeamPingKind Kind, double Delay)[] plan =
            [(OpenScct.Server.TeamPingKind.Generic, 0), (OpenScct.Server.TeamPingKind.Check, 1.3), (OpenScct.Server.TeamPingKind.Mine, 2.6)];
        if (state.Context != 0 && step < plan.Length && t > 3)
        {
            firstSendAt = firstSendAt < 0 ? t : firstSendAt;
            if (t >= firstSendAt + plan[step].Delay)
            {
                int result = pings.Send(plan[step].Kind, camera, out OpenScct.Core.FVector target);
                Console.WriteLine($"t={t:F1} send {plan[step].Kind} from ({camera.X:F0},{camera.Y:F0},{camera.Z:F0}) yaw {camera.Yaw:F0} pitch {camera.Pitch:F0} -> {result} at ({target.X:F0},{target.Y:F0},{target.Z:F0})");
                step++;
            }
        }

        int count = pings.Rows(rows);
        mostRows = Math.Max(mostRows, count);
        for (int i = 0; i < count; i++)
        {
            ownRows += rows[i].Own;
        }

        if (t >= nextReport)
        {
            nextReport += 1;
            string list = string.Join(" ", rows.Take(count).Select(r =>
                $"[{(OpenScct.Server.TeamPingKind)r.Kind} team {r.Team} from #{r.SenderId} '{hud.Texts.Text(r.SenderNameId)}'{(r.Own != 0 ? " own" : "")} at ({r.X:F0},{r.Y:F0},{r.Z:F0}) age {r.Age:F2} opacity {r.Opacity:F2}]"));
            Console.WriteLine($"t={t:F1} ping state: context {state.Context} team {state.Team} supported {state.Supported} sent {state.Sent} received {state.Received} refused {state.Refused} | {count} rows {list}");
        }

        Thread.Sleep(5);
    }

    TeamPingState end = pings.State();
    Console.WriteLine($"ping summary: sent {end.Sent} received {end.Received} refused {end.Refused}, most rows at once {mostRows}, frames with an own row {ownRows}");
    return end.Received > 0 ? 0 : 2;
}

static unsafe string Pairs(HudRound r) =>
    $"score {r.TeamScore[0]}:{r.TeamScore[1]}, team lives {r.TeamLives[0]}:{r.TeamLives[1]}, team size {r.TeamSize[0]}:{r.TeamSize[1]}";

static unsafe string Describe(HudFeature hud, HudMessage m)
{
    string text = new(m.Text);
    string who = m.Category == HudMessageCategory.Activity
        ? $" kind {(HudActivityKind)m.Kind} cause {(HudActivityCause)m.Cause} actor '{hud.Texts.Text(m.ActorNameId)}' (team {m.ActorTeam}) victim '{hud.Texts.Text(m.VictimNameId)}' (team {m.VictimTeam}) flags {m.Flags}"
        : m.ActorNameId != 0 ? $" from '{hud.Texts.Text(m.ActorNameId)}'" : "";
    return $"t={m.Time:F1} category {m.Category} switch {m.Switch} for {m.Duration:F0} s prio {m.Priority}{who}: \"{text}\"";
}
