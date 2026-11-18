using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Backdrop;

namespace OpenScct.UnrealBridge;

/// <summary>What the backdrop match is doing, for the menu's director (FScctBackdropStatus in Runtime/ScctBackdropApi.h).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BackdropStatus
{
    /// <summary><see cref="BackdropPrepareState"/> of the match being made ready.</summary>
    public int Prepare;

    /// <summary>The id of the backdrop session in the bridge's slot, 0 when the slot holds none (or holds a real match).</summary>
    public int SessionId;

    /// <summary>Client ticks the session in the slot has run.</summary>
    public int Ticks;

    /// <summary>Pawns alive on the client.</summary>
    public int PawnsAlive;

    /// <summary>1 when the spectator has a view to draw.</summary>
    public int ViewValid;

    /// <summary>1 when the match is over (a side has won, or the end banner shows).</summary>
    public int MatchEnded;

    /// <summary>The actor id of the pawn being watched, 0 for none.</summary>
    public int TargetActorId;

    /// <summary>1 when the watched pawn is a mercenary.</summary>
    public int TargetMerc;
}

/// <summary>The backdrop's table (Source/OpenSCCT/Runtime/ScctBackdropApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BackdropApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int> CanPlay;
    public delegate* unmanaged[Cdecl]<char*, int> Begin;
    public delegate* unmanaged[Cdecl]<int> Adopt;
    public delegate* unmanaged[Cdecl]<void> Cancel;
    public delegate* unmanaged[Cdecl]<BackdropStatus*, int> GetStatus;
    public delegate* unmanaged[Cdecl]<void> NextTarget;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetError;
}

/// <summary>
/// The entry points of the menu's live backdrop (<see cref="BackdropMatch"/>): whether a map can be one, making its
/// match ready on a worker, adopting the ready match into the bridge's session slot on the game thread, and what it
/// is doing once it plays. Every entry catches its exceptions; none may cross into native code.
/// </summary>
public static unsafe class BackdropExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(BackdropApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->CanPlay = &CanPlay;
        api->Begin = &Begin;
        api->Adopt = &Adopt;
        api->Cancel = &Cancel;
        api->GetStatus = &GetStatus;
        api->NextTarget = &NextTarget;
        api->GetError = &GetError;
        return ApiVersion;
    }

    /// <summary>1 when a backdrop match can start on the map at once (<see cref="BackdropMatch.CanPlay"/>).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CanPlay(char* map)
    {
        try
        {
            return map != null && BackdropMatch.CanPlay(new string(map)) ? 1 : 0;
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>Starts making the map's match ready on a worker. Returns 1 when it was started (or already is).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Begin(char* map)
    {
        try
        {
            if (map == null)
            {
                return 0;
            }

            BackdropMatch.Begin(new string(map));
            return 1;
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>
    /// Game thread: puts the ready match in the bridge's session slot (ending a session already there) and returns
    /// its id; 0 when none is ready or it could not be adopted.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Adopt()
    {
        try
        {
            return BackdropMatch.Take() is { } session ? Exports.AdoptPrepared(session) : 0;
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>Drops the match being made ready or waiting. The session in the slot is not touched.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Cancel()
    {
        try
        {
            BackdropMatch.Cancel();
        }
        catch (Exception)
        {
        }
    }

    /// <summary>Writes what the backdrop is doing. Returns 1 when a backdrop session plays in the slot.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetStatus(BackdropStatus* status)
    {
        if (status == null)
        {
            return 0;
        }

        *status = default;
        try
        {
            status->Prepare = (int)BackdropMatch.State;
            if (Exports.Current is not { IsBackdrop: true, IsDisposed: false } session)
            {
                return 0;
            }

            status->SessionId = session.Id;
            status->Ticks = (int)Math.Min(session.Ticks, int.MaxValue);
            status->ViewValid = session.Session.View.Valid ? 1 : 0;
            int alive = 0;
            foreach (SBasePawn pawn in session.Session.Client.World.Level.AllActors<SBasePawn>())
            {
                if (pawn is { bDeleteMe: false, Health: > 0 })
                {
                    alive++;
                }
            }

            status->PawnsAlive = alive;
            if (session.Session.LocalController is { } pc)
            {
                status->MatchEnded = pc.bGameEnded || pc.GameReplicationInfo is { WinningTeam: not 255 } ? 1 : 0;
                if ((pc.ViewTarget as SBasePawn ?? pc.SpecTarget) is { bDeleteMe: false } watched)
                {
                    status->TargetActorId = watched.ActorId;
                    status->TargetMerc = watched is SPAWNDEFENSE ? 1 : 0;
                }
            }

            return 1;
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>Cuts to the next player: the spectator's Fire, which the visitor state reads as "next player".</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void NextTarget()
    {
        try
        {
            if (Exports.Current is { IsBackdrop: true, IsDisposed: false } session)
            {
                session.Command("Fire", 0);
            }
        }
        catch (Exception)
        {
        }
    }

    /// <summary>Why the last start failed, cut to fit and zero terminated. Returns the full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetError(char* buffer, int capacity)
    {
        try
        {
            string text = BackdropMatch.Error;
            if (buffer != null && capacity > 0)
            {
                int count = Math.Min(text.Length, capacity - 1);
                text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
                buffer[count] = '\0';
            }

            return text.Length;
        }
        catch (Exception)
        {
            return 0;
        }
    }
}
