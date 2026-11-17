using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Server;
using OpenScct.UnrealBridge.Pose;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The one play session Unreal drives: a local match (the dedicated server's code on its own thread, with bots) that
/// the local client joins. Unreal calls <see cref="Update"/> once a frame on its game thread and then reads the
/// drawn state; nothing here is touched from another thread.
/// </summary>
public sealed class BridgeSession : IDisposable
{
    public const int ApiVersion = 2;

    /// <summary>A view jump longer than this is drawn as a cut, not blended (as the Godot client).</summary>
    private const float ViewCutDistance = 128f;

    /// <summary>A pawn that moved farther than this in one tick was placed, not walked: no blend.</summary>
    private const float PawnCutDistanceSquared = 40000f;

    private readonly PlaySession _session;
    private readonly Dictionary<int, PawnSample> _previous = new();
    private readonly Dictionary<int, PawnSample> _current = new();
    private readonly List<int> _order = new();
    private readonly Dictionary<int, PawnPoser> _posers = new();
    private readonly List<int> _gone = new();

    private BridgeSession(PlaySession session)
    {
        _session = session;
        _session.AfterClientTick = (_, _) => Snapshot();
        // The character files are read while the map load is still blocking, not on the first drawn frame.
        Character(0);
        Character(1);
    }

    /// <summary>Sets the converted content folder (the one holding <c>svmap</c> and <c>shared</c>).</summary>
    public static void Configure(string contentRoot)
    {
        ContentPaths.Configure(contentRoot, exeDir: null);
    }

    /// <summary>Hosts <paramref name="map"/> locally and joins it. Blocks while the map loads.</summary>
    public static BridgeSession StartLocal(string map, string mode, bool merc, int bots)
    {
        var settings = new PlaySettings
        {
            Map = map,
            Mode = mode,
            Side = merc ? PlaySide.Merc : PlaySide.Spy,
            Bots = bots,
            PlayerName = "Player",
        }.ForLocalLaunch();
        return new BridgeSession(new PlaySession(settings, new StopwatchClock()));
    }

    public PlaySession Session => _session;

    /// <summary>Runs the simulation ticks that are due. Returns how many ran.</summary>
    public int Update() => _session.Update();

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

    /// <summary>Writes the pawns as drawn this frame. Returns the count written.</summary>
    public int GetPawns(Span<BridgePawn> pawns)
    {
        float alpha = Alpha;
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
                Flags = (now.Visible ? BridgePawnFlags.Visible : 0) | (now.Local ? BridgePawnFlags.Local : 0),
                X = location.X,
                Y = location.Y,
                Z = location.Z,
                PrePivotX = prePivot.X,
                PrePivotY = prePivot.Y,
                PrePivotZ = prePivot.Z,
                Yaw = LerpAngle(from.Yaw, now.Yaw, alpha),
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
            Pitch = LerpAngle(Degrees(from.Rotation.Pitch), Degrees(now.Rotation.Pitch), alpha),
            Yaw = LerpAngle(Degrees(from.Rotation.Yaw), Degrees(now.Rotation.Yaw), alpha),
            Roll = LerpAngle(Degrees(from.Rotation.Roll), Degrees(now.Rotation.Roll), alpha),
            Fov = from.Fov + (now.Fov - from.Fov) * alpha,
            Valid = 1,
            SpyFirstPerson = now.SpyFirstPerson ? 1 : 0,
        };
    }

    public void Dispose() => _session.Dispose();

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
                Team = SGameInfo.TeamIndexOf(pawn.Controller),
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
