using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.UnrealBridge.Test;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The test-only reads of the Unreal client's named tests (Source/OpenSCCT/Test/ScctTestRunnerSubsystem): tick and
/// netcode counters of the running session, where the client and the local server have each pawn, and the simulation
/// check of the boot test. Unreal asks for <see cref="TestExports.GetApi"/> once (type
/// "OpenScct.UnrealBridge.TestExports, OpenScct.UnrealBridge", method "GetApi") and keeps the table.
/// Source/OpenSCCT/Test/ScctTestApi.h mirrors it field for field; later test tasks append entries at the end and bump
/// the version. No entry throws: a failure returns 0 (or -1) with the reason in <c>LastError</c>, and the match goes on.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct TestApi
{
    public int Version;

    /// <summary>Writes the running session's counters (<see cref="BridgeTestCounters"/>). Returns its id, 0 when none runs (zeroed).</summary>
    public delegate* unmanaged[Cdecl]<BridgeTestCounters*, int> GetCounters;

    /// <summary>
    /// Writes the session's pawns (<see cref="BridgeTestPawn"/>): every pawn the client has, and once the local server
    /// is readable (stopped, or an inline host) the server's state of each and the server pawns the client lacks.
    /// Returns the count written.
    /// </summary>
    public delegate* unmanaged[Cdecl]<BridgeTestPawn*, int, int> GetPawns;

    /// <summary>
    /// Stops the local match's server (its thread ends after the tick it is in) so its state can be read exactly; the
    /// client stops ticking with it. For the end of a check: the session is good for nothing else after. Returns 1, or
    /// 0 for no session or a remote round.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int> StopServer;

    /// <summary>
    /// The boot test's simulation check (the Godot client's BootSmoke): loads the simulation assemblies and steps a new
    /// World <paramref name="ticks"/> fixed ticks. Returns the ticks the world ran, or -1 with the reason in LastError.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, int> StepWorld;

    /// <summary>Copies the last failure's text. Returns its length.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

public static unsafe class TestExports
{
    public const int ApiVersion = 1;

    private static readonly string[] SimulationAssemblies =
        ["OpenScct.Provenance", "OpenScct.Core", "OpenScct.Physics", "OpenScct.Game", "OpenScct.Net", "OpenScct.Content", "OpenScct.Server"];

    private static string s_lastError = "";
    private static bool s_tracking;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(TestApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        // From now on every session keeps its pawns' last two ticks from its first tick (a session running already
        // starts keeping them on the first read).
        if (!s_tracking)
        {
            s_tracking = true;
            Exports.SessionStarted += session => TestPawnTrack.Of(session);
        }

        api->Version = ApiVersion;
        api->GetCounters = &GetCounters;
        api->GetPawns = &GetPawns;
        api->StopServer = &StopServer;
        api->StepWorld = &StepWorld;
        api->LastError = &LastError;
        return ApiVersion;
    }

    /// <summary>The local server's state can be read on this thread now: a stopped server or an inline host.</summary>
    internal static bool ServerReadable(PlaySession play) =>
        !play.IsRemote && (play.IsStopped || play.Settings.HostMode != PlayHostMode.Thread);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetCounters(BridgeTestCounters* counters)
    {
        if (counters == null)
        {
            return 0;
        }

        try
        {
            *counters = default;
            if (Exports.Current is not { IsDisposed: false } session)
            {
                return 0;
            }

            PlaySession play = session.Session;
            counters->SessionId = session.Id;
            counters->ClientTicks = session.Ticks;
            counters->ClientState = (int)play.Client.State;
            counters->ClientPawns = TestPawnTrack.Of(session).ClientPawns;
            counters->Flags = (play.IsRemote ? BridgeTestFlags.Remote : 0) | (play.IsStopped ? BridgeTestFlags.ServerStopped : 0) |
                (play.ServerFailure is not null ? BridgeTestFlags.ServerFailed : 0);
            counters->ServerTicks = play.IsRemote ? -1 : play.Host.World.TickCount;
            if (play.LocalController is { } controller && play.Client.Movement.EndpointOf(controller)?.Client is { } moves)
            {
                counters->CorrectionsApplied = moves.CorrectionsApplied;
                counters->CorrectionsReceived = moves.AuthoritativeCorrectionsReceived;
            }

            counters->Reinits = -1;
            if (ServerReadable(play))
            {
                counters->Reinits = play.ServerSnapshot(host =>
                    play.ServerController is { } server && host.Movement.EndpointOf(server)?.Server is { } serverMoves
                        ? serverMoves.Sent.Reinits
                        : 0);
            }

            return session.Id;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPawns(BridgeTestPawn* pawns, int capacity)
    {
        if (pawns == null || capacity <= 0 || Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        try
        {
            return TestPawnTrack.Of(session).Read(new Span<BridgeTestPawn>(pawns, capacity));
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StopServer()
    {
        try
        {
            if (Exports.Current is not { IsDisposed: false } session || session.IsRemote)
            {
                s_lastError = "No local match is running.";
                return 0;
            }

            session.Session.Stop();
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StepWorld(int ticks)
    {
        try
        {
            foreach (string assembly in SimulationAssemblies)
            {
                try
                {
                    Assembly.Load(assembly);
                }
                catch (Exception e) when (e is FileNotFoundException or FileLoadException or BadImageFormatException)
                {
                    s_lastError = $"could not load {assembly}: {e.Message}";
                    return -1;
                }
            }

            var world = new World(1);
            world.Step(Math.Max(ticks, 0));
            return (int)Math.Min(world.TickCount, int.MaxValue);
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity)
    {
        string text = s_lastError;
        if (buffer != null && capacity > 0)
        {
            int length = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, length).CopyTo(new Span<char>(buffer, length));
            buffer[length] = '\0';
        }

        return text.Length;
    }
}
