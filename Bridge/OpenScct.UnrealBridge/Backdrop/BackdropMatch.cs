using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using OpenScct.Bots;
using OpenScct.Content;
using OpenScct.Maps;
using OpenScct.Server;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Backdrop;

/// <summary>How far the match being made ready for the menu's backdrop is (<see cref="BackdropMatch.State"/>).</summary>
public enum BackdropPrepareState
{
    /// <summary>Nothing is being made ready and nothing waits.</summary>
    Idle = 0,

    /// <summary>A worker is loading the map and starting its server.</summary>
    Loading = 1,

    /// <summary>A started match waits to be adopted (<see cref="BackdropMatch.Take"/>).</summary>
    Ready = 2,

    /// <summary>The start failed; <see cref="BackdropMatch.Error"/> says why. Cleared by the next begin or cancel.</summary>
    Failed = 3,
}

/// <summary>
/// The match behind the main menu: a local Story match played by four bots (two spies against two mercs) that the
/// machine's own player only watches. It is an ordinary local match (<see cref="BridgeSession.StartLocal(LocalMatch)"/>)
/// with two differences: the local player logs in as a spectator, and the start runs on a worker thread, so the menu
/// keeps drawing while the map loads. The started match then waits here until the game thread adopts it into the
/// bridge's session slot (<see cref="Exports.AdoptPrepared"/>).
/// <para>One match is made ready at a time. A real start (a local match, an online round) first waits for a start
/// that is under way and throws its result away (<see cref="Quiesce"/>), so two maps never load at once behind a
/// match the player asked for.</para>
/// </summary>
public static class BackdropMatch
{
    /// <summary>
    /// The name the spectating player logs in with. The simulation has no setting for a spectating local player:
    /// its login options are "?Name={PlayerName}?Team={Team}" (PlaySettings.LoginOptions), and the game rules read
    /// the first value of each key (GameInfo.ParseOption). The two options are therefore carried in the name: the
    /// rules read Name "Menu", SpectatorOnly "true" and Team 255 (the spectator team), and the "?Team=0" that follows
    /// is never reached. The simulation is not changed for this.
    /// </summary>
    private const string SpectatorLogin = "Menu?SpectatorOnly=true?Team=255";

    private static readonly object s_gate = new();
    private static Task? s_task;
    private static BridgeSession? s_ready;
    private static string s_error = "";
    private static string s_map = "";
    private static int s_generation;

    /// <summary>The map being made ready or waiting, empty for none.</summary>
    public static string Map
    {
        get
        {
            lock (s_gate)
            {
                return s_map;
            }
        }
    }

    /// <summary>Why the last start failed.</summary>
    public static string Error
    {
        get
        {
            lock (s_gate)
            {
                return s_error;
            }
        }
    }

    public static BackdropPrepareState State
    {
        get
        {
            lock (s_gate)
            {
                return s_ready is not null ? BackdropPrepareState.Ready
                    : s_task is { IsCompleted: false } ? BackdropPrepareState.Loading
                    : s_error.Length > 0 ? BackdropPrepareState.Failed
                    : BackdropPrepareState.Idle;
            }
        }
    }

    /// <summary>What the backdrop plays on <paramref name="map"/>: Story, four bots, the local player watching.</summary>
    public static LocalMatch For(string map) => new()
    {
        Map = map,
        Mode = "Story",
        Merc = false,
        Bots = 4,
        MatchPlayers = 4,
        Difficulty = "normal",
        PlayerName = SpectatorLogin,
        DemoFolder = null,
    };

    /// <summary>
    /// True when a backdrop match can start on <paramref name="map"/> without a wait the menu would show: the map's
    /// sim files are installed and both sides' bot navigation is already prepared on this machine (in the cache, a
    /// downloaded pack or the map's own pack). A map whose navigation would have to be built (minutes) is not used.
    /// </summary>
    public static bool CanPlay(string map)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(map) || new MapCatalog(MapFolders.AllRoots()).FindFolder(map) is not { } found)
            {
                return false;
            }

            string folder = Path.GetFullPath(found);
            SvMapManifest manifest = SvMapManifest.Load(Path.Combine(folder, SvMapHash.ManifestFile));
            byte mask = GameplayDoc.Load(Path.Combine(folder, GameplayDoc.RelativePath)).GEActivationMask;
            return AiNavCache.HasPreparedGraphs(folder, manifest.Map, manifest.SimHash, mask);
        }
        catch (Exception)
        {
            // A map that cannot be read is not a backdrop.
            return false;
        }
    }

    /// <summary>
    /// Starts making a match on <paramref name="map"/> ready on a worker. A start already under way for another map
    /// is dropped (its match is thrown away when it finishes); one for the same map goes on.
    /// </summary>
    public static void Begin(string map)
    {
        BridgeSession? stale;
        int generation;
        lock (s_gate)
        {
            if (s_map == map && (s_ready is not null || s_task is { IsCompleted: false }))
            {
                return;
            }

            stale = s_ready;
            s_ready = null;
            s_error = "";
            s_map = map;
            generation = ++s_generation;
            Task? before = s_task;
            LocalMatch match = For(map);
            s_task = Task.Run(() =>
            {
                // One map loads at a time: a dropped start finishes first.
                try
                {
                    before?.Wait();
                }
                catch (Exception)
                {
                }

                bool wanted;
                lock (s_gate)
                {
                    wanted = generation == s_generation;
                }

                if (!wanted)
                {
                    return;
                }

                BridgeSession? session = null;
                string error = "";
                try
                {
                    // Checked here, on the worker: reading a map's manifest and graph headers is not for the menu's frame.
                    if (!CanPlay(match.Map))
                    {
                        throw new InvalidOperationException(match.Map + " has no converted content or no prepared bot navigation on this PC.");
                    }

                    session = BridgeSession.StartLocal(match);
                    session.IsBackdrop = true;
                    BackdropCamera.Install(session);
                }
                catch (Exception e)
                {
                    error = e.ToString();
                }

                lock (s_gate)
                {
                    if (generation == s_generation)
                    {
                        s_ready = session;
                        s_error = session is null ? (error.Length > 0 ? error : "The backdrop match did not start.") : "";
                        session = null;
                    }
                }

                // Cancelled or replaced while it loaded: nobody will adopt it.
                DisposeQuietly(session);
            });
        }

        DisposeQuietly(stale);
    }

    /// <summary>The match that waits, handed to the caller (who now owns it), or null when none is ready.</summary>
    public static BridgeSession? Take()
    {
        lock (s_gate)
        {
            BridgeSession? session = s_ready;
            s_ready = null;
            if (session is not null)
            {
                s_map = "";
            }

            return session;
        }
    }

    /// <summary>Drops the match that waits and the start under way, without waiting for it.</summary>
    public static void Cancel()
    {
        BridgeSession? stale;
        lock (s_gate)
        {
            stale = s_ready;
            s_ready = null;
            s_error = "";
            s_map = "";
            s_generation++;
        }

        DisposeQuietly(stale);
    }

    /// <summary>
    /// For a real start: drops the backdrop's match as <see cref="Cancel"/> does, and waits for a start that is under
    /// way to finish and throw its match away (at most <paramref name="timeoutMs"/>), so the real match does not load
    /// its map beside another load.
    /// </summary>
    public static void Quiesce(int timeoutMs = 20000)
    {
        Task? task;
        lock (s_gate)
        {
            task = s_task;
        }

        Cancel();
        if (task is { IsCompleted: false })
        {
            try
            {
                task.Wait(timeoutMs);
            }
            catch (Exception)
            {
            }
        }
    }

    private static void DisposeQuietly(BridgeSession? session)
    {
        if (session is null)
        {
            return;
        }

        try
        {
            session.Dispose();
        }
        catch (Exception)
        {
            // A backdrop that fails to stop cleanly is nobody's error to read.
        }
    }
}
