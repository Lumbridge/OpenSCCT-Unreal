using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The table of entry points Unreal calls. Unreal asks for <see cref="GetApi"/> once and keeps the table. Every
/// entry catches its exceptions (none may cross into native code) and keeps the text for <see cref="LastError"/>.
/// Strings are UTF-16, zero terminated.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int> Configure;
    public delegate* unmanaged[Cdecl]<char*, char*, int, int, int> StartLocal;
    public delegate* unmanaged[Cdecl]<void> Stop;
    public delegate* unmanaged[Cdecl]<int> GetState;
    public delegate* unmanaged[Cdecl]<int> Update;
    public delegate* unmanaged[Cdecl]<BridgeInput*, void> SetInput;
    public delegate* unmanaged[Cdecl]<char*, int, void> Command;
    public delegate* unmanaged[Cdecl]<BridgePawn*, int, int> GetPawns;
    public delegate* unmanaged[Cdecl]<BridgeView*, int> GetView;
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

public static unsafe class Exports
{
    private static BridgeSession? s_session;
    private static string s_lastError = "";
    private static bool s_failed;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(BridgeApi* api)
    {
        api->Version = BridgeSession.ApiVersion;
        api->Configure = &Configure;
        api->StartLocal = &StartLocal;
        api->Stop = &Stop;
        api->GetState = &GetState;
        api->Update = &Update;
        api->SetInput = &SetInput;
        api->Command = &Command;
        api->GetPawns = &GetPawns;
        api->GetView = &GetView;
        api->LastError = &LastError;
        return BridgeSession.ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Configure(char* contentRoot) => Guard(() => BridgeSession.Configure(new string(contentRoot)));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartLocal(char* map, char* mode, int merc, int bots)
    {
        string mapName = new(map), modeName = new(mode);
        return Guard(() =>
        {
            s_session?.Dispose();
            s_session = null;
            s_failed = false;
            s_session = BridgeSession.StartLocal(mapName, modeName, merc != 0, bots);
        });
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Stop() => Guard(() =>
    {
        s_session?.Dispose();
        s_session = null;
        s_failed = false;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState() =>
        (int)(s_failed ? BridgeState.Failed : s_session is null ? BridgeState.Idle : BridgeState.Playing);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Update()
    {
        if (s_session is not { } session || s_failed)
        {
            return 0;
        }

        try
        {
            int ticks = session.Update();
            if (session.Failure is { } failure)
            {
                Fail(failure);
            }

            return ticks;
        }
        catch (Exception e)
        {
            Fail(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetInput(BridgeInput* input)
    {
        if (s_session is { } session && !s_failed)
        {
            session.SetInput(in *input);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Command(char* name, int arg)
    {
        if (s_session is { } session && !s_failed)
        {
            session.Command(new string(name), arg);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPawns(BridgePawn* pawns, int capacity)
    {
        if (s_session is not { } session || s_failed || capacity <= 0)
        {
            return 0;
        }

        try
        {
            return session.GetPawns(new Span<BridgePawn>(pawns, capacity));
        }
        catch (Exception e)
        {
            Fail(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetView(BridgeView* view)
    {
        *view = default;
        if (s_session is not { } session || s_failed)
        {
            return 0;
        }

        try
        {
            *view = session.GetView();
            return view->Valid;
        }
        catch (Exception e)
        {
            Fail(e.ToString());
            return 0;
        }
    }

    /// <summary>Copies the last error into <paramref name="buffer"/>. Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(s_lastError.Length, capacity - 1);
            s_lastError.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
            buffer[count] = '\0';
        }

        return s_lastError.Length;
    }

    private static int Guard(Action action)
    {
        try
        {
            action();
            return 1;
        }
        catch (Exception e)
        {
            Fail(e.ToString());
            return 0;
        }
    }

    private static void Fail(string message)
    {
        s_lastError = message;
        s_failed = true;
    }
}
