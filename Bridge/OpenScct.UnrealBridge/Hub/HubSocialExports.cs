using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.UnrealBridge.Hub;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The hub HUD's table (Source/OpenSCCT/Runtime/ScctHubSocialApi.h mirrors it): what <see cref="HubSocial"/> reads and
/// sends for the visit of <see cref="HubBridge"/>, plus the hosted hub's bot visitors. Unreal asks for
/// <see cref="GetApi"/> once and keeps the table. Every entry catches its exceptions (none may cross into native code);
/// an unexpected one goes to the main table's LastError and the call returns its empty answer, the visit goes on.
/// Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubSocialApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<HubSocialState*, int> GetState;
    public delegate* unmanaged[Cdecl]<HubSocialPlayer*, int, int> GetPlayers;
    public delegate* unmanaged[Cdecl]<float, float, float, HubSocialTag*, int, int> GetNameTags;
    public delegate* unmanaged[Cdecl]<int, HubSocialChatLine*, int, int> GetChat;
    public delegate* unmanaged[Cdecl]<float, float, float, HubSocialSign*, int, int> GetSigns;
    public delegate* unmanaged[Cdecl]<HubSocialDestination*, int, int> GetDestinations;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<float, float, float, char*, int, int> GetPrompt;
    public delegate* unmanaged[Cdecl]<char*, char*, int, int> GetKeyHint;
    public delegate* unmanaged[Cdecl]<int, char*, int> SendChat;
    public delegate* unmanaged[Cdecl]<char*, int> RunCommand;
    public delegate* unmanaged[Cdecl]<char*, int> Travel;
    public delegate* unmanaged[Cdecl]<char*, int, void> MuteChat;
    public delegate* unmanaged[Cdecl]<int, int> StartBots;
}

public static unsafe class HubSocialExports
{
    public const int ApiVersion = 1;

    private static HubSocial? s_social;

    private static HubSocial Social => s_social ??= new HubSocial(HubBridge.Instance);

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HubSocialApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetState = &GetState;
        api->GetPlayers = &GetPlayers;
        api->GetNameTags = &GetNameTags;
        api->GetChat = &GetChat;
        api->GetSigns = &GetSigns;
        api->GetDestinations = &GetDestinations;
        api->GetText = &GetText;
        api->GetPrompt = &GetPrompt;
        api->GetKeyHint = &GetKeyHint;
        api->SendChat = &SendChat;
        api->RunCommand = &RunCommand;
        api->Travel = &Travel;
        api->MuteChat = &MuteChat;
        api->StartBots = &StartBots;
        return ApiVersion;
    }

    /// <summary>Writes what the HUD reads every frame. Returns Valid (1 while in a hub).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(HubSocialState* state)
    {
        if (state == null)
        {
            return 0;
        }

        try
        {
            *state = Social.State();
            return state->Valid;
        }
        catch (Exception e)
        {
            Failed(e);
            *state = default;
            state->Team = -1;
            return 0;
        }
    }

    /// <summary>Writes the roster (by side, then by name). Returns the number of players (only <paramref name="capacity"/> are written).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPlayers(HubSocialPlayer* players, int capacity) =>
        Guarded(() => Social.Players(Span(players, capacity)));

    /// <summary>Writes the name tags round the eye (UE units), farthest first. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetNameTags(float x, float y, float z, HubSocialTag* tags, int capacity) =>
        Guarded(() => Social.NameTags(new FVector(x, y, z), Span(tags, capacity)));

    /// <summary>Writes the chat lines newer than serial <paramref name="after"/>, oldest first. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetChat(int after, HubSocialChatLine* lines, int capacity) =>
        Guarded(() => Social.Chat(after, Span(lines, capacity)));

    /// <summary>Writes the world signs within reach of the eye, nearest first. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSigns(float x, float y, float z, HubSocialSign* signs, int capacity) =>
        Guarded(() => Social.Signs(new FVector(x, y, z), Span(signs, capacity)));

    /// <summary>Writes the travel menu's districts. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetDestinations(HubSocialDestination* destinations, int capacity) =>
        Guarded(() => Social.Destinations(Span(destinations, capacity)));

    /// <summary>Copies a text (<see cref="HubSocialText"/>). Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity) =>
        Copy(buffer, capacity, () => Social.Text((HubSocialText)kind));

    /// <summary>Copies what U does where the eye (UE units) is, empty for nothing. Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPrompt(float x, float y, float z, char* buffer, int capacity) =>
        Copy(buffer, capacity, () => Social.Prompt(new FVector(x, y, z)));

    /// <summary>Copies the key the player's bindings put a command on ("P"), or "Unbound". Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetKeyHint(char* command, char* buffer, int capacity)
    {
        string name = command == null ? string.Empty : new string(command);
        return Copy(buffer, capacity, () => Social.KeyHint(name));
    }

    /// <summary>Says a chat line on a channel (<see cref="HubChatChannel"/>). Returns 1 when something was sent.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SendChat(int channel, char* text)
    {
        string line = text == null ? string.Empty : new string(text);
        return Guarded(() => Social.Send((HubChatChannel)Math.Clamp(channel, 0, 2), line) ? 1 : 0);
    }

    /// <summary>Runs a hub command that talks to the hub (HubSpy, HubMerc, HubPlaza, HubUse, HubCancel, HubWatch, HubEmote1..8). Returns 1 when it ran.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RunCommand(char* command)
    {
        string name = command == null ? string.Empty : new string(command);
        return Guarded(() => Social.Command(name) ? 1 : 0);
    }

    /// <summary>Travels to the district with this id. Returns 1 when asked.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Travel(char* id)
    {
        string name = id == null ? string.Empty : new string(id);
        return Guarded(() => Social.Travel(name) ? 1 : 0);
    }

    /// <summary>Hides (1) or shows again (0) a player's chat lines for the visit, by name.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void MuteChat(char* name, int muted)
    {
        string who = name == null ? string.Empty : new string(name);
        Guarded(() =>
        {
            Social.MuteChat(who, muted != 0);
            return 0;
        });
    }

    /// <summary>Sends that many bot visitors into the hub this PC hosts (0 sends them away). Returns 1 when they are on their way.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartBots(int count) => Guarded(() => HubBridge.Instance.StartBots(count) ? 1 : 0);

    // ---- guards -------------------------------------------------------------------------------------------------

    private static Span<T> Span<T>(T* rows, int capacity)
        where T : unmanaged => rows == null || capacity <= 0 ? default : new Span<T>(rows, capacity);

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

    private static int Copy(char* buffer, int capacity, Func<string> text)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, text());
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    private static void Failed(Exception e) => Exports.ReportFailure(e.ToString());
}
