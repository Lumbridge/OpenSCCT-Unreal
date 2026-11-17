using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Controls;
using OpenScctGame.Play.Session;

// The console check of the controls: a map with bots, scripted device state played through the input mapper as
// Unreal would send it (one BridgeRawInput a frame), and what the simulation got printed with a verdict per step.
// Arguments: content root, map, scratch folder for the saved bindings and settings.

string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
string scratch = args.Length > 2 ? args[2] : Path.Combine(Path.GetTempPath(), "ControlsProbe");

int failures = 0;
void Check(bool ok, string what)
{
    Console.WriteLine($"   {(ok ? "ok    " : "FAILED")} {what}");
    failures += ok ? 0 : 1;
}

// ---- The default tables, and that they survive a save and a load -------------------------------------------------

var defaults = new ControlBindings();
foreach (bool merc in new[] { false, true })
{
    SideBindings side = defaults.Of(merc);
    Console.WriteLine($"Default keys, {(merc ? "merc" : "spy")} ({side.Keys.Bindings.Count} bound):");
    foreach ((string key, string command) in side.Keys.Bindings)
    {
        Console.WriteLine($"   {key,-15} {command}");
    }

    Console.WriteLine($"Default pad, {(merc ? "merc" : "spy")}:");
    foreach (string control in ControlBindings.PadControls)
    {
        Console.WriteLine($"   {control,-15} {(side.Pad[control].Length == 0 ? "(none)" : side.Pad[control])}");
    }
}

Console.WriteLine("Files:");
Directory.CreateDirectory(scratch);
string bindingsPath = Path.Combine(scratch, "bindings.json"), settingsPath = Path.Combine(scratch, "controls.json");
var edited = new ControlBindings();
edited.Spy.Keys.Bind("Ctrl+F", "SwitchHeadset 1");
edited.Spy.Keys.Bind("T", "");
edited.Merc.SetPad("P1", "Action:reload");
edited.Save(bindingsPath);
ControlBindings loaded = ControlBindings.Load(bindingsPath);
Check(loaded.ToJson() == edited.ToJson() && loaded.Spy.Keys.Bindings["Ctrl+F"] == "SwitchHeadset 1" &&
    !loaded.Spy.Keys.Bindings.ContainsKey("T") && loaded.Merc.Pad["P1"] == "Action:reload" &&
    loaded.Merc.Keys.SameAs(defaults.Merc.Keys),
    $"bindings saved and loaded the same ({new FileInfo(bindingsPath).Length} bytes): a chord added, a key unbound, a paddle bound");
var tuned = new ControlSettings { InvertY = true, MercFovCap = 100f };
tuned.SpyKbm.FreeLook = 2f;
tuned.MercKbm.ScopeBehavior = "Hold";
tuned.Save(settingsPath);
ControlSettings reread = ControlSettings.Load(settingsPath);
BridgeControlSettings flat = ControlsMarshal.ToBridge(reread);
ControlSettings back = ControlsMarshal.FromBridge(in flat, reread);
Check(reread.ToJson() == tuned.ToJson() && back.ToJson() == tuned.ToJson() && flat.SpyKbm.FreeLook == 2f && flat.MercKbm.ScopeHold == 1,
    "settings saved, loaded and passed through the C struct the same");
var stock = new ControlSettings();
Check(stock.SpyKbm.FreeLook == 1.4f && stock.SpyKbm.Look == 1f && stock.MercKbm.Look == 1f && stock.MercKbm.ScopeVertical == 2.45f &&
    stock.SpyKbm.Scope == 0.75f && !stock.InvertY && stock.Widescreen && stock.MercFovCap == 112f && MouseLook.UnitsPerCount == 3f,
    $"original defaults: spy free look 1.4, gun look 1, merc look 1, scope 0.75, scope vertical 2.45, no invert, widescreen with a 112 degree cap; a count turns {MouseLook.UnitsPerCount} x multiplier rotation units");
(float h169, float v169) = ViewFov.Screen(90f, 16f / 9f, stock, capped: true);
(float h43, float v43) = ViewFov.Screen(90f, 4f / 3f, stock, capped: true);
Check(MathF.Abs(v169 - v43) < 0.01f && MathF.Abs(h43 - 90f) < 0.01f && h169 > 100f,
    $"FOV 90 (4:3) is {h43:F1} x {v43:F1} on 4:3 and {h169:F1} x {v169:F1} on 16:9 (same height, more width)");

// ---- A spy on the map ------------------------------------------------------------------------------------------

BridgeSession.Configure(content);
ControlsStore.Settings = new ControlSettings();
ControlsStore.Bindings = new ControlBindings();
var load = Stopwatch.StartNew();
var probe = new Driver(BridgeSession.StartLocal(map, "Story", merc: false, bots: 3));
Console.WriteLine($"{map} as spy, loaded in {load.Elapsed.TotalSeconds:F1} s");
if (!probe.WaitForPawn())
{
    Console.WriteLine("FAILED: no local pawn after 8 s" + (probe.Bridge.Failure is { } f ? ": " + f : ""));
    return 1;
}

LocalInput input = probe.Bridge.Session.Input;

Console.WriteLine("Keyboard:");
probe.Run(0.3);
var before = probe.Bridge.Session.LocalPawn!.Location;
probe.Held.Add(BridgeKey.W);
probe.Run(1.2);
var after = probe.Bridge.Session.LocalPawn!.Location;
float walked = (after - before).Size();
Check(input.Get("BT_Up") == 1 && walked > 100f, $"W held: BT_Up {input.Get("BT_Up")}, the pawn ran {walked:F0} units in 1.2 s");
probe.Held.Remove(BridgeKey.W);
probe.Run(0.15);
Check(input.Get("BT_Up") == 0, "W released: BT_Up 0");

probe.ResetLog();
probe.Taps.Add(BridgeKey.C);
probe.Run(0.1);
Check(probe.ButtonTicks("bCrouch") == 1, $"C down and up inside one frame: bCrouch held for exactly {probe.ButtonTicks("bCrouch")} tick");

probe.ResetLog();
probe.Held.Add(BridgeKey.Space);
probe.Run(0.25);
Check(probe.Saw("Jump") == 1 && probe.MaxUp == 280f, $"Space held: Jump ran {probe.Saw("Jump")} time, aUp {probe.MaxUp}");
probe.Held.Remove(BridgeKey.Space);
probe.Run(0.8);
Check(input.Up == 0f, "Space released: aUp 0");

probe.ResetLog();
probe.Taps.Add(BridgeKey.MiddleMouse);
probe.Run(0.1);
bool walking = !probe.Bridge.Session.LocalController!.bFastSpeed;
probe.Taps.Add(BridgeKey.MiddleMouse);
probe.Run(0.1);
// The press and the release each report the walk state, as PlayInput does: "SetWalk 1" twice, then "SetWalk" twice.
Check(walking && probe.Bridge.Session.LocalController!.bFastSpeed && probe.Log.FirstOrDefault() == "SetWalk 1" && probe.Log.LastOrDefault() == "SetWalk",
    "middle mouse toggles walk: the first press walks (SetWalk 1), the second runs again (SetWalk 0): " + string.Join(", ", probe.Log));

probe.ResetLog();
probe.Wheel = 1;
probe.Run(0.1);
probe.Wheel = -2;
probe.Run(0.1);
Check(probe.Saw("NextGadget") == 1 && probe.Saw("PreviousGadget") == 2, $"wheel: one notch up NextGadget x{probe.Saw("NextGadget")}, two down PreviousGadget x{probe.Saw("PreviousGadget")}");

probe.ResetLog();
probe.Taps.Add(BridgeKey.LeftMouse);
probe.Run(0.1);
Check(probe.Saw("Fire") == 1 && probe.Saw("GadgetQuickChoice") == 1 && probe.ButtonTicks("bFire") >= 1,
    "left mouse: Button bFire, Function Fire and GadgetQuickChoice 0, in the binding's order: " + string.Join(", ", probe.Log.Where(l => !l.StartsWith('#'))));

probe.ResetLog();
probe.Taps.Add(BridgeKey.X);
probe.Run(0.3);
bool first = probe.View.SpyFirstPerson == 1;
probe.Taps.Add(BridgeKey.X);
probe.Run(0.3);
Check(first && probe.View.SpyFirstPerson == 0, "X toggles the spy's first person (SetSpyFirstPerson 1, then 0); the view reported it");

probe.ResetLog();
probe.Taps.Add(BridgeKey.D1);
probe.Taps.Add(BridgeKey.F1);
probe.Run(0.1);
Check(probe.Saw("SelectGadget") == 1 && probe.Saw("SwitchHeadset 1") == 1, "1 selects gadget slot 0, F1 switches the first vision");

Console.WriteLine("Interface commands:");
List<LocalCommand> local = probe.Controls.Keys.LocalCommands;
local.Clear();
probe.Taps.Add(BridgeKey.Escape);
probe.Taps.Add(BridgeKey.F8);
probe.Run(0.1);
Check(Local("ShowMenu", true) && Local("DisplaySessionStatus", true) && Local("ToggleVoiceMute", true) && probe.Bridge.Session.Log.Count == 0,
    "Escape and F8 raise ShowMenu, DisplaySessionStatus and ToggleVoiceMute for Unreal, and nothing unknown reaches the pawn: " + Describe());
local.Clear();
probe.Held.Add(BridgeKey.CapsLock);
probe.Run(0.2);
bool talking = Local("PushToTalk", true) && !Local("PushToTalk", false);
probe.Held.Remove(BridgeKey.CapsLock);
probe.Run(0.1);
Check(talking && Local("PushToTalk", false), "Caps Lock holds PushToTalk: a press, and the release when it is let go");
local.Clear();
probe.Taps.Add(BridgeKey.H);
probe.Run(0.1);
bool single = !Local("ToggleAutoHack", true);
probe.Taps.Add(BridgeKey.H);
probe.Run(0.1);
Check(single && Local("ToggleAutoHack", true) && ControlsStore.Settings.AutoHackComms && probe.Bridge.Session.AutoHackComms,
    "H twice within 300 ms toggles auto-hack (one H does not): the setting and the session have it on");

Console.WriteLine("Hold or toggle, chords:");
probe.Held.Add(BridgeKey.K);
probe.Run(0.2);
Check(input.Get("bCloak") == 1, "K holds bCloak");
probe.Held.Remove(BridgeKey.K);
probe.Run(0.1);
ControlsStore.Bindings.Spy.Keys.Bind("Ctrl+F", "SwitchHeadset 2");
probe.ResetLog();
probe.Taps.Add(BridgeKey.F);
probe.Run(0.1);
int plain = probe.ButtonTicks("bROF");
probe.Held.Add(BridgeKey.Ctrl);
probe.Run(0.05);
probe.Taps.Add(BridgeKey.F);
probe.Run(0.1);
probe.Held.Remove(BridgeKey.Ctrl);
probe.Run(0.05);
Check(plain == 1 && probe.ButtonTicks("bROF") == 1 && probe.Saw("SwitchHeadset 2") == 1,
    "a chord bound while playing: F alone is bROF, Ctrl+F is SwitchHeadset 2 and not bROF");
ControlsStore.Bindings.Spy.Keys.Bind("Ctrl+F", "");

Console.WriteLine("Losing the input:");
probe.Held.Add(BridgeKey.W);
probe.Held.Add(BridgeKey.C);
probe.Run(0.2);
bool heldBefore = input.Get("BT_Up") == 1 && input.Get("bCrouch") == 1;
probe.Play = false;
probe.Run(0.2);
bool cleared = input.Get("BT_Up") == 0 && input.Get("bCrouch") == 0;
probe.Play = true;
probe.Run(0.2);
bool blocked = input.Get("BT_Up") == 0 && input.Get("bCrouch") == 0;
probe.Held.Remove(BridgeKey.W);
probe.Run(0.05);
probe.Held.Add(BridgeKey.W);
probe.Run(0.2);
Check(heldBefore && cleared && blocked && input.Get("BT_Up") == 1 && input.Get("bCrouch") == 0,
    "play flag off releases W and C; back on, they stay up while still held; W pressed again goes down, C (never let go) stays up");
probe.Held.Clear();
probe.Run(0.2);

// ---- The view with the mouse turn drawn ahead ------------------------------------------------------------------

Console.WriteLine("Mouse look and the look-ahead view:");
probe.Run(0.3);
foreach ((string name, bool firstPerson) in new[] { ("third person (following camera)", false), ("first person", true) })
{
    if (firstPerson)
    {
        probe.Taps.Add(BridgeKey.X);
        probe.Run(0.4);
    }

    float yaw0 = probe.Bridge.Session.LocalController!.Rotation.Yaw;
    probe.MouseX = 6f;
    probe.Run(0.25); // the look-ahead proves itself on the first ticks that take counts
    probe.Steps.Clear();
    probe.Run(1.0);
    probe.MouseX = 0f;
    float countsUnits = MouseLook.UnitsPerCount * probe.Bridge.Session.Mouse.SpyFreeLook * 6f * 360f / 65536f;
    float[] look = Signed(probe.Steps.Select(s => s.Look)), plainSteps = Signed(probe.Steps.Select(s => s.Plain));
    int still = plainSteps.Count(s => MathF.Abs(s) < 1e-4f), lookStill = look.Count(s => MathF.Abs(s) < countsUnits * 0.5f);
    Console.WriteLine($"   {name}: 6 counts a frame for 1 s over {look.Length} frames, each worth {countsUnits:F3} deg");
    Console.WriteLine($"      look-ahead view: yaw step per frame min {look.Min():F3} max {look.Max():F3} mean {look.Average():F3} deg, {lookStill} frames lagging");
    Console.WriteLine($"      plain view     : yaw step per frame min {plainSteps.Min():F3} max {plainSteps.Max():F3} mean {plainSteps.Average():F3} deg");
    Check(probe.Controls.LookAheadActive && probe.View.LookAhead == 1, "the simulation turned exactly as predicted, so the look-ahead is on");
    Check(look.Min() > countsUnits * 0.5f && look.Max() < countsUnits * 1.5f,
        "every frame's counts are in that frame's view: the turn per frame stays within half a step of the counts' worth");
    probe.Run(0.3);
    float turned = FRotatorDegrees(probe.Bridge.Session.LocalController!.Rotation.Yaw - (int)yaw0);
    BridgeView plainView = probe.Bridge.GetView();
    Check(MathF.Abs(Angle(probe.View.Yaw - plainView.Yaw)) < 0.02f && MathF.Abs(Angle(probe.View.AimYaw - probe.View.Yaw)) < 0.02f,
        $"mouse still: the look-ahead view rests on the simulated one (controller turned {turned:F1} deg in all), no snap back");
}

probe.MouseY = 4f;
probe.Run(0.5);
probe.MouseY = 0f;
probe.Run(0.2);
float up = probe.View.Pitch;
ControlSettings inverted = ControlSettings.FromJson(ControlsStore.Settings.ToJson());
inverted.InvertY = true;
ControlsStore.Settings = inverted;
probe.MouseY = 4f;
probe.Run(1.0);
probe.MouseY = 0f;
probe.Run(0.2);
Check(up > 1f && probe.View.Pitch < up - 1f, $"mouse pushed away looks up ({up:F1} deg); with invert_y the same push looks down ({probe.View.Pitch:F1} deg)");
ControlsStore.Settings = new ControlSettings { AutoHackComms = true };
probe.Taps.Add(BridgeKey.X);
probe.Run(0.3);

// ---- Controller ------------------------------------------------------------------------------------------------

Console.WriteLine("Controller:");
probe.Run(0.2);
before = probe.Bridge.Session.LocalPawn!.Location;
probe.LeftY = 1f;
probe.Run(1.0);
walked = (probe.Bridge.Session.LocalPawn!.Location - before).Size();
Check(input.PadStick && input.PadForward > 0.99f && input.Get("BT_Up") == 1 && walked > 100f && probe.Controls.Pad.Active,
    $"left stick forward: analog move {input.PadForward:F2}, BT_Up held, the pawn ran {walked:F0} units");
probe.LeftY = 0.5f;
probe.ResetLog();
probe.Run(0.3);
Check(input.PadForward is > 0.3f and < 0.4f && probe.Saw("SetWalk 1") > 0, $"left stick half way: analog move {input.PadForward:F2} past the 0.25 deadzone, and the spy walks (below the 0.95 run threshold)");
probe.LeftY = 0f;
probe.Run(0.2);
Check(!input.PadStick && input.Get("BT_Up") == 0, "left stick centred: no move");

float padYaw = probe.View.Yaw;
probe.RightX = 1f;
probe.Run(0.5);
probe.RightX = 0f;
probe.Run(0.2);
float padTurn = MathF.Abs(Angle(probe.View.Yaw - padYaw));
// Free look 1.6 on the Aggressive curve: the rim turns at 3 x the multiplier, 9300 units a second per unit.
float expected = PadMapper.StickCountsPerSecond * 3f * 1.6f * MouseLook.UnitsPerCount * 0.5f * 360f / 65536f;
Check(MathF.Abs(padTurn - expected) < expected * 0.08f, $"right stick full right for 0.5 s turned {padTurn:F0} deg (Reloaded's rate for the spy's pad free look gives {expected:F0})");

probe.ResetLog();
probe.Taps.Add(BridgeKey.PadY);
probe.Run(0.15);
probe.Held.Add(BridgeKey.PadA);
probe.Run(0.15);
bool usingHeld = input.Get("bUsing") == 1;
probe.Held.Remove(BridgeKey.PadA);
probe.Run(0.1);
Check(probe.Saw("Jump") == 1 && usingHeld && input.Get("bUsing") == 0, "Y jumps, A holds bUsing");
probe.ResetLog();
probe.Held.Add(BridgeKey.PadLS);
probe.Run(0.1);
probe.Held.Add(BridgeKey.PadY);
probe.Run(0.1);
probe.Held.Clear();
probe.Run(0.1);
Check(probe.Saw("SelectGadget") == 1 && probe.Saw("Jump") == 0, "left stick click + Y selects gadget slot 0 and does not jump");
probe.ResetLog();
probe.Held.Add(BridgeKey.PadB);
probe.Run(0.06);
probe.Held.Clear();
probe.Run(0.1);
int crouchTicks = probe.ButtonTicks("bCrouch");
probe.Held.Add(BridgeKey.PadB);
probe.Run(0.35);
bool roll = input.Get("bRoll") == 1;
probe.Held.Clear();
probe.Run(0.1);
Check(crouchTicks == 1 && roll && probe.Saw("LongRoll") == 1 && probe.ButtonTicks("bCrouch") == 1,
    "B tapped is one tick of bCrouch; held past 0.2 s it is the long roll instead (LongRoll, bRoll held)");
local.Clear();
probe.Taps.Add(BridgeKey.PadStart);
probe.Run(0.1);
Check(Local("ShowMenu", true), "Start raises ShowMenu");
probe.LeftTrigger = 1f;
probe.Run(0.3);
bool alt = input.Get("bAltFire") == 1;
probe.LeftTrigger = 0f;
probe.Run(0.1);
Check(alt && input.Get("bAltFire") == 0, "left trigger past its threshold holds bAltFire");
Check(probe.Bridge.Failure is null, "no failure in the session");
probe.Bridge.Dispose();

// ---- A merc ----------------------------------------------------------------------------------------------------

load.Restart();
ControlsStore.Settings = new ControlSettings();
probe = new Driver(BridgeSession.StartLocal(map, "Story", merc: true, bots: 3));
Console.WriteLine($"{map} as merc, loaded in {load.Elapsed.TotalSeconds:F1} s");
if (!probe.WaitForPawn())
{
    Console.WriteLine("FAILED: no local pawn after 8 s" + (probe.Bridge.Failure is { } f2 ? ": " + f2 : ""));
    return 1;
}

input = probe.Bridge.Session.Input;
probe.Run(0.3);
Check(probe.Controls.Merc && probe.View.SpyFirstPerson == 0 && probe.View.Valid == 1,
    $"the merc's bindings play; view fov {probe.View.Fov:F0} (4:3) is {probe.View.HorizontalFov:F1} x {probe.View.VerticalFov:F1} on 16:9");
float mercYaw0 = probe.View.Yaw;
probe.MouseX = 6f;
probe.Run(0.25);
probe.Steps.Clear();
probe.Run(1.0);
probe.MouseX = 0f;
float mercStep = MouseLook.UnitsPerCount * 6f * 360f / 65536f;
float[] mercLook = Signed(probe.Steps.Select(s => s.Look)), mercPlain = Signed(probe.Steps.Select(s => s.Plain));
Console.WriteLine($"   6 counts a frame for 1 s over {mercLook.Length} frames, each worth {mercStep:F3} deg (look 1.0)");
Console.WriteLine($"      look-ahead view: yaw step per frame min {mercLook.Min():F3} max {mercLook.Max():F3} mean {mercLook.Average():F3} deg");
Console.WriteLine($"      plain view     : yaw step per frame min {mercPlain.Min():F3} max {mercPlain.Max():F3} mean {mercPlain.Average():F3} deg");
Check(probe.Controls.LookAheadActive && mercLook.Min() > mercStep * 0.5f && mercLook.Max() < mercStep * 1.5f,
    "merc first person: the look-ahead is on and every frame turns by its own counts");
probe.Run(0.3);
Check(MathF.Abs(Angle(probe.View.Yaw - probe.Bridge.GetView().Yaw)) < 0.02f, $"mouse still: the views agree (turned {Angle(probe.View.Yaw - mercYaw0):F1} deg)");

probe.ResetLog();
probe.Taps.Add(BridgeKey.B);
probe.Run(0.5);
bool scoped = probe.Bridge.Session.LocalController!.bBinocularVision || probe.Saw("SwitchBinocularVision") == 1;
probe.Taps.Add(BridgeKey.B);
probe.Run(0.5);
Check(scoped && probe.Saw("SwitchBinocularVision") == 2, "B toggles the scope (SwitchBinocularVision on each press)");
ControlSettings hold = new();
hold.MercKbm.ScopeBehavior = "Hold";
ControlsStore.Settings = hold;
probe.ResetLog();
probe.Held.Add(BridgeKey.B);
probe.Run(0.4);
int setOn = probe.Saw("SetScope 1");
probe.Held.Remove(BridgeKey.B);
probe.Run(0.3);
Check(setOn > 0 && probe.Saw("SwitchBinocularVision") == 0 && probe.Log.Last(l => l.StartsWith("SetScope", StringComparison.Ordinal)) == "SetScope",
    "scope_behavior Hold: B held sends SetScope 1 each tick, its release SetScope 0");
probe.ResetLog();
probe.Taps.Add(BridgeKey.Q);
probe.Taps.Add(BridgeKey.E);
probe.Taps.Add(BridgeKey.G);
probe.Run(0.15);
Check(probe.Saw("GadLaserSwitch") == 1 && probe.Saw("GadTorcheSwitch") == 1 && probe.Saw("SwapWeapon") == 1, "Q, E and G: GadLaserSwitch, GadTorcheSwitch, SwapWeapon");
probe.ResetLog();
probe.Taps.Add(BridgeKey.PadX);
probe.Run(0.1);
probe.RightTrigger = 1f;
probe.Run(0.2);
bool fire = input.Get("bFire") == 1;
probe.RightTrigger = 0f;
probe.Run(0.1);
Check(probe.ButtonTicks("bReload") >= 1 && fire && probe.Saw("Fire") == 1, "merc pad: X reloads, the right trigger fires");
Check(probe.Bridge.Failure is null, "no failure in the session");
probe.Bridge.Dispose();

// ---- The same through the function tables Unreal gets ----------------------------------------------------------

Console.WriteLine("Through the exported tables:");
ControlsStore.Settings = new ControlSettings();
ControlsStore.Bindings = new ControlBindings();
Check(Tables.Run(content, map, bindingsPath, out string report), report);

Console.WriteLine(failures == 0 ? "ALL CHECKS PASSED" : $"{failures} CHECK(S) FAILED");
return failures == 0 ? 0 : 1;

bool Local(string name, bool pressed) => local.Any(c => c.Name == name && c.Pressed == pressed);

string Describe() => string.Join(", ", local.Select(c => c.Name + (c.Pressed ? " down" : " up")));

// Yaw steps with the turn's direction taken out (the mean is positive).
static float[] Signed(IEnumerable<float> steps)
{
    float[] all = steps.ToArray();
    float sign = all.Length > 0 && all.Average() < 0f ? -1f : 1f;
    return all.Select(s => s * sign).ToArray();
}

static float Angle(float degrees)
{
    degrees %= 360f;
    return degrees > 180f ? degrees - 360f : degrees < -180f ? degrees + 360f : degrees;
}

static float FRotatorDegrees(int units) => OpenScct.Core.FRotator.NormalizeAxis(units) * (360f / 65536f);

/// <summary>Plays frames as Unreal would: device state in, session update, the view out.</summary>
internal sealed class Driver
{
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private double _last;
    private float _lastLookYaw, _lastPlainYaw;
    private bool _haveYaw;

    public Driver(BridgeSession bridge)
    {
        Bridge = bridge;
        Controls = ControlsFeature.Of(bridge);

        // After the controls' own handler: the tick's input as the simulation is about to take it.
        bridge.BeforeTick += () =>
        {
            LocalInput input = bridge.Session.Input;
            foreach (InputCommand command in input.Commands)
            {
                Log.Add(command.ToString());
            }

            for (int i = 0; i < LocalInput.ButtonNames.Count; i++)
            {
                if (input[i] != 0)
                {
                    Log.Add("#" + LocalInput.ButtonNames[i]);
                }
            }

            MaxUp = MathF.Max(MaxUp, input.Up);
        };
    }

    public BridgeSession Bridge { get; }

    public ControlsFeature Controls { get; }

    public HashSet<BridgeKey> Held { get; } = new();

    /// <summary>Keys that go down and up again inside the next frame.</summary>
    public List<BridgeKey> Taps { get; } = new();

    /// <summary>Per tick: the commands in order, and "#name" for each button held.</summary>
    public List<string> Log { get; } = new();

    /// <summary>The yaw change of each frame, in the look-ahead view and in the bridge's plain view.</summary>
    public List<(float Look, float Plain)> Steps { get; } = new();

    public float MaxUp { get; private set; }

    public float MouseX, MouseY, LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger;

    /// <summary>Notches for the next frame only, up positive.</summary>
    public int Wheel;

    public bool Play = true;

    public BridgeLookView View { get; private set; }

    public void ResetLog()
    {
        Log.Clear();
        MaxUp = 0f;
    }

    public int Saw(string command) => Log.Count(l => l == command);

    public int ButtonTicks(string button) => Log.Count(l => l == "#" + button);

    public bool WaitForPawn()
    {
        for (int i = 0; i < 2000 && Bridge.Session.LocalPawn is null && Bridge.Failure is null; i++)
        {
            Frame();
            Thread.Sleep(4);
        }

        return Bridge.Session.LocalPawn is not null;
    }

    public void Run(double seconds)
    {
        double until = _clock.Elapsed.TotalSeconds + seconds;
        do
        {
            Frame();
            Thread.Sleep(4);
        }
        while (_clock.Elapsed.TotalSeconds < until);
    }

    private unsafe void Frame()
    {
        double now = _clock.Elapsed.TotalSeconds;
        var raw = new BridgeRawInput
        {
            MouseX = MouseX, MouseY = MouseY, LeftX = LeftX, LeftY = LeftY, RightX = RightX, RightY = RightY,
            LeftTrigger = LeftTrigger, RightTrigger = RightTrigger,
            DeltaSeconds = (float)(now - _last),
            Flags = Play ? BridgeRawInputFlags.Play : 0,
        };
        _last = now;
        foreach (BridgeKey key in Held)
        {
            raw.Keys[(int)key] = 1;
        }

        foreach (BridgeKey key in Taps)
        {
            raw.Keys[(int)key] |= 2;
        }

        Taps.Clear();
        raw.Keys[(int)(Wheel > 0 ? BridgeKey.MouseWheelUp : BridgeKey.MouseWheelDown)] = (byte)Math.Abs(Wheel);
        Wheel = 0;

        Controls.Frame(in raw);
        Bridge.Update();
        View = Controls.GetView(16f / 9f, 0f);
        BridgeView plain = Bridge.GetView();
        if (View.Valid == 1 && _haveYaw)
        {
            Steps.Add((Wrap(View.Yaw - _lastLookYaw), Wrap(plain.Yaw - _lastPlainYaw)));
        }

        _haveYaw = View.Valid == 1;
        _lastLookYaw = View.Yaw;
        _lastPlainYaw = plain.Yaw;
    }

    private static float Wrap(float degrees)
    {
        degrees %= 360f;
        return degrees > 180f ? degrees - 360f : degrees < -180f ? degrees + 360f : degrees;
    }
}

/// <summary>Calls the bridge as native code does: through the entry points' function pointers and the flat structs.</summary>
internal static unsafe class Tables
{
    public static bool Run(string content, string map, string bindingsPath, out string report)
    {
        BridgeApi bridge;
        ControlsApi controls;
        int bridgeVersion = ((delegate* unmanaged<BridgeApi*, int>)&Exports.GetApi)(&bridge);
        int version = ((delegate* unmanaged<ControlsApi*, int>)&ControlsExports.GetApi)(&controls);
        char* text = stackalloc char[256];

        BridgeControlSettings settings;
        controls.GetSettings(&settings);
        bool defaults = settings.SpyKbm.FreeLook == 1.4f && settings.MercKbm.ScopeVertical == 2.45f && settings.MercFovCap == 112f &&
            settings.PadDeadzoneLeft == 0.25f && settings.SpyPad.LookResponse == 3 && settings.InvertY == 0;
        settings.MercFovCap = 500f;
        settings.SpyKbm.WalkHold = 1;
        controls.SetSettings(&settings);
        controls.GetSettings(&settings);
        bool set = settings.MercFovCap == 112f && settings.SpyKbm.WalkHold == 1;

        controls.GetKeyName((int)BridgeKey.D5, text, 256);
        string keyName = new(text);
        int length;
        fixed (char* key = "LeftMouse")
        {
            length = controls.GetBinding(0, 0, key, text, 256);
        }

        string leftMouse = new(text);
        int bound, loadedOk, lastErrorLength;
        fixed (char* key = "P2")
        fixed (char* action = "Action:gear")
        {
            bound = controls.SetBinding(1, 1, key, action);
            controls.GetBinding(1, 1, key, text, 256);
        }

        string paddle = new(text);
        fixed (char* path = bindingsPath)
        {
            loadedOk = controls.LoadBindings(path);
        }

        fixed (char* path = bindingsPath + "\not\a\file")
        fixed (char* key = "T")
        {
            // The file the first part of this probe saved has T unbound for the spy.
            controls.GetBinding(0, 0, key, text, 256);
            lastErrorLength = controls.SaveBindings(path) == 0 ? controls.LastError(null, 0) : -1;
        }

        string unbound = new(text);
        controls.ResetBindings(0);
        controls.ResetBindings(1);

        int started;
        fixed (char* root = content)
        fixed (char* mapName = map)
        fixed (char* mode = "Story")
        {
            started = bridge.Configure(root) != 0 && bridge.StartLocal(mapName, mode, 0, 2) != 0 ? 1 : 0;
        }

        var raw = default(BridgeRawInput);
        BridgeLookView view = default;
        var commands = stackalloc BridgeLocalCommand[8];
        var clock = Stopwatch.StartNew();
        double last = 0;
        int frames = 0, valid = 0, taken = 0;
        string command = "";
        float yawStart = float.NaN;
        while (started != 0 && clock.Elapsed.TotalSeconds < 6 && bridge.GetState() == (int)BridgeState.Playing)
        {
            double now = clock.Elapsed.TotalSeconds;
            raw.DeltaSeconds = (float)(now - last);
            last = now;
            raw.Flags = BridgeRawInputFlags.Play;
            raw.MouseX = valid > 60 ? 5f : 0f;
            raw.Keys[(int)BridgeKey.W] = (byte)(valid > 30 ? 1 : 0);
            raw.Keys[(int)BridgeKey.F10] = (byte)(valid == 40 ? 2 : 0);
            controls.Frame(&raw);
            bridge.Update();
            if (controls.GetView(&view, 16f / 9f, 0f) != 0)
            {
                valid++;
                if (valid == 60) yawStart = view.Yaw;
            }

            int n = controls.TakeLocalCommands(commands, 8);
            for (int i = 0; i < n; i++)
            {
                taken++;
                controls.GetName(commands[i].NameId, text, 256);
                command += new string(text) + (commands[i].Pressed != 0 ? " down " : " up ");
            }

            frames++;
            if (valid > 120) break;
            Thread.Sleep(4);
        }

        int side = controls.GetSide();
        int state = bridge.GetState();
        bridge.LastError(text, 256);
        string error = new(text);
        bridge.Stop();
        float turned = MathF.Abs(view.Yaw - yawStart);
        report = $"tables v{bridgeVersion} and v{version}: settings read ({defaults}) and clamped on write ({set}); key 31 is '{keyName}'; " +
            $"LeftMouse is '{leftMouse}' ({length} chars); merc paddle P2 bound ({bound}) to '{paddle}'; saved bindings loaded ({loadedOk}), spy T is '{unbound}'; " +
            $"a save to a bad path failed with a {lastErrorLength} char error; session: {frames} frames, {valid} views, side {side}, state {state}, " +
            $"mouse turned the view {turned:F1} deg, commands: {command.Trim()}{(error.Length > 0 ? "; error " + error : "")}";
        return bridgeVersion == BridgeSession.ApiVersion && version == ControlsExports.ApiVersion && defaults && set && keyName == "5" &&
            leftMouse == "Button bFire | Function Fire | GadgetQuickChoice 0" && length == leftMouse.Length && bound == 1 && paddle == "Action:gear" &&
            loadedOk == 1 && unbound.Length == 0 && lastErrorLength > 0 && started == 1 && valid > 120 && side == 0 &&
            state == (int)BridgeState.Playing && turned > 1f && command.Contains("ShowConnectedPlayers down") && taken == 2;
    }
}
