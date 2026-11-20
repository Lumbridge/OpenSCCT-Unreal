using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using OpenScct.Launcher.Content;
using OpenScct.Launcher.Install;
using OpenScct.Maps;

namespace OpenScct.UnrealSetup;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        bool interactive = !args.Contains("--non-interactive", StringComparer.Ordinal);
        using var cancel = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; cancel.Cancel(); };
        try
        {
            string tools = Directory.GetParent(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar))!.FullName;
            string directory = Path.GetFullPath(Option(args, "--content-dir") ??
                Environment.GetEnvironmentVariable(UnrealSteamInstall.ContentDirectoryVariable) ?? UnrealSteamInstall.DefaultDirectory);
            string recipe = UnrealSteamInstall.ReadRecipe(tools) ??
                throw new InvalidDataException("The Unreal converter is missing or damaged. Repair or extract the complete Unreal release.");
            bool rebuild = args.Contains("--rebuild", StringComparer.Ordinal);
            if (!rebuild && UnrealSteamInstall.ActiveRoot(directory, recipe) is not null)
            {
                Console.WriteLine("Unreal game files are ready.");
                return 0;
            }
            string? selected = Option(args, "--source");
            string? source = GodotSteamContent.FindSource(selected);
            if (interactive && (selected is null || source is null)) source = ChooseSource(source);
            if (source is null) throw new InvalidDataException("Select the installed Steam copy of Splinter Cell Chaos Theory (app 13570).");
            await BuildAsync(source, directory, tools, recipe, cancel.Token);
            Console.WriteLine("Unreal game files are ready.");
            if (interactive) Message(rebuild ? "Setup is complete. Restart OpenSCCT Unreal to use the rebuilt game files." :
                "Setup is complete. Return to OpenSCCT Unreal and select Play.", 0x40);
            return 0;
        }
        catch (OperationCanceledException) { Console.Error.WriteLine("Setup cancelled."); return 2; }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex.Message);
            if (interactive) Message(ex.Message, 0x10);
            return 1;
        }
    }

    private static string? Option(string[] args, string key)
    {
        int at = Array.IndexOf(args, key);
        if (at < 0) return null;
        if (at + 1 >= args.Length || args[at + 1].StartsWith("--", StringComparison.Ordinal))
            throw new ArgumentException($"Supply a folder after {key}.");
        return args[at + 1];
    }

    private static async Task BuildAsync(string source, string directory, string tools, string recipe, CancellationToken cancel)
    {
        foreach (string code in SteamVersusMaps.All)
            if (!File.Exists(Path.Combine(source, "Packages", "Maps", code + ".sdc")))
                throw new InvalidDataException($"Steam is missing {code}.sdc. Verify Chaos Theory's installed files in Steam.");
        string original = Directory.GetParent(source)!.FullName;
        if (Inside(directory, original) || Inside(original, directory))
            throw new IOException("Choose an Unreal data folder separate from the Steam installation.");
        string builds = Path.Combine(directory, SteamContentInstall.BuildsDirectory);
        Directory.CreateDirectory(builds);
        using var buildLock = OpenLock(directory);
        // Dedicated servers and editors can hold a previous generation for the life of their match.
        if (!UnrealIsRunning())
        {
            string? current = SteamContentInstall.ActiveRoot(directory);
            foreach (string existing in Directory.EnumerateDirectories(builds))
                if (!string.Equals(existing, current, StringComparison.OrdinalIgnoreCase) && Guid.TryParseExact(Path.GetFileName(existing), "N", out _))
                    TryDelete(existing, builds);
        }
        string id = Guid.NewGuid().ToString("N");
        string output = Path.Combine(builds, id);
        Directory.CreateDirectory(output);
        bool published = false;
        string logPath = Path.Combine(directory, "steam-build.log");
        try
        {
            Console.WriteLine($"Building Unreal game files from {source}\nProgress log: {logPath}");
            using var log = new StreamWriter(logPath) { AutoFlush = true };
            // The installed converter can live in a read-only game folder.
            Environment.SetEnvironmentVariable("PYTHONDONTWRITEBYTECODE", "1");
            var state = await ContentBuilder.RunAsync(Path.Combine(tools, "python", "python.exe"), recipe, source,
                output, ContentBuildPlan.DefaultJobs, new ConsoleProgress(), log.WriteLine, cancel,
                steamBase: true, toolDirectory: Path.Combine(tools, "repo", "content"));
            if (!state.Ok) throw new InvalidDataException($"Conversion failed. See {logPath}.");
            if (SteamContentInstall.MissingFile(output) is { } missing)
                throw new InvalidDataException($"Conversion did not produce {missing}. See {logPath}.");
            cancel.ThrowIfCancellationRequested();
            File.Copy(Path.Combine(tools, UnrealSteamInstall.RecipeFile), Path.Combine(output, UnrealSteamInstall.RecipeFile));
            if (UnrealSteamInstall.ReadRecipe(output) != recipe)
                throw new IOException("The Unreal converter changed during setup. Run setup again with the updated installation.");
            AtomicFile.WriteAllText(Path.Combine(directory, SteamContentInstall.PointerFile),
                JsonSerializer.Serialize(new { format = SteamContentInstall.Format, build = id }));
            published = true;
        }
        finally
        {
            if (!published) TryDelete(output, builds);
        }
    }

    private static bool UnrealIsRunning()
    {
        foreach (string name in new[] { "OpenSCCTUnreal", "OpenSCCTUnreal-Win64-Shipping", "UnrealEditor", "svm-server" })
        {
            var processes = Process.GetProcessesByName(name);
            foreach (var process in processes) process.Dispose();
            if (processes.Length > 0) return true;
        }
        return false;
    }

    private static void TryDelete(string directory, string builds)
    {
        try { SafeDelete.Tree(directory, builds); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { Console.Error.WriteLine($"Could not remove {directory}: {ex.Message}"); }
    }

    private static bool Inside(string path, string parent)
    {
        string relative = Path.GetRelativePath(parent, path);
        return !Path.IsPathRooted(relative) && relative != ".." && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal);
    }

    private static FileStream OpenLock(string directory)
    {
        try { return new FileStream(Path.Combine(directory, "steam-build.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
        catch (IOException ex) { throw new IOException("Unreal setup is already using this data folder. Wait for it to finish.", ex); }
    }

    private sealed class ConsoleProgress : IProgress<ContentProgress>
    {
        private int _last = -1;
        public void Report(ContentProgress value)
        {
            if (value.Done == _last || value.Total <= 0) return;
            _last = value.Done;
            Console.WriteLine($"{value.Done}/{value.Total}  {value.Running.FirstOrDefault() ?? value.Line}");
        }
    }

    private static string? ChooseSource(string? detected)
    {
        if (detected is not null)
        {
            int answer = Message($"Build Unreal's game files from this Steam installation?\n\n{detected}\n\nChoose No to browse for another installation.", 0x23);
            if (answer == 6) return detected;
            if (answer != 7) throw new OperationCanceledException();
        }
        // Windows supplies this picker independently of any .NET desktop runtime or game launcher.
        var picker = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe"))
            { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true };
        foreach (var arg in new[] { "-NoProfile", "-STA", "-Command",
            "Add-Type -AssemblyName System.Windows.Forms; $d=New-Object Windows.Forms.FolderBrowserDialog; " +
            "$d.Description='Choose your installed Steam copy of Splinter Cell Chaos Theory'; $d.ShowNewFolderButton=$false; " +
            "if($d.ShowDialog() -eq 'OK'){[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); [Console]::Write($d.SelectedPath)}" })
            picker.ArgumentList.Add(arg);
        picker.StandardOutputEncoding = System.Text.Encoding.UTF8;
        using var process = Process.Start(picker) ?? throw new IOException("Windows could not open the folder picker.");
        string selected = process.StandardOutput.ReadToEnd();
        process.WaitForExit();
        if (selected.Length == 0) throw new OperationCanceledException();
        return GodotSteamContent.FindSource(selected);
    }

    private static int Message(string text, uint style) => MessageBoxW(IntPtr.Zero, text, "OpenSCCT Unreal setup", style | 0x10000);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern int MessageBoxW(IntPtr owner, string text, string caption, uint type);
}
