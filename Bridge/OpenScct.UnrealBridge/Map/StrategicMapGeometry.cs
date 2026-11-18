using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using OpenScct.Content.Collision;
using OpenScct.Core;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge;

[StructLayout(LayoutKind.Sequential)]
public struct BridgeMapTriangle
{
    public FVector A, B, C;
}

/// <summary>A cutaway of the selected storey in simulation coordinates, from the round's loaded static collision surfaces.</summary>
internal static class StrategicMapGeometry
{
    // FloorAt classifies a pawn's centre. Include the supporting surface under a standing pawn on the new floor.
    private const float StandingHalfHeight = 87.5f;

    public static BridgeMapTriangle[] Build(CollisionData data, StrategicMapData map, int floor)
    {
        float lower = floor > 0 ? map.Floors[floor].FloorZ - StandingHalfHeight : float.MinValue;
        float upper = floor + 1 < map.Floors.Count ? map.Floors[floor + 1].FloorZ - StandingHalfHeight : float.MaxValue;
        var mesh = new List<BridgeMapTriangle>();
        var sky = new List<CollisionZone>();
        foreach (var zone in data.Zones)
        {
            if ((zone.Flags & CollisionZoneFlags.SkyZone) != 0) sky.Add(zone);
        }

        Span<FVector> face = stackalloc FVector[6];
        Span<FVector> clipped = stackalloc FVector[6];
        foreach (Collider col in data.Colliders)
        {
            if (col.Kind is not (ColliderKind.WorldBsp or ColliderKind.MeshPoly or ColliderKind.MeshHull)
                || (col.Flags & ColliderFlags.StartsDisabled) != 0 || col.Parent != CollisionData.None
                || col.AabbMax.Z < lower || col.AabbMin.Z >= upper || col.TriCount == 0)
            {
                continue;
            }
            FCoords axes = FCoords.FromRotator(col.Rotation).Transpose();
            bool identity = col.HasIdentityTransform;
            bool mirrored = col.Scale.X * col.Scale.Y * col.Scale.Z < 0f;
            FVector World(uint index)
            {
                FVector local = data.Vertices[(int)index];
                return identity ? local : ((local - col.PrePivot) * col.Scale).TransformVectorBy(axes) + col.Location;
            }

            foreach (CollisionTriangle tri in data.TrianglesOf(col))
            {
                if ((data.Materials[tri.Material].Flags & CollisionMaterialFlags.Invisible) != 0) continue;
                FVector a = World(tri.I0), b = World(tri.I1), c = World(tri.I2);
                if (mirrored) (b, c) = (c, b);
                if (Math.Max(a.Z, Math.Max(b.Z, c.Z)) < lower || Math.Min(a.Z, Math.Min(b.Z, c.Z)) >= upper) continue;
                FVector normal = (b - a) ^ (c - a);
                if (normal.SizeSquared() < 0.01f || normal.Z < -0.5f * normal.Size()) continue;
                if (IsSky(sky, (a + b + c) / 3f)) continue;

                face[0] = a;
                face[1] = b;
                face[2] = c;
                int count = Clip(face[..3], clipped, lower, above: true);
                count = Clip(clipped[..count], face, upper, above: false);
                for (int i = 2; i < count; i++)
                {
                    mesh.Add(new BridgeMapTriangle { A = face[0], B = face[i - 1], C = face[i] });
                }
            }
        }
        return mesh.ToArray();
    }

    private static bool IsSky(List<CollisionZone> sky, FVector at)
    {
        foreach (var zone in sky)
        {
            if (zone.Contains(at)) return true;
        }
        return false;
    }

    // Clip the actual triangle at a floor boundary. A stair or a tall wall can belong to two storeys.
    private static int Clip(ReadOnlySpan<FVector> input, Span<FVector> output, float z, bool above)
    {
        if (input.IsEmpty) return 0;
        int count = 0;
        FVector previous = input[^1];
        foreach (FVector point in input)
        {
            bool inside = above ? point.Z >= z : point.Z < z;
            bool wasInside = above ? previous.Z >= z : previous.Z < z;
            if (inside != wasInside)
            {
                output[count++] = previous + (point - previous) * ((z - previous.Z) / (point.Z - previous.Z));
            }
            if (inside) output[count++] = point;
            previous = point;
        }
        return count;
    }
}
