using System.Collections.Generic;
using System.Numerics;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.Pose;

/// <summary>
/// A turn of one bone laid over a pawn's pose, about an axis in the pawn's own frame (X forward, Y right, Z up, as it
/// faces), by <see cref="Radians"/> (right-handed about the axis in render space). A port of OpenSCCT's
/// game/Scripts/Views/PawnPoseOverlay.cs without Godot.
/// </summary>
public readonly record struct PoseTurn(string Bone, Vector3 Axis, float Radians);

/// <summary>
/// Extra posing of a drawn pawn on top of its simulated animation (an OpenSCCT addition: the hub's emotes,
/// Hub/HubEmotePoses.cs). <see cref="PawnPoser"/> asks every pose it builds; nothing is written to the simulation.
/// </summary>
public interface IPawnPoseOverlay
{
    /// <summary>
    /// Adds the turns <paramref name="pawn"/> wears now to <paramref name="turns"/> (in the order to apply, parents
    /// first), and returns the hips' drop in uu (0 for none).
    /// </summary>
    float Collect(SBasePawn pawn, List<PoseTurn> turns);
}
