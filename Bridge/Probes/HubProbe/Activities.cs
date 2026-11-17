using System;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Hub;

// The hub activities' table, called through its pointers as Unreal calls it, in the hosted hub of the social mode:
// the marks the props draw (gates, checkpoints, the firing line, the jukebox, the laptops, the goals), the jukebox's
// record, the ball, the activity texts and the nearest board, and an emote (HubEmote3, cheer) the hub sends back so
// the local player wears it.
internal static unsafe partial class Probe
{
    private static bool Activities(float ex, float ey, float ez)
    {
        HubActivityApi api = default;
        delegate* unmanaged<HubActivityApi*, int> getApi = &HubActivityExports.GetApi;
        int version = getApi(&api);
        Console.WriteLine($"activity table: version {version}, {sizeof(HubActivityApi)} bytes; state {sizeof(HubActivityState)}, mark {sizeof(HubActivityMark)}, target {sizeof(HubActivityTarget)}, voice mix {sizeof(HubVoiceMixRow)} bytes");
        if (sizeof(HubActivityApi) != 8 + 6 * sizeof(nint) || sizeof(HubActivityState) != 48 || sizeof(HubActivityMark) != 120 || sizeof(HubActivityTarget) != 24 || sizeof(HubVoiceMixRow) != 96)
        {
            Console.WriteLine("FAIL: the layout does not match Runtime/ScctHubActivityApi.h");
            return false;
        }

        HubActivityState state;
        api.GetState(&state);
        Console.WriteLine($"activities: valid {state.Valid} flags 0x{state.Flags:x} track {state.Track} marks {state.MarkCount} targets {state.TargetCount} me {state.LocalPlayerId} ball ({state.BallX:F0}, {state.BallY:F0}, {state.BallZ:F0})");
        var marks = new HubActivityMark[64];
        int markCount;
        fixed (HubActivityMark* m = marks)
        {
            markCount = api.GetMarks(m, marks.Length);
        }

        var kinds = new int[9];
        for (int i = 0; i < Math.Min(markCount, marks.Length); i++)
        {
            fixed (HubActivityMark* m = &marks[i])
            {
                kinds[Math.Clamp(m->Kind, 0, 8)]++;
                if (m->Kind is (int)HubMarkKind.Jukebox or (int)HubMarkKind.Goal or (int)HubMarkKind.Start or (int)HubMarkKind.Laptop)
                {
                    Console.WriteLine($"   mark {(HubMarkKind)m->Kind} '{new string(m->Id)}' at ({m->X:F0}, {m->Y:F0}, {m->Z:F0}) radius {m->Radius:F0} flags 0x{m->Flags:x}");
                }
            }
        }

        Console.WriteLine($"marks by kind: start {kinds[1]} finish {kinds[2]} checkpoint {kinds[3]} travel {kinds[4]} firing line {kinds[5]} jukebox {kinds[6]} laptop {kinds[7]} goal {kinds[8]}");
        Console.WriteLine($"record '{ActivityText(api, HubActivityText.TrackTitle, ex, ey, ez)}'; board here '{ActivityText(api, HubActivityText.BoardTitle, ex, ey, ez)}'; notices '{ActivityText(api, HubActivityText.Notices, ex, ey, ez).Replace('\n', '|')}'");

        fixed (char* cheer = "HubEmote3")
        {
            s_social.RunCommand(cheer);
        }

        Run(TimeSpan.FromSeconds(1.5), _ => false);
        api.GetState(&state);
        string emote = ActivityText(api, HubActivityText.LocalEmote, ex, ey, ez);
        Console.WriteLine($"after HubEmote3: flags 0x{state.Flags:x}, local emote '{emote}'");
        // The bots' voices as the local ear would play them: by distance, muffled through the hub's walls.
        var mixes = new HubVoiceMixRow[16];
        int mixCount = 0;
        fixed (HubVoiceMixRow* v = mixes)
        {
            for (int frame = 0; frame < 30; frame++)
            {
                mixCount = api.GetVoiceMixes(ex, ey, ez, 1f / 30f, v, mixes.Length);
            }
        }

        for (int i = 0; i < Math.Min(mixCount, mixes.Length); i++)
        {
            fixed (HubVoiceMixRow* v = &mixes[i])
            {
                Console.WriteLine($"   voice of '{new string(v->Name)}' (player {v->PlayerId}): {v->Distance:F0} uu, gain {v->Gain:F2}, low-pass {v->LowPassHz:F0} Hz{((v->Flags & HubVoiceMixRow.Occluded) != 0 ? ", through a wall" : "")}");
            }
        }

        bool ok = mixCount >= 3 && markCount > 0 && kinds[6] == 1 && state.Track >= 0 && (state.Flags & HubActivityFlags.Emoting) != 0 && emote.StartsWith("cheer", StringComparison.Ordinal);
        if (!ok)
        {
            Console.WriteLine($"FAIL: marks {markCount} (jukebox {kinds[6]}), track {state.Track}, emote '{emote}'");
        }

        return ok;
    }

    private static string ActivityText(HubActivityApi api, HubActivityText kind, float x, float y, float z)
    {
        char* buffer = stackalloc char[2048];
        api.GetText((int)kind, x, y, z, buffer, 2048);
        return new string(buffer);
    }
}
