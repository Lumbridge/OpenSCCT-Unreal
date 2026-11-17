using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Controls;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The key bindings editor's entry points (<see cref="BindingsEditor"/>). Unreal asks for <see cref="BindingsExports.GetApi"/>
/// once (type <c>OpenScct.UnrealBridge.BindingsExports, OpenScct.UnrealBridge</c>) and keeps the table.
/// Source/OpenSCCT/Runtime/ScctBindingsApi.h mirrors it field for field. Strings are UTF-16, zero terminated; a text
/// handed back is copied as far as it fits and its full length returned. None of these needs a session.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BindingsApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int> Begin;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetRows;
    public delegate* unmanaged[Cdecl]<int, char*, char*, char*, int, int> Check;
    public delegate* unmanaged[Cdecl]<int, char*, char*, char*, int*, char*, int, int> Conflict;
    public delegate* unmanaged[Cdecl]<int, char*, char*, char*, int, char*, int, int> Commit;
    public delegate* unmanaged[Cdecl]<int, char*, char*, void> Clear;
    public delegate* unmanaged[Cdecl]<int, void> ResetKeys;
    public delegate* unmanaged[Cdecl]<int> GetSplit;
    public delegate* unmanaged[Cdecl]<int, void> SetSplit;
    public delegate* unmanaged[Cdecl]<int> HasEdits;
    public delegate* unmanaged[Cdecl]<int> HasUnsaved;
    public delegate* unmanaged[Cdecl]<void> Apply;
    public delegate* unmanaged[Cdecl]<void> Discard;
    public delegate* unmanaged[Cdecl]<int, char*, char*, int, int> Describe;
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

/// <summary>Bits of the flags <see cref="BindingsApi.Conflict"/> hands back.</summary>
public static class BindingsConflictFlags
{
    /// <summary>The key is already one of the action's: nothing to do.</summary>
    public const int AlreadyBound = 1;

    /// <summary>The key and the slot being changed can trade what they do.</summary>
    public const int CanSwap = 2;
}

public static unsafe class BindingsExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(BindingsApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Begin = &Begin;
        api->GetRows = &GetRows;
        api->Check = &Check;
        api->Conflict = &Conflict;
        api->Commit = &Commit;
        api->Clear = &Clear;
        api->ResetKeys = &ResetKeys;
        api->GetSplit = &GetSplit;
        api->SetSplit = &SetSplit;
        api->HasEdits = &HasEdits;
        api->HasUnsaved = &HasUnsaved;
        api->Apply = &Apply;
        api->Discard = &Discard;
        api->Describe = &Describe;
        api->LastError = &LastError;
        return ApiVersion;
    }

    /// <summary>Starts editing from the bindings in play, keeping edits already held. Returns 1.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Begin() => Guard(() => { BindingsEditor.Begin(); return 1; });

    /// <summary>The side's rows (BindingsEditor.Rows). Returns the full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRows(int merc, char* buffer, int capacity) =>
        Guard(() => Copy(BindingsEditor.Rows(merc != 0), buffer, capacity));

    /// <summary>Why the key cannot take the action ("" when it can). Returns the full length (0: it can).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Check(int merc, char* action, char* key, char* buffer, int capacity) =>
        Guard(() => Copy(BindingsEditor.Check(merc != 0, Text(action), Text(key)), buffer, capacity));

    /// <summary>
    /// What the key does apart from the action ("" when free or already the action's); <paramref name="flags"/> gets
    /// <see cref="BindingsConflictFlags"/>. Returns the full length.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Conflict(int merc, char* action, char* oldKey, char* key, int* flags, char* buffer, int capacity) => Guard(() =>
    {
        string text = BindingsEditor.Conflict(merc != 0, Text(action), Text(oldKey), Text(key), out bool already, out bool canSwap);
        if (flags != null)
        {
            *flags = (already ? BindingsConflictFlags.AlreadyBound : 0) | (canSwap ? BindingsConflictFlags.CanSwap : 0);
        }

        return Copy(text, buffer, capacity);
    });

    /// <summary>
    /// Binds the key to the action in place of <paramref name="oldKey"/> (empty: as one more key); mode is
    /// <see cref="BindingCommitMode"/>. Returns 1, or 0 with the reason in <paramref name="error"/>.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Commit(int merc, char* action, char* oldKey, char* key, int mode, char* error, int capacity) => Guard(() =>
    {
        bool done = BindingsEditor.Commit(merc != 0, Text(action), Text(oldKey), Text(key), (BindingCommitMode)Math.Clamp(mode, 0, 2), out string why);
        Copy(why, error, capacity);
        return done ? 1 : 0;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Clear(int merc, char* action, char* key) => Guard(() => { BindingsEditor.Clear(merc != 0, Text(action), Text(key)); return 0; });

    /// <summary>Puts the side's keys (the shared keys while the layouts are shared) back to the defaults in the draft.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ResetKeys(int merc) => Guard(() => { BindingsEditor.ResetKeys(merc != 0); return 0; });

    /// <summary>1 when each side has its own key layout in the draft, 0 when they share the spy's.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSplit() => Guard(() => BindingsEditor.Draft.Split ? 1 : 0);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetSplit(int split) => Guard(() => { BindingsEditor.SetSplit(split != 0); return 0; });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int HasEdits() => Guard(() => BindingsEditor.HasEdits ? 1 : 0);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int HasUnsaved() => Guard(() => BindingsEditor.HasUnsaved ? 1 : 0);

    /// <summary>Puts the draft in play; a running match plays it from its next frame. Save it with ControlsApi.SaveBindings.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Apply() => Guard(() => { BindingsEditor.Apply(); return 0; });

    /// <summary>Drops the draft and puts the file's bindings back in play.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Discard() => Guard(() => { BindingsEditor.Discard(); return 0; });

    /// <summary>What a key does in the draft, by action names. Returns the full length (0: nothing).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Describe(int merc, char* key, char* buffer, int capacity) =>
        Guard(() => Copy(BindingDraft.CommandOf(BindingsEditor.LayoutOf(merc != 0), Text(key)).Length == 0
            ? "" : BindingDraft.Describe(BindingsEditor.LayoutOf(merc != 0), Text(key), merc != 0), buffer, capacity));

    /// <summary>The last failure inside this table (an editor fault never stops a match). Returns the full length.</summary>
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

    private static string s_lastError = "";

    private static string Text(char* text) => text == null ? "" : new string(text);

    private static int Guard(Func<int> body)
    {
        try
        {
            return body();
        }
        catch (Exception e)
        {
            // Nothing may cross into native code. An editor fault is kept for LastError rather than reported as the
            // session's failure, which would end a match being played under the menu.
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
