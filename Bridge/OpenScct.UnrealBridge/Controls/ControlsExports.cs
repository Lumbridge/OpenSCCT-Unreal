using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Controls;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The controls' entry points. Unreal asks for <see cref="ControlsExports.GetApi"/> once
/// (type <c>OpenScct.UnrealBridge.ControlsExports, OpenScct.UnrealBridge</c>) and keeps the table.
/// Source/OpenSCCT/Runtime/ScctControlsApi.h mirrors it field for field. Strings are UTF-16, zero terminated.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ControlsApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<BridgeRawInput*, void> Frame;
    public delegate* unmanaged[Cdecl]<BridgeLookView*, float, float, int> GetView;
    public delegate* unmanaged[Cdecl]<BridgeLocalCommand*, int, int> TakeLocalCommands;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<int> GetSide;
    public delegate* unmanaged[Cdecl]<int> IsPadActive;
    public delegate* unmanaged[Cdecl]<BridgeControlSettings*, void> GetSettings;
    public delegate* unmanaged[Cdecl]<BridgeControlSettings*, void> SetSettings;
    public delegate* unmanaged[Cdecl]<char*, int> LoadSettings;
    public delegate* unmanaged[Cdecl]<char*, int> SaveSettings;
    public delegate* unmanaged[Cdecl]<char*, int> LoadBindings;
    public delegate* unmanaged[Cdecl]<char*, int> SaveBindings;
    public delegate* unmanaged[Cdecl]<int, int, char*, char*, int, int> GetBinding;
    public delegate* unmanaged[Cdecl]<int, int, char*, char*, int> SetBinding;
    public delegate* unmanaged[Cdecl]<int, void> ResetBindings;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetBindingsJson;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetKeyName;
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

public static unsafe class ControlsExports
{
    public const int ApiVersion = 1;

    // Command names cross as ids: the table only grows, so an id stays good for the life of the process.
    private static readonly List<string> s_names = new();
    private static readonly Dictionary<string, int> s_nameIds = new(StringComparer.OrdinalIgnoreCase);
    private static string s_lastError = "";

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(ControlsApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Frame = &Frame;
        api->GetView = &GetView;
        api->TakeLocalCommands = &TakeLocalCommands;
        api->GetName = &GetName;
        api->GetSide = &GetSide;
        api->IsPadActive = &IsPadActive;
        api->GetSettings = &GetSettings;
        api->SetSettings = &SetSettings;
        api->LoadSettings = &LoadSettings;
        api->SaveSettings = &SaveSettings;
        api->LoadBindings = &LoadBindings;
        api->SaveBindings = &SaveBindings;
        api->GetBinding = &GetBinding;
        api->SetBinding = &SetBinding;
        api->ResetBindings = &ResetBindings;
        api->GetBindingsJson = &GetBindingsJson;
        api->GetKeyName = &GetKeyName;
        api->LastError = &LastError;
        return ApiVersion;
    }

    /// <summary>
    /// The devices' state of this rendered frame. Call it once a frame, before the bridge's Update, and do not call
    /// the bridge's SetInput or Command for anything a binding covers: this writes the simulation's input itself.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Frame(BridgeRawInput* raw)
    {
        if (raw == null || Exports.Current is not { } session)
        {
            return;
        }

        try
        {
            ControlsFeature.Of(session).Frame(in *raw);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    /// <summary>
    /// The camera to draw this frame, with the mouse turn the simulation has not taken yet. Call it after Update.
    /// <paramref name="aspect"/> is the picture's width over height (0 for 16:9); <paramref name="clearance"/> the
    /// radius in units of the sphere holding the camera's near plane (0 for the default). Returns 1 with a view.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetView(BridgeLookView* view, float aspect, float clearance)
    {
        if (view == null)
        {
            return 0;
        }

        *view = default;
        if (Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            *view = ControlsFeature.Of(session).GetView(aspect, clearance);
            return view->Valid;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Moves the interface commands raised since the last call into <paramref name="commands"/>, oldest first.
    /// Returns the count written; what does not fit stays for the next call.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeLocalCommands(BridgeLocalCommand* commands, int capacity)
    {
        if (commands == null || capacity <= 0 || Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            List<LocalCommand> pending = ControlsFeature.Of(session).Keys.LocalCommands;
            int count = Math.Min(capacity, pending.Count);
            for (int i = 0; i < count; i++)
            {
                LocalCommand command = pending[i];
                commands[i] = new BridgeLocalCommand { NameId = Intern(command.Name), Arg = command.Arg, Pressed = command.Pressed ? 1 : 0 };
            }

            pending.RemoveRange(0, count);
            return count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Copies the name behind a command name id. Returns its full length, 0 for an unknown id.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetName(int id, char* buffer, int capacity)
    {
        try
        {
            return (uint)id < (uint)s_names.Count ? Copy(s_names[id], buffer, capacity) : 0;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    /// <summary>The side whose bindings play: 0 spy, 1 merc, -1 with no session.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSide()
    {
        try
        {
            return Exports.Current is { } session ? (ControlsFeature.Of(session).Merc ? 1 : 0) : -1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    /// <summary>1 while the controller did something in the last frame (show its button prompts), else 0.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int IsPadActive()
    {
        try
        {
            return Exports.Current is { } session && ControlsFeature.Of(session).Pad.Active ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Reads the settings in use. Works with no session.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void GetSettings(BridgeControlSettings* settings)
    {
        if (settings == null)
        {
            return;
        }

        try
        {
            *settings = ControlsMarshal.ToBridge(ControlsStore.Settings);
        }
        catch (Exception e)
        {
            *settings = default;
            s_lastError = e.ToString();
        }
    }

    /// <summary>Replaces the settings in use (values out of range are clamped). A running session takes them on its next frame.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetSettings(BridgeControlSettings* settings)
    {
        if (settings == null)
        {
            return;
        }

        try
        {
            ControlsStore.Settings = ControlsMarshal.FromBridge(in *settings, ControlsStore.Settings);
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
        }
    }

    /// <summary>Reads the settings from a JSON file (the defaults when it does not exist). Returns 1, or 0 with <see cref="LastError"/> set.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LoadSettings(char* path) => Try(path, static p => ControlsStore.Settings = ControlSettings.Load(p));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SaveSettings(char* path) => Try(path, static p => ControlsStore.Settings.Save(p));

    /// <summary>Reads both sides' bindings from a JSON file (the defaults when it does not exist). Returns 1, or 0 with <see cref="LastError"/> set.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LoadBindings(char* path) =>
        Try(path, static p => ControlsStore.Bindings = ControlBindings.Load(p, ControlsStore.Settings.MouseWheel));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SaveBindings(char* path) => Try(path, static p => ControlsStore.Bindings.Save(p));

    /// <summary>
    /// Copies what a key or pad control is bound to: the command of a key ("W", "Ctrl+F", "Double+H") in the
    /// original's binding grammar, or the action of a pad control ("A", "LB+Y", "B:Hold"). Returns its full length,
    /// 0 when unbound.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetBinding(int merc, int pad, char* key, char* buffer, int capacity)
    {
        try
        {
            SideBindings side = ControlsStore.Bindings.Of(merc != 0);
            string name = key == null ? "" : new string(key);
            string? bound;
            if (pad != 0)
            {
                side.Pad.TryGetValue(name, out bound);
            }
            else
            {
                side.Keys.Bindings.TryGetValue(BindingKey.TryParse(name, out BindingKey parsed) ? parsed.Canonical : name, out bound);
            }

            return Copy(bound ?? "", buffer, capacity);
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return Copy("", buffer, capacity);
        }
    }

    /// <summary>
    /// Binds a key or pad control (an empty command unbinds it). Returns 1, or 0 for a name that is not a key or a
    /// pad control. A running session plays the change from its next frame; save it with SaveBindings.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetBinding(int merc, int pad, char* key, char* command)
    {
        try
        {
            SideBindings side = ControlsStore.Bindings.Of(merc != 0);
            string name = key == null ? "" : new string(key);
            string text = command == null ? "" : new string(command);
            if (pad == 0)
            {
                return side.Keys.Bind(name, text) ? 1 : 0;
            }

            if (Array.IndexOf(ControlBindings.PadControls, name) < 0)
            {
                return 0;
            }

            side.SetPad(name, text.Trim());
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    /// <summary>Puts one side's keys and pad controls back to the defaults.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ResetBindings(int merc)
    {
        try
        {
            ControlsStore.Bindings.Reset(merc != 0, ControlsStore.Settings.MouseWheel);
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
        }
    }

    /// <summary>Copies every binding as the JSON a save writes (for a bindings menu to list). Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetBindingsJson(char* buffer, int capacity)
    {
        try
        {
            return Copy(ControlsStore.Bindings.ToJson(), buffer, capacity);
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return Copy("", buffer, capacity);
        }
    }

    /// <summary>
    /// Copies the binding name of a key code ("W", "Shift", "LeftMouse"; for a pad button its control name: "A",
    /// "LB"). Returns its full length, 0 for a spare code.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetKeyName(int code, char* buffer, int capacity)
    {
        try
        {
            return Copy(BridgeKeys.NameOf(code) ?? "", buffer, capacity);
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    /// <summary>
    /// Copies the last error of a settings or bindings call (a file that could not be read or written). Returns its
    /// full length. Failures that stop the session are reported through the bridge's own LastError instead.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity)
    {
        try
        {
            return Copy(s_lastError, buffer, capacity);
        }
        catch (Exception)
        {
            // Nothing may cross into native code, and there is nowhere left to report this.
            return 0;
        }
    }

    private static int Intern(string name)
    {
        if (!s_nameIds.TryGetValue(name, out int id))
        {
            id = s_names.Count;
            s_names.Add(name);
            s_nameIds[name] = id;
        }

        return id;
    }

    private static int Try(char* path, Action<string> action)
    {
        try
        {
            if (path == null)
            {
                s_lastError = "No path given.";
                return 0;
            }

            action(new string(path));
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    /// <summary>Copies <paramref name="text"/> with its terminator, as much as fits. Returns the full length.</summary>
    private static int Copy(string text, char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
            buffer[count] = '\0';
        }

        return text.Length;
    }
}

/// <summary>The settings between their class and the struct that crosses to C++.</summary>
public static class ControlsMarshal
{
    public static BridgeControlSettings ToBridge(ControlSettings s)
    {
        ArgumentNullException.ThrowIfNull(s);
        return new BridgeControlSettings
        {
            InvertY = s.InvertY ? 1 : 0,
            InvertXSpy = s.InvertXSpy ? 1 : 0,
            SpyRollFreeLook = s.SpyRollFreeLook ? 1 : 0,
            AutoHackComms = s.AutoHackComms ? 1 : 0,
            MouseWheelSpeed = s.MouseWheel == "Speed" ? 1 : 0,
            Widescreen = s.Widescreen ? 1 : 0,
            MercFovCap = s.MercFovCap,
            MouseScale = s.MouseScale,
            SpyKbm = ToBridge(s.SpyKbm),
            MercKbm = ToBridge(s.MercKbm),
            SpyPad = ToBridge(s.SpyPad),
            MercPad = ToBridge(s.MercPad),
            PadEnabled = s.Pad.Enabled ? 1 : 0,
            PadMove = s.Pad.Move ? 1 : 0,
            PadAim = s.Pad.Aim ? 1 : 0,
            PadBinds = s.Pad.Binds ? 1 : 0,
            PadSlowWalk = s.Pad.SlowWalk ? 1 : 0,
            PadSpyMove8 = s.Pad.SpyMove8 ? 1 : 0,
            PadMercMove8 = s.Pad.MercMove8 ? 1 : 0,
            PadDeadzoneLeft = s.Pad.DeadzoneLeft,
            PadDeadzoneRight = s.Pad.DeadzoneRight,
            PadVerticalScale = s.Pad.VerticalScale,
            PadRunThreshold = s.Pad.RunThreshold,
            PadTriggerThreshold = s.Pad.TriggerThreshold,
            PadLookScale = s.Pad.LookScale,
        };
    }

    /// <summary>New settings from the struct; what the struct does not carry (the clutch keys) comes from <paramref name="basis"/>.</summary>
    public static ControlSettings FromBridge(in BridgeControlSettings b, ControlSettings basis)
    {
        ArgumentNullException.ThrowIfNull(basis);
        var s = new ControlSettings
        {
            InvertY = b.InvertY != 0,
            InvertXSpy = b.InvertXSpy != 0,
            SpyRollFreeLook = b.SpyRollFreeLook != 0,
            AutoHackComms = b.AutoHackComms != 0,
            MouseWheel = b.MouseWheelSpeed != 0 ? "Speed" : "Gadget",
            Widescreen = b.Widescreen != 0,
            MercFovCap = b.MercFovCap,
            MouseScale = b.MouseScale,
            SpyKbm = FromBridge(in b.SpyKbm),
            MercKbm = FromBridge(in b.MercKbm),
            SpyPad = FromBridge(in b.SpyPad),
            MercPad = FromBridge(in b.MercPad),
            ClutchOneKey = basis.ClutchOneKey,
            ClutchTwoKey = basis.ClutchTwoKey,
            Pad = new PadTuning
            {
                Enabled = b.PadEnabled != 0,
                Move = b.PadMove != 0,
                Aim = b.PadAim != 0,
                Binds = b.PadBinds != 0,
                SlowWalk = b.PadSlowWalk != 0,
                SpyMove8 = b.PadSpyMove8 != 0,
                MercMove8 = b.PadMercMove8 != 0,
                DeadzoneLeft = b.PadDeadzoneLeft,
                DeadzoneRight = b.PadDeadzoneRight,
                VerticalScale = b.PadVerticalScale,
                RunThreshold = b.PadRunThreshold,
                TriggerThreshold = b.PadTriggerThreshold,
                LookScale = b.PadLookScale,
            },
        };
        s.Clamp();
        return s;
    }

    private static BridgeLookSettings ToBridge(LookSettings l) => new()
    {
        WalkHold = l.WalkBehavior == "Hold" ? 1 : 0,
        ScopeHold = l.ScopeBehavior == "Hold" ? 1 : 0,
        PerspectiveHold = l.PerspectiveBehavior == "Hold" ? 1 : 0,
        CloakHold = l.CloakBehavior == "Hold" ? 1 : 0,
        FreeLook = l.FreeLook,
        Look = l.Look,
        Scope = l.Scope,
        ScopeVertical = l.ScopeVertical,
        Vertical = l.Vertical,
        Cam = l.Cam,
        LookResponse = Math.Max(0, Array.IndexOf(ControlSettings.Responses, l.LookResponse)),
        GunLookResponse = Math.Max(0, Array.IndexOf(ControlSettings.Responses, l.GunLookResponse)),
        ClutchOne = l.ClutchOne,
        ClutchOneEnabled = l.ClutchOneEnabled ? 1 : 0,
        ClutchTwo = l.ClutchTwo,
        ClutchTwoEnabled = l.ClutchTwoEnabled ? 1 : 0,
        SwapLsLb = l.SwapLsLb ? 1 : 0,
        SwapRsRb = l.SwapRsRb ? 1 : 0,
    };

    private static LookSettings FromBridge(in BridgeLookSettings b) => new()
    {
        WalkBehavior = b.WalkHold != 0 ? "Hold" : "Toggle",
        ScopeBehavior = b.ScopeHold != 0 ? "Hold" : "Toggle",
        PerspectiveBehavior = b.PerspectiveHold != 0 ? "Hold" : "Toggle",
        CloakBehavior = b.CloakHold != 0 ? "Hold" : "Toggle",
        FreeLook = b.FreeLook,
        Look = b.Look,
        Scope = b.Scope,
        ScopeVertical = b.ScopeVertical,
        Vertical = b.Vertical,
        Cam = b.Cam,
        LookResponse = Response(b.LookResponse),
        GunLookResponse = Response(b.GunLookResponse),
        ClutchOne = b.ClutchOne,
        ClutchOneEnabled = b.ClutchOneEnabled != 0,
        ClutchTwo = b.ClutchTwo,
        ClutchTwoEnabled = b.ClutchTwoEnabled != 0,
        SwapLsLb = b.SwapLsLb != 0,
        SwapRsRb = b.SwapRsRb != 0,
    };

    private static string Response(int index) =>
        (uint)index < (uint)ControlSettings.Responses.Length ? ControlSettings.Responses[index] : ControlSettings.Responses[0];
}
