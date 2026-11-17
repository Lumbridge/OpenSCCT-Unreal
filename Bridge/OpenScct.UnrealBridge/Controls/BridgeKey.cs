using System;
using System.Collections.Generic;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>
/// The physical keys and buttons Unreal reports to the bridge, one byte each in <c>BridgeRawInput.Keys</c>. The
/// members are named after the original's input keys (the <c>IK_</c> names of its key layouts, as OpenSCCT's
/// game/Scripts/Input/GodotKeys.cs lists them), so a binding table reads like the original's ini: the name of a code
/// is its binding key (<see cref="BridgeKeys.NameOf"/>), with <c>D0</c>..<c>D9</c> for the digit row and a
/// <c>Pad</c> prefix on the controller's buttons. Source/OpenSCCT/Runtime/ScctControlsApi.h mirrors the values and
/// maps each to its Unreal FKey; change both together.
/// </summary>
public enum BridgeKey
{
    A = 0, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    D0 = 26, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    F1 = 36, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    NumPad0 = 48, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8, NumPad9,

    /// <summary>Either Shift key (the original has one name for both; the same for Ctrl and Alt).</summary>
    Shift = 58,
    Ctrl, Alt, Space, Tab, Escape, Enter, Backspace, PageUp, PageDown, Home, End, Insert, Delete, Up, Down, Left, Right,
    Tilde = 76, CapsLock, Pause, NumLock, ScrollLock,

    /// <summary>The numpad's * + - / and . keys, by the original's names.</summary>
    GreyStar = 81,
    GreyPlus, GreyMinus, GreySlash, NumPadPeriod,
    Semicolon = 86, EqualSign, Comma, Minus, Period, Slash, LeftBracket, Backslash, RightBracket, SingleQuote,
    LeftMouse = 96, RightMouse, MiddleMouse, MouseX1, MouseX2,

    /// <summary>The wheel has no release: its byte is the number of notches turned since the last frame.</summary>
    MouseWheelUp = 101,
    MouseWheelDown,

    // The controller's buttons (Xbox names). The triggers and sticks are axes of BridgeRawInput.
    PadA = 103, PadB, PadX, PadY, PadLB, PadRB, PadLS, PadRS, PadDpadUp, PadDpadDown, PadDpadLeft, PadDpadRight,
    PadStart, PadBack, PadP1, PadP2, PadP3, PadP4,
}

public static class BridgeKeys
{
    /// <summary>The length of <c>BridgeRawInput.Keys</c> (codes past the last key are spare).</summary>
    public const int Count = 128;

    public const int FirstPad = (int)BridgeKey.PadA;
    public const int LastPad = (int)BridgeKey.PadP4;

    private static readonly string?[] s_names = BuildNames();
    private static readonly Dictionary<string, BridgeKey> s_codes = BuildCodes();

    /// <summary>
    /// The binding name of a code: the original's key name for the keyboard and mouse ("W", "1", "Shift",
    /// "LeftMouse", "MouseWheelUp"), the control name for a pad button ("A", "LB", "DpadUp"). Null for a spare code.
    /// </summary>
    public static string? NameOf(int code) => (uint)code < Count ? s_names[code] : null;

    public static bool IsPad(int code) => code is >= FirstPad and <= LastPad;

    public static bool IsWheel(int code) => code is (int)BridgeKey.MouseWheelUp or (int)BridgeKey.MouseWheelDown;

    /// <summary>The code of a keyboard or mouse binding name (not the pad's: "A" is the A key).</summary>
    public static bool TryCode(string name, out BridgeKey key) => s_codes.TryGetValue(name, out key);

    private static string?[] BuildNames()
    {
        var names = new string?[Count];
        foreach (BridgeKey key in Enum.GetValues<BridgeKey>())
        {
            string name = key.ToString();
            if (key is >= BridgeKey.D0 and <= BridgeKey.D9)
            {
                name = name[1..];
            }
            else if (IsPad((int)key))
            {
                name = name[3..];
            }
            else if (key == BridgeKey.EqualSign)
            {
                // The original's name; "Equals" cannot be an enum member's name.
                name = "Equals";
            }

            names[(int)key] = name;
        }

        return names;
    }

    private static Dictionary<string, BridgeKey> BuildCodes()
    {
        var codes = new Dictionary<string, BridgeKey>(StringComparer.OrdinalIgnoreCase);
        for (int code = 0; code < FirstPad; code++)
        {
            if (s_names[code] is { } name)
            {
                codes[name] = (BridgeKey)code;
            }
        }

        return codes;
    }
}
