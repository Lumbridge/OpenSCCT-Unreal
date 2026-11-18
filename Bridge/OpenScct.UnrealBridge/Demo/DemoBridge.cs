using System;
using System.Diagnostics;
using System.IO;
using OpenScct.Net;
using OpenScct.UnrealBridge.Events;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Demo;

/// <summary>
/// Demo playback for Unreal (docs/DEMOS.md): the one <see cref="DemoPlayback"/> being watched, put into the bridge's
/// main session slot like an online round, so the level draws its pawns, poses and view through the main table. It
/// takes the part of the Godot client's DemoLibrary.Watch/Enter and DemoHud's per-frame work (the seek budget, the
/// camera's return, the paused view) with the scenes taken out:
/// <list type="number">
/// <item><see cref="Open"/> reads the file and checks it can play here (protocol, map installed); Unreal then opens
/// the demo's level.</item>
/// <item><see cref="Start"/>, from that level, makes the client and play sessions and adopts them.</item>
/// <item><see cref="Frame"/>, once a frame before the main table's Update, runs a seek's fast-forward within a budget,
/// eases the chase camera back and keeps a paused view current.</item>
/// <item>The controls (pause, speed, seek, camera, player) change the playback; a seek back is a new playback
/// adopted in place of the old one (<see cref="DemoPlayback.Restart"/>), on the same level.</item>
/// </list>
/// Single-threaded: Unreal's game thread only.
/// </summary>
public static class DemoBridge
{
    /// <summary>The playback speeds of the speed control (DemoHud.Speeds).</summary>
    public static readonly double[] Speeds = [0.25, 0.5, 1.0, 2.0, 4.0];

    /// <summary>How long one frame may spend fast-forwarding a seek (DemoHud.SeekBudgetMs).</summary>
    private const double SeekBudgetMs = 30.0;

    private static DemoPlayback? s_player;
    private static BridgeSession? s_adopted;
    private static string s_error = string.Empty;
    private static int s_revision = 1;
    private static ulong s_protocol;

    /// <summary>The playback opened or being watched, or null.</summary>
    public static DemoPlayback? Player => s_player;

    /// <summary>The playback's session in the main slot, or null (opened but not started, or stopped).</summary>
    public static BridgeSession? Adopted => s_adopted is { IsDisposed: false } adopted ? adopted : null;

    /// <summary>Why the last open, start or seek failed, as a player reads it; empty when nothing failed.</summary>
    public static string Error => s_error;

    public static int Revision => s_revision;

    /// <summary>This build's replication protocol: a demo plays only when it was recorded on the same.</summary>
    public static ulong Protocol => s_protocol != 0 ? s_protocol : s_protocol = OpenScct.Server.NetClasses.Create().ComputeProtocolHash();

    /// <summary>
    /// Reads <paramref name="path"/> and checks it can be watched here; the playback starts at
    /// <paramref name="seekTick"/> (0 for the beginning). Any playback before it is stopped. Returns the demo's map
    /// (the level Unreal opens for it), or null with the reason in <see cref="Error"/>.
    /// </summary>
    public static string? Open(string path, uint seekTick = 0)
    {
        Stop();
        s_error = string.Empty;
        DemoPlayback player;
        try
        {
            player = DemoPlayback.Open(path, seekTick);
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException or EndOfStreamException or ArgumentException)
        {
            return Fail("Could not open the demo: " + e.Message);
        }

        if (player.Header.ProtocolHash != Protocol)
        {
            player.Dispose();
            return Fail($"The demo was recorded on another game version ({player.Header.GameVersion}).");
        }

        if (!OnlineBridge.Instance.Catalog.CanPlay(player.Header.Map))
        {
            player.Dispose();
            return Fail($"The demo is on {player.Header.Map}, which is not installed.");
        }

        s_player = player;
        Touch();
        return player.Header.Map;
    }

    /// <summary>
    /// Makes the opened playback's sessions and puts them in the main slot (blocks while the client's map loads).
    /// Returns the session id, or 0 with the reason in <see cref="Error"/>.
    /// </summary>
    public static int Start()
    {
        if (s_player is not { } player)
        {
            Fail("No demo is open.");
            return 0;
        }

        if (Adopted is { } running)
        {
            return running.Id;
        }

        try
        {
            // A visit would lose its round to the main slot (and the server would time the player out): leave it.
            OnlineBridge.Existing?.Leave();
            player.Clock.BeginSeek();
            PlaySession session = player.CreateSession(OnlineBridge.Instance.Catalog);
            BridgeSession adopted = Exports.AdoptRemote(session);
            s_adopted = adopted;
            adopted.Ticked += () =>
            {
                if (ReferenceEquals(s_player, player) && player.Session is { } played)
                {
                    player.AfterTick(played);
                }
            };
            Touch();
            return adopted.Id;
        }
        catch (Exception e) when (e is InvalidOperationException or IOException or InvalidDataException)
        {
            Stop();
            Fail(e.Message);
            return 0;
        }
    }

    /// <summary>
    /// Once a frame, before the main table's Update (DemoHud._Process): a seek's fast-forward within its budget (a
    /// checkpoint that cannot be restored falls back to the beginning), the chase camera's return, and a paused view
    /// that follows the camera controls.
    /// </summary>
    public static void Frame(float seconds)
    {
        if (s_player is not { } player || Adopted is not { } adopted || player.Session is not { } session || session.IsStopped)
        {
            return;
        }

        if (player.IsSeeking)
        {
            EventsFeature events = EventsFeature.Of(adopted);
            events.Quiet = true;
            long start = Stopwatch.GetTimestamp();
            try
            {
                while (player.FastForward(30) && Stopwatch.GetElapsedTime(start).TotalMilliseconds < SeekBudgetMs)
                {
                }
            }
            catch (InvalidDataException error) when (player.UsingCheckpoint)
            {
                Console.Error.WriteLine("SVM_DEMO checkpoint seek failed: " + error.Message);
                Replace(player.Restart(player.SeekTarget, useCheckpoint: false));
                return;
            }
            finally
            {
                events.Quiet = false;
            }

            if (!player.IsSeeking)
            {
                events.FinishSeek();
                player.Clock.ResumeRealTime();
            }
            Touch();
        }

        player.AdvanceCamera(seconds);
        if (player.Clock.Paused)
        {
            session.SetViewNow(player.ViewFor(session.View));
        }
    }

    /// <summary>Ends the playback: its session leaves the main slot and the recording is closed.</summary>
    public static void Stop()
    {
        BridgeSession? adopted = s_adopted;
        DemoPlayback? player = s_player;
        s_adopted = null;
        s_player = null;
        if (adopted is not null)
        {
            Exports.Release(adopted);
        }

        player?.Dispose();
        if (adopted is not null || player is not null)
        {
            Touch();
        }
    }

    // ---- the controls -------------------------------------------------------------------------------------------

    public static void TogglePause()
    {
        if (s_player is not { } player) return;
        if (player.AtEnd)
        {
            Restart();
            return;
        }

        player.Clock.Paused = !player.Clock.Paused;
        Touch();
    }

    public static void SetPaused(bool paused)
    {
        if (s_player is { } player)
        {
            player.Clock.Paused = paused;
            Touch();
        }
    }

    /// <summary>The speed control's position (0 to 4: 0.25, 0.5, 1, 2, 4).</summary>
    public static int SpeedIndex => s_player is { } player ? Math.Max(0, Array.IndexOf(Speeds, player.Clock.Speed)) : 2;

    public static void SetSpeedIndex(int index)
    {
        if (s_player is { } player)
        {
            player.Clock.Speed = Speeds[Math.Clamp(index, 0, Speeds.Length - 1)];
            Touch();
        }
    }

    public static void StepSpeed(int step)
    {
        if (s_player is { } player)
        {
            int index = Array.IndexOf(Speeds, player.Clock.Speed);
            SetSpeedIndex((index < 0 ? 2 : index) + step);
        }
    }

    /// <summary>Moves by <paramref name="seconds"/> of recording (the ±5 s buttons, the arrows).</summary>
    public static void Skip(int seconds)
    {
        if (s_player is { } player)
        {
            Seek((uint)Math.Clamp((long)player.CurrentTick + (long)seconds * Math.Max(1, player.Header.TickRate), player.FirstTick, player.LastTick));
        }
    }

    /// <summary>Seeks to a server tick: restart from the closest bootstrap so audio and visual state share one timeline.</summary>
    public static void Seek(uint tick)
    {
        if (s_player is not { } player) return;
        tick = Math.Clamp(tick, player.FirstTick, player.LastTick);
        if (tick != player.CurrentTick || player.IsSeeking)
        {
            Replace(player.Restart(tick));
        }
    }

    /// <summary>Seeks to a timeline marker with the seek bar's three-second lead-in (DemoSeekBar.TickAt).</summary>
    public static void SeekMarker(int index)
    {
        if (s_player is not { } player || index < 0 || index >= player.Recording.Index.Markers.Count) return;
        DemoMarker marker = player.Recording.Index.Markers[index];
        uint lead = 3u * (uint)Math.Max(1, player.Header.TickRate);
        Seek(marker.Tick > player.FirstTick + lead ? marker.Tick - lead : player.FirstTick);
    }

    /// <summary>From the beginning (Home), playing again when it had finished.</summary>
    public static void Restart()
    {
        if (s_player is not { } player) return;
        bool atEnd = player.AtEnd;
        DemoPlayback restart = player.Restart(player.FirstTick);
        if (atEnd) restart.Clock.Paused = false;
        Replace(restart);
    }

    /// <summary>The next playback takes the slot (a seek back, a restart): the old one stops, the new one starts on the same level.</summary>
    private static void Replace(DemoPlayback next)
    {
        DemoPlayback? old = s_player;
        BridgeSession? oldAdopted = s_adopted;
        s_player = next;
        s_adopted = null;
        if (oldAdopted is not null)
        {
            Exports.Release(oldAdopted);
        }

        old?.Dispose();
        if (oldAdopted is not null)
        {
            Start();
        }

        Touch();
    }

    public static void Touch() => s_revision++;

    private static string? Fail(string message)
    {
        s_error = message;
        Console.Error.WriteLine("SVM_DEMO " + message);
        Touch();
        return null;
    }
}
