using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using OpenScct.Net;
using OpenScct.Server;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The in-match chat's table of entry points: sending a typed line from the round. Unreal asks for
/// <see cref="HudChatExports.GetApi"/> once (<c>Host.GetFunction("OpenScct.UnrealBridge.HudChatExports", "GetApi")</c>)
/// and keeps the table; Source/OpenSCCT/Runtime/ScctHudChatApi.h mirrors it field for field.
/// <para>The line goes out on the running session's own connection (ClientSession.SendChat), which is what Godot's
/// ChatInput does through its SessionChatLink: in a local match that is the connection to the in-process server, in an
/// online round the visit's connection (the online client's Chat does the same call). The server checks the chat
/// rules and relays the line to its recipients, the sender included, so the line comes back as a received chat line
/// (the HUD's Chat or TeamChat message, and the online transcript's line in a visit).</para>
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudChatApi
{
    public int Version;

    /// <summary>(text, teamOnly): 1 when the line was handed to the connection, 0 when there is none or nothing to send.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> Send;

    /// <summary>1 while a session runs whose connection has joined its server (a typed line would go out).</summary>
    public delegate* unmanaged[Cdecl]<int> CanSend;
}

public static unsafe class HudChatExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the chat API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudChatApi* api)
    {
        api->Version = ApiVersion;
        api->Send = &Send;
        api->CanSend = &CanSend;
        return ApiVersion;
    }

    private static ClientSession? Client => Exports.Current?.Session.Client is { State: EClientState.Joined } client ? client : null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CanSend()
    {
        try
        {
            return Client is not null ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Send(char* text, int teamOnly)
    {
        try
        {
            string line = Clean(text == null ? "" : new string(text));
            if (line.Length == 0 || Client is not { } client)
            {
                return 0;
            }

            client.SendChat(teamOnly != 0 ? EChatScope.TeamSay : EChatScope.Say, line);
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The typed line as ChatInput sends it: no control characters, at most ChatK.MaxChars, trimmed.</summary>
    private static string Clean(string text)
    {
        var result = new StringBuilder(Math.Min(text.Length, ChatK.MaxChars));
        foreach (char c in text)
        {
            if (result.Length >= ChatK.MaxChars)
            {
                break;
            }

            if (!char.IsControl(c))
            {
                result.Append(c);
            }
        }

        if (result.Length > 0 && char.IsHighSurrogate(result[^1]))
        {
            // Never cut a character in two.
            result.Length--;
        }

        return result.ToString().Trim();
    }
}
