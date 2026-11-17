using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Game.Movement;
using OpenScct.UnrealBridge.Events;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Test;

/// <summary>
/// The play inspector's view of one session (Test/InspectExports.cs; Source/OpenSCCT/Test/ScctPlayInspector.h): after
/// every client tick it notes which pawns are landing, how far the local pawn moved beyond what its velocity explains
/// (the Godot client's PlayInspector snap check) and how long it moved; on request it describes each client pawn's
/// animation channel 0 and the channels whose sequence has no clip in the simulation or in the drawn character file.
/// </summary>
public sealed class InspectTrack : IDisposable
{
    /// <summary>A client tick that moves the local pawn this much beyond its velocity is a snap (PlayInspector.InspectSnapDistance).</summary>
    public const float SnapDistance = 40f;

    /// <summary>Horizontal speed above which a pawn counts as moving (PlayInspector.InspectMovingSpeed).</summary>
    public const float MovingSpeed = 60f;

    /// <summary>Ticks after a fall during which a walking pawn counts as just landed.</summary>
    private const int LandingTicks = 6;

    private readonly BridgeSession _session;
    private readonly Dictionary<int, long> _lastFalling = new();
    private readonly Dictionary<string, int> _nameIds = new(StringComparer.Ordinal);
    private readonly List<string> _names = [""];
    private SBasePawn? _lastLocal;
    private FVector _lastLocation;
    private long _ticks;

    private InspectTrack(BridgeSession session)
    {
        _session = session;
        _session.Ticked += OnTicked;
    }

    public static InspectTrack Of(BridgeSession session) => session.Feature(static s => new InspectTrack(s));

    public void Dispose() => _session.Ticked -= OnTicked;

    public int Snaps { get; private set; }

    public float WorstSnap { get; private set; }

    public long Ticks => _ticks;

    public long LocalMovingTicks { get; private set; }

    private void OnTicked()
    {
        _ticks++;
        PlaySession play = _session.Session;
        foreach (SBasePawn pawn in play.Client.World.Level.AllActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe && pawn.Physics == EPhysics.PHYS_Falling)
            {
                _lastFalling[pawn.ActorId] = _ticks;
            }
        }

        if (play.LocalPawn is not { bDeleteMe: false } local)
        {
            _lastLocal = null;
            return;
        }

        FVector v = local.Velocity;
        if ((v.X * v.X) + (v.Y * v.Y) > MovingSpeed * MovingSpeed)
        {
            LocalMovingTicks++;
        }

        FVector location = local.Location + local.NetworkCorrectionOffset;
        if (ReferenceEquals(local, _lastLocal))
        {
            FVector step = location - _lastLocation;
            float unexplained = (step - (v * Sim.DeltaTime)).Size();
            if (unexplained > SnapDistance)
            {
                Snaps++;
            }

            WorstSnap = Math.Max(WorstSnap, unexplained);
        }

        _lastLocal = local;
        _lastLocation = location;
    }

    /// <summary>Fills <paramref name="counters"/> (the sound figures from the session's events feature).</summary>
    public void ReadCounters(ref BridgeInspectCounters counters)
    {
        counters.SessionId = _session.Id;
        counters.Snaps = Snaps;
        counters.WorstSnap = WorstSnap;
        counters.ClientTicks = _ticks;
        counters.LocalMovingTicks = LocalMovingTicks;
        EventsFeature events = EventsFeature.Of(_session);
        counters.HasSoundLibrary = events.SoundCount > 0 ? 1 : 0;
        counters.SoundsStarted = events.SoundCallsStarted;
        counters.SoundsUnknown = events.SoundCallsUnknown;
        counters.SoundEvents = events.SoundCallsStarted + events.SoundCallsSilent + events.SoundCallsDuplicate +
            events.SoundCallsOutOfReach + events.SoundCallsUnknown;
    }

    /// <summary>Writes every living client pawn. Returns the count written.</summary>
    public int ReadPawns(Span<BridgeInspectPawn> pawns)
    {
        PlaySession play = _session.Session;
        SBasePawn? local = play.LocalPawn;
        int count = 0;
        foreach (SBasePawn pawn in play.Client.World.Level.AllActors<SBasePawn>())
        {
            if (count >= pawns.Length)
            {
                break;
            }

            if (pawn.bDeleteMe)
            {
                continue;
            }

            Pose.PawnPoser? poser = _session.PoserOf(pawn.ActorId);
            bool walking = pawn.Physics == EPhysics.PHYS_Walking;
            int flags = (ReferenceEquals(pawn, local) ? BridgeInspectPawnFlags.Local : 0) |
                (pawn.bIsCrouched ? BridgeInspectPawnFlags.Crouched : 0) |
                (poser is { Placed: true } && ReferenceEquals(poser.Pawn, pawn) ? BridgeInspectPawnFlags.Posed : 0) |
                (!pawn.bHidden && (pawn.Role != ENetRole.ROLE_SimulatedProxy || pawn.NetworkPoseReady) ? BridgeInspectPawnFlags.Visible : 0) |
                (walking ? BridgeInspectPawnFlags.Walking : 0) |
                (pawn.Physics == EPhysics.PHYS_Falling ? BridgeInspectPawnFlags.Falling : 0) |
                (walking && _lastFalling.TryGetValue(pawn.ActorId, out long fell) && _ticks - fell <= LandingTicks
                    ? BridgeInspectPawnFlags.JustLanded
                    : 0);

            PawnAnimation anim = pawn.PawnAnim;
            PawnAnimChannel c0 = anim[0];
            int missingSim = 0;
            int missingMesh = 0;
            int missingName = 0;
            for (int c = 0; c < PawnAnimation.ChannelCount && c < 31; c++)
            {
                PawnAnimChannel ch = anim[c];
                if (ch.Sequence.IsNone || (c > 0 && !(ch.BlendAlpha > 0f)))
                {
                    continue;
                }

                string sequence = ch.Sequence.ToString();
                bool noSim = ch.Clip is null;
                bool noMesh = !noSim && poser is not null && poser.Character.Find(sequence) is null;
                if (noSim)
                {
                    missingSim |= 1 << c;
                }

                if (noMesh)
                {
                    missingMesh |= 1 << c;
                }

                if ((noSim || noMesh) && missingName == 0)
                {
                    missingName = NameId(sequence);
                }
            }

            FVector at = pawn.Location + pawn.NetworkCorrectionOffset;
            pawns[count++] = new BridgeInspectPawn
            {
                ActorId = pawn.ActorId,
                Flags = flags,
                Channel0Name = c0.Sequence.IsNone ? 0 : NameId(c0.Sequence.ToString()),
                Channel0Frame = c0.Frame,
                Channel0Rate = c0.Rate,
                Channel0Frames = c0.Frames,
                MissingSimClips = missingSim,
                MissingMeshClips = missingMesh,
                MissingName = missingName,
                X = at.X,
                Y = at.Y,
                Z = at.Z,
                VelocityX = pawn.Velocity.X,
                VelocityY = pawn.Velocity.Y,
                VelocityZ = pawn.Velocity.Z,
                CollisionRadius = pawn.CollisionRadius,
                CollisionHeight = pawn.CollisionHeight,
            };
        }

        return count;
    }

    /// <summary>The text of a name id from <see cref="ReadPawns"/>, empty for 0 or an unknown id.</summary>
    public string NameOf(int id) => id > 0 && id < _names.Count ? _names[id] : "";

    private int NameId(string name)
    {
        if (!_nameIds.TryGetValue(name, out int id))
        {
            id = _names.Count;
            _names.Add(name);
            _nameIds[name] = id;
        }

        return id;
    }
}
