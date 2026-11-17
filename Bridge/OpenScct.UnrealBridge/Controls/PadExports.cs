using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Controls;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The controller's extras beyond the controls table (<see cref="ControlsExports"/>): the vibration settings (which
/// the settings struct does not carry), the match's rumbles for Unreal to play, and the capture of a pad binding
/// gesture (a button, Hold B or a combination). Unreal asks for <see cref="PadExports.GetApi"/> once (type
/// <c>OpenScct.UnrealBridge.PadExports, OpenScct.UnrealBridge</c>) and keeps the table.
/// Source/OpenSCCT/Runtime/ScctPadApi.h mirrors it field for field. Strings are UTF-16, zero terminated; a text handed
/// back is copied as far as it fits and its full length returned.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct PadApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int*, float*, int> GetRumble;
    public delegate* unmanaged[Cdecl]<int, float, int> SetRumble;
    public delegate* unmanaged[Cdecl]<int> Begin;
    public delegate* unmanaged[Cdecl]<BridgeRumble*, int, int> DrainRumbles;
    public delegate* unmanaged[Cdecl]<char*, float, int> RaiseForCheck;
    public delegate* unmanaged[Cdecl]<void> CaptureBegin;
    public delegate* unmanaged[Cdecl]<char*, float, char*, int, int*, int> CaptureUpdate;
    public delegate* unmanaged[Cdecl]<BridgeRumble*, void> TestRumble;
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

/// <summary>States of <see cref="PadApi.CaptureUpdate"/>.</summary>
public static class PadCaptureState
{
    /// <summary>Waiting for every control to be let go before the gesture.</summary>
    public const int Releasing = 0;

    /// <summary>Waiting for the gesture; the text is what is held so far (empty for nothing).</summary>
    public const int Listening = 1;

    /// <summary>Done: the text is the gesture's control name ("A", "LS+Y", "B:Hold").</summary>
    public const int Captured = 2;

    /// <summary>A combination that cannot be bound: the text says so; let go to try again.</summary>
    public const int Refused = 3;
}

public static unsafe class PadExports
{
    public const int ApiVersion = 1;

    private static string s_lastError = "";
    private static PadBindingCapture? s_capture;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(PadApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetRumble = &GetRumble;
        api->SetRumble = &SetRumble;
        api->Begin = &Begin;
        api->DrainRumbles = &DrainRumbles;
        api->RaiseForCheck = &RaiseForCheck;
        api->CaptureBegin = &CaptureBegin;
        api->CaptureUpdate = &CaptureUpdate;
        api->TestRumble = &TestRumble;
        api->LastError = &LastError;
        return ApiVersion;
    }

    /// <summary>The vibration settings in use: on (1) or off, and the strength 0 to 1. Returns 1.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRumble(int* enabled, float* strength) => Guard(() =>
    {
        PadTuning pad = ControlsStore.Settings.Pad;
        if (enabled != null)
        {
            *enabled = pad.Rumble ? 1 : 0;
        }

        if (strength != null)
        {
            *strength = pad.RumbleStrength;
        }

        return 1;
    });

    /// <summary>Sets the vibration in the settings in use (the controls table's SaveSettings writes it). Returns 1.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetRumble(int enabled, float strength) => Guard(() =>
    {
        // The settings are replaced, never edited in place, so readers see the change.
        ControlSettings copy = ControlSettings.FromJson(ControlsStore.Settings.ToJson());
        copy.Pad.Rumble = enabled != 0;
        copy.Pad.RumbleStrength = strength;
        ControlsStore.Settings = copy;
        return 1;
    });

    /// <summary>Starts the rumble of the running session. Returns 1 when a session runs.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Begin() => Feature() is null ? 0 : 1;

    /// <summary>Takes the waiting rumbles, oldest first. Returns the count written.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int DrainRumbles(BridgeRumble* rumbles, int capacity) => Guard(() =>
        rumbles == null || capacity <= 0 || Feature() is not { } feature ? 0 : feature.Drain(new Span<BridgeRumble>(rumbles, capacity)));

    /// <summary>
    /// Raises a Vibration_* notification on the local player's controller through the simulation, for a check: it
    /// comes back through DrainRumbles as a real one does. Returns 1, or 0 without a local player.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RaiseForCheck(char* what, float a) => Guard(() =>
    {
        if (Feature() is not { } feature)
        {
            s_lastError = "No match is running.";
            return 0;
        }

        if (!feature.RaiseForCheck(Text(what), a))
        {
            s_lastError = "The match has no local player.";
            return 0;
        }

        return 1;
    });

    /// <summary>Starts a new capture (the controls held now must be let go first).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void CaptureBegin()
    {
        s_capture = new PadBindingCapture();
    }

    /// <summary>
    /// One frame of the capture: <paramref name="down"/> is the pad controls held now ("A", "LB", "LT"...), LF between
    /// them; <paramref name="delta"/> the frame's seconds. Writes the text of <see cref="PadCaptureState"/> and
    /// returns its full length.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CaptureUpdate(char* down, float delta, char* buffer, int capacity, int* state) => Guard(() =>
    {
        s_capture ??= new PadBindingCapture();
        var held = new HashSet<string>(Text(down).Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries), StringComparer.Ordinal);
        bool wasArmed = s_capture.Armed;
        string? captured = s_capture.Update(held, delta);
        int now;
        string text;
        if (captured is not null)
        {
            now = PadCaptureState.Captured;
            text = captured;
        }
        else if (!s_capture.Armed && wasArmed && s_capture.Error is { } error)
        {
            now = PadCaptureState.Refused;
            text = error;
        }
        else
        {
            now = s_capture.Armed ? PadCaptureState.Listening : PadCaptureState.Releasing;
            text = s_capture.Preview ?? "";
        }

        if (state != null)
        {
            *state = now;
        }

        return Copy(text, buffer, capacity);
    });

    /// <summary>The Options screen's TEST: the blast's rumble at the strength in use (vibration on or off).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void TestRumble(BridgeRumble* rumble)
    {
        if (rumble == null)
        {
            return;
        }

        try
        {
            float s = Math.Clamp(ControlsStore.Settings.Pad.RumbleStrength, 0f, 1f);
            RumbleShape shape = PadRumbleRules.Test;
            *rumble = new BridgeRumble
            {
                Priority = shape.Priority,
                Low = shape.Low / 65535f * s,
                High = shape.High / 65535f * s,
                Seconds = shape.Seconds,
                Strength = s,
                Play = 1,
                Result = BridgeRumbleResult.Played,
            };
            const string name = "Test";
            for (int i = 0; i < name.Length; i++)
            {
                rumble->Name[i] = name[i];
            }
        }
        catch (Exception e)
        {
            *rumble = default;
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

    /// <summary>The rumble of the running session, null when none runs.</summary>
    private static RumbleFeature? Feature()
    {
        if (Exports.Current is not { } session)
        {
            return null;
        }

        try
        {
            return RumbleFeature.Of(session);
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return null;
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
