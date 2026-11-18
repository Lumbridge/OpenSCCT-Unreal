using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Net;
using OpenScct.Server;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Demo;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.Options;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The developer console's table (Source/OpenSCCT/Runtime/ScctConsoleApi.h mirrors it): a typed line goes through the
/// Godot client's own <see cref="ConsoleCommands"/> (linked source, UI/ConsoleCommands.cs), so every word that console
/// knows, its usage lines, its help and its rules about when a command is allowed are the same here, and a command
/// added there arrives with the next build. What the Godot console does through its window (DevConsole.cs) is done
/// here from the bridge's state: the online visit's connection for <c>login</c>, <c>cmd</c>, <c>say</c> and the chat
/// commands, the local match's own connection for the practice commands of an offline game, the local pawn for
/// <c>whereami</c>. The server's answers (system lines online, the Server's chat lines offline) are queued for
/// <see cref="ConsoleApi.TakeLine"/> as the Godot console prints them when they arrive.
/// <para>Not run here, because Unreal owns them: the settings words (<c>set</c>, <c>get</c>, <c>current</c> and a key
/// typed alone; the Unreal client has its own settings and keys), <c>grabarea</c> (the overlay is Unreal's) and
/// <c>quit</c>. <see cref="ConsoleApi.Run"/> answers those with <see cref="ConsoleResult.Client"/> or
/// <see cref="ConsoleResult.Quit"/> and does nothing.</para>
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ConsoleApi
{
    public int Version;

    /// <summary>(line, buffer, capacity, result): runs a typed line; writes what it printed (lines joined by '\n'), returns the full length. result is a <see cref="ConsoleResult"/>.</summary>
    public delegate* unmanaged[Cdecl]<char*, char*, int, int*, int> Run;

    /// <summary>(buffer, capacity): the next line a server sent for the console; 1 when one was written, 0 when none waits.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> TakeLine;

    /// <summary>(buffer, capacity): the text the last command asked to copy to the clipboard (whereami copy), taken once; returns its full length, 0 for none.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> TakeCopy;
}

/// <summary>What <see cref="ConsoleApi.Run"/> made of a line.</summary>
public enum ConsoleResult
{
    /// <summary>Not a word of the console: Unreal's own commands get the line.</summary>
    Unknown = 0,

    /// <summary>Run; the output holds what it printed.</summary>
    Done = 1,

    /// <summary>quit or exit: Unreal closes the game.</summary>
    Quit = 2,

    /// <summary>A word Unreal runs itself (settings, grabarea); nothing was done.</summary>
    Client = 3,
}

public static unsafe class ConsoleExports
{
    public const int ApiVersion = 1;

    private const int MaxWaiting = 200;

    private static readonly ConcurrentQueue<string> s_lines = new();
    private static readonly Host s_host = new();
    private static readonly ConsoleCommands s_commands = new(s_host);
    private static readonly GameSettings s_godotKeys = new();
    private static OnlineClient? s_watched;
    private static ClientSession? s_watchedOffline;
    private static string? s_copy;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(ConsoleApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Run = &Run;
        api->TakeLine = &TakeLine;
        api->TakeCopy = &TakeCopy;
        return ApiVersion;
    }

    /// <summary>Runs a typed line as the Godot console would; the lines it printed.</summary>
    public static ConsoleResult Execute(string line, out IReadOnlyList<string> output)
    {
        output = [];
        string[] parts = line.Trim().Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length == 0)
        {
            return ConsoleResult.Unknown;
        }

        string verb = parts[0].ToLowerInvariant();
        switch (verb)
        {
            case "quit" or "exit":
                return ConsoleResult.Quit;
            case "set" or "get" or "current" or "grabarea":
                return ConsoleResult.Client;
        }

        // A key of the Godot client's settings file typed alone: the Unreal client's settings have other keys.
        if (SettingsConsole.Get(s_godotKeys, verb) is not null)
        {
            return ConsoleResult.Unknown;
        }

        Watch();
        IReadOnlyList<string> printed = s_commands.Run(line);
        if (printed.Count == 1 && printed[0].StartsWith("unknown command '", StringComparison.Ordinal))
        {
            return ConsoleResult.Unknown;
        }

        output = printed;
        return ConsoleResult.Done;
    }

    /// <summary>The server lines waiting for the console, oldest first.</summary>
    public static bool TryTakeLine(out string line)
    {
        Watch();
        return s_lines.TryDequeue(out line!);
    }

    /// <summary>The offline match whose own server answers the practice commands (not an online round, not a demo).</summary>
    private static ClientSession? OfflineClient =>
        !s_host.Online && DemoBridge.Player is null && Exports.Current is { IsRemote: false } session ? session.Session.Client : null;

    /// <summary>Follows the connections whose servers answer, as DevConsole.Watch and WatchOffline do.</summary>
    private static void Watch()
    {
        OnlineClient? client = OnlineBridge.Existing?.Client;
        if (!ReferenceEquals(client, s_watched))
        {
            if (s_watched is not null)
            {
                s_watched.MessageReceived -= OnMessage;
            }

            s_watched = client;
            if (client is not null)
            {
                client.MessageReceived += OnMessage;
            }
        }

        ClientSession? offline = OfflineClient;
        if (!ReferenceEquals(offline, s_watchedOffline))
        {
            if (s_watchedOffline is not null)
            {
                s_watchedOffline.ChatReceived -= OnOfflineChat;
            }

            s_watchedOffline = offline;
            if (offline is not null)
            {
                offline.ChatReceived += OnOfflineChat;
            }
        }
    }

    private static void OnMessage(SessionMessage message)
    {
        if (message.IsSystem)
        {
            Queue(message.Text);
        }
    }

    private static void OnOfflineChat(ChatLine line)
    {
        if (line.SenderId == 0)
        {
            Queue(line.Text);
        }
    }

    private static void Queue(string text)
    {
        if (s_lines.Count < MaxWaiting)
        {
            s_lines.Enqueue(text);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Run(char* line, char* buffer, int capacity, int* result)
    {
        ConsoleResult made = ConsoleResult.Unknown;
        string text = "";
        try
        {
            made = Execute(line == null ? "" : new string(line), out IReadOnlyList<string> output);
            text = string.Join('\n', output);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            made = ConsoleResult.Done;
            text = "the command failed: " + e.Message;
        }

        if (result != null)
        {
            *result = (int)made;
        }

        return Write(text, buffer, capacity);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeLine(char* buffer, int capacity)
    {
        try
        {
            if (!TryTakeLine(out string line))
            {
                return 0;
            }

            Write(line, buffer, capacity);
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeCopy(char* buffer, int capacity)
    {
        string? text = s_copy;
        if (text is null)
        {
            return 0;
        }

        if (text.Length < capacity)
        {
            s_copy = null;
        }

        return Write(text, buffer, capacity);
    }

    private static int Write(string text, char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
            buffer[count] = '\0';
        }

        return text.Length;
    }

    /// <summary>The Godot console's host (DevConsole's IConsoleHost half) from the bridge's state.</summary>
    private sealed class Host : IConsoleHost
    {
        private static OnlineClient? Client => OnlineBridge.Existing?.Client;

        // Never reached with a settings word (Execute hands those to Unreal); the help and the rest never read it.
        public GameSettings Settings => s_godotKeys;

        public void SaveSettings(GameSettings settings, bool display)
        {
        }

        public bool Online => Client is { State: OnlineClientState.Lobby or OnlineClientState.Loading or OnlineClientState.Match or OnlineClientState.Results };

        public void ServerCommand(string command)
        {
            // The player's own server: its console answers the host without a login, as Unreal's Admin does.
            if (OnlineBridge.Existing is { Hosting: not null } hosting && hosting.HostCommand(command))
            {
                return;
            }

            Client?.Command(command);
        }

        public void ServerLogin(string password) => Client?.Admin(password);

        public void Chat(string text) => Client?.Chat(text);

        public bool CanSendBotCommands => Online || OfflineClient is not null;

        public void BotCommand(string command)
        {
            if (Online)
            {
                Client!.Chat("!" + command);
            }
            else
            {
                OfflineClient?.SendChat(EChatScope.Say, "!" + command);
            }
        }

        public string? LocationReport =>
            Exports.Current is { Session: { LocalController: { } controller, LocalPawn: { } pawn } } session
                ? PlayerLocationReport.Format(session.Map, pawn, controller)
                : null;

        public void CopyText(string text) => s_copy = text;

        public void Quit()
        {
        }
    }
}
