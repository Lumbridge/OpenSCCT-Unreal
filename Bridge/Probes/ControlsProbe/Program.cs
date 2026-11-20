using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Controls;
using OpenScctGame.Play.Session;

// The console check of the controls: a map with bots, scripted device state played through the input mapper as
// Unreal would send it (one BridgeRawInput a frame), and what the simulation got printed with a verdict per step.
// Arguments: content root, map, scratch folder for the saved bindings and settings.

string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
string scratch = args.Length > 2 ? args[2] : Path.Combine(Path.GetTempPath(), "ControlsProbe");

int failures = 0;
void Check(bool ok, string what)
{
    Console.WriteLine($"   {(ok ? "ok    " : "FAILED")} {what}");
    failures += ok ? 0 : 1;
}

if (args.Contains("--aim-timing"))
{
    BridgeSession.Configure(content);
    var timing = new Driver(BridgeSession.StartLocal(map, "Story", merc: true, bots: 0));
    try
    {
        if (!timing.WaitForPawn()) throw new InvalidOperationException("No local merc for aim timing: " + timing.Bridge.Failure);
        timing.Run(.35);
        AimTimingChecks.Run(timing, Check);
        return failures == 0 ? 0 : 1;
    }
    finally { timing.Bridge.Dispose(); }
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

// ---- The bindings menu: a new key replaces the default, it is not added beside it --------------------------------

{
    string freeKey = ActionBindings.Canonical("Ctrl+F12", pad: false);
    var kept = new List<string>();
    int checkedActions = 0;
    foreach (bool merc in new[] { false, true })
    {
        foreach (ControlAction action in ControlActions.All.Where(a => merc ? a.Merc : a.Spy))
        {
            foreach (bool pad in new[] { false, true })
            {
                var fresh = new ControlBindings();
                SideBindings side = fresh.Of(merc);
                ActionBindings.Bind(side, pad, action, pad ? "P1" : "Ctrl+F12", keepOthers: false);
                List<string> now = ActionBindings.KeysOf(side, pad, action);
                checkedActions++;
                if (now.Count != 1 || !string.Equals(now[0], pad ? "P1" : freeKey, StringComparison.OrdinalIgnoreCase))
                {
                    kept.Add($"{(merc ? "merc" : "spy")} {(pad ? "pad" : "keys")} {action.Id}: {string.Join(", ", now)}");
                }
            }
        }
    }

    Check(kept.Count == 0, $"every action of both sides, keys and controller ({checkedActions} rebinds): only the new key does it afterwards" +
        (kept.Count == 0 ? "" : " -- still on the old one: " + string.Join("; ", kept)));

    var pads = new ControlBindings();
    ControlAction Act(string id) => ActionBindings.Find(id)!;
    bool readDefaults = ActionBindings.KeysOf(pads.Spy, true, Act("fire")).SequenceEqual(["RT"])
        && ActionBindings.KeysOf(pads.Spy, true, Act("crouch")).SequenceEqual(["B"])
        && ActionBindings.KeysOf(pads.Spy, true, Act("wall")).SequenceEqual(["DpadDown"])
        && ActionBindings.KeysOf(pads.Merc, true, Act("altfire")).SequenceEqual(["LT"]);
    ActionBindings.Bind(pads.Spy, true, Act("fire"), "RB", keepOthers: false);
    ActionBindings.Bind(pads.Spy, true, Act("wall"), "P2", keepOthers: false);
    Check(readDefaults && pads.Spy.Pad["RT"] == "" && pads.Spy.Pad["RB"] == "Action:fire" && pads.Spy.Pad["DpadDown"] == "Action:weaponlaser" &&
        pads.Spy.Pad["P2"] == "Action:wall",
        "the controller's contextual defaults read as bound (spy fire RT, crouch B, back to wall D-pad down, merc gadget LT); fire moved to RB leaves RT free, back to wall moved to P2 leaves D-pad down the pistol laser");

    var keys = new ControlBindings();
    ActionBindings.Bind(keys.Spy, false, Act("fire"), "P", keepOthers: false);
    Check(keys.Spy.Keys.Bindings["LeftMouse"] == "GadgetQuickChoice 0" && keys.Spy.Keys.Bindings["P"] == "Button bFire | Function Fire" &&
        ActionBindings.LabelsOf(keys.Spy, false, false, "LeftMouse").Count == 1,
        "fire moved to P: the left mouse button keeps only its quick gadget, P fires");
}
unsafe
{
    // The sizes Source/OpenSCCT/Runtime/ScctControlsApi.h asserts for its mirrors.
    Check(sizeof(BridgeRawInput) == 200 && sizeof(BridgeKeyEdge) == 16 && sizeof(BridgeLookView) == 76 && sizeof(BridgeLocalCommand) == 12 &&
        sizeof(BridgeLookSettings) == 72 && sizeof(BridgeControlSettings) == 372 && sizeof(ControlsApi) == 152,
        $"struct sizes for the C++ mirror: raw input {sizeof(BridgeRawInput)}, look view {sizeof(BridgeLookView)}, local command {sizeof(BridgeLocalCommand)}, look settings {sizeof(BridgeLookSettings)}, control settings {sizeof(BridgeControlSettings)}, table {sizeof(ControlsApi)}");
}

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

// ---- Other frame rates, the wheel's speed mode, the clutch key --------------------------------------------------

Console.WriteLine("Frame rates:");
foreach ((string rate, double pace, float counts) in new[] { ("240 frames a second (a tick every 4th frame)", 1.0 / 240.0, 3f), ("30 frames a second (2 ticks a frame)", 1.0 / 30.0, 24f) })
{
    probe.Pace = pace;
    probe.MouseX = counts;
    probe.Run(0.3);
    probe.Steps.Clear();
    probe.Run(1.0);
    probe.MouseX = 0f;
    float worth = MouseLook.UnitsPerCount * probe.Bridge.Session.Mouse.SpyFreeLook * counts * 360f / 65536f;
    float[] ahead = Signed(probe.Steps.Select(s => s.Look)), behind = Signed(probe.Steps.Select(s => s.Plain));
    Console.WriteLine($"   {rate}: {counts} counts a frame over {ahead.Length} frames, each worth {worth:F3} deg");
    Console.WriteLine($"      look-ahead view: yaw step per frame min {ahead.Min():F3} max {ahead.Max():F3} mean {ahead.Average():F3} deg");
    Console.WriteLine($"      plain view     : yaw step per frame min {behind.Min():F3} max {behind.Max():F3} mean {behind.Average():F3} deg");
    Check(probe.Controls.LookAheadActive && ahead.Min() > worth * 0.5f && ahead.Max() < worth * 1.5f,
        "the look-ahead view turns by each frame's own counts at this frame rate too");
    probe.Run(0.3);
    Check(MathF.Abs(Angle(probe.View.Yaw - probe.Bridge.GetView().Yaw)) < 0.02f, "mouse still: no snap back");
    probe.Pace = 0;
}

// One frame's worth of counts after a still mouse: the look-ahead view has it in that very frame, the plain view
// only once a tick has taken it and the blend has caught up.
probe.Pace = 1.0 / 240.0;
probe.Run(0.2);
float burstWorth = MouseLook.UnitsPerCount * probe.Bridge.Session.Mouse.SpyFreeLook * 40f * 360f / 65536f;
float sameFrameLook = 0f, sameFramePlain = 0f;
int bursts = 0;
for (int i = 0; i < 20; i++)
{
    probe.Run(0.05);
    probe.Steps.Clear();
    probe.MouseX = 40f;
    probe.Run(0);
    probe.MouseX = 0f;
    sameFrameLook += MathF.Abs(probe.Steps[^1].Look);
    sameFramePlain += MathF.Abs(probe.Steps[^1].Plain);
    bursts++;
}

probe.Pace = 0;
sameFrameLook /= bursts;
sameFramePlain /= bursts;
Check(MathF.Abs(sameFrameLook - burstWorth) < burstWorth * 0.05f && sameFramePlain < burstWorth * 0.5f,
    $"40 counts in one frame after a still mouse, {bursts} times at 240 frames a second: that frame's look-ahead view turned {sameFrameLook:F3} deg of the counts' {burstWorth:F3}, the plain view {sameFramePlain:F3}");

probe.ResetLog();
probe.Pace = 1.0 / 240.0;
probe.Taps.Add(BridgeKey.C);
probe.Run(0.1);
int fastTapTicks = probe.ButtonTicks("bCrouch");
probe.ResetLog();
probe.Pace = 1.0 / 30.0;
probe.Taps.Add(BridgeKey.C);
probe.Run(0.2);
probe.Pace = 0;
Check(fastTapTicks == 1 && probe.ButtonTicks("bCrouch") == 1,
    $"a key down and up inside one frame is one tick of its button at 240 frames a second ({fastTapTicks}) and at 30 ({probe.ButtonTicks("bCrouch")})");

Console.WriteLine("Wheel speed mode, clutch key:");
ControlSettings speedWheel = ControlSettings.FromJson(ControlsStore.Settings.ToJson());
speedWheel.MouseWheel = "Speed";
speedWheel.ClutchOneKey = "Ctrl+V";
speedWheel.SpyKbm.ClutchOne = 0.5f;
speedWheel.SpyKbm.ClutchOneEnabled = true;
ControlsStore.Settings = speedWheel;
string wheelUpNow = ControlsStore.Bindings.Spy.Keys.Bindings["MouseWheelUp"], wheelDownNow = ControlsStore.Bindings.Spy.Keys.Bindings["MouseWheelDown"];
probe.Run(0.1);
probe.ResetLog();
probe.Wheel = -1;
probe.Run(0.1);
bool wheelWalks = !probe.Bridge.Session.LocalController!.bFastSpeed;
probe.Wheel = 1;
probe.Run(0.1);
Check(wheelUpNow == "Run" && wheelDownNow == "Walk" && wheelWalks && probe.Bridge.Session.LocalController!.bFastSpeed &&
    probe.Saw("NextGadget") == 0 && probe.Saw("PreviousGadget") == 0,
    $"mouse_wheel Speed: the wheel is bound to {wheelUpNow} and {wheelDownNow}; a notch down walks, a notch up runs: " + string.Join(", ", probe.Log.Where(l => !l.StartsWith('#'))));
probe.Held.Add(BridgeKey.V);
probe.Run(0.1);
float clutchPlain = probe.Bridge.Session.Mouse.Clutch;
probe.Held.Clear();
probe.Run(0.1);
probe.Held.Add(BridgeKey.Ctrl);
probe.Run(0.05);
probe.Held.Add(BridgeKey.V);
probe.Run(0.1);
float clutchHeld = probe.Bridge.Session.Mouse.Clutch;
probe.Held.Clear();
probe.Run(0.1);
Check(clutchPlain == 1f && clutchHeld == 0.5f && probe.Bridge.Session.Mouse.Clutch == 1f,
    $"clutch key Ctrl+V: V alone leaves the look at x{clutchPlain}, Ctrl+V held makes it x{clutchHeld}, released x{probe.Bridge.Session.Mouse.Clutch}");
ControlsStore.Settings = new ControlSettings { AutoHackComms = true };
Check(ControlsStore.Bindings.Spy.Keys.Bindings["MouseWheelUp"] == "NextGadget", "mouse_wheel back to Gadget: the wheel is NextGadget again");
probe.Run(0.2);

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
float hipFov = probe.View.Fov;
probe.Taps.Add(BridgeKey.B);
probe.Run(0.5);
bool scoped = probe.Bridge.Session.LocalController!.bBinocularVision || probe.Saw("SwitchBinocularVision") == 1;
bool aimed = probe.Bridge.Session.LocalPawn?.MyWeapon is OpenScct.Game.Svm.MercRifle { AimRequested: true };
float aimBlend = probe.View.RifleAimBlend, aimFov = probe.View.Fov;
probe.Taps.Add(BridgeKey.B);
probe.Run(0.5);
Check(scoped && probe.Saw("SwitchBinocularVision") == 2, "B toggles the scope (SwitchBinocularVision on each press)");
Check(aimed ? aimBlend > 0.95f && aimFov < hipFov * 0.85f && probe.View.RifleAimBlend < 0.05f : aimBlend == 0f,
    aimed
        ? $"rifle aimed: RifleAimBlend eased to {aimBlend:F2} and the view's fov went from {hipFov:F1} to {aimFov:F1}; lowered again the blend is {probe.View.RifleAimBlend:F2} and the fov {probe.View.Fov:F1}"
        : $"the merc's weapon here is not an aimed rifle ({probe.Bridge.Session.LocalPawn?.MyWeapon?.GetType().Name ?? "none"}): no aim zoom, blend {aimBlend:F2}");
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
unsafe
{
    var raw = new BridgeRawInput { Flags = BridgeRawInputFlags.Play | BridgeRawInputFlags.AllowMercFreeLook, DeltaSeconds = 1f / 60f };
    probe.Controls.Frame(in raw);
    var mouseBefore = probe.Controls.Keys.PendingMouse;
    float padBefore = input.MouseX;
    raw.Keys[(int)BridgeKey.Alt] = 1;
    raw.MouseX = 10f;
    raw.RightX = .75f;
    bool held = probe.Controls.Frame(in raw) && probe.Controls.MercAimTurn == System.Numerics.Vector2.Zero;
    raw.Keys[(int)BridgeKey.Alt] = 0;
    bool release = probe.Controls.Frame(in raw) && probe.Controls.MercAimTurn == System.Numerics.Vector2.Zero;
    bool stayed = probe.Controls.Keys.PendingMouse == mouseBefore && input.MouseX == padBefore;
    bool ordinary = !probe.Controls.Frame(in raw) && probe.Controls.Keys.PendingMouse.X == mouseBefore.X + raw.MouseX
        && input.MouseX > padBefore && probe.Controls.MercAimTurn.X > 0f;
    Check(held && release && stayed && ordinary, "free-look takes the release frame's mouse and stick samples, and normal aim resumes on the very next frame");

    var pc = probe.Bridge.Session.LocalController!;
    probe.Controls.Keys.FillTick(input, pc);
    input.MouseX = input.MouseY = 0f;
    pc.LocalMyDesiredRotation = new OpenScct.Core.FRotator(OpenScct.Game.Movement.MovementK.PitchMax - 6, 0, 0);
    raw.MouseX = raw.RightX = 0f;
    raw.MouseY = probe.Bridge.Session.Mouse.InvertY ? -100f : 100f;
    probe.Controls.Frame(in raw);
    float reached = probe.Controls.MercAimTurn.Y;
    probe.Controls.Frame(in raw);
    Check(reached == FRotatorDegrees(6) && probe.Controls.MercAimTurn.Y == 0f,
        "two render frames before a tick report only the pitch travel available up to the stop");

    ControlsStore.Bindings = new ControlBindings();
    var pawn = pc.SBPawn!;
    SidearmEquipment.Give(pawn, SidearmKind.Unsuppressed);
    Check(SidearmEquipment.Select(pawn, 1, force: true), "select the merc sidearm for optic checks");
    if (pawn.MyWeapon is MercSidearm pistol)
    {
        pawn.WeaponSwitchUntil = pawn.Level.TimeSeconds;
        void Settle(ref BridgeRawInput sample)
        {
            for (int frame = 0; frame < 45; frame++)
            {
                probe.Controls.Frame(in sample);
                probe.Controls.Keys.FillTick(input, pc);
            }
        }
        foreach (bool allowFreeLook in new[] { false, true })
        {
            raw = new BridgeRawInput { Flags = BridgeRawInputFlags.Play, DeltaSeconds = 1f / 60f };
            if (allowFreeLook) raw.Flags |= BridgeRawInputFlags.AllowMercFreeLook;
            foreach (var sidearm in Sidearms.All)
            {
                pistol.Kind = (int)sidearm.Kind;
                pistol.Configure(fill: true);
                foreach (int optic in new[] { 0, WeaponAttachments.RedDot, WeaponAttachments.Acog })
                {
                    WeaponAttachments.Apply(pistol, optic);
                    raw.Keys[(int)BridgeKey.RightMouse] = 0;
                    Settle(ref raw);
                    raw.Keys[(int)BridgeKey.RightMouse] = 1;
                    Settle(ref raw);
                    var view = probe.Controls.GetView(16f / 9f, 0f);
                    Check(view.RifleAimBlend > .95f && pistol.AimRequested && input.Get("bAltFire") == 0 &&
                        view.OpticMagnification == 1f && Math.Abs(view.ScopeLensPower - (optic == WeaponAttachments.Acog ? 4f : 0f)) < .01f,
                        $"{sidearm.Name}, free-look input={allowFreeLook}, optic={optic}: aim {view.RifleAimBlend:F3}, lens power {view.ScopeLensPower:F2}");
                }
            }
            raw.Keys[(int)BridgeKey.RightMouse] = 0;
            Settle(ref raw);
            pistol.ClipAmmo = pistol.Ammo = 0;
            raw.Keys[(int)BridgeKey.RightMouse] = 1;
            Settle(ref raw);
            Check(pistol.AimRequested, "an empty pistol can still be aimed");
            pistol.Configure(fill: true);
            pistol.ReloadUntil = pawn.Level.TimeSeconds + 1f;
            Settle(ref raw);
            Check(probe.Controls.GetView(16f / 9f, 0f).RifleAimBlend < .01f, "pistol reload lowers its fitted optic");
            pistol.SetAiming(pc, true);
            Check(!pistol.AimRequested, "authority rejects aim during pistol reload");
            pistol.ReloadUntil = 0f;
            Settle(ref raw);
            Check(!pistol.AimRequested, "a held button cannot reopen sights after a reload");
            raw.Keys[(int)BridgeKey.RightMouse] = 0;
            raw.Keys[(int)BridgeKey.V] = 1;
            Settle(ref raw);
            Check(input.Get("bAltFire") == 1 && probe.Controls.GetView(16f / 9f, 0f).RifleAimBlend < .01f,
                "a pistol with an optic keeps its independent gadget binding");
            raw.Keys[(int)BridgeKey.V] = 0;
            Settle(ref raw);
        }
        WeaponAttachments.Apply(pistol, 0);
        raw = new BridgeRawInput { Flags = BridgeRawInputFlags.Play | BridgeRawInputFlags.AllowMercFreeLook, DeltaSeconds = 1f / 60f };
        Settle(ref raw);
        raw.Keys[(int)BridgeKey.RightMouse] = 1;
        Settle(ref raw);
        raw.Flags = BridgeRawInputFlags.MercMenuView;
        Settle(ref raw);
        Check(!pistol.AimRequested && probe.Controls.GetView(16f / 9f, 0f).RifleAimBlend > .99f,
            "pause releases gameplay aim while preserving the visible pistol pose");
        raw.Flags = BridgeRawInputFlags.Play | BridgeRawInputFlags.AllowMercFreeLook;
        raw.Keys[(int)BridgeKey.RightMouse] = 0;
        Settle(ref raw);
        raw.Keys[(int)BridgeKey.RightMouse] = 1;
        Settle(ref raw);
        Check(SidearmEquipment.Select(pawn, 0, force: true) && !pistol.AimRequested, "holstering immediately clears pistol aim");
        pawn.WeaponSwitchUntil = pawn.Level.TimeSeconds;
        Settle(ref raw);
        Check(pawn.MyWeapon is SWeaponDefense { AimRequested: false }, "the incoming primary does not inherit held sidearm aim");
        raw.Keys[(int)BridgeKey.RightMouse] = 0;
        Settle(ref raw);
        foreach (int slot in new[] { 1, 0 })
        {
            Check(SidearmEquipment.Select(pawn, slot, force: true), $"select weapon slot {slot} for ACOG");
            pawn.WeaponSwitchUntil = pawn.Level.TimeSeconds;
            var scopedGun = (SWeaponDefense)pawn.MyWeapon!;
            foreach (bool allowFreeLook in new[] { false, true })
            {
                raw = new BridgeRawInput { Flags = BridgeRawInputFlags.Play, DeltaSeconds = 1f / 60f };
                if (allowFreeLook) raw.Flags |= BridgeRawInputFlags.AllowMercFreeLook;
                WeaponAttachments.Apply(scopedGun, WeaponAttachments.Acog);
                Settle(ref raw);
                var wideHip = probe.Controls.GetView(16f / 9f, 0f);
                var narrowHip = probe.Controls.GetView(4f / 3f, 0f);
                raw.Keys[(int)BridgeKey.RightMouse] = 1;
                Settle(ref raw);
                var wideAim = probe.Controls.GetView(16f / 9f, 0f);
                var narrowAim = probe.Controls.GetView(4f / 3f, 0f);
                Check(Math.Abs(wideHip.HorizontalFov - wideAim.HorizontalFov) < .01f &&
                    Math.Abs(narrowHip.VerticalFov - narrowAim.VerticalFov) < .01f &&
                    Math.Abs(wideAim.ScopeLensPower - 4f) < .01f && Math.Abs(narrowAim.ScopeLensPower - 4f) < .01f &&
                    wideAim.RifleAimBlend > .99f,
                    $"{scopedGun.GetType().Name} ACOG, free-look input={allowFreeLook}: 4x lens with stable peripheral FOV at 16:9 and 4:3");
                pc.bBinocularVision = true;
                Check(probe.Controls.GetView(16f / 9f, 0f).ScopeLensPower == 0f, "binoculars release the scoped render target");
                pc.bBinocularVision = false;
                if (scopedGun is MercSidearm reloading)
                    reloading.ReloadUntil = pawn.Level.TimeSeconds + 1f;
                else
                {
                    scopedGun.ClipAmmo = Math.Max(0, scopedGun.ClipMaxAmmo - 1);
                    scopedGun.Ammo = Math.Max(scopedGun.Ammo, scopedGun.ClipMaxAmmo + 1);
                    scopedGun.GotoState("s_Reloading");
                    Check(scopedGun.IsInState("s_Reloading"), "partially spent primary starts its reload");
                }
                Settle(ref raw);
                var reloadView = probe.Controls.GetView(16f / 9f, 0f);
                Check(reloadView.RifleAimBlend < .01f && reloadView.OpticMagnification == 1f,
                    "reload takes the eye out of the optic and preserves the peripheral FOV");
                if (scopedGun is MercSidearm done) done.ReloadUntil = 0f;
                scopedGun.GotoState("s_Selected");
                raw.Keys[(int)BridgeKey.RightMouse] = 0;
                Settle(ref raw);
                raw.Keys[(int)BridgeKey.RightMouse] = 1;
                Settle(ref raw);
                WeaponAttachments.Apply(scopedGun, 0);
                Settle(ref raw);
                var ironAim = probe.Controls.GetView(16f / 9f, 0f);
                Check(ironAim.ScopeLensPower == 0f && ironAim.RifleAimBlend > .99f,
                    $"{scopedGun.GetType().Name}, free-look input={allowFreeLook}: removing ACOG retains aimed iron sights");
            }
        }
    }
}
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

    /// <summary>
    /// The length of a frame in seconds, kept by spinning (240 a second, 30 a second); 0 sleeps between frames,
    /// which Windows makes about 15 ms.
    /// </summary>
    public double Pace;

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
            double next = _clock.Elapsed.TotalSeconds + Pace;
            Frame();
            if (Pace > 0)
            {
                while (_clock.Elapsed.TotalSeconds < next)
                {
                    Thread.SpinWait(50);
                }
            }
            else
            {
                Thread.Sleep(4);
            }
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
        int bridgeVersion = ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApiSized)(&bridge, sizeof(BridgeApi));
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
