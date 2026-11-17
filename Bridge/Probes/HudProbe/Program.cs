using System;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Hud;

// HudProbe <content root> [map] [spy|merc] [seconds] [mode]
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

BridgeSession.Configure(content);
var load = Stopwatch.StartNew();
using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 3);
// As Unreal must: the HUD is asked for before the first Update, so the first messages are received.
HudFeature hud = HudFeature.Of(session);
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
bool fired = false;
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

    ticks += session.Update();
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
            Console.WriteLine($"   score: #{p.PlayerId} '{hud.Texts.Text(p.NameId)}' team {p.Team} flags {p.Flags} obj/kills {p.ObjOrKills} k {p.Kills} d {p.Deaths} score {p.Score} lives {p.Lives} ping {p.Ping}");
        }
    }

    Thread.Sleep(4);
}

Console.WriteLine($"done: {ticks} ticks, {frames} frames, {drained} messages drained of {hud.Messages.Total} queued, {hud.Texts.Count} texts, {hud.Received} HUD calls received");
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
