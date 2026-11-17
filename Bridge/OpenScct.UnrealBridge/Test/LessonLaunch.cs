using System;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Test;

/// <summary>
/// How a TestRange drill's match is started, as the Godot client's game/Scripts/Tutorials/InteractiveLessonPlay.cs
/// starts it: TestRange, the spy's side, the editor's Play Level rules, the server ticked inline (the lesson moves
/// pawns on both sides between ticks), one mercenary bot for spy-grab and none for vision-cancelling, and the local
/// player placed at the drill's start (<see cref="BrowserLesson.Start"/>).
/// <para>Unreal arms a drill (<c>LessonExports.Arm</c>) just before the level starts its match; the next local start
/// on TestRange takes it (<see cref="Apply"/>), any other start drops it.</para>
/// </summary>
public static class LessonLaunch
{
    public const string Map = "TestRange";

    private static string? s_armed;

    /// <summary>The drills there are, by the names the Godot client and the wiki use.</summary>
    public static bool IsKind(string? kind) => kind is "vision-cancelling" or "spy-grab";

    /// <summary>The drill the next TestRange start is for, or null.</summary>
    public static string? Armed => s_armed;

    /// <summary>The kind of the drill whose match was started last (null when the last start was no drill).</summary>
    public static string? Launched { get; private set; }

    public static void Arm(string? kind)
    {
        if (kind is not null && !IsKind(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, "A drill is vision-cancelling or spy-grab.");
        }

        s_armed = kind;
    }

    /// <summary>
    /// The match as a drill when one is armed and <paramref name="match"/> is on TestRange; the match unchanged
    /// otherwise. Either way the armed drill is used up.
    /// </summary>
    public static LocalMatch Apply(LocalMatch match)
    {
        ArgumentNullException.ThrowIfNull(match);
        string? kind = s_armed;
        s_armed = null;
        Launched = null;
        if (kind is null || !string.Equals(match.Map, Map, StringComparison.OrdinalIgnoreCase))
        {
            return match;
        }

        Launched = kind;
        return match with
        {
            Map = Map,
            Merc = false,
            Bots = kind == "spy-grab" ? 1 : 0,
            PlayLevel = true,
            InlineHost = true,
            StartAt = BrowserLesson.Start(kind),
        };
    }
}
