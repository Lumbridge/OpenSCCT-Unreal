using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using OpenScct.Core;
using OpenScct.Server.Hub;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// What the hub's activities show (an OpenSCCT addition), ported from the Godot client's game/Scripts/Hub/
/// HubHud.Activities.cs (the clock of the course run and the range round, the notices, the nearest score board) and
/// HubProps.cs (which marks are drawn and which checkpoint is next, the pop-up targets' flip, the plaza ball) with the
/// drawing taken out: Unreal draws (Source/OpenSCCT/Hub/ScctHubActivities, ScctHubProps). It also queues the chime
/// cues of the activities for Unreal's synthesiser (HubAudio.Cue) and installs the emote poses
/// (<see cref="HubEmotePoses"/>) as the pose overlay of every drawn pawn. Reads the visit of <see cref="HubBridge"/>;
/// single-threaded: Unreal's game thread only.
/// </summary>
public sealed class HubActivity
{
    /// <summary>Cues kept for Unreal at most (a frame takes them all; more means nobody reads).</summary>
    private const int CuesKept = 32;

    private readonly HubBridge _hub;
    private readonly HubEmotePoses _poses;
    private readonly ConcurrentQueue<string> _cues = new();
    private readonly List<(string Text, bool Big)> _toasts = new();
    private HubClient? _client;

    public HubActivity(HubBridge hub)
    {
        _hub = hub ?? throw new ArgumentNullException(nameof(hub));
        _poses = new HubEmotePoses(hub);
        PawnPoser.Overlay = _poses;
    }

    private HubClient? Client
    {
        get
        {
            HubClient? now = _hub.Client;
            if (!ReferenceEquals(now, _client))
            {
                Attach(now);
            }

            return _client;
        }
    }

    private void Attach(HubClient? client)
    {
        if (_client is { } old)
        {
            old.Play.Cue -= OnCue;
            old.Play.Fanfare -= OnFanfare;
            old.Play.TrackChanged -= OnTrack;
        }

        _client = client;
        _cues.Clear();
        if (client is null)
        {
            return;
        }

        client.Play.Cue += OnCue;
        client.Play.Fanfare += OnFanfare;
        client.Play.TrackChanged += OnTrack;
    }

    private void OnCue(string cue)
    {
        if (_cues.Count < CuesKept)
        {
            _cues.Enqueue(cue);
        }
    }

    private void OnFanfare(string text) => OnCue("fanfare");

    private void OnTrack(int track) => _hub.Log($"the jukebox plays record {track} \"{_client?.Play.TrackTitle}\"");

    // ---- reads --------------------------------------------------------------------------------------------------

    public HubActivityState State()
    {
        if (Client is not { } client)
        {
            return new HubActivityState { Track = -1 };
        }

        HubPlayState play = client.Play;
        int me = client.LocalPlayer?.PlayerReplicationInfo?.PlayerID ?? -1;
        FVector ball = play.HasBall ? play.BallNow() : default;
        var revision = new HashCode();
        foreach (HubMark m in play.Marks)
        {
            revision.Add(m);
        }

        return new HubActivityState
        {
            Valid = 1,
            Flags = (_hub.Round is not null ? HubActivityFlags.InHub : 0)
                | (play.Running ? HubActivityFlags.Running : 0)
                | (play.InRound ? HubActivityFlags.InRound : 0)
                | (play.HasBall ? HubActivityFlags.HasBall : 0)
                | (me >= 0 && play.EmoteOf(me) is not null ? HubActivityFlags.Emoting : 0),
            Track = play.Track,
            MarkCount = play.Marks.Count,
            MarkRevision = revision.ToHashCode(),
            TargetCount = play.Targets.Count,
            LocalPlayerId = me,
            EmotePoses = _poses.Posed,
            Now = (float)play.Now,
            BallX = ball.X,
            BallY = ball.Y,
            BallZ = ball.Z,
        };
    }

    /// <summary>The marks with what HubProps shows of them: a checkpoint the run passed is hidden, the next one pulses.</summary>
    public int Marks(Span<HubActivityMark> target)
    {
        if (Client is not { } client)
        {
            return 0;
        }

        HubPlayState play = client.Play;
        int index = 0;
        for (int i = 0; i < play.Marks.Count && i < target.Length; i++)
        {
            HubMark m = play.Marks[i];
            int flags = HubActivityFlags.Shown;
            if (m.Kind == "checkpoint" && m.Id.StartsWith(play.CourseId + "/", StringComparison.Ordinal))
            {
                // HubProps.HighlightNextCheckpoint: the checkpoints of the course run, in order.
                index++;
                bool next = play.Running && index == play.Passed + 1;
                bool done = play.Running && index <= play.Passed;
                flags = (done ? 0 : HubActivityFlags.Shown) | (next ? HubActivityFlags.Next : 0);
            }

            ref HubActivityMark row = ref target[i];
            row.Kind = (int)KindOf(m.Kind);
            row.Flags = flags;
            row.X = m.At.X;
            row.Y = m.At.Y;
            row.Z = m.At.Z;
            row.Radius = m.Radius;
            unsafe
            {
                fixed (HubActivityMark* r = &row)
                {
                    OnlineTextCopy.Fixed(r->Id, HubActivityMark.IdCapacity, m.Id);
                }
            }
        }

        return play.Marks.Count;
    }

    /// <summary>The range's targets with their flip (up and down about the foot over a fifth of a second; HubProps.DrawTargets).</summary>
    public int Targets(Span<HubActivityTarget> target)
    {
        if (Client is not { } client)
        {
            return 0;
        }

        HubPlayState play = client.Play;
        int me = client.LocalPlayer?.PlayerReplicationInfo?.PlayerID ?? -1;
        int i = 0;
        foreach (HubTargetView t in play.Targets.Values)
        {
            if (i < target.Length)
            {
                float since = (float)(play.Now - t.ChangedAt);
                float raise = Math.Clamp(since / 0.2f, 0f, 1f);
                ref HubActivityTarget row = ref target[i];
                row.Id = t.Id;
                row.Flags = (t.Owner == me ? HubActivityFlags.Mine : 0) | (t.Up || raise < 1f ? HubActivityFlags.Up : 0);
                row.X = t.Foot.X;
                row.Y = t.Foot.Y;
                row.Z = t.Foot.Z;
                row.Angle = t.Up ? (1f - raise) * 90f : raise * 90f;
            }

            i++;
        }

        return play.Targets.Count;
    }

    public string Text(HubActivityText kind, FVector eye)
    {
        if (Client is not { } client)
        {
            return string.Empty;
        }

        HubPlayState play = client.Play;
        switch (kind)
        {
            case HubActivityText.Activity:
                return ActivityLine(play);
            case HubActivityText.Notices:
            {
                play.Toasts(_toasts);
                var text = new StringBuilder();
                foreach ((string line, bool big) in _toasts)
                {
                    text.Append(big ? "> " + line + " <" : line).Append('\n');
                }

                return text.ToString().TrimEnd('\n');
            }

            case HubActivityText.BoardTitle:
                return NearestBoard(client, eye) is { } board ? board.Lines[0] : string.Empty;
            case HubActivityText.BoardLines:
                return NearestBoard(client, eye) is { } rows ? string.Join('\n', rows.Lines, 1, rows.Lines.Length - 1) : string.Empty;
            case HubActivityText.TrackTitle:
                return play.TrackTitle;
            case HubActivityText.LocalEmote:
            {
                int me = client.LocalPlayer?.PlayerReplicationInfo?.PlayerID ?? -1;
                return me >= 0 && play.EmoteState(me, out string emote, out double since, out _)
                    ? string.Create(CultureInfo.InvariantCulture, $"{emote} {since:F1} s") : string.Empty;
            }

            default:
                return string.Empty;
        }
    }

    /// <summary>The next chime cue the activities raised ("start", "cp", "fall", "finish", "best", "hit", "head", "end", "fanfare"), or false.</summary>
    public bool TakeCue(out string cue)
    {
        _ = Client;
        return _cues.TryDequeue(out cue!);
    }

    // ---- helpers ------------------------------------------------------------------------------------------------

    /// <summary>The clock of what the player is doing (HubHud.DrawActivities).</summary>
    private static string ActivityLine(HubPlayState play)
    {
        if (play.Running)
        {
            return $"{play.CourseTitle}  {HubScores.Time(play.RunMs)}\n" +
                   (play.CourseId == "easy" ? $"checkpoint {play.Passed}/{play.Checkpoints}" : $"section {Math.Min(play.Passed + 1, play.Checkpoints)}/{play.Checkpoints}  {play.SectionTitle}") +
                   (play.LastSplit >= 0 ? $"   split {HubScores.Time(play.LastSplit)}" : string.Empty) +
                   $"   falls {play.Falls}";
        }

        if (play.InRound)
        {
            double left = Math.Max(0.0, play.RoundEndsAt - play.Now);
            int accuracy = play.RangeShots > 0 ? (int)Math.Round(100.0 * play.RangeHits / play.RangeShots) : 0;
            return string.Create(CultureInfo.InvariantCulture,
                $"RANGE  {left:0.0} s   {play.RangePoints} pts\nhits {play.RangeHits}/{play.RangeShots}  {accuracy}%   headshots {play.RangeHeads}");
        }

        return play.Now - play.LastRunAt < 6.0 ? "COURSE  " + HubScores.Time(play.LastRun) : string.Empty;
    }

    /// <summary>
    /// The nearest board within <see cref="HubPlayK.BoardShowRange"/> of the eye when the world does not show it: a
    /// board the hub put up as a world sign is read there (the HUD's signs), not twice (HubHud.DrawBoard).
    /// </summary>
    private static HubBoard? NearestBoard(HubClient client, FVector eye)
    {
        HubBoard? nearest = null;
        float best = HubPlayK.BoardShowRange;
        foreach ((string id, HubBoard b) in client.Play.Boards)
        {
            float d = (b.At - eye).Size();
            if (d < best && b.Lines.Length > 0 && !client.Signs.ContainsKey(id))
            {
                best = d;
                nearest = b;
            }
        }

        return nearest;
    }

    private static HubMarkKind KindOf(string kind) => kind switch
    {
        "start" => HubMarkKind.Start,
        "finish" => HubMarkKind.Finish,
        "checkpoint" => HubMarkKind.Checkpoint,
        "travel" => HubMarkKind.Travel,
        "firingline" => HubMarkKind.FiringLine,
        "jukebox" => HubMarkKind.Jukebox,
        "laptop" => HubMarkKind.Laptop,
        "goal" => HubMarkKind.Goal,
        _ => HubMarkKind.Other,
    };
}
