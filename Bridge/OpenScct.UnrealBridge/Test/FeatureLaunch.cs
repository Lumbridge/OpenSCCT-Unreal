using System;
using OpenScct.Core;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Test;

/// <summary>
/// Where the next local start puts the local player, for the client's per-feature checks (Source/OpenSCCT/Test/Features):
/// the traversal check starts the spy at a TestRange station's spot facing its wall, as the Godot client's
/// RangeTraversalSmoke places the spy there (fixtures/testrange/stations.json). The match takes the editor's Play
/// Level rules, so the spy spawns at once without a lobby. Unreal arms it (<c>FeatureTestExports.SetNextStart</c>)
/// just before it opens the level; the next local start takes it (<see cref="Apply"/>) whatever its map, and it is
/// used up either way.
/// </summary>
public static class FeatureLaunch
{
    private static (FVector Location, FRotator Rotation)? s_armed;

    /// <summary>Arms the next start at <paramref name="location"/> (the pawn's centre), facing <paramref name="yaw"/> (65536 a turn).</summary>
    public static void Arm(FVector location, int yaw) => s_armed = (location, new FRotator(0, yaw & 0xFFFF, 0));

    public static void Disarm() => s_armed = null;

    /// <summary>The match placed at the armed start with Play Level rules, or unchanged when none is armed.</summary>
    public static LocalMatch Apply(LocalMatch match)
    {
        ArgumentNullException.ThrowIfNull(match);
        if (s_armed is not { } start)
        {
            return match;
        }

        s_armed = null;
        return match with { StartAt = start, PlayLevel = true };
    }
}
