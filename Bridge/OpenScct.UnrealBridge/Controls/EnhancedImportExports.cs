using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using OpenScct.UnrealBridge.Controls;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The Enhanced import's table (Source/OpenSCCT/Runtime/ScctEnhancedImportApi.h mirrors it): "Import from Enhanced
/// SCCT Versus" on the settings page and -ScctImportEnhanced. An import puts the controls and the key layouts it found
/// in use at once (ControlsStore, which Unreal then reads back and saves) and hands Unreal the rest as text lines:
/// <code>
///   source &lt;folder the import read&gt;
///   set &lt;settings console key&gt;=&lt;value&gt;   one per setting Unreal keeps (volumes, display, graphics, HUD, voice)
///   loadout &lt;kit text&gt;                     the profile's gear (ScctProfileOptions ApplyLoadoutText)
///   note &lt;line&gt;                            the report for the player, a line per group taken over or left out
///   controls &lt;line&gt;                        what the controls became, for the log
///   error &lt;reason&gt;                         nothing was imported (the only line then)
/// </code>
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct EnhancedImportApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int, char*, int, int> Import;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetUsualProfileFolder;
}

public static unsafe class EnhancedImportExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(EnhancedImportApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Import = &Import;
        api->GetUsualProfileFolder = &GetUsualProfileFolder;
        return ApiVersion;
    }

    /// <summary>
    /// Imports from <paramref name="folder"/> (any folder of an install, or a profile folder; empty with
    /// <paramref name="withoutInstall"/>: the profile on this PC) and, unless <paramref name="apply"/> is false,
    /// puts the controls and bindings in use. Returns the lines described on the class.
    /// </summary>
    public static string ImportText(string folder, bool withoutInstall, bool apply = true)
    {
        EnhancedImport? source;
        string from = withoutInstall ? EnhancedImport.UsualProfileFolder : folder;
        try
        {
            source = withoutInstall ? EnhancedImport.FindWithoutInstall() : EnhancedImport.Find(folder);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return "error Could not import from " + from + ": " + e.Message;
        }

        if (source is null)
        {
            return withoutInstall
                ? "error No Enhanced SCCT Versus profile on this PC (looked in " + from + ")."
                : "error Nothing to import: no Enhanced SCCT Versus install was found at " + from + ". Choose the folder that holds its System folder.";
        }

        EnhancedImportResult result;
        try
        {
            result = source.Apply(ControlsStore.Settings, ControlsStore.Bindings);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return "error Could not import from " + from + ": " + e.Message;
        }

        if (apply)
        {
            ControlsStore.Settings = result.Controls;
            ControlsStore.Bindings = result.Bindings;
        }

        var text = new StringBuilder();
        text.Append("source ").Append(source.Root ?? from).Append('\n');
        foreach (ImportedSetting setting in result.Settings)
        {
            text.Append("set ").Append(setting.Key).Append('=').Append(setting.Value).Append('\n');
        }

        if (result.Loadout.Length > 0)
        {
            text.Append("loadout ").Append(result.Loadout).Append('\n');
        }

        foreach (string line in result.Report)
        {
            text.Append("note ").Append(line).Append('\n');
        }

        foreach (string line in Describe(result))
        {
            text.Append("controls ").Append(line).Append('\n');
        }

        return text.ToString();
    }

    /// <summary>The controls and the key layouts as they came out, a few lines for the log.</summary>
    public static IEnumerable<string> Describe(EnhancedImportResult result)
    {
        ControlSettings c = result.Controls;
        static string Look(LookSettings l) => FormattableString.Invariant($"free {l.FreeLook:0.###} look {l.Look:0.###} scope {l.Scope:0.###} cam {l.Cam:0.###} {l.LookResponse}");
        yield return FormattableString.Invariant($"invert_y {c.InvertY}, invert_x_spy {c.InvertXSpy}, mouse_wheel {c.MouseWheel}, widescreen {c.Widescreen}, ws_fov {c.MercFovCap:0.#}");
        yield return "spy mouse " + Look(c.SpyKbm) + "; merc mouse " + Look(c.MercKbm);
        yield return "spy pad " + Look(c.SpyPad) + "; merc pad " + Look(c.MercPad);
        yield return FormattableString.Invariant($"controller enabled {c.Pad.Enabled}, deadzones {c.Pad.DeadzoneLeft:0.###}/{c.Pad.DeadzoneRight:0.###}, rumble {c.Pad.Rumble} {c.Pad.RumbleStrength:0.##}");
        foreach (bool merc in new[] { false, true })
        {
            KeyLayout keys = result.Bindings.KeysOf(merc);
            var shown = new List<string>();
            foreach (string key in new[] { "W", "A", "S", "D", "Space", "LeftMouse", "RightMouse", "H", "Double+H", "I", "K", "X", "MouseWheelUp", "F8" })
            {
                if (keys.Bindings.TryGetValue(key, out string? command))
                {
                    shown.Add(key + "=" + command);
                }
            }

            yield return $"{(merc ? "merc" : "spy")} keys ({keys.Bindings.Count} bound, split {result.Bindings.Split}): {string.Join("; ", shown)}";
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Import(char* folder, int withoutInstall, char* buffer, int capacity)
    {
        try
        {
            return Write(ImportText(folder == null ? "" : new string(folder), withoutInstall != 0), buffer, capacity);
        }
        catch (Exception e)
        {
            // Nothing may cross into native code.
            return Write("error " + e.GetType().Name + ": " + e.Message, buffer, capacity);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetUsualProfileFolder(char* buffer, int capacity)
    {
        try
        {
            return Write(EnhancedImport.UsualProfileFolder, buffer, capacity);
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>Writes the text, cut to fit and zero terminated. Returns its full length.</summary>
    private static int Write(string text, char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, capacity));
            buffer[count] = '\0';
        }

        return text.Length;
    }
}
