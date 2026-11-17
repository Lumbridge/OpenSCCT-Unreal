using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net.Voice;
using OpenScct.Server;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Voice;

/// <summary>
/// How a talker reaches the local player (<see cref="VoiceTalkerInfo.Link"/>), or how the local player reaches an
/// enemy (<see cref="VoiceContext.TalkLink"/>). The server decides who hears whom (OpenSCCT's VoiceRouting, rules 8 to
/// 14, fed by SessionVoiceLinks from its copy of the controllers); the packets carry only the all or team channel, so
/// the client names the link from what its own copy of the round shows.
/// </summary>
public enum VoiceLink
{
    /// <summary>A team mate, or anyone in the lobby or in an Open voice round, on the all channel.</summary>
    All = 0,

    /// <summary>A team mate on the team channel.</summary>
    Team = 1,

    /// <summary>An enemy tagged by the local player's team, heard through Hack Comm (the key, or auto-hack) (rule 13).</summary>
    Hacked = 2,

    /// <summary>The enemy the scope's or the binoculars' voice trace found (rule 12).</summary>
    Trace = 3,

    /// <summary>
    /// Downed chat (rule 9): for a talker, an enemy chatting with the local player while he lies on the ground (being
    /// interrogated); for the local player, talking to a downed enemy next to him with the Hack Comm key held.
    /// </summary>
    Downed = 4,

    /// <summary>Grab talk (rule 14): the spy and the merc he holds, both ways, while the spy holds the Hack Comm key.</summary>
    Grab = 5,
}

/// <summary>
/// The local player's side of the special cases of voice routing, read from the visit's round: the links each heard
/// enemy came through, whether Hack Comm is listening, and whether the HUD's Speak prompt (a held merc, or a downed
/// enemy within fDistToTalkWhenOnGround) is being answered with the Hack Comm key, which makes the microphone talk on
/// the all channel: the only channel those links carry (team talk never crosses to the other side).
/// </summary>
internal static class VoiceLinks
{
    private static readonly Name NAME_s_LyingOnGround = new("s_LyingOnGround");

    /// <summary>The local player's part, once a frame.</summary>
    public readonly record struct Own(VoiceLink TalkLink, bool HackComm, ushort TraceTalker);

    /// <summary>The local controller of the visit's round, or null in the lobby or between maps.</summary>
    public static SPlayerController? LocalController(OnlineClient? client)
    {
        if (client is null || client.CurrentMap is not { } map || MapCatalog.IsMenuMap(map))
        {
            return null;
        }

        return client.Session?.LocalPlayer is SPlayerController { bDeleteMe: false } pc ? pc : null;
    }

    public static Own ReadOwn(SPlayerController? pc)
    {
        if (pc is null)
        {
            return default;
        }

        bool key = pc.bSpecialsChat != 0;
        VoiceLink talk = VoiceLink.All;
        if (key && pc.Pawn is SBasePawn { bDeleteMe: false, Health: > 0 } me && !IsDowned(pc, me))
        {
            if (pc.bCanTalkToGuy || IsGrabbing(me))
            {
                talk = VoiceLink.Grab;
            }
            else if (NearDownedEnemy(pc, me))
            {
                talk = VoiceLink.Downed;
            }
        }

        ushort trace = pc.VoiceChatTarget is Pawn { bDeleteMe: false, PlayerReplicationInfo: { } pri } ? VoiceK.TalkerOf(pri.PlayerID) : (ushort)0;
        return new Own(talk, key || pc.bAutoHackComms, trace);
    }

    /// <summary>
    /// The link a heard talker came through: by his team and the packet's channel for a mate, else by what the round
    /// shows (grab, the local player down, the voice trace's target), and hacked comms for any other enemy, the one
    /// link left that crosses teams in a Classic or Modern round.
    /// </summary>
    public static VoiceLink Of(SPlayerController? pc, ushort talker, EVoiceChannel channel, int team)
    {
        VoiceLink plain = channel == EVoiceChannel.Team ? VoiceLink.Team : VoiceLink.All;
        if (pc is null || talker == 0)
        {
            return plain;
        }

        int localTeam = pc.PlayerReplicationInfo?.Team?.TeamIndex ?? -1;
        if (team is not (0 or 1) || localTeam is not (0 or 1) || team == localTeam ||
            pc.GameReplicationInfo is { VoiceChatMode: EVoiceChatMode.VCM_Open })
        {
            return plain;
        }

        SBasePawn? me = pc.Pawn as SBasePawn;
        SBasePawn? him = PawnOf(pc.XLevel, talker);
        if (me is not null && him is not null && (ReferenceEquals(me.m_AttackTarget, him) || ReferenceEquals(him.m_AttackTarget, me)))
        {
            return VoiceLink.Grab;
        }

        if (me is not null && IsDowned(pc, me))
        {
            return VoiceLink.Downed;
        }

        if (him is not null && ReferenceEquals(pc.VoiceChatTarget, him))
        {
            return VoiceLink.Trace;
        }

        return VoiceLink.Hacked;
    }

    private static SBasePawn? PawnOf(Level level, ushort talker)
    {
        foreach (SBasePawn pawn in level.AllActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe && pawn.PlayerReplicationInfo is { } pri && VoiceK.TalkerOf(pri.PlayerID) == talker)
            {
                return pawn;
            }
        }

        return null;
    }

    private static bool IsDowned(SPlayerController pc, SBasePawn me) => me.bLyingOnGround || pc.IsInState(NAME_s_LyingOnGround);

    private static bool IsGrabbing(SBasePawn me) => me.m_AttackTarget is { bDeleteMe: false };

    /// <summary>The HUD's test for the Speak prompt next to a downed enemy (HudReader.ReadPrompts), for either side.</summary>
    private static bool NearDownedEnemy(SPlayerController pc, SBasePawn me)
    {
        float range = pc.fDistToTalkWhenOnGround;
        if (!(range > 0f))
        {
            return false;
        }

        // By the pawns: a client has the other players' pawns but not their controllers.
        foreach (SBasePawn q in pc.XLevel.AllActors<SBasePawn>())
        {
            if (!q.bDeleteMe && !ReferenceEquals(q, me) && q.SPawnType != me.SPawnType && q.Health > 0 && q.bLyingOnGround &&
                (q.Location - me.Location).Size() < range)
            {
                return true;
            }
        }

        return false;
    }
}
