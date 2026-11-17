using System;
using System.IO;
using System.Text.Json;
using OpenScct.Game.GameFlow;

namespace OpenScct.UnrealBridge.Tutorials;

/// <summary>
/// The player profile's tutorial part, as the Godot client's TutorialProfile keeps it (game/Scripts/Tutorials/
/// TutorialProfile.cs, spec/game/tutorials.md): the tutorial maps done, the exam and the maps the first-time prompt was
/// confirmed for, in a small JSON file of <see cref="TutorialProgress"/> under the same names. Unreal names the file
/// (Saved/Profile/tutorials.json) and loads it once a run; a won tutorial marks its map done and saves at once.
/// <para>The in-game help flag (SPlayerProfile.bDisplayHelpInGame) is not kept here: in Unreal it travels with the level
/// as its <c>?Help</c> option, which the level's profile options hand to the simulation before the map loads, so a
/// Visit a Map level has it on and every other level has it off, as Godot's flag is set by the visit's launch and
/// cleared by any other.</para>
/// </summary>
public static class TutorialStore
{
    private static readonly JsonSerializerOptions s_json = new() { WriteIndented = true };

    private static TutorialProgress s_current = new();

    /// <summary>The progress in use (never null).</summary>
    public static TutorialProgress Current => s_current;

    /// <summary>The file loaded or saved last ("" before either).</summary>
    public static string FilePath { get; private set; } = "";

    /// <summary>
    /// Reads <paramref name="path"/> into use and remembers it as the file. A missing file starts from no progress and
    /// is not an error; a file that cannot be read keeps no progress and throws.
    /// </summary>
    public static bool Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        FilePath = Path.GetFullPath(path);
        s_current = new TutorialProgress();
        if (!File.Exists(FilePath))
        {
            return false;
        }

        s_current = JsonSerializer.Deserialize<TutorialProgress>(File.ReadAllText(FilePath), s_json) ?? new TutorialProgress();
        return true;
    }

    /// <summary>Writes the progress to the file loaded last (Controller.RequestWriteProfile); nothing before a load.</summary>
    public static bool Save()
    {
        if (FilePath.Length == 0)
        {
            return false;
        }

        string? folder = Path.GetDirectoryName(FilePath);
        if (!string.IsNullOrEmpty(folder))
        {
            Directory.CreateDirectory(folder);
        }

        // Written beside and moved over, so a crash mid-write cannot leave half a file.
        string temp = FilePath + ".tmp";
        File.WriteAllText(temp, JsonSerializer.Serialize(s_current, s_json));
        File.Move(temp, FilePath, overwrite: true);
        return true;
    }

    /// <summary>Forgets every tutorial done and the exam (unattended checks start from nothing), and saves.</summary>
    public static void Reset()
    {
        s_current = new TutorialProgress();
        Save();
    }

    /// <summary>
    /// SPlayerController GameEnded.PlayerTick's offline tutorial branch (TutorialRoundWatch): a won tutorial map is
    /// marked done (MarkTutoAsDone, the exam also sets the exam flag) and the profile written. False for a map that is
    /// not a tutorial or a round that was not won.
    /// </summary>
    public static bool FinishRound(string map, bool won)
    {
        if (!won || !TutorialMaps.CurLevelIsTutorial(map, out int numTut, out int numTeam))
        {
            return false;
        }

        s_current.MarkTutoAsDone(numTut, numTeam);
        Save();
        return true;
    }

    /// <summary>
    /// What follows a tutorial or visit round once the end delay is over (TutorialRoundWatch.Leave): the next spy
    /// tutorial not done (none once the exam is passed), the next merc tutorial not done (none once all three are), or
    /// null for the Tutorials page (a visit, a map that is not a tutorial, or nothing left).
    /// </summary>
    public static string? NextAfter(string map, bool visit)
    {
        if (visit || !TutorialMaps.CurLevelIsTutorial(map, out _, out int team))
        {
            return null;
        }

        TutorialProgress progress = s_current;
        if ((team == 0 && progress.ExamDone) || (team == 1 && progress.AllMercDone()))
        {
            return null;
        }

        return TutorialMaps.NextTutorial(team, i => progress.GetMapTutorialDone(i, team));
    }
}
