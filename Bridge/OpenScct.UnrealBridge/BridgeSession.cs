using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Server;
using OpenScct.UnrealBridge.Pose;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge;

/// <summary>What <see cref="BridgeSession.StartLocal(LocalMatch)"/> hosts and joins.</summary>
public sealed record LocalMatch
{
    /// <summary>The map's folder name (<c>svmap/&lt;Map&gt;</c> of the content).</summary>
    public string Map { get; init; } = "AquaE";

    /// <summary>Story, DiskHunt, Deathmatch or MercDeathMatch.</summary>
    public string Mode { get; init; } = "Story";

    public bool Merc { get; init; }

    /// <summary>Server-side bots filling the other slots.</summary>
    public int Bots { get; init; }

    /// <summary>easy, normal, hard or expert.</summary>
    public string Difficulty { get; init; } = "normal";

    public string PlayerName { get; init; } = "Player";

    /// <summary>The mercs carry OpenSCCT's sidearms.</summary>
    public bool Sidearms { get; init; }

    /// <summary>The mercs may pick OpenSCCT's additional rifles.</summary>
    public bool Rifles { get; init; }

    /// <summary>
    /// A practice round, as the Godot client's --practice: no time limit, unlimited lives, 5 s respawn, and the
    /// level's PracticeRules on (unlimited ammunition, charges and mask air, instant gear changes, the practice aids).
    /// </summary>
    public bool OfflineRules { get; init; }

    /// <summary>Visit a Map practice: unlimited ammunition and gadgets, instant gear changes, and Switch Team in the menu.</summary>
    public bool UnlimitedGear { get; init; }
    /// <summary>The player's own rules over the map's (and over the practice rules): minutes (0 for no limit), lives of
    /// each spy and merc (0 for unlimited) and the respawn's seconds; negative keeps what the rules gave.</summary>
    public int TimeLimit { get; init; } = -1;

    public int SpyLives { get; init; } = -1;

    public int MercLives { get; init; } = -1;

    public int RespawnSeconds { get; init; } = -1;
    /// <summary>
    /// Where the local player first spawns instead of a PlayerStart (the map editor's Play From Camera; a TestRange
    /// drill's ledge or lane, Test/LessonLaunch.cs), or null. Later spawns use the map's starts.
    /// </summary>
    public (FVector Location, FRotator Rotation)? StartAt { get; init; }

    /// <summary>The editor's Play Level rules (unlimited lives, no time limit) instead of the map's settings.</summary>
    public bool PlayLevel { get; init; }

    /// <summary>The server ticks on the caller's thread just before the client (the drills read it between ticks).</summary>
    public bool InlineHost { get; init; }

    /// <summary>
    /// The server and the client talk over a simulated link with these latency, jitter and loss figures instead of
    /// loopback (the netcode checks, Test/NetLinkLaunch.cs); the server then ticks inline. Null for loopback.
    /// </summary>
    public SimLinkProfile? SimLink { get; init; }

    /// <summary>The server's network behaviour: OpenSCCT's tuned rates (Enhanced) or the original's (Stock).</summary>
    public NetProfile NetworkProfile { get; init; } = NetProfile.Enhanced;

    /// <summary>
    /// Builds the play session itself from the settings and clock instead of the usual local host (a check's
    /// dedicated server on a simulated link), or null.
    /// </summary>
    public Func<PlaySettings, IClock, PlaySession>? CreateSession { get; init; }
}

/// <summary>
/// The one play session Unreal drives, whichever kind it is: a local match (the dedicated server's code on its own
/// thread, with bots, joined by the local client: <see cref="StartLocal(LocalMatch)"/>) or a round on a remote server
/// (the online visit's connection: <see cref="FromRemote"/>). Both are the same <see cref="PlaySession"/> underneath,
/// so pawns, poses, the view, input and every feature read them the same way.
/// <para>Unreal calls <see cref="Update"/> once a frame on its game thread and then reads the drawn state; nothing
/// here is touched from another thread.</para>
/// <para>The order inside one client tick, which the features rely on:
/// <list type="number">
/// <item><see cref="BeforeTick"/>, before the tick's input is applied (the input mapper fills the input here);</item>
/// <item>the play session applies the input and runs the queued exec commands;</item>
/// <item>the handlers a feature added to <c>ClientSession.BeforeWorldTick</c> (what the commands raised is readable
/// there and nowhere later);</item>
/// <item>the world tick and the calc view;</item>
/// <item>the pawns are snapshotted and posed, then <see cref="Ticked"/>, in the order the features were made.</item>
/// </list></para>
/// </summary>
public sealed class BridgeSession : IDisposable
{
    public const int ApiVersion = 3;

    /// <summary>A view jump longer than this is drawn as a cut, not blended (as the Godot client).</summary>
    private const float ViewCutDistance = 128f;

    /// <summary>A pawn that moved farther than this in one tick was placed, not walked: no blend.</summary>
    private const float PawnCutDistanceSquared = 40000f;

    private static int s_lastId;

    private readonly PlaySession _session;
    private readonly Dictionary<int, PawnSample> _previous = new();
    private readonly Dictionary<int, PawnSample> _current = new();
    private readonly List<int> _order = new();
    private readonly Dictionary<int, PawnPoser> _posers = new();
    private readonly List<int> _gone = new();
    private readonly Dictionary<Type, object> _features = new();
    private readonly List<object> _featureOrder = new();
    private int _ticksUnread;
    private bool _disposed;

    private BridgeSession(PlaySession session)
    {
        _session = session;
        Id = Interlocked.Increment(ref s_lastId);
        _session.BeforeClientTick = (_, _) => BeforeTick?.Invoke();
        _session.AfterClientTick = (_, _) =>
        {
            Ticks++;
            _ticksUnread++;
            Snapshot();
            Ticked?.Invoke();
        };
        // The character files are read while the map load is still blocking, not on the first drawn frame.
        Character(0);
        Character(1);
    }

    /// <summary>Sets the converted content folder (the one holding <c>svmap</c> and <c>shared</c>).</summary>
    public static void Configure(string contentRoot)
    {
        // The bridge's own folder stands for the exe's: its maps/ holds the maps shipped with the bridge (TestBox).
        // The runtime Unreal hosts has no base directory of its own, so the default catalog cannot find them there.
        ContentPaths.Configure(contentRoot, exeDir: Path.GetDirectoryName(typeof(BridgeSession).Assembly.Location));
    }

    /// <summary>Hosts <paramref name="map"/> locally and joins it. Blocks while the map loads.</summary>
    public static BridgeSession StartLocal(string map, string mode, bool merc, int bots) =>
        StartLocal(new LocalMatch { Map = map, Mode = mode, Merc = merc, Bots = bots });

    /// <summary>Hosts a local match and joins it. Blocks while the map loads.</summary>
    public static BridgeSession StartLocal(LocalMatch match)
    {
        ArgumentNullException.ThrowIfNull(match);
        var settings = new PlaySettings
        {
            Map = match.Map,
            Mode = match.Mode,
            Side = match.Merc ? PlaySide.Merc : PlaySide.Spy,
            Bots = match.Bots,
            Difficulty = match.Difficulty,
            PlayerName = match.PlayerName,
            Sidearms = match.Sidearms,
            Rifles = match.Rifles,
            OfflineRules = match.OfflineRules,
            UnlimitedGear = match.OfflineRules || match.UnlimitedGear,
            StartAt = match.StartAt,
            Rules = match.PlayLevel ? PlayRules.PlayLevel : PlayRules.MapSettings,
            HostMode = match.InlineHost || match.SimLink is not null ? PlayHostMode.Inline : PlayHostMode.Thread,
            SimLink = match.SimLink,
            NetworkProfile = match.NetworkProfile,
        }.ForLocalLaunch();
        var clock = new StopwatchClock();
        var session = match.CreateSession?.Invoke(settings, clock) ?? new PlaySession(settings, clock);
        OverrideRules(session, match);
        return Wrap(session);
    }

    /// <summary>
    /// Puts the player's own time limit, lives and respawn over the rules the match started with. PlaySettings has no
    /// place for them (it starts every local match from the map's untouched match page), so they are written where
    /// the rules read them: the host's match info, its replicated copy and the lives already handed out. It runs on
    /// the host's thread, on its first tick after the game exists.
    /// </summary>
    private static void OverrideRules(PlaySession session, LocalMatch match)
    {
        if (match.TimeLimit < 0 && match.SpyLives < 0 && match.MercLives < 0 && match.RespawnSeconds < 0)
        {
            return;
        }

        bool done = false;
        session.Host.BeforeWorldTick += _ =>
        {
            if (done || session.Loader.Game is not { MatchInfo: { } info } game)
            {
                return;
            }

            done = true;
            bool mercDeathMatch = info.GameMode == EGameMode.GM_MercDeathMatch;
            if (match.TimeLimit >= 0) info.TimeLimit = match.TimeLimit;
            if (match.SpyLives >= 0 && !mercDeathMatch) info.NumLivesATT = match.SpyLives == 0 ? OpenScct.Game.GameFlow.K.UnlimitedLives : match.SpyLives;
            if (match.MercLives >= 0) info.NumLivesDEF = match.MercLives == 0 ? OpenScct.Game.GameFlow.K.UnlimitedLives : match.MercLives;
            if (match.RespawnSeconds >= 0) info.TimesRespawn = match.RespawnSeconds;
            if (game.GameReplicationInfo is { } gri)
            {
                gri.TimeLimit = info.TimeLimit;
                gri.TeamLivesATT = info.NumLivesATT;
                gri.TeamLivesDEF = info.NumLivesDEF;
                gri.TimesRespawn = info.TimesRespawn;
                for (int i = 0; i < gri.PRIArray.Count; i++)
                {
                    if (gri.PRIArray[i] is { Team: { } team } pri)
                    {
                        pri.NumLives = team.TeamIndex == 0 ? info.NumLivesATT : info.NumLivesDEF;
                    }
                }
            }
        };
    }

    /// <summary>
    /// Wraps a round on a remote server: the online visit's play session of that round (it has no host here and leaves
    /// the connection to the visit when disposed). The visit keeps ticking the round on its own clock; this session's
    /// <see cref="Update"/> then only finds the ticks it has not reported yet.
    /// </summary>
    public static BridgeSession FromRemote(PlaySession round)
    {
        ArgumentNullException.ThrowIfNull(round);
        return Wrap(round);
    }

    /// <summary>
    /// The play session is running by now (a local one has its server thread and bots): when the wrapper cannot be
    /// made (a broken character file), the play session is stopped again instead of left ticking with no owner.
    /// </summary>
    private static BridgeSession Wrap(PlaySession play)
    {
        try
        {
            return new BridgeSession(play);
        }
        catch
        {
            play.BeforeClientTick = null;
            play.AfterClientTick = null;
            play.Dispose();
            throw;
        }
    }

    /// <summary>
    /// This session's number: unique in the process, never 0. Unreal keeps it to tell a new match from the one it
    /// knows, and to address <c>StopSession</c> and <c>UpdateSession</c> to the session it started.
    /// </summary>
    public int Id { get; }

    public PlaySession Session => _session;

    /// <summary>True for a round on a remote server, false for a local match.</summary>
    public bool IsRemote => _session.IsRemote;

    public bool IsDisposed => _disposed;

    /// <summary>Client simulation ticks run so far.</summary>
    public long Ticks { get; private set; }

    /// <summary>The map's folder name.</summary>
    public string Map => Path.GetFileName(_session.MapFolder.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));

    /// <summary>The local player's side: his pawn's once he has one, else the side the session was started for.</summary>
    public bool LocalMerc => _session.LocalPawn is { } pawn ? pawn is SPAWNDEFENSE : _session.Settings.Side == PlaySide.Merc;

    /// <summary>
    /// Raised on Unreal's game thread just before each client simulation tick, before the tick's input is applied
    /// and before the queued exec commands run. What those commands raise is NOT readable here (it is on
    /// <c>Session.Client.BeforeWorldTick</c>, for a handler added after the session was made).
    /// </summary>
    public event Action? BeforeTick;

    /// <summary>
    /// Raised on Unreal's game thread after each client simulation tick, once the pawns are snapshotted and posed.
    /// Handlers run in the order they were added, which is the order the features were made.
    /// </summary>
    public event Action? Ticked;

    /// <summary>Raised once when the session ends, after its features were disposed and before the play session stops.</summary>
    public event Action? Ended;

    /// <summary>
    /// The per-session state of a bridge feature (events, HUD, devices...), made on first use. Features live in
    /// their own files and keep their state here, so it ends with the session: a feature that implements
    /// <see cref="IDisposable"/> is disposed when the session is (the last made first), which is where it takes its
    /// hooks off objects that outlive the session (a remote round's connection goes on to the results and the next
    /// map).
    /// </summary>
    public T Feature<T>(Func<BridgeSession, T> create)
        where T : class
    {
        if (!_features.TryGetValue(typeof(T), out object? feature))
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            feature = create(this);
            _features[typeof(T)] = feature;
            _featureOrder.Add(feature);
        }

        return (T)feature;
    }

    /// <summary>
    /// Runs the simulation ticks that are due, and returns how many client ticks ran since the last call. For a
    /// remote round the online visit runs the ticks (on its own Update, earlier in the frame); they are counted
    /// here all the same, so "a tick happened" reads the same for both kinds of session.
    /// </summary>
    public int Update()
    {
        if (_disposed || _session.IsStopped)
        {
            return 0;
        }

        if (!_session.IsRemote)
        {
            // The frame drawn since the last call, as the Godot client's PlayRoot reports it after drawing: it sets
            // the time the player's shots are traced at (lag compensation). The online visit reports its rounds.
            _session.Client.ReportRenderedFrame(_session.Alpha);
        }

        _session.Update();
        int ticks = _ticksUnread;
        _ticksUnread = 0;
        return ticks;
    }

    public float Alpha => _session.Alpha;

    public string? Failure => _session.ServerFailure?.ToString() ?? _session.LoginError;

    public unsafe void SetInput(in BridgeInput input)
    {
        LocalInput target = _session.Input;
        for (int i = 0; i < BridgeInput.ButtonCount && i < LocalInput.ButtonNames.Count; i++)
        {
            target.Set(LocalInput.ButtonNames[i], input.Buttons[i] != 0);
        }

        // Mouse counts add up until the next tick takes them; a frame without a tick must not lose them.
        target.MouseX += input.MouseX;
        target.MouseY += input.MouseY;
        target.Up = input.Up;
    }

    /// <summary>Queues an exec command (Jump, Fire, SwitchHeadset...) for the next tick.</summary>
    public void Command(string name, int arg) => _session.Input.Commands.Add(new InputCommand(name, arg));

    /// <summary>
    /// True when the local pawn is seen from its own eyes, so its body is not drawn in the main view (only its
    /// shadow): the Godot client's rule (game/Scripts/Play/PlayRoot.cs DrawViews, "firstPerson"): no free camera, and
    /// either no behind view or the spy's first-person view.
    /// </summary>
    public bool HideLocalBody =>
        _session.LocalController is { FreeCameraFlying: 0 } pc && (!pc.bBehindView || _session.View.SpyFirstPerson) &&
        _session.LocalPawn is not null;

    /// <summary>Writes the pawns as drawn this frame. Returns the count written.</summary>
    public int GetPawns(Span<BridgePawn> pawns)
    {
        float alpha = Alpha;
        bool hideLocal = HideLocalBody;
        int count = 0;
        foreach (int id in _order)
        {
            if (count >= pawns.Length)
            {
                break;
            }

            PawnSample now = _current[id];
            PawnSample from = _previous.TryGetValue(id, out PawnSample before) && before.ResetId == now.ResetId &&
                DistanceSquared(before.Location, now.Location) <= PawnCutDistanceSquared
                ? before
                : now;

            FVector location = Lerp(from.Location, now.Location, alpha);
            FVector prePivot = Lerp(from.PrePivot, now.PrePivot, alpha);
            pawns[count++] = new BridgePawn
            {
                ActorId = id,
                Kind = now.Merc ? 1 : 0,
                Team = now.Team,
                Health = now.Health,
                Flags = (now.Visible ? BridgePawnFlags.Visible : 0) | (now.Local ? BridgePawnFlags.Local : 0) |
                    (now.Local && hideLocal ? BridgePawnFlags.FirstPerson : 0),
                X = location.X,
                Y = location.Y,
                Z = location.Z,
                PrePivotX = prePivot.X,
                PrePivotY = prePivot.Y,
                PrePivotZ = prePivot.Z,
                Yaw = Wrap180(LerpAngle(from.Yaw, now.Yaw, alpha)),
                CollisionRadius = now.Radius,
                CollisionHeight = now.Height,
                AimYaw = from.AimYaw + ((now.AimYaw - from.AimYaw) * alpha),
                AimPitch = from.AimPitch + ((now.AimPitch - from.AimPitch) * alpha),
            };
        }

        return count;
    }

    /// <summary>
    /// The character file of a pawn kind (0 spy <c>SPerso.ATT_01</c>, 1 merc <c>SPerso.DEF_01</c>: the teams' stock
    /// meshes, as the Godot client's PawnOutfits.StockAsset) from the shared content of the session's map, or null
    /// when the content does not have it.
    /// </summary>
    public GlbCharacter? Character(int kind) => CharacterOf(kind, SharedContent.FindRoot(_session.MapFolder));

    /// <summary>The same for the configured content, for a caller with no session yet.</summary>
    public static GlbCharacter? ConfiguredCharacter(int kind) => CharacterOf(kind, ContentPaths.Shared);

    private static GlbCharacter? CharacterOf(int kind, string? sharedRoot)
    {
        if (sharedRoot is null || kind is < 0 or > 1)
        {
            return null;
        }

        string path = Path.Combine(sharedRoot, "chars", "SPerso", kind == 1 ? "DEF_01.glb" : "ATT_01.glb");
        return File.Exists(path) ? GlbCharacter.Load(path) : null;
    }

    /// <summary>
    /// Writes the bones of a character's skeleton, in the order <see cref="GetPawnPose"/> writes poses. Returns the
    /// bone count; bones past the end of <paramref name="bones"/> are not written.
    /// </summary>
    public static unsafe int GetSkeleton(GlbCharacter character, Span<BridgeBoneInfo> bones)
    {
        int count = Math.Min(bones.Length, character.BoneCount);
        for (int b = 0; b < count; b++)
        {
            ref BridgeBoneInfo bone = ref bones[b];
            bone.Parent = character.Parents[b];
            string name = character.BoneNames[b];
            int length = Math.Min(name.Length, BridgeBoneInfo.NameCapacity - 1);
            fixed (char* target = bone.Name)
            {
                name.AsSpan(0, length).CopyTo(new Span<char>(target, length));
                new Span<char>(target + length, BridgeBoneInfo.NameCapacity - length).Clear();
            }
        }

        return character.BoneCount;
    }

    /// <summary>
    /// Writes a pawn's pose as drawn this frame (the same <see cref="Alpha"/> as <see cref="GetPawns"/>). Returns the
    /// bone count, or 0 when the pawn has no pose (unknown, not ticked yet, or its character file is missing); bones
    /// past the end of <paramref name="bones"/> are not written.
    /// </summary>
    public int GetPawnPose(int actorId, Span<BridgeBone> bones) =>
        _posers.TryGetValue(actorId, out PawnPoser? poser) ? poser.Pose(Alpha, bones) : 0;

    /// <summary>The poser of a pawn (for the probe), or null.</summary>
    public PawnPoser? PoserOf(int actorId) => _posers.TryGetValue(actorId, out PawnPoser? poser) ? poser : null;

    /// <summary>
    /// The view between the last two ticks, with no look-ahead: the mouse turn the simulation has not taken yet is
    /// not in it. The controls' own GetView (Controls/ControlsFeature.cs) is the camera to draw; this one stays for
    /// callers without the controls table and carries <see cref="BridgeView.HideLocalBody"/>.
    /// </summary>
    public BridgeView GetView()
    {
        ViewSample now = _session.View;
        if (!now.Valid)
        {
            return default;
        }

        ViewSample from = _session.PreviousView;
        bool cut = !from.Valid || from.SpyFirstPerson != now.SpyFirstPerson ||
            DistanceSquared(from.Location, now.Location) > ViewCutDistance * ViewCutDistance;
        if (cut)
        {
            from = now;
        }

        float alpha = Alpha;
        FVector location = Lerp(from.Location, now.Location, alpha);
        return new BridgeView
        {
            X = location.X,
            Y = location.Y,
            Z = location.Z,
            Pitch = Wrap180(LerpAngle(Degrees(from.Rotation.Pitch), Degrees(now.Rotation.Pitch), alpha)),
            Yaw = Wrap180(LerpAngle(Degrees(from.Rotation.Yaw), Degrees(now.Rotation.Yaw), alpha)),
            Roll = Wrap180(LerpAngle(Degrees(from.Rotation.Roll), Degrees(now.Rotation.Roll), alpha)),
            Fov = from.Fov + (now.Fov - from.Fov) * alpha,
            Valid = 1,
            SpyFirstPerson = now.SpyFirstPerson ? 1 : 0,
            HideLocalBody = HideLocalBody ? 1 : 0,
        };
    }

    /// <summary>
    /// Ends the session: the features are disposed (the last made first), then the play session (a local match stops
    /// its server thread and bots; a remote round only stops, its connection stays with the online visit). Safe to
    /// call twice. A feature that throws while it is disposed does not keep the rest from ending; the first such
    /// error is thrown once everything is stopped.
    /// </summary>
    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        Exception? first = null;
        for (int i = _featureOrder.Count - 1; i >= 0; i--)
        {
            if (_featureOrder[i] is IDisposable feature)
            {
                try
                {
                    feature.Dispose();
                }
                catch (Exception e)
                {
                    first ??= e;
                }
            }
        }

        try
        {
            Ended?.Invoke();
        }
        catch (Exception e)
        {
            first ??= e;
        }

        BeforeTick = null;
        Ticked = null;
        Ended = null;
        _session.BeforeClientTick = null;
        _session.AfterClientTick = null;
        _session.Dispose();
        _posers.Clear();
        if (first is not null)
        {
            throw new InvalidOperationException("A bridge feature failed while the session ended: " + first.Message, first);
        }
    }

    private void Snapshot()
    {
        _previous.Clear();
        foreach (KeyValuePair<int, PawnSample> pair in _current)
        {
            _previous[pair.Key] = pair.Value;
        }

        _current.Clear();
        _order.Clear();
        SBasePawn? local = _session.LocalPawn;
        foreach (SBasePawn pawn in _session.Client.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            _current[pawn.ActorId] = new PawnSample
            {
                Location = pawn.Location + pawn.NetworkCorrectionOffset,
                PrePivot = pawn.PrePivot,
                Yaw = Degrees(pawn.Rotation.Yaw),
                ResetId = pawn.NetworkResetId,
                Merc = pawn is SPAWNDEFENSE,
                Team = TeamOf(pawn),
                Health = pawn.Health,
                Visible = !pawn.bHidden && (pawn.Role != ENetRole.ROLE_SimulatedProxy || pawn.NetworkPoseReady),
                Local = ReferenceEquals(pawn, local),
                Radius = pawn.CollisionRadius,
                Height = pawn.CollisionHeight,
                AimYaw = pawn.PawnAnim.Aim.FullYaw * RadiansToDegrees,
                AimPitch = pawn.PawnAnim.Aim.FullPitch * RadiansToDegrees,
            };
            _order.Add(pawn.ActorId);
            PoserFor(pawn)?.OnTick();
        }

        foreach (int id in _posers.Keys)
        {
            if (!_current.ContainsKey(id))
            {
                _gone.Add(id);
            }
        }

        foreach (int id in _gone)
        {
            _posers.Remove(id);
        }

        _gone.Clear();
    }

    /// <summary>
    /// A pawn's team on a client: the team of its replicated player info, and its kind while that has not arrived
    /// (as the Godot client's game/Scripts/Play/Demo/DemoXRayView.cs). The controller cannot be asked: another
    /// player's pawn has none on a client, and <c>SGameInfo.TeamIndexOf(null)</c> is 0 for everyone.
    /// </summary>
    private static int TeamOf(SBasePawn pawn) => pawn.PlayerReplicationInfo?.Team?.TeamIndex ?? (pawn is SPAWNDEFENSE ? 1 : 0);

    /// <summary>The pawn's poser, made on its first tick (a respawn is a new pawn and starts a new pose).</summary>
    private PawnPoser? PoserFor(SBasePawn pawn)
    {
        if (_posers.TryGetValue(pawn.ActorId, out PawnPoser? poser) && ReferenceEquals(poser.Pawn, pawn))
        {
            return poser;
        }

        if (Character(pawn is SPAWNDEFENSE ? 1 : 0) is not { } character)
        {
            _posers.Remove(pawn.ActorId);
            return null;
        }

        poser = new PawnPoser(pawn, character);
        _posers[pawn.ActorId] = poser;
        return poser;
    }

    private const float RadiansToDegrees = 180f / MathF.PI;

    private static float Degrees(int rotatorUnits) => rotatorUnits * (360f / 65536f);

    private static FVector Lerp(FVector a, FVector b, float t) =>
        new(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t);

    private static float LerpAngle(float a, float b, float t)
    {
        float delta = (b - a) % 360f;
        if (delta > 180f)
        {
            delta -= 360f;
        }
        else if (delta < -180f)
        {
            delta += 360f;
        }

        return a + delta * t;
    }

    /// <summary>An angle in degrees brought into -180 to 180 (the simulation's rotators wind up past a turn).</summary>
    private static float Wrap180(float degrees)
    {
        degrees %= 360f;
        return degrees > 180f ? degrees - 360f : degrees < -180f ? degrees + 360f : degrees;
    }

    private static float DistanceSquared(FVector a, FVector b)
    {
        float x = a.X - b.X, y = a.Y - b.Y, z = a.Z - b.Z;
        return x * x + y * y + z * z;
    }

    private struct PawnSample
    {
        public FVector Location, PrePivot;
        public float Yaw, Radius, Height, AimYaw, AimPitch;
        public uint ResetId;
        public bool Merc, Visible, Local;
        public int Team, Health;
    }

    private sealed class StopwatchClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
