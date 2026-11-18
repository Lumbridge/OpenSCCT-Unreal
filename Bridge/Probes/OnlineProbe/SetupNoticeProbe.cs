using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;

/// <summary>
/// A step of OnlineProbe host: the host changes the lobby's setup and both players get the lobby's line about it as
/// a system chat line (Online/OnlineBridge.SetupNotice.cs, the Godot lobby's LobbyPresentation.ChangeNotice): one
/// rule changed says "1 match rule", a change of map and mode names the map's title and the mode.
/// </summary>
internal static unsafe class SetupNoticeProbe
{
    public static bool Run(OnlineApi online, OnlineBridge guest, string otherMap)
    {
        bool ok = true;
        int sent;
        fixed (char* options = "?TimeLimit=7")
        {
            sent = online.SetSetupOptions(options);
        }

        (List<string> host, List<string> other) = Collect(online, guest);
        Console.WriteLine($"setup notice after a time limit of 7 (sent {sent}): host [{string.Join(" | ", host)}], guest [{string.Join(" | ", other)}]");
        ok &= host.Count == 1 && other.Count == 1 && host[0] == "Host changed 1 match rule." && other[0] == host[0];

        fixed (char* map = otherMap)
        {
            sent = online.SetMatch(map, (int)OpenScct.Core.EGameMode.GM_DeathMatch);
        }

        (host, other) = Collect(online, guest);
        Console.WriteLine($"setup notice after {otherMap} Deathmatch (sent {sent}): host [{string.Join(" | ", host)}], guest [{string.Join(" | ", other)}]");
        ok &= host.Count == 1 && other.Count == 1 && host[0].StartsWith("Host changed map to ", StringComparison.Ordinal) &&
            host[0].Contains(", mode to Deathmatch", StringComparison.Ordinal) && other[0] == host[0];

        return ok;
    }

    /// <summary>Two seconds of frames: the "Host changed" system lines each side got.</summary>
    private static (List<string> Host, List<string> Guest) Collect(OnlineApi online, OnlineBridge guest)
    {
        var host = new List<string>();
        var other = new List<string>();
        var wait = Stopwatch.StartNew();
        while (wait.Elapsed.TotalSeconds < 2)
        {
            online.Update();
            guest.Update();
            OnlineChatLine line;
            while (online.TakeChat(&line) == 1)
            {
                string text = new string(line.Text);
                if (line.System != 0 && text.StartsWith("Host changed", StringComparison.Ordinal))
                {
                    host.Add(text);
                }
            }

            while (guest.TakeChat(out SessionMessage message))
            {
                if (message.Sender is null && message.Text.StartsWith("Host changed", StringComparison.Ordinal))
                {
                    other.Add(message.Text);
                }
            }

            Thread.Sleep(10);
        }

        return (host, other);
    }
}
