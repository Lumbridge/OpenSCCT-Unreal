using System;
using System.IO;
using System.Linq;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Controls;
using OpenScctGame.Play.Session;

// The console check of the key bindings editor (Controls/BindingsEditor.cs): the rows a side lists, a conflict and
// each of its answers, clearing, the shared layout, apply, discard and the file. No map is loaded.
// Argument: a scratch folder for the bindings file.

string scratch = args.Length > 0 ? args[0] : Path.Combine(Path.GetTempPath(), "BindingsProbe");
Directory.CreateDirectory(scratch);
string file = Path.Combine(scratch, "bindings.json");
File.Delete(file);

int failures = 0;
void Check(bool ok, string what)
{
    Console.WriteLine($"   {(ok ? "ok    " : "FAILED")} {what}");
    failures += ok ? 0 : 1;
}

string Keys(bool merc, string id) => string.Join(", ", BindingsEditor.KeysOf(merc, id));

// As the game starts: the file is read (there is none, so the defaults) and kept as the saved state.
ControlsStore.Bindings = ControlBindings.Load(file);
ControlsStore.SavedBindingsJson = ControlsStore.Bindings.ToJson();
BindingsEditor.Begin();

Console.WriteLine("Rows:");
foreach (bool merc in new[] { false, true })
{
    string[] rows = BindingsEditor.Rows(merc).Split('\n', StringSplitOptions.RemoveEmptyEntries);
    Console.WriteLine($"  {(merc ? "merc" : "spy")}: {rows.Length} actions");
    foreach (string row in rows.Take(14))
    {
        string[] f = row.Split('\t');
        Console.WriteLine($"     {f[1],-13} {f[2],-40} {string.Join(" / ", f.Skip(5))}");
    }
}

Check(Keys(false, "fire") == "LeftMouse" && Keys(false, "quick1") == "LeftMouse" && Keys(false, "chat") == "Enter, F5",
    $"a key with several actions shows on each (fire: {Keys(false, "fire")}, quick use 1: {Keys(false, "quick1")}); chat has two keys ({Keys(false, "chat")})");
Check(BindingsEditor.Rows(false).Contains("\tNight vision\t") && BindingsEditor.Rows(true).Contains("\tMotion tracking vision\t") &&
    !BindingsEditor.Rows(true).Contains("\troll\t") && BindingsEditor.Rows(false).StartsWith("forward\tMovement", StringComparison.Ordinal),
    "each side lists its own actions with its own names, movement first");

Console.WriteLine("Mercenary free-look:");
const string freeLook = ControlActionList.MercFreeLookId;
Check(Keys(true, freeLook) == "Alt" && !BindingsEditor.Rows(false).Contains(freeLook, StringComparison.Ordinal) &&
    BindingsEditor.Check(true, freeLook, "MouseWheelDown").Length > 0, "the merc action defaults to Alt and requires a held input");
var savedKeys = ControlBindings.FromJson("""{"merc":{"keys":{"Alt":"Walk"}},"hub":1}""");
Check(savedKeys.Merc.Keys.Bindings["Alt"] == "Walk" && BindingDraft.Keys(savedKeys.Merc.Keys, ControlActionList.Find(freeLook)!).Count == 0,
    "loading an existing custom layout preserves its Alt binding");
var mercMapper = new InputMapper(new LocalInput(), ControlBindings.DefaultKeys(), true, new ControlSettings());
var mercKeys = new byte[BridgeKeys.Count];
mercMapper.Frame(mercKeys, 0, 0, 0, true);
mercKeys[(int)BridgeKey.Alt] = 1;
mercMapper.Frame(mercKeys, 0, 0, 0, true);
Check(mercMapper.MercFreeLookHeld, "Alt captures free-look on the first frame of its press");
string padLook = ControlActions.VirtualPrefix + "P1:" + freeLook;
mercMapper.Key(padLook, true);
mercKeys[(int)BridgeKey.Alt] = 0;
mercMapper.Frame(mercKeys, 0, 0, 0, true);
Check(mercMapper.MercFreeLookHeld, "a bound controller button keeps free-look held after keyboard release");
mercMapper.Frame(mercKeys, 0, 0, 0, false);
Check(!mercMapper.MercFreeLookHeld, "loss of gameplay focus releases free-look");

Console.WriteLine("Mercenary sights and gadget:");
Check(Keys(true, "mercaim") == "RightMouse" &&
      BindingsEditor.KeysOf(true, "mercgadget").Order().SequenceEqual(new[] { "MouseX1", "V" }),
    "the default shoulder and alternate gadget keys are visible in the mercenary editor");
ControlBindings migrated = ControlBindings.FromJson("""
    {"version":1,"hub":1,"merc":{"keys":{"RightMouse":"Button bReload","V":"Talk","MouseX1":"ShowMenu"},"pad":{"LT":"Action:reload","LB+RB":"Action:jump"}}}
    """);
Check(migrated.Merc.Keys.Bindings["RightMouse"] == "Button bReload" && migrated.Merc.Keys.Bindings["V"] == "Talk" &&
      migrated.Merc.Keys.Bindings["MouseX1"] == "ShowMenu" && migrated.Merc.Pad["LT"] == "Action:reload" &&
      migrated.Merc.Pad["LB+RB"] == "Action:jump", "loading an older profile preserves bespoke mouse, keyboard and controller bindings");
Check(migrated.Merc.Keys.Bindings["MouseX2"] == "Button bMercAim" &&
      migrated.Merc.Keys.Bindings["M"] == "Button bMercGadget", "occupied stock controls leave free alternatives available");
migrated.Merc.Keys.Bind("MouseX2", "");
Check(!ControlBindings.FromJson(migrated.ToJson()).Merc.Keys.Bindings.ContainsKey("MouseX2"),
    "an explicitly cleared shoulder binding stays cleared after save and reload");

Console.WriteLine("Capture and conflicts:");
ControlBindings priorMerc = ControlBindings.FromJson("""
    {"version":2,"hub":1,"merc":{"keys":{"G":"Button bBodycamAim | SwitchFireMode","H":"Button bBodycamFreeLook"},"order":{"bodycamaim":["G"]},"pad":{"LT":"Pad:mercbodycamtrigger","P1":"Action:bodycamgadget","LB+RB":""}}}
    """);
Check(priorMerc.Merc.Keys.Bindings["G"] == "Button bMercAim | SwitchFireMode"
    && priorMerc.Merc.Keys.Bindings["H"] == ControlActionList.MercFreeLookCommand
    && priorMerc.Merc.Keys.InputOrders["mercaim"].SequenceEqual(new[] { "G" })
    && priorMerc.Merc.Pad["LT"] == "Pad:merctrigger" && priorMerc.Merc.Pad["P1"] == "Action:mercgadget"
    && priorMerc.Merc.Pad["LB+RB"] == "" && !priorMerc.ToJson().Contains("bodycam", StringComparison.OrdinalIgnoreCase),
    "existing merc bindings keep their keys, controller slots, secondary commands and saved order");
string conflict = BindingsEditor.Conflict(false, "use", "Shift", "F", out bool already, out bool canSwap);
Check(conflict == "Switch fire mode" && !already && canSwap, $"spy F -> Interact conflicts with \"{conflict}\" (swap possible: {canSwap})");
Check(BindingsEditor.Commit(false, "use", "Shift", "F", BindingCommitMode.Replace, out string error) && Keys(false, "use") == "F" &&
    BindingsEditor.LayoutOf(false).Bindings["F"] == "InvButton bUsing" && !BindingsEditor.LayoutOf(false).Bindings.ContainsKey("Shift"),
    $"replace: Interact is on F alone ({Keys(false, "use")}), Shift is free {error}");
Check(BindingsEditor.Commit(false, "jump", "Space", "C", BindingCommitMode.Swap, out error) && Keys(false, "jump") == "C" && Keys(false, "crouch") == "Space",
    $"swap: jump on C ({Keys(false, "jump")}), crouch moved to Space ({Keys(false, "crouch")}) {error}");
Check(BindingsEditor.Commit(false, "map", null, "Ctrl+M", BindingCommitMode.Share, out error) && Keys(false, "map") == "F7, Ctrl+M",
    $"a second key with a modifier ({Keys(false, "map")}) {error}");
Check(BindingsEditor.Commit(false, "reload", null, "Double+R", BindingCommitMode.Share, out error) && Keys(false, "reload") == "R, Double+R",
    $"a double tap ({Keys(false, "reload")}) {error}");
Check(!BindingsEditor.Commit(false, "map", null, "Double+M", BindingCommitMode.Share, out error) && error.Contains("double tap"),
    $"an interface action refuses a double tap: {error}");
Check(!BindingsEditor.Commit(false, "ping", null, "MouseWheelUp", BindingCommitMode.Share, out error) && error.Contains("hold"),
    $"team ping refuses the wheel: {error}");
Check(!BindingsEditor.Commit(false, "chat", null, "MiddleMouse", BindingCommitMode.Share, out error) && error.Contains("keyboard"),
    $"chat refuses a mouse button: {error}");
BindingsEditor.Clear(false, "fire", "LeftMouse");
Check(Keys(false, "fire") == "" && BindingsEditor.LayoutOf(false).Bindings["LeftMouse"] == "GadgetQuickChoice 0",
    $"clearing fire off the left mouse button keeps its quick use ({BindingsEditor.LayoutOf(false).Bindings["LeftMouse"]})");
Check(Keys(true, "use") == "Shift", "the merc's layout is not touched by the spy's edits");

Console.WriteLine("Apply, save, discard:");
Check(BindingsEditor.HasEdits && !BindingsEditor.HasUnsaved, "edits are held, nothing in play changed yet");
BindingsEditor.Apply();
Check(!BindingsEditor.HasEdits && BindingsEditor.HasUnsaved && ControlsStore.Bindings.KeysOf(false).Bindings["F"] == "InvButton bUsing",
    "apply puts them in play (what the match's input mapper reads next frame), not saved yet");
ControlsStore.Bindings.Save(file);
ControlsStore.SavedBindingsJson = ControlsStore.Bindings.ToJson();
ControlBindings reread = ControlBindings.Load(file);
Check(reread.ToJson() == ControlsStore.Bindings.ToJson() && BindingDraft.Keys(reread.Spy.Keys, ControlActionList.Find("map")!).SequenceEqual(new[] { "F7", "Ctrl+M" }),
    $"saved and read back the same, with the keys' order ({new FileInfo(file).Length} bytes)");
BindingsEditor.Commit(false, "use", "F", "G", BindingCommitMode.Replace, out _);
BindingsEditor.Apply();
BindingsEditor.Discard();
Check(ControlsStore.Bindings.KeysOf(false).Bindings["F"] == "InvButton bUsing" && !BindingsEditor.HasUnsaved,
    "leaving without saving puts the file's bindings back in play");

Console.WriteLine("Shared layout and reset:");
BindingsEditor.Begin();
BindingsEditor.SetSplit(false);
Check(Keys(true, "use") == "F", $"shared: the merc plays the spy's keys (merc Interact on {Keys(true, "use")})");
BindingsEditor.Commit(true, "reload", "R", "T", BindingCommitMode.Replace, out _);
Check(Keys(false, "reload").StartsWith("T", StringComparison.Ordinal), $"shared: an edit on the merc's page is the spy's too (spy reload {Keys(false, "reload")})");
BindingsEditor.Apply();
Check(!ControlsStore.Bindings.Split && ControlsStore.Bindings.KeysOf(true).SameAs(ControlsStore.Bindings.KeysOf(false)), "applied: both sides play one layout");
BindingsEditor.SetSplit(true);
BindingsEditor.ResetKeys(false);
Check(Keys(false, "use") == "Shift" && Keys(true, "use") == "F", "split again: the merc keeps the shared layout, the spy's reset to the defaults");

Console.WriteLine("Clutch keys (the mouse and aim section):");
ControlsStore.Settings = new ControlSettings();
Check(ClutchKeys.Set(1, "ctrl+V", out string clutchError) && ClutchKeys.Get(1) == "Ctrl+V" && ControlSettings.FromJson(ControlsStore.Settings.ToJson()).ClutchOneKey == "Ctrl+V",
    $"clutch one on Ctrl+V, kept through the settings file's JSON ({ClutchKeys.Get(1)}) {clutchError}");
Check(!ClutchKeys.Set(2, "MouseWheelUp", out clutchError) && ClutchKeys.Get(2) == "", $"a wheel direction is refused: {clutchError}");
Check(!ClutchKeys.Set(2, "Double+G", out clutchError) && clutchError.Contains("double"), $"a double tap is refused: {clutchError}");
Check(ClutchKeys.Set(2, "MiddleMouse", out _) && ClutchKeys.Set(1, "", out _) && ClutchKeys.Get(1) == "" && ClutchKeys.Get(2) == "MiddleMouse",
    "clutch two on the middle mouse button, clutch one cleared");

Console.WriteLine("Binding gestures in play:");
var live = new ControlBindings();
ControlAction reload = ActionBindings.Find("reload")!;
Check(ActionBindings.Bind(live.Spy, false, reload, "Double+H", false, doubleTapSlot: true) &&
    live.Spy.Keys.Bindings.ContainsKey("R") && live.Spy.Keys.Bindings["H"].Contains("bSpecialsChat") &&
    ControlBindings.FromJson(live.ToJson()).Spy.Keys.Bindings["Double+H"] == reload.Command,
    "editing the double-tap slot keeps ordinary bindings and survives a save/load");
Check(!ActionBindings.Bind(live.Spy, false, reload, "Double+MouseWheelUp", false, true) &&
    !ActionBindings.Bind(live.Spy, false, ActionBindings.Find("map")!, "Double+M", false, true) &&
    ActionBindings.Bind(live.Spy, false, reload, "", false, true) && live.Spy.Keys.Bindings.ContainsKey("R") &&
    !live.Spy.Keys.Bindings.ContainsKey("Double+H"), "wheel and interface doubles refused, clearing a double leaves the single");
Check(new ControlSettings().SpyRollFreeLook && ControlSettings.FromJson("{}").SpyRollFreeLook &&
    !ControlSettings.FromJson("""{"spy_roll_free_look":false}""").SpyRollFreeLook, "new and missing roll-look settings default on, explicit false loads off");

static BridgeKeyEdge Edge(BridgeKey key, bool down, int time) => new() { Code = (int)key, Pressed = down ? 1 : 0, TimeMsec = time };
static string Taps(params BridgeKeyEdge[] events)
{
    var layout = new KeyLayout();
    layout.Bind("LeftMouse", "PushToTalk");
    layout.Bind("Double+LeftMouse", "ToggleAutoHack");
    var target = new LocalInput();
    var mapper = new InputMapper(target, layout, false, new ControlSettings());
    var keys = new byte[BridgeKeys.Count];
    mapper.Frame(keys, 0, 0, 0, true);
    mapper.Frame(keys, 0, 0, 0.5, true, events, 1000);
    mapper.FillTick(target, null);
    return string.Join(",", mapper.LocalCommands.Select(c => c.Name + (c.Pressed ? "+" : "-")));
}
const BridgeKey button = BridgeKey.LeftMouse;
const string single = "PushToTalk+,PushToTalk-";
Check(Taps(Edge(button, true, 10), Edge(button, false, 20)) == single, "first tap runs its ordinary press and release");
Check(Taps(Edge(button, true, 10), Edge(button, false, 20), Edge(button, true, 310), Edge(button, false, 320)) ==
    single + ",PushToTalk+,ToggleAutoHack+,PushToTalk-", "two complete taps in one render frame trigger at 300 ms, after the ordinary press");
Check(Taps(Edge(button, true, 10), Edge(button, false, 20), Edge(button, true, 311), Edge(button, false, 320)) ==
    single + "," + single, "301 ms is two ordinary taps");
Check(Taps(Edge(button, true, 10), Edge(button, false, 20), Edge(BridgeKey.J, true, 30), Edge(BridgeKey.J, false, 40),
    Edge(button, true, 50), Edge(button, false, 60)) == single + "," + single, "intervening key in the same frame cancels the double tap");

unsafe
{
    // The sizes Source/OpenSCCT/Runtime/ScctBindingsApi.h and ScctMouseAimApi.h assert for their mirrors.
    Check(sizeof(BindingsApi) == 128, $"table size for the C++ mirror: {sizeof(BindingsApi)}");
    Check(sizeof(BridgeRawInput) == 200 && sizeof(BridgeKeyEdge) == 16, "raw input and edge sizes match the C++ table");
    Check(sizeof(MouseAimApi) == 48, $"mouse and aim table size for the C++ mirror: {sizeof(MouseAimApi)}");
    // And Runtime/ScctLoadoutApi.h (Bridge/Probes/LoadoutProbe checks each struct and plays a match with it).
    Check(sizeof(LoadoutApi) == 112 && sizeof(BridgeLoadout) == 1280 && sizeof(BridgeGearItem) == 236 && sizeof(BridgeGearAnswer) == 200,
        $"loadout table sizes for the C++ mirror: {sizeof(LoadoutApi)}, loadout {sizeof(BridgeLoadout)}, item {sizeof(BridgeGearItem)}, answer {sizeof(BridgeGearAnswer)}");
}

Console.WriteLine(failures == 0 ? "All bindings editor checks passed." : $"{failures} check(s) FAILED.");
return failures == 0 ? 0 : 1;
