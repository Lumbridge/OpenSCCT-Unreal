using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Content;
using OpenScct.Content.Loading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Devices;

namespace OpenScct.UnrealBridge;

/// <summary>
/// One map Emitter that rides another actor this frame (<c>GetAttachedEmitters</c>): a mover's rider (AttachTag), or
/// an actor hung on a bone of a skeletal prop (ActorsToAttach) or of a pawn. Source/OpenSCCT/Runtime/ScctWorldEmitterApi.h
/// mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeWorldEmitter
{
    /// <summary>The Emitter's map name in the world table's names (its level particle actors carry "Scct:" + this).</summary>
    public int NameId;

    /// <summary><see cref="BridgeWorldEmitterFlags"/>.</summary>
    public int Flags;

    /// <summary>
    /// Where the Emitter is, blended between the last two ticks (a mover's rider: the simulation carries it). With
    /// OnBone or OnPawn: its RelativeLocation and RelativeRotation in the bone's frame.
    /// </summary>
    public float X, Y, Z;

    /// <summary>Degrees.</summary>
    public float Pitch, Yaw, Roll;

    /// <summary>Where the map placed it (the frame its level particle actors were imported in).</summary>
    public float PlacedX, PlacedY, PlacedZ;

    /// <summary>Degrees.</summary>
    public float PlacedPitch, PlacedYaw, PlacedRoll;

    /// <summary>The name id of the actor it rides: the mover, or the skeletal prop with OnBone; 0 when that actor is not followed.</summary>
    public int AttachNameId;

    /// <summary>With OnBone or OnPawn: the name id of the bone.</summary>
    public int AttachBoneId;

    /// <summary>With OnPawn: the simulation ActorId of the pawn (FScctBridgePawn.ActorId).</summary>
    public int AttachPawnId;
}

public static class BridgeWorldEmitterFlags
{
    /// <summary>Based on a mover or another actor that is not world geometry: X..Roll is its place now.</summary>
    public const int Rides = 1;

    /// <summary>Hung on bone AttachBoneId of the skeletal prop AttachNameId: X..Roll is relative to the drawn bone.</summary>
    public const int OnBone = 2;

    /// <summary>Hung on bone AttachBoneId of pawn AttachPawnId: X..Roll is relative to the drawn bone.</summary>
    public const int OnPawn = 4;
}

/// <summary>
/// The map's placed Emitters that ride another actor, for Unreal: a port of the Godot client's
/// game/Scripts/Views/AttachedEffects.cs for emitters (the lights are in <see cref="WorldState"/>). Each map Emitter
/// is followed after every tick; one based on a mover is handed out at its place blended between the last two ticks
/// (the simulation carries a soft rider with its base), one hung on a bone goes out as the bone and its relative
/// transform, because the simulation has no posed skeleton and Unreal hangs it on the drawn bone. Not ported: the
/// bHardAttach frame (HardRelMatrix), which no Enhanced map sets. It reads the simulation and never writes it.
/// </summary>
public sealed class WorldEmitters
{
    private const float RotatorToDegrees = 360f / 65536f;

    /// <summary>An emitter that moved farther than this in one tick was placed, not carried: no blend.</summary>
    private const float CutDistanceSquared = 40000f;

    private readonly BridgeSession _session;
    private readonly List<Follower> _followers = [];
    private MapLoadResult? _loaded;

    private WorldEmitters(BridgeSession session)
    {
        _session = session;
        TryBind();
        session.Ticked += OnTick;
    }

    /// <summary>The session's emitter followers, made (and hooked to its ticks) on first use.</summary>
    public static WorldEmitters Of(BridgeSession session) => session.Feature(s => new WorldEmitters(s));

    /// <summary>The map Emitters followed (riding now or not).</summary>
    public int Count => _followers.Count;

    private void TryBind()
    {
        if (_loaded is not null || _session.Session.Client.Map?.Loaded is not { } loaded)
        {
            return;
        }

        WorldState world = WorldState.Of(_session);
        _followers.Clear();
        foreach (ActorRecord record in loaded.Gameplay.Actors)
        {
            if (loaded.ActorOf(record) is Emitter emitter && !emitter.bDeleteMe)
            {
                _followers.Add(new Follower(world.Intern(record.Id), emitter));
            }
        }

        _loaded = loaded;
    }

    private void OnTick()
    {
        if (_loaded is null)
        {
            TryBind();
            return;
        }

        foreach (Follower f in _followers)
        {
            f.OnTick();
        }
    }

    /// <summary>Writes the Emitters riding another actor now. Returns how many there are (more than the span holds: only that many written).</summary>
    public int GetAttached(Span<BridgeWorldEmitter> emitters)
    {
        if (_loaded is null)
        {
            TryBind();
            if (_loaded is null)
            {
                return 0;
            }
        }

        WorldState world = WorldState.Of(_session);
        float alpha = Math.Clamp(_session.Alpha, 0f, 1f);
        int total = 0;
        foreach (Follower f in _followers)
        {
            if (!f.Riding)
            {
                continue;
            }

            if (total < emitters.Length)
            {
                emitters[total] = f.Draw(world, alpha);
            }

            total++;
        }

        return total;
    }

    private static float LerpDegrees(int from, int to, float a) =>
        (from + (FRotator.NormalizeAxis(to - from) * a)) * RotatorToDegrees;

    private sealed class Follower(int nameId, Emitter actor)
    {
        private readonly FVector _placedLocation = actor.Location;
        private readonly FRotator _placedRotation = actor.Rotation;
        private FVector _prevLocation = actor.Location;
        private FVector _location = actor.Location;
        private FRotator _prevRotation = actor.Rotation;
        private FRotator _rotation = actor.Rotation;
        private Name _boneName;
        private int _boneId;

        /// <summary>True while the Emitter is based on another actor (a mover, a prop's bone, a pawn's bone): AttachedEffects.Follower.Riding.</summary>
        public bool Riding => actor is { bDeleteMe: false, Base: { bDeleteMe: false, bWorldGeometry: false } };

        public void OnTick()
        {
            _prevLocation = _location;
            _prevRotation = _rotation;
            _location = actor.Location;
            _rotation = actor.Rotation;
        }

        public BridgeWorldEmitter Draw(WorldState world, float alpha)
        {
            var drawn = new BridgeWorldEmitter
            {
                NameId = nameId,
                Flags = BridgeWorldEmitterFlags.Rides,
                PlacedX = _placedLocation.X,
                PlacedY = _placedLocation.Y,
                PlacedZ = _placedLocation.Z,
                PlacedPitch = _placedRotation.Pitch * RotatorToDegrees,
                PlacedYaw = _placedRotation.Yaw * RotatorToDegrees,
                PlacedRoll = _placedRotation.Roll * RotatorToDegrees,
            };

            Actor based = actor.Base!;
            drawn.AttachNameId = world.NameIdOf(based);
            if (!actor.AttachmentBone.IsNone && based is SAnimatedMesh or SBasePawn)
            {
                // AttachedEffects.Place / BonePlace: the bone's frame turned by RelativeRotation and moved by
                // RelativeLocation; the simulation has no posed skeleton, so the bone goes out and C++ hangs it.
                if (actor.AttachmentBone != _boneName)
                {
                    _boneName = actor.AttachmentBone;
                    _boneId = world.Intern(_boneName.ToString());
                }

                drawn.AttachBoneId = _boneId;
                if (based is SBasePawn pawn)
                {
                    drawn.Flags |= BridgeWorldEmitterFlags.OnPawn;
                    drawn.AttachPawnId = pawn.ActorId;
                }
                else
                {
                    drawn.Flags |= BridgeWorldEmitterFlags.OnBone;
                }

                drawn.X = actor.RelativeLocation.X;
                drawn.Y = actor.RelativeLocation.Y;
                drawn.Z = actor.RelativeLocation.Z;
                drawn.Pitch = actor.RelativeRotation.Pitch * RotatorToDegrees;
                drawn.Yaw = actor.RelativeRotation.Yaw * RotatorToDegrees;
                drawn.Roll = actor.RelativeRotation.Roll * RotatorToDegrees;
                return drawn;
            }

            // A soft rider: where the simulation carried it, between the last two ticks (AttachedEffects.Follower.Lerp).
            float a = FVector.DistSquared(_prevLocation, _location) > CutDistanceSquared ? 1f : alpha;
            FVector location = _prevLocation + ((_location - _prevLocation) * a);
            drawn.X = location.X;
            drawn.Y = location.Y;
            drawn.Z = location.Z;
            drawn.Pitch = LerpDegrees(_prevRotation.Pitch, _rotation.Pitch, a);
            drawn.Yaw = LerpDegrees(_prevRotation.Yaw, _rotation.Yaw, a);
            drawn.Roll = LerpDegrees(_prevRotation.Roll, _rotation.Roll, a);
            return drawn;
        }
    }
}

/// <summary>
/// The table of attached-emitter entry points (Source/OpenSCCT/Runtime/ScctWorldEmitterApi.h mirrors it). Every entry
/// works on the running session and returns 0 when there is none.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct WorldEmitterApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<BridgeWorldEmitter*, int, int> GetAttachedEmitters;
}

public static unsafe class WorldEmitterExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(WorldEmitterApi* api)
    {
        api->Version = ApiVersion;
        api->GetAttachedEmitters = &GetAttachedEmitters;
        return ApiVersion;
    }

    /// <summary>
    /// Writes the map Emitters that ride a mover or hang on a bone this frame. Returns how many there are (which can
    /// be more than <paramref name="capacity"/>: only that many are written).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAttachedEmitters(BridgeWorldEmitter* emitters, int capacity)
    {
        try
        {
            if (Exports.Current is not { IsDisposed: false } session)
            {
                return 0;
            }

            Span<BridgeWorldEmitter> span = emitters == null || capacity <= 0 ? default : new Span<BridgeWorldEmitter>(emitters, capacity);
            return WorldEmitters.Of(session).GetAttached(span);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
