using System;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using OpenScct.Net.Discovery;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// Crash reports (rl:ac.crash), a port of the Godot client's game/Scripts/App/CrashLog.cs: an exception nothing caught
/// on any managed thread, or a faulted task nobody awaited, writes <c>crash-&lt;utc&gt;.log</c> with the managed stack,
/// the build, the OS and what was being played. The handlers go in when the bridge assembly loads; the reports go to
/// <c>%LOCALAPPDATA%\OpenSCCT\crashes</c> until Unreal names its own folder (Saved/Crashes, where the engine's native
/// reports are). Native crashes keep Unreal's own handler, which carries the same context as game data.
/// </summary>
public static class CrashLog
{
    private static string s_folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenSCCT", "crashes");
    private static int s_installed;

    /// <summary>What is being played ("AquaE Story, spy, offline"), or "" in the menus; written into the report.</summary>
    public static string Context { get; set; } = "";

    /// <summary>The path of the last report written, or "".</summary>
    public static string LastPath { get; private set; } = "";

    /// <summary>Raised with each report's path (or the reason it could not be written), for the process log.</summary>
    public static event Action<string>? Written;

    public static string Folder => s_folder;

#pragma warning disable CA2255 // The bridge is loaded once by the Unreal host; the handlers must be in before anything of it runs.
    [ModuleInitializer]
    internal static void InstallOnLoad() => Install(null);
#pragma warning restore CA2255

    /// <summary>Starts listening (once) and, with a folder, moves the reports there.</summary>
    public static void Install(string? folder)
    {
        if (!string.IsNullOrWhiteSpace(folder))
        {
            s_folder = folder;
        }

        if (System.Threading.Interlocked.Exchange(ref s_installed, 1) != 0)
        {
            return;
        }

        AppDomain.CurrentDomain.UnhandledException += (_, e) => Write(e.ExceptionObject as Exception, e.IsTerminating ? "unhandled (terminating)" : "unhandled");
        TaskScheduler.UnobservedTaskException += (_, e) => Write(e.Exception, "unobserved task");
    }

    /// <summary>Writes a report for <paramref name="error"/>; returns its path, or null when it could not be written.</summary>
    public static string? Write(Exception? error, string kind)
    {
        string folder = s_folder;
        DateTime now = DateTime.UtcNow;
        string playing = Context;
        var text = new StringBuilder();
        text.AppendLine(CultureInfo.InvariantCulture, $"OpenSCCT crash report ({kind})");
        text.AppendLine(CultureInfo.InvariantCulture, $"UTC: {now:yyyy-MM-dd HH:mm:ss.fff}");
        text.AppendLine(CultureInfo.InvariantCulture, $"Client: Unreal");
        text.AppendLine(CultureInfo.InvariantCulture, $"Build: {BuildInfo.Text}");
        text.AppendLine(CultureInfo.InvariantCulture, $"OS: {Environment.OSVersion} ({(Environment.Is64BitProcess ? "64" : "32")}-bit)");
        text.AppendLine(CultureInfo.InvariantCulture, $"Playing: {(playing.Length == 0 ? "(menus)" : playing)}");
        text.AppendLine();
        text.AppendLine(error?.ToString() ?? "(no exception object)");
        try
        {
            Directory.CreateDirectory(folder);
            string path = Path.Combine(folder, string.Create(CultureInfo.InvariantCulture, $"crash-{now:yyyyMMdd-HHmmss-fff}.log"));
            File.WriteAllText(path, text.ToString());
            LastPath = path;
            Notify("crash report written: " + path + " (" + kind + ", playing " + (playing.Length == 0 ? "(menus)" : playing) + ")");
            return path;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Notify("crash report could not be written to " + folder + ": " + e.Message);
            return null;
        }
    }

    private static void Notify(string line)
    {
        try
        {
            Written?.Invoke(line);
        }
        catch (Exception)
        {
            // A report is being written because something already went wrong: the notice must not add to it.
        }
    }
}
