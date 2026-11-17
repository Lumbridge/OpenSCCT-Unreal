using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using OpenScct.UnrealBridge.Controls;
using OpenScct.UnrealBridge.Devices;
using OpenScct.UnrealBridge.Equipment;
using OpenScct.UnrealBridge.Events;
using OpenScct.UnrealBridge.Hud;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The table of entry points Unreal calls. Unreal asks <see cref="Exports.GetApiVersion"/> first, then
/// <see cref="Exports.GetApiSized"/> once with the size of its own table, and keeps the table. Every entry catches its
/// exceptions (none may cross into native code) and keeps the text for <see cref="LastError"/>. Strings are UTF-16,
/// zero terminated.
/// <para>The table only ever grows at its end, so a caller built against an older version can use the entries it
/// knows from a newer bridge, and <see cref="Exports.GetApiSized"/> never writes more than the caller has room for.</para>
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

    // Version 2. New entries are added at the end, so the ones above keep their places.
    public delegate* unmanaged[Cdecl]<int, BridgeBoneInfo*, int, int> GetSkeleton;
    public delegate* unmanaged[Cdecl]<int, BridgeBone*, int, int> GetPawnPose;

    // Version 3.
    public delegate* unmanaged[Cdecl]<BridgeStartOptions*, int> StartLocalEx;
    public delegate* unmanaged[Cdecl]<BridgeSessionInfo*, int> GetSession;
    public delegate* unmanaged[Cdecl]<int, int> StopSession;
    public delegate* unmanaged[Cdecl]<int, int> UpdateSession;
    public delegate* unmanaged[Cdecl]<int, int> Shutdown;
}

/// <summary>
/// The main session slot and the entry points of the main table.
/// <para>One session plays at a time, whichever kind: a local match (<c>StartLocal</c>, <c>StartLocalEx</c>) or a
/// round on a remote server, which the online bridge puts here when its visit enters a round
/// (<see cref="AdoptRemote"/>) and takes out again at the round's end (<see cref="Release"/>). Every per-frame
/// entry of every table reads <see cref="Current"/>, so Unreal draws both kinds through the same calls.</para>
/// <para>Each session has an id (<c>GetSession</c>). A caller that holds one can stop and update only its own
/// session (<c>StopSession</c>, <c>UpdateSession</c>): a call for a session that is gone or was replaced does
/// nothing, so a second game world in the process cannot end the first world's match by cleaning up.</para>
/// </summary>
public static unsafe class Exports
{
    /// <summary>No table of this bridge is anywhere near this size: a larger "size" is a caller passing something else.</summary>
    private const int MaxTableSize = 1 << 16;

    private static BridgeSession? s_session;

    /// <summary>The last start failed, or the session stopped on an error. Cleared by a stop or the next start.</summary>
    private static bool s_failed;

    /// <summary>The id of the session that failed, so its owner's StopSession still clears the failure.</summary>
    private static int s_failedId;

    private static string s_lastError = "";

    /// <summary>The running session, for the feature entry points kept in their own files; null when none runs.</summary>
    public static BridgeSession? Current => s_session;

    /// <summary>A session took the main slot (a local match started, or an online round was entered).</summary>
    public static event Action<BridgeSession>? SessionStarted;

    /// <summary>The session left the main slot; it is stopped by the time this is raised.</summary>
    public static event Action<BridgeSession>? SessionEnded;

    /// <summary>
    /// Ends the running session with <paramref name="message"/> as the error Unreal reads: the state is Failed
    /// until a stop or the next start. With no session running only the error text is kept.
    /// </summary>
    internal static void ReportFailure(string message) => Fail(message);

    // ---- the session slot ---------------------------------------------------------------------------------------

    /// <summary>
    /// Puts a round on a remote server in the main slot (the online bridge calls it when its visit enters a round).
    /// A session already there is ended first. The online visit keeps ticking the round; the returned session is
    /// what the main table and the features read it through.
    /// </summary>
    internal static BridgeSession AdoptRemote(PlaySession round)
    {
        EndCurrent();
        ClearFailure();
        return Adopt(BridgeSession.FromRemote(round));
    }

    /// <summary>
    /// Takes <paramref name="session"/> out of the main slot, if it is still there, and ends it (the online bridge
    /// calls it when a round is over). Other sessions in the slot are left alone.
    /// </summary>
    internal static void Release(BridgeSession session)
    {
        if (ReferenceEquals(s_session, session))
        {
            EndCurrent();
        }
        else
        {
            End(session);
        }
    }

    /// <summary>
    /// Makes <paramref name="session"/> the current one and starts the features that must see it from its first
    /// tick, in a fixed order (it is the order their tick handlers run in): the controls (they fill the input before
    /// each tick), the events (the sound and effect calls of the first ticks, the level's song), the HUD (the
    /// mission briefing and join lines arrive in the first ticks), the level state and the equipment.
    /// </summary>
    private static BridgeSession Adopt(BridgeSession session)
    {
        try
        {
            ControlsFeature.Of(session);
            EventsFeature.Of(session);
            HudFeature.Of(session);
            WorldState.Of(session);
            EquipmentFeature.For(session);
        }
        catch
        {
            try
            {
                session.Dispose();
            }
            catch (Exception)
            {
                // The start's own error is the one to report.
            }

            throw;
        }

        s_session = session;
        try
        {
            SessionStarted?.Invoke(session);
        }
        catch (Exception e)
        {
            // A listener's error is not the session's: the match plays on.
            s_lastError = e.ToString();
        }

        return session;
    }

    /// <summary>
    /// Ends the session in the slot. The slot is emptied first, so a failing stop cannot leave a dead session in it.
    /// Never throws: the session is stopped whatever a feature or a listener did while it ended (their error is kept
    /// for <c>LastError</c>), so the start, the release or the stop that asked for it goes on.
    /// </summary>
    private static void EndCurrent()
    {
        if (s_session is not { } session)
        {
            return;
        }

        s_session = null;
        End(session);
        try
        {
            SessionEnded?.Invoke(session);
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
        }
    }

    /// <summary>Disposes <paramref name="session"/>; an error while it ends is kept for <c>LastError</c>, not thrown.</summary>
    private static void End(BridgeSession session)
    {
        try
        {
            session.Dispose();
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
        }
    }

    private static void ClearFailure()
    {
        s_failed = false;
        s_failedId = 0;
    }

    private static int IdOf(BridgeSession? session) => session?.Id ?? 0;

    // ---- the table ----------------------------------------------------------------------------------------------

    /// <summary>The version of the table <see cref="GetApiSized"/> fills. Ask this first: it writes nothing.</summary>
    [UnmanagedCallersOnly]
    public static int GetApiVersion() => BridgeSession.ApiVersion;

    /// <summary>
    /// The size of the table as version 2 had it (the version, padding and twelve entries), the last one handed over
    /// without a size.
    /// </summary>
    private const int Version2TableSize = 8 + 12 * 8;

    /// <summary>
    /// The hand-over of versions 1 and 2, which takes no size: kept under its old name and signature so a caller
    /// built against version 2 that loads this bridge gets its own 104 bytes and nothing past them, reads a version
    /// it does not know and refuses cleanly. New callers use <see cref="GetApiSized"/>.
    /// </summary>
    [UnmanagedCallersOnly]
    public static int GetApi(BridgeApi* api) => Fill(api, Version2TableSize);

    /// <summary>
    /// Copies the table into <paramref name="api"/>, at most <paramref name="size"/> bytes of it (the caller's
    /// <c>sizeof</c> of its own table): a caller built against an older, shorter table gets the entries it knows, and
    /// one built against a newer, longer table keeps its own (zeroed) values for the entries this bridge lacks.
    /// Returns the API version, or 0 when nothing was written (a null table or a size that cannot be one).
    /// </summary>
    [UnmanagedCallersOnly]
    public static int GetApiSized(BridgeApi* api, int size) => Fill(api, size);

    private static int Fill(BridgeApi* api, int size)
    {
        if (api == null || size < sizeof(int) || size > MaxTableSize)
        {
            return 0;
        }

        BridgeApi table = default;
        table.Version = BridgeSession.ApiVersion;
        table.Configure = &Configure;
        table.StartLocal = &StartLocal;
        table.Stop = &Stop;
        table.GetState = &GetState;
        table.Update = &Update;
        table.SetInput = &SetInput;
        table.Command = &Command;
        table.GetPawns = &GetPawns;
        table.GetView = &GetView;
        table.LastError = &LastError;
        table.GetSkeleton = &GetSkeleton;
        table.GetPawnPose = &GetPawnPose;
        table.StartLocalEx = &StartLocalEx;
        table.GetSession = &GetSession;
        table.StopSession = &StopSession;
        table.UpdateSession = &UpdateSession;
        table.Shutdown = &Shutdown;
        int bytes = Math.Min(size, sizeof(BridgeApi));
        Buffer.MemoryCopy(&table, api, bytes, bytes);
        return BridgeSession.ApiVersion;
    }

    /// <summary>
    /// Sets the content folder. Returns 1, or 0 with the reason in <c>LastError</c>; a refused folder does not put
    /// the bridge into the Failed state, so the next Configure or start is judged on its own.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Configure(char* contentRoot)
    {
        try
        {
            BridgeSession.Configure(contentRoot == null ? "" : new string(contentRoot));
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    /// <summary>Hosts a local match and joins it. Blocks while the map loads. Returns 1, or 0 with the reason in <c>LastError</c>.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartLocal(char* map, char* mode, int merc, int bots)
    {
        if (map == null || mode == null)
        {
            s_lastError = "StartLocal needs a map and a mode.";
            return 0;
        }

        return Start(new LocalMatch { Map = new string(map), Mode = new string(mode), Merc = merc != 0, Bots = bots }) != 0 ? 1 : 0;
    }

    /// <summary>
    /// Hosts a local match with every option (sidearms, rifles, practice rules, bot skill, player name) and joins it.
    /// Blocks while the map loads. Returns the new session's id, or 0 with the reason in <c>LastError</c>.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartLocalEx(BridgeStartOptions* options)
    {
        if (options == null || options->Size < sizeof(int))
        {
            s_lastError = "StartLocalEx needs its options with Size set.";
            return 0;
        }

        // Only what the caller's struct holds is read; the rest stays zero.
        BridgeStartOptions o = default;
        int bytes = Math.Min(options->Size, sizeof(BridgeStartOptions));
        Buffer.MemoryCopy(options, &o, sizeof(BridgeStartOptions), bytes);
        string map = FixedText(o.Map, BridgeStartOptions.MapCapacity);
        string mode = FixedText(o.Mode, BridgeStartOptions.ModeCapacity);
        string name = FixedText(o.PlayerName, BridgeStartOptions.NameCapacity);
        return Start(new LocalMatch
        {
            Map = map,
            Mode = mode.Length > 0 ? mode : "Story",
            Merc = o.Merc != 0,
            Bots = o.Bots,
            Difficulty = o.Difficulty switch { 1 => "easy", 3 => "hard", 4 => "expert", _ => "normal" },
            PlayerName = name.Length > 0 ? name : "Player",
            Sidearms = (o.Flags & BridgeStartFlags.Sidearms) != 0,
            Rifles = (o.Flags & BridgeStartFlags.Rifles) != 0,
            OfflineRules = (o.Flags & BridgeStartFlags.OfflineRules) != 0,
            ResponsiveSmoke = (o.Flags & BridgeStartFlags.ResponsiveSmoke) != 0,
            UnlimitedGear = (o.Flags & BridgeStartFlags.UnlimitedGear) != 0,
            TimeLimit = o.TimeLimitPlusOne - 1,
            SpyLives = o.SpyLivesPlusOne - 1,
            MercLives = o.MercLivesPlusOne - 1,
            RespawnSeconds = o.RespawnPlusOne - 1,
        });
    }

    /// <summary>The session before, of either kind, is ended first. Returns the new session's id, 0 when the start failed.</summary>
    private static int Start(LocalMatch match)
    {
        try
        {
            EndCurrent();
            ClearFailure();
            // The player's demo-saving choice records the match (Demo/DemoSaving.cs).
            match = match with { DemoFolder = match.DemoFolder ?? Demo.DemoSaving.OfflineFolder() };
            BridgeSession started = Adopt(BridgeSession.StartLocal(Test.LessonLaunch.Apply(match)));
            Demo.DemoSaving.Started(started);
            return started.Id;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            s_failed = true;
            s_failedId = 0;
            return 0;
        }
    }

    /// <summary>Ends whatever session is in the slot and clears a failure. For a caller that owns the whole bridge.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Stop()
    {
        try
        {
            ClearFailure();
            EndCurrent();
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
        }
    }

    /// <summary>
    /// Ends the session <paramref name="sessionId"/> (or clears its failure) and returns 1. A session that is gone or
    /// was replaced by another is left alone: 0. This is the stop for a game world's clean-up.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StopSession(int sessionId)
    {
        try
        {
            if (sessionId == 0 || sessionId != (s_session is not null ? s_session.Id : s_failedId))
            {
                return 0;
            }

            ClearFailure();
            EndCurrent();
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState() => (int)State;

    private static BridgeState State => s_failed ? BridgeState.Failed : s_session is null ? BridgeState.Idle : BridgeState.Playing;

    /// <summary>
    /// What is in the main slot. Returns the session's id, 0 when none plays (also after a failure). Ask every frame:
    /// a changed id is a new match (a local start, or the online visit entering its next round).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSession(BridgeSessionInfo* info)
    {
        try
        {
            BridgeSession? session = s_session;
            if (info != null)
            {
                *info = default;
                info->State = (int)State;
                if (session is not null)
                {
                    info->Id = session.Id;
                    info->Flags = (session.IsRemote ? BridgeSessionFlags.Remote : 0) | (session.LocalMerc ? BridgeSessionFlags.Merc : 0);
                    info->Ticks = (int)Math.Min(session.Ticks, int.MaxValue);
                    string map = session.Map;
                    int length = Math.Min(map.Length, BridgeSessionInfo.MapCapacity - 1);
                    map.AsSpan(0, length).CopyTo(new Span<char>(info->Map, length));
                }
            }

            return IdOf(session);
        }
        catch (Exception e)
        {
            Fail(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Runs the simulation ticks that are due. Returns how many client ticks ran since the last call (for an online
    /// round the online table's Update ran them earlier in the frame; they are still counted here).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Update() => UpdateCurrent();

    /// <summary>The same for the session <paramref name="sessionId"/> only: -1 and nothing run when that session is gone or was replaced.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int UpdateSession(int sessionId) => sessionId != 0 && sessionId == IdOf(s_session) ? UpdateCurrent() : -1;

    private static int UpdateCurrent()
    {
        if (s_session is not { } session)
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
        if (s_session is { } session && input != null)
        {
            try
            {
                session.SetInput(in *input);
            }
            catch (Exception e)
            {
                Fail(e.ToString());
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Command(char* name, int arg)
    {
        if (s_session is { } session && name != null)
        {
            try
            {
                session.Command(new string(name), arg);
            }
            catch (Exception e)
            {
                Fail(e.ToString());
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPawns(BridgePawn* pawns, int capacity)
    {
        if (s_session is not { } session || pawns == null || capacity <= 0)
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
        if (view == null)
        {
            return 0;
        }

        *view = default;
        if (s_session is not { } session)
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

    /// <summary>
    /// Writes the skeleton of a pawn kind (0 spy, 1 merc), in the order of the character file's skin joints. Returns
    /// the bone count (which can be more than <paramref name="capacity"/>: only that many are written), 0 when the
    /// content has no such character. Works before a session starts, once the content is configured.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSkeleton(int kind, BridgeBoneInfo* bones, int capacity)
    {
        try
        {
            Pose.GlbCharacter? character = s_session is { } session ? session.Character(kind) : BridgeSession.ConfiguredCharacter(kind);
            return character is null
                ? 0
                : BridgeSession.GetSkeleton(character, bones == null || capacity <= 0 ? default : new Span<BridgeBoneInfo>(bones, capacity));
        }
        catch (Exception e)
        {
            Fail(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Writes a pawn's pose for the frame being drawn: each bone relative to its parent, in <c>GetSkeleton</c>'s
    /// order. Returns the bone count (only <paramref name="capacity"/> are written), 0 when the pawn has no pose yet.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPawnPose(int actorId, BridgeBone* bones, int capacity)
    {
        if (s_session is not { } session)
        {
            return 0;
        }

        try
        {
            return session.GetPawnPose(actorId, bones == null || capacity <= 0 ? default : new Span<BridgeBone>(bones, capacity));
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
        string error = s_lastError;
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(error.Length, capacity - 1);
            error.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
            buffer[count] = '\0';
        }

        return error.Length;
    }

    /// <summary>
    /// The stop for the module's shutdown: leaves the online server and ends the session, whatever state the bridge
    /// is in, and never throws. A local match's server thread is joined without a time limit by the simulation, so
    /// the stop runs on a worker and is given up after <paramref name="timeoutMs"/> (0 or less: 3 s): a server stuck
    /// in a tick then cannot hang the editor's or the game's exit. Returns 1 when everything stopped in time, 0 when
    /// it was given up (the process should be on its way out).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Shutdown(int timeoutMs)
    {
        try
        {
            BridgeSession? session = s_session;
            s_session = null;
            ClearFailure();
            Task stop = Task.Run(() =>
            {
                try
                {
                    // The visit first: it releases its round, tells the server and closes the connection.
                    OnlineBridge.Existing?.Dispose();
                    // The hub visit and the hub this PC hosts likewise.
                    Hub.HubBridge.Existing?.Dispose();
                }
                finally
                {
                    session?.Dispose();
                }
            });
            bool stopped;
            try
            {
                stopped = stop.Wait(timeoutMs > 0 ? timeoutMs : 3000);
            }
            catch (AggregateException e)
            {
                s_lastError = e.InnerException?.ToString() ?? e.ToString();
                stopped = true;
            }

            if (session is not null)
            {
                SessionEnded?.Invoke(session);
            }

            return stopped ? 1 : 0;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    private static string FixedText(char* text, int capacity)
    {
        int length = 0;
        while (length < capacity && text[length] != '\0')
        {
            length++;
        }

        return new string(text, 0, length);
    }

    /// <summary>
    /// Keeps <paramref name="message"/> for <c>LastError</c> and, when a session runs, ends it: a local match's server
    /// thread and bots must not go on ticking behind a session nobody reads any more, and a remote round that
    /// stopped tells the online visit to leave. The state is Failed until a stop or the next start.
    /// </summary>
    private static void Fail(string message)
    {
        s_lastError = message;
        if (s_session is not { } session)
        {
            return;
        }

        s_failed = true;
        s_failedId = session.Id;
        EndCurrent();
        // The failure is the error to read, not what ending the session may have added.
        s_lastError = message;
    }
}
