using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.UnrealBridge.Hub;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The hub activities' table (Source/OpenSCCT/Runtime/ScctHubActivityApi.h mirrors it): what <see cref="HubActivity"/>
/// reads for the visit of <see cref="HubBridge"/>: the activities' state, the marks and targets the props draw, the
/// HUD's activity texts and the chime cues. Fetching the table also installs the emote poses as the pawns' pose overlay.
/// Every entry catches its exceptions (none may cross into native code); an unexpected one goes to the main table's
/// LastError and the call returns its empty answer. Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubActivityApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<HubActivityState*, int> GetState;
    public delegate* unmanaged[Cdecl]<HubActivityMark*, int, int> GetMarks;
    public delegate* unmanaged[Cdecl]<HubActivityTarget*, int, int> GetTargets;
    public delegate* unmanaged[Cdecl]<int, float, float, float, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<char*, int, int> TakeCue;
    public delegate* unmanaged[Cdecl]<float, float, float, float, HubVoiceMixRow*, int, int> GetVoiceMixes;
}

public static unsafe class HubActivityExports
{
    public const int ApiVersion = 1;

    private static HubActivity? s_activity;

    private static HubVoiceHook? s_voice;

    private static HubActivity Activity => s_activity ??= new HubActivity(HubBridge.Instance);

    private static HubVoiceHook Voice => s_voice ??= new HubVoiceHook(HubBridge.Instance);

    /// <summary>Fills <paramref name="api"/> and installs the emote poses. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HubActivityApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        try
        {
            _ = Activity;
        }
        catch (Exception e)
        {
            Failed(e);
        }

        api->Version = ApiVersion;
        api->GetState = &GetState;
        api->GetMarks = &GetMarks;
        api->GetTargets = &GetTargets;
        api->GetText = &GetText;
        api->TakeCue = &TakeCue;
        api->GetVoiceMixes = &GetVoiceMixes;
        return ApiVersion;
    }

    /// <summary>
    /// Updates the proximity mix of every other player's voice as heard from the ear (UE units) over
    /// <paramref name="dt"/> seconds and writes them, nearest first. Returns how many talkers there are.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetVoiceMixes(float x, float y, float z, float dt, HubVoiceMixRow* rows, int capacity) =>
        Guarded(() => Voice.Mixes(new FVector(x, y, z), dt, rows == null || capacity <= 0 ? default : new Span<HubVoiceMixRow>(rows, capacity)));

    /// <summary>Writes the activities' state. Returns Valid (1 while in a hub).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(HubActivityState* state)
    {
        if (state == null)
        {
            return 0;
        }

        try
        {
            *state = Activity.State();
            return state->Valid;
        }
        catch (Exception e)
        {
            Failed(e);
            *state = default;
            state->Track = -1;
            return 0;
        }
    }

    /// <summary>Writes the marks. Returns how many there are (only <paramref name="capacity"/> are written).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMarks(HubActivityMark* marks, int capacity) =>
        Guarded(() => Activity.Marks(marks == null || capacity <= 0 ? default : new Span<HubActivityMark>(marks, capacity)));

    /// <summary>Writes the range's targets. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetTargets(HubActivityTarget* targets, int capacity) =>
        Guarded(() => Activity.Targets(targets == null || capacity <= 0 ? default : new Span<HubActivityTarget>(targets, capacity)));

    /// <summary>Copies a text (<see cref="HubActivityText"/>) as seen from the eye (UE units). Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, float x, float y, float z, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, Activity.Text((HubActivityText)kind, new FVector(x, y, z)));
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>Copies the next chime cue. Returns its length, or -1 when none is waiting.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeCue(char* buffer, int capacity)
    {
        try
        {
            return Activity.TakeCue(out string cue) ? OnlineTextCopy.Buffer(buffer, capacity, cue) : -1;
        }
        catch (Exception e)
        {
            Failed(e);
            return -1;
        }
    }

    private static int Guarded(Func<int> read)
    {
        try
        {
            return read();
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    private static void Failed(Exception e) => Exports.ReportFailure(e.ToString());
}
