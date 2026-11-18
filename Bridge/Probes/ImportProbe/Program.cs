using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Controls;

// The console check of "Import from Enhanced SCCT Versus" (Controls/EnhancedImport.cs) and of the Help & feedback
// report (Menu/ReportExports.cs). It writes a small Reloaded install into the folder given (default: a temp folder):
//   <folder>\System\SCCT_Versus.config      sensitivities, display, HUD, voice, controller
//   <folder>\System\_Profiles_.lnk          a shortcut to <folder>\Profiles, as Reloaded's own
//   <folder>\Profiles\Tester_prf.ini        inversions, volumes, wheel, gear
//   <folder>\Profiles\Tester_kbd_0.ini      the spy's keys (aliases, a moved jump and hack key)
//   <folder>\Profiles\Tester_kbd_1.ini      the merc's keys (arrows)
// imports it without applying, prints every mapped value and checks the important ones. The folder is left in
// place, so an Unreal run can import the same fixture (-ScctImportEnhanced=<folder>).
// Arguments: fixture folder.

string root = Path.GetFullPath(args.Length > 0 ? args[0] : Path.Combine(Path.GetTempPath(), "ScctEnhancedFixture"));
int failures = 0;
void Check(bool ok, string what)
{
    Console.WriteLine($"   {(ok ? "ok    " : "FAILED")} {what}");
    failures += ok ? 0 : 1;
}

string system = Path.Combine(root, "System"), profiles = Path.Combine(root, "Profiles");
if (Directory.Exists(root))
{
    Directory.Delete(root, recursive: true);
}

Directory.CreateDirectory(system);
Directory.CreateDirectory(profiles);
File.WriteAllText(Path.Combine(system, "SCCT_Versus.config"), """
    // Reloaded's settings: JSON with comments and trailing commas.
    {
      "use_split_spy_merc_keyboard": true,
      "controls_spy_config_kbm": { "free_look": 2.2, "gun_look": 1.3, "scope": 0.6, "cam": 1.5, "free_look_response": "Linear" },
      "controls_merc_config_kbm": { "look": 0.9, "scope": 0.5, "scope_vertical": 2.0, "clutch_two": 0.5, "clutch_two_enabled": true },
      "controls_spy_config_controller": { "free_look": 1.9, "gun_look": 1.1, "free_look_response": "CurveMedium" },
      "controller_enabled": true,
      "controller_left_thumb_deadzone": 0.3,
      "controller_rumble_enabled": true,
      "controller_rumble_strength": 0.6,
      "display_mode": "Windowed",
      "display_monitor": 1,
      "window_position": "Centered",
      "widescreen": true,
      "ws_fov": 105,
      "resolution_scale": 0.8,
      "filter_level": 8,
      "msaa": false,
      "smaa": true,
      "ambient_occlusion": false,
      "shadow_quality": 3,
      "low_latency_mode": "On",
      "fps_merc": 144,
      "show_fps": true,
      "show_net_stats": true,
      "show_net_loss": false,
      "classic_crosshair": true,
      "voice_push_to_talk": true,
      "voice_activation_threshold": 0.05,
      "voice_activation_hold_ms": 450,
      "voice_input_gain": 1.5,
      "voice_noise_suppression_level": "High",
      "voice_jitter_frames": 4,
      "voice_output_volume": 0.7,
    }
    """);
File.WriteAllBytes(Path.Combine(system, "_Profiles_.lnk"), Shortcut(profiles));
const char Marker = '§';
File.WriteAllText(Path.Combine(profiles, "Tester_prf.ini"), string.Join("\r\n", new[]
{
    "[Engine.SPlayerProfile]",
    $"ATTEquipment[0]=Class{Marker}(677)'SBase{Marker}(180).SGadSmokeGrenade{Marker}(707695)'",
    $"ATTEquipment[1]=Class{Marker}(677)'SBase{Marker}(180).SGadFlashBang{Marker}(707689)'",
    $"ATTEquipment[2]=Class{Marker}(677)'SBase{Marker}(180).SGadStickyCam{Marker}(212280)'",
    $"ATTEquipment[3]=Class{Marker}(677)'SBase{Marker}(180).SGadHeartBeatSensor{Marker}(945352)'",
    $"DEFEquipment[0]=Class{Marker}(677)'SBase{Marker}(180).SGadFragGrenade{Marker}(707690)'",
    $"DEFEquipment[1]=Class{Marker}(677)'SBase{Marker}(180).SGadMultiMinesLauncher{Marker}(179964)'",
    $"DEFEquipment[2]=Class{Marker}(677)'SBase{Marker}(180).SGadGasMask{Marker}(854071)'",
    $"DEFEquipment[3]=Class{Marker}(677)'SBase{Marker}(180).SGadBackPack{Marker}(854069)'",
    $"DEFWeapClass=Class{Marker}(677)'SBase{Marker}(180).SWeaponDefense{Marker}(190223)'",
    "InvertY=True",
    "InvertX=False",
    "bInFeet=True",
    "allowVoiceChat=True",
    "iEffectVolume=0.600000",
    "iMusicVolume=0.250000",
    "iVoiceChatVolume=0.500000",
    "SelectedKeyboard=0",
    "SelectedMouse=1",
    "bObjectiveSounds=False",
}), Encoding.Latin1);
File.WriteAllText(Path.Combine(profiles, "Tester_kbd_0.ini"), """
    [Engine.Input]
    IK_None=
    IK_W=AliasHaut
    IK_S=AliasBas
    IK_A=AliasGauche
    IK_D=AliasDroite
    IK_Ctrl=Jump
    IK_Space=
    IK_LeftMouse=Fire | GadgetQuickChoice 0
    IK_RightMouse=AltFire | GadgetQuickChoice 1 | HoldBreath
    IK_G=HackComm
    IK_H=
    IK_MouseX=Axis aMouseX Speed=6.00
    """, Encoding.Latin1);
File.WriteAllText(Path.Combine(profiles, "Tester_kbd_1.ini"), """
    [Engine.Input]
    IK_Up=AliasHaut
    IK_Down=AliasBas
    IK_Left=AliasGauche
    IK_Right=AliasDroite
    IK_W=
    IK_S=
    IK_A=
    IK_D=
    IK_LeftMouse=Fire | GadgetQuickChoice 0
    """, Encoding.Latin1);
Console.WriteLine($"Fixture written to {root}");

// ---- the import, from a folder inside the install (as a player may pick) ------------------------------------------

EnhancedImport? source = EnhancedImport.Find(Path.Combine(root, "Profiles"));
Check(source is not null && source.Root == root, "any folder inside the install finds the install (here its Profiles folder)");
source = EnhancedImport.Find(system);
Check(source is not null && source.Root == root && source.ProfileName == "Tester", $"the install found from its System folder, profile \"{source?.ProfileName}\" through the shortcut");
if (source is null)
{
    return 1;
}

EnhancedImportResult result = source.Apply(new ControlSettings(), new ControlBindings());
Console.WriteLine("Unreal's settings (settings console keys):");
foreach (ImportedSetting setting in result.Settings)
{
    Console.WriteLine($"   {setting.Key} = {setting.Value}");
}

Console.WriteLine($"Kit: {result.Loadout}");
Console.WriteLine("Report:");
foreach (string line in result.Report)
{
    Console.WriteLine("   " + line);
}

Console.WriteLine("Controls:");
foreach (string line in EnhancedImportExports.Describe(result))
{
    Console.WriteLine("   " + line);
}

string Value(string key) => result.Settings.LastOrDefault(s => s.Key == key).Value ?? "(none)";
ControlSettings c = result.Controls;
Check(c.InvertY && !c.InvertXSpy && c.MouseWheel == "Speed", "profile: Y inverted, X not, the wheel sets walk and run");
Check(Math.Abs(c.SpyKbm.FreeLook - 2.2f) < 1e-4 && Math.Abs(c.SpyKbm.Look - 1.3f) < 1e-4 && Math.Abs(c.MercKbm.Look - 0.9f) < 1e-4 &&
    c.MercKbm.ClutchTwoEnabled && Math.Abs(c.MercFovCap - 105f) < 1e-4, "config: mouse sensitivities, the merc's second clutch and ws_fov 105");
Check(c.Pad.Enabled && Math.Abs(c.Pad.DeadzoneLeft - 0.3f) < 1e-4 && c.Pad.Rumble && c.SpyPad.LookResponse == "CurveMedium", "config: controller tuning");
Check(Value("Display.WindowMode") == "Windowed" && Value("Graphics.RenderScale") == "80" && Value("Graphics.TextureFiltering") == "8x" &&
    Value("Graphics.AntiAliasing") == "TSR" && Value("Performance.MaxFps") == "144", "config: display, render scale 80, 8x filtering, TSR for SMAA, 144 FPS");
Check(Value("Hud.FpsMode") == "Simple" && Value("Hud.NetMode") == "Simple" && Value("Hud.ClassicCrosshair") == "true", "config: HUD readouts and the classic crosshair");
Check(Value("Voice.PushToTalk") == "true" && Value("Voice.Threshold") == "0.05" && Value("Voice.NoiseSuppressionDb") == "High" && Value("Audio.Voice") == "70",
    "config: voice settings, its output volume 70 (the profile's 50 gives way)");
Check(Value("Audio.Effects") == "60" && Value("Audio.Objective") == "0" && Value("Audio.ObjectiveSounds") == "false" && Value("Audio.Music") == "25" &&
    Value("Hud.DistanceInFeet") == "true", "profile: effects 60, objective sounds off, music 25, feet");
Check(result.Loadout == "spy:SBase.SGadSmokeGrenade,SBase.SGadFlashBang,SBase.SGadStickyCam,SBase.SGadHeartBeatSensor;" +
    "merc:SBase.SGadFragGrenade,SBase.SGadMultiMinesLauncher,SBase.SGadGasMask,SBase.SGadBackPack;weapon:SBase.SWeaponDefense", "profile: both kits and the merc's weapon");

KeyLayout spy = result.Bindings.KeysOf(false), merc = result.Bindings.KeysOf(true);
string Key(KeyLayout layout, string key) => layout.Bindings.TryGetValue(key, out string? command) ? command : "(unbound)";
Check(result.Bindings.Split, "split keyboards: each role its own layout");
Check(Key(spy, "W") == "Button BT_Up" && Key(spy, "Ctrl") == "Function Jump | Axis aUp Speed=+280.0" && Key(spy, "Space") == "(unbound)",
    "spy: aliases expanded, the jump moved to Ctrl and Space unbound");
Console.WriteLine($"   spy G = {Key(spy, "G")}; H = {Key(spy, "H")}; Double+G = {Key(spy, "Double+G")}; Double+H = {Key(spy, "Double+H")}");
Check(Key(spy, "G").StartsWith("Button bSpecialsChat", StringComparison.Ordinal) && !Key(spy, "H").Contains("bSpecialsChat", StringComparison.Ordinal) && Key(spy, "Double+G") == ControlActions.AutoHackCommand,
    "spy: hacking moved to G, and auto hack on its double tap");
Check(Key(spy, "I") == "ChangeGear" && Key(spy, "K").StartsWith("Button bCloak", StringComparison.Ordinal) && Key(spy, "X").StartsWith("SwitchSpyPerspective", StringComparison.Ordinal) && Key(spy, "MouseX") == "(unbound)",
    "spy: OpenSCCT's first-run actions added on free keys, the mouse axes left to the look");
Check(Key(spy, "MouseWheelUp") == "Run" && Key(spy, "MouseWheelDown") == "Walk", "spy: the wheel's keys follow the profile's walk and run wheel");
Check(Key(merc, "Up") == "Button BT_Up" && Key(merc, "W") == "(unbound)" && Key(merc, "LeftMouse").StartsWith("Button bFire | Function Fire", StringComparison.Ordinal),
    "merc: the arrows move, WASD unbound");

// ---- the exported text (what Unreal reads), without putting it in use ---------------------------------------------

string text = EnhancedImportExports.ImportText(system, withoutInstall: false, apply: false);
string[] lines = text.Split('\n', StringSplitOptions.RemoveEmptyEntries);
Check(lines[0] == "source " + root && lines.Count(l => l.StartsWith("set ", StringComparison.Ordinal)) == result.Settings.Count &&
    lines.Any(l => l.StartsWith("loadout spy:", StringComparison.Ordinal)) && lines.Any(l => l.StartsWith("note ", StringComparison.Ordinal)),
    $"the table's text: {lines.Length} lines (source, set, loadout, note, controls)");
Check(EnhancedImportExports.ImportText(Path.Combine(root, "nowhere"), false, apply: false).StartsWith("error ", StringComparison.Ordinal), "a folder that is not there: an error line");

// ---- the Help & feedback report (no match: the menus) --------------------------------------------------------------

Console.WriteLine("Report details:");
string details = ReportExports.Details();
foreach (string line in details.Split('\n'))
{
    Console.WriteLine("   " + line);
}

string url = ReportExports.Url(BridgeReportLink.BugReport);
Console.WriteLine("Bug report address: " + url);
Check(details.Contains("Source: OpenSCCT game", StringComparison.Ordinal) && details.Contains("Session: Menus", StringComparison.Ordinal),
    "the details name the game, its version and the menus");
Check(url.StartsWith("https://master.lumbridge.co.uk/help/report-bug?area=game&version=", StringComparison.Ordinal) && url.Contains("&environment=", StringComparison.Ordinal),
    "the bug report address carries the version and the details");
string withCapture = ReportExports.Url(BridgeReportLink.BugReport, new string('a', 64));
Check(withCapture.EndsWith("&capture=" + new string('a', 64), StringComparison.Ordinal), "a capture token is added to the address");

Console.WriteLine(failures == 0 ? "All checks passed." : $"{failures} check(s) FAILED.");
return failures == 0 ? 0 : 1;

// A Windows shortcut to a local folder, as the parts EnhancedImport reads it (MS-SHLLINK: the header with
// HasLinkInfo, then a LinkInfo with VolumeIDAndLocalBasePath and an ANSI local base path, an empty common suffix).
static byte[] Shortcut(string target)
{
    var bytes = new List<byte>();
    var header = new byte[0x4C];
    BitConverter.GetBytes(0x4Cu).CopyTo(header, 0);
    BitConverter.GetBytes(0x2u).CopyTo(header, 0x14); // HasLinkInfo
    bytes.AddRange(header);
    byte[] path = Encoding.Latin1.GetBytes(target + "\0");
    const int InfoHeader = 0x1C;
    int volumeAt = InfoHeader, volumeSize = 0x11;
    int pathAt = volumeAt + volumeSize, suffixAt = pathAt + path.Length;
    int size = suffixAt + 1;
    var info = new byte[size];
    BitConverter.GetBytes((uint)size).CopyTo(info, 0);
    BitConverter.GetBytes((uint)InfoHeader).CopyTo(info, 4);
    BitConverter.GetBytes(1u).CopyTo(info, 8); // VolumeIDAndLocalBasePath
    BitConverter.GetBytes((uint)volumeAt).CopyTo(info, 12);
    BitConverter.GetBytes((uint)pathAt).CopyTo(info, 16);
    BitConverter.GetBytes(0u).CopyTo(info, 20);
    BitConverter.GetBytes((uint)suffixAt).CopyTo(info, 24);
    BitConverter.GetBytes((uint)volumeSize).CopyTo(info, volumeAt);
    path.CopyTo(info, pathAt);
    bytes.AddRange(info);
    return bytes.ToArray();
}
