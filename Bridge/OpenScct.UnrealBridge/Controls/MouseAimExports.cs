using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Controls;

namespace OpenScct.UnrealBridge;

/// <summary>
/// What the Options screen's mouse and aim section needs beyond the controls table (<see cref="ControlsExports"/>):
/// the clutch keys, which the settings struct does not carry, and the default settings to reset to. Unreal asks for
/// <see cref="MouseAimExports.GetApi"/> once (type <c>OpenScct.UnrealBridge.MouseAimExports, OpenScct.UnrealBridge</c>)
/// and keeps the table. Source/OpenSCCT/Runtime/ScctMouseAimApi.h mirrors it field for field. Strings are UTF-16,
/// zero terminated; a text handed back is copied as far as it fits and its full length returned. None of these needs
/// a session.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MouseAimApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetClutchKey;
    public delegate* unmanaged[Cdecl]<char*, char*, int, int> CheckClutchKey;
    public delegate* unmanaged[Cdecl]<int, char*, int> SetClutchKey;
    public delegate* unmanaged[Cdecl]<BridgeControlSettings*, void> GetDefaults;
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

/// <summary>The clutch keys of the settings in use (<see cref="ControlSettings.ClutchOneKey"/>, ClutchTwoKey).</summary>
public static class ClutchKeys
{
    /// <summary>The key of the first (1) or second (2) clutch, as a binding name; "" for none.</summary>
    public static string Get(int which) => which == 2 ? ControlsStore.Settings.ClutchTwoKey : ControlsStore.Settings.ClutchOneKey;

    /// <summary>
    /// Why <paramref name="key"/> cannot be a clutch key, or "" when it can. A clutch acts while its key is held, so
    /// it needs a key or mouse button that is let go: not a wheel direction, and not a double tap.
    /// </summary>
    public static string Check(string key)
    {
        if (key.Length == 0)
        {
            return "";
        }

        if (!BindingKey.TryParse(key, out BindingKey parsed))
        {
            return $"'{key}' is not the name of a key.";
        }

        if (parsed.Modifiers.HasFlag(BindingModifiers.Double))
        {
            return "A sensitivity modifier is held, so it cannot be a double tap.";
        }

        return parsed.IsPulse ? "A sensitivity modifier is held, so it needs a key or mouse button, not the mouse wheel." : "";
    }

    /// <summary>Sets a clutch key ("" for none) in the settings in use; a running match reads it from its next frame. False with the reason.</summary>
    public static bool Set(int which, string key, out string error)
    {
        key = (key ?? "").Trim();
        if (BindingKey.TryParse(key, out BindingKey parsed))
        {
            key = parsed.Canonical;
        }

        error = Check(key);
        if (error.Length > 0)
        {
            return false;
        }

        // The settings are replaced, never edited in place, so readers see the change.
        ControlSettings copy = ControlSettings.FromJson(ControlsStore.Settings.ToJson());
        if (which == 2)
        {
            copy.ClutchTwoKey = key;
        }
        else
        {
            copy.ClutchOneKey = key;
        }

        ControlsStore.Settings = copy;
        return true;
    }
}

public static unsafe class MouseAimExports
{
    public const int ApiVersion = 1;

    private static string s_lastError = "";

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(MouseAimApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetClutchKey = &GetClutchKey;
        api->CheckClutchKey = &CheckClutchKey;
        api->SetClutchKey = &SetClutchKey;
        api->GetDefaults = &GetDefaults;
        api->LastError = &LastError;
        return ApiVersion;
    }

    /// <summary>The key of clutch 1 or 2 in the settings in use. Returns the full length (0: none).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetClutchKey(int which, char* buffer, int capacity) => Guard(() => Copy(ClutchKeys.Get(which), buffer, capacity));

    /// <summary>Why the key cannot be a clutch key ("" when it can). Returns the full length (0: it can).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CheckClutchKey(char* key, char* buffer, int capacity) => Guard(() => Copy(ClutchKeys.Check(Text(key)), buffer, capacity));

    /// <summary>Sets the key of clutch 1 or 2 (empty: none). Returns 1, or 0 with the reason in LastError.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetClutchKey(int which, char* key) => Guard(() =>
    {
        if (ClutchKeys.Set(which, Text(key), out string error))
        {
            return 1;
        }

        s_lastError = error;
        return 0;
    });

    /// <summary>The settings a new player starts with (what a reset goes back to).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void GetDefaults(BridgeControlSettings* settings)
    {
        if (settings == null)
        {
            return;
        }

        try
        {
            var defaults = new ControlSettings();
            defaults.Clamp();
            *settings = ControlsMarshal.ToBridge(defaults);
        }
        catch (Exception e)
        {
            *settings = default;
            s_lastError = e.ToString();
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity)
    {
        try
        {
            return Copy(s_lastError, buffer, capacity);
        }
        catch (Exception)
        {
            return 0;
        }
    }

    private static string Text(char* text) => text == null ? "" : new string(text);

    private static int Guard(Func<int> body)
    {
        try
        {
            return body();
        }
        catch (Exception e)
        {
            // Nothing may cross into native code; a fault here never ends a match being played under the menu.
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
