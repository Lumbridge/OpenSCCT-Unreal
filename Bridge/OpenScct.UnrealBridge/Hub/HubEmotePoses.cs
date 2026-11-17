using System;
using System.Collections.Generic;
using System.Numerics;
using OpenScct.Game.SBase;
using OpenScct.Server.Hub;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// The hub's emotes drawn as poses (an OpenSCCT addition, spec/additions/hub.md; a port of the Godot client's
/// game/Scripts/Hub/HubEmotePoses.cs): the character packages hold no emote clips, so each emote is a small program of
/// bone turns laid over the pawn's own animation (<see cref="IPawnPoseOverlay"/>, installed as
/// <see cref="PawnPoser.Overlay"/>), blended in and out. Turns are about the pawn's frame: F forward, R right, U up
/// (right-handed in render space, so a negative turn about F raises the right arm out to the side, a positive one the
/// left; a positive turn about R raises an arm forward; a positive turn about U swings the right arm in toward the
/// chest). The bone names are the same in the spy's and the merc's skeletons. These are our own numbers, tuned by eye
/// in the Godot client; they carry no provenance.
/// <para>It poses only while the hub's visit is drawn (<see cref="HubBridge.Round"/>): a pad's match has its own
/// players, whose ids the hub's emotes must not be matched against.</para>
/// </summary>
public sealed class HubEmotePoses : IPawnPoseOverlay
{
    private const float BlendSeconds = 0.25f;

    private static readonly Vector3 F = new(1f, 0f, 0f);
    private static readonly Vector3 R = new(0f, 1f, 0f);
    private static readonly Vector3 U = new(0f, 0f, 1f);

    private readonly HubBridge _hub;

    public HubEmotePoses(HubBridge hub) => _hub = hub ?? throw new ArgumentNullException(nameof(hub));

    /// <summary>Poses that carried an emote since the start (each pawn each drawn frame counts once): for the log.</summary>
    public int Posed { get; private set; }

    /// <summary>The emote of the last pose that carried one ("cheer"), for the log.</summary>
    public string LastEmote { get; private set; } = string.Empty;

    public float Collect(SBasePawn pawn, List<PoseTurn> turns)
    {
        if (_hub.Round is null || _hub.Client?.Play is not { } play || pawn.PlayerReplicationInfo is not { } pri ||
            !play.EmoteState(pri.PlayerID, out string emote, out double since, out double left))
        {
            return 0f;
        }

        // Blend in over the first quarter second, out over the quarter second after it ends.
        float w = (float)Math.Clamp(Math.Min(since / BlendSeconds, 1.0 + (left / BlendSeconds)), 0.0, 1.0);
        if (w <= 0f)
        {
            return 0f;
        }

        Posed++;
        LastEmote = emote;
        float t = (float)since;
        var pose = new EmotePose(turns, w);
        switch (emote)
        {
            case "wave":
                pose.Turn("B R UpperArm", F, -150f);
                pose.Turn("B R ForeArm", F, 25f * MathF.Sin(t * 9f));
                pose.Turn("B Head", U, 8f);
                break;
            case "salute":
                pose.Turn("B R UpperArm", F, -95f);
                pose.Turn("B R UpperArm", R, 25f);
                pose.Turn("B R ForeArm", F, 125f);
                pose.Turn("B Spine1", R, -4f);
                break;
            case "cheer":
            {
                float pump = 12f * MathF.Max(0f, MathF.Sin(t * 7f));
                pose.Turn("B L UpperArm", F, 160f - pump);
                pose.Turn("B R UpperArm", F, -160f + pump);
                pose.Turn("B Head", R, -12f);
                pose.Turn("B Spine1", R, -6f);
                break;
            }

            case "dance":
            {
                float s = MathF.Sin(t * 6f);
                float c = MathF.Cos(t * 6f);
                pose.Turn("B Pelvis", U, 14f * s);
                pose.Turn("B Spine", F, 8f * c);
                pose.Turn("B Spine1", U, -10f * s);
                pose.Turn("B L UpperArm", F, 70f + (25f * c));
                pose.Turn("B R UpperArm", F, -70f + (25f * c));
                pose.Turn("B L ForeArm", F, 50f);
                pose.Turn("B R ForeArm", F, -50f);
                pose.Turn("B Head", F, 10f * s);
                break;
            }

            case "sit":
                // On the floor, legs out in front, leaning back on the hands.
                pose.Turn("B Pelvis", R, -10f);
                pose.Turn("B L Thigh", R, 80f);
                pose.Turn("B R Thigh", R, 80f);
                pose.Turn("B L Calf", R, -15f);
                pose.Turn("B R Calf", R, -15f);
                pose.Turn("B Spine1", R, -10f);
                pose.Turn("B L UpperArm", R, -35f);
                pose.Turn("B R UpperArm", R, -35f);
                pose.Turn("B L UpperArm", F, 15f);
                pose.Turn("B R UpperArm", F, -15f);
                return pose.Drop(72f);
            case "point":
                pose.Turn("B R UpperArm", R, 88f);
                pose.Turn("B R UpperArm", U, -8f);
                pose.Turn("B Head", U, -6f);
                break;
            case "laugh":
            {
                float shake = 5f * MathF.Sin(t * 16f);
                pose.Turn("B Spine1", R, 14f + shake);
                pose.Turn("B Head", R, -18f - shake);
                pose.Turn("B L UpperArm", R, 22f);
                pose.Turn("B R UpperArm", R, 22f);
                pose.Turn("B L UpperArm", U, -20f);
                pose.Turn("B R UpperArm", U, 20f);
                pose.Turn("B L ForeArm", U, -50f);
                pose.Turn("B R ForeArm", U, 50f);
                break;
            }

            case "clap":
            {
                float clap = 18f * (0.5f + (0.5f * MathF.Sin(t * 13f)));
                pose.Turn("B L UpperArm", R, 55f);
                pose.Turn("B R UpperArm", R, 55f);
                pose.Turn("B L UpperArm", U, -22f);
                pose.Turn("B R UpperArm", U, 22f);
                pose.Turn("B L ForeArm", U, -(35f + clap));
                pose.Turn("B R ForeArm", U, 35f + clap);
                break;
            }
        }

        return 0f;
    }

    /// <summary>Adds weighted turns (degrees) and the weighted hips' drop.</summary>
    private readonly struct EmotePose
    {
        private readonly List<PoseTurn> _turns;
        private readonly float _w;

        public EmotePose(List<PoseTurn> turns, float w)
        {
            _turns = turns;
            _w = w;
        }

        public void Turn(string bone, Vector3 axis, float degrees) =>
            _turns.Add(new PoseTurn(bone, axis, degrees * MathF.PI / 180f * _w));

        public float Drop(float uu) => uu * _w;
    }
}
