using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.UnrealBridge.LightPlan;

namespace OpenScct.UnrealBridge.Test;

/// <summary>How the actor light rig agrees with the bots' light field over the sampled floor (<see cref="LightProbe.CheckRig"/>).</summary>
public struct LightProbeRigCheck
{
    /// <summary>Floor nodes compared (a standing pawn's centre over each).</summary>
    public int Nodes;

    /// <summary>The rig asked as the field is built (map lights only, where the map placed them): the largest difference of the level and of the facing level, and how many nodes differ at all.</summary>
    public float MaxLevel, MaxFacing;
    public int Differ;

    /// <summary>The rig as the game asks it (lights where they are now, runtime lights in): the largest difference of the level and how many nodes differ.</summary>
    public float LiveMaxLevel;
    public int LiveDiffer;

    /// <summary>Nodes whose rig has four lights, and nodes where one of the chosen lights has a blocked line to the pawn.</summary>
    public int Full, Occluded;
}

/// <summary>The check of the lighting plan's actor light rig against the simulation's own light field.</summary>
public sealed partial class LightProbe
{
    /// <summary>
    /// Compares, on every floor node the last <see cref="FindSpots"/> sampled, the level the rig gives a standing
    /// pawn with the level of the bots' light field (<c>LevelAround</c>, and <c>LevelAt</c> towards a viewer 3 m
    /// away). Both run the same rules on the same file, so the difference should be exactly 0.
    /// </summary>
    public LightProbeRigCheck CheckRig()
    {
        var check = default(LightProbeRigCheck);
        if (_grid is null || _playNodes is null)
        {
            return check;
        }

        LightPlanState state = LightPlanState.Of(_session);
        _field.Time = _session.Session.Client.World.Level.Info.TimeSeconds;
        _field.FollowSwitches();
        IReadOnlyList<FVector> nodes = _grid.Nodes;
        var rig = new RigResult();
        var up = new FVector(0f, 0f, CentreHeight);
        const int AsField = BridgeLightRigOptions.NoRuntimeLights | BridgeLightRigOptions.PlacedOnly | BridgeLightRigOptions.NoOcclusion;
        foreach (int n in _playNodes)
        {
            FVector centre = nodes[n] + up;
            FVector viewer = centre + new FVector(300f, 0f, 60f);
            float level = _field.LevelAround(centre, PawnRadius);
            float facing = _field.LevelAt(centre, PawnRadius, viewer);
            state.Rig(centre, PawnRadius, viewer, AsField, null, rig);
            float dl = MathF.Abs(rig.Level - level), df = MathF.Abs(rig.LevelFacing - facing);
            check.MaxLevel = MathF.Max(check.MaxLevel, dl);
            check.MaxFacing = MathF.Max(check.MaxFacing, df);
            check.Differ += dl > 0f || df > 0f ? 1 : 0;

            state.Rig(centre, PawnRadius, viewer, 0, null, rig);
            float live = MathF.Abs(rig.Level - level);
            check.LiveMaxLevel = MathF.Max(check.LiveMaxLevel, live);
            check.LiveDiffer += live > 0f ? 1 : 0;
            check.Full += rig.Count >= 4 ? 1 : 0;
            for (int i = 0; i < rig.Count; i++)
            {
                if ((rig.Lights[i].Flags & BridgeRigLightFlags.Occluded) != 0)
                {
                    check.Occluded++;
                    break;
                }
            }

            check.Nodes++;
        }

        return check;
    }
}
