using System;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.Game.Combat;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Hud;

// HudProbe <content root> [map] [spy|merc] [seconds] [mode] [api]
// With "api" as the sixth argument the whole run goes through the two function tables exactly as Unreal calls them
// (Exports.GetApi and HudExports.GetApi, unmanaged function pointers, raw buffers), instead of the managed methods.
string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
bool merc = args.Length > 2 && args[2].Equals("merc", StringComparison.OrdinalIgnoreCase);
double seconds = args.Length > 3 ? double.Parse(args[3], CultureInfo.InvariantCulture) : 14;
string mode = args.Length > 4 ? args[4] : "Story";

// The struct sizes ScctHudApi.h asserts.
Console.WriteLine($"sizes: HudState {Unsafe.SizeOf<HudState>()}, HudRound {Unsafe.SizeOf<HudRound>()}, HudGadget {Unsafe.SizeOf<HudGadget>()}, " +
    $"HudInteraction {Unsafe.SizeOf<HudInteraction>()}, HudMenuLine {Unsafe.SizeOf<HudMenuLine>()}, HudObjective {Unsafe.SizeOf<HudObjective>()}, " +
    $"HudObjectiveLine {Unsafe.SizeOf<HudObjectiveLine>()}, HudBlip {Unsafe.SizeOf<HudBlip>()}, HudShownMessage {Unsafe.SizeOf<HudShownMessage>()}, " +
    $"HudScoreRow {Unsafe.SizeOf<HudScoreRow>()}, HudMessage {Unsafe.SizeOf<HudMessage>()}, HudApi {Unsafe.SizeOf<HudApi>()}");

if (args.Length > 5 && args[5].Equals("api", StringComparison.OrdinalIgnoreCase))
{
    return ApiRun(content, map, mode, merc, seconds);
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
    int bridgeVersion = ((delegate* unmanaged<BridgeApi*, int>)&Exports.GetApi)(&bridgeTable);
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
