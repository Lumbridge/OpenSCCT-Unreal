using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Provenance;

namespace OpenScct.UnrealBridge;

/// <summary>Whether the match runs responsive smoke, and how much of it there is now (ScctSmokeApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeSmokeState
{
    /// <summary>1 when the match runs responsive smoke (GameReplicationInfo.ResponsiveSmokeEnabled on the client).</summary>
    public int Enabled;

    /// <summary>Clouds the client's world holds now.</summary>
    public int FieldCount;

    /// <summary>The cells per axis of one cloud's grid in the simulation (ResponsiveSmokeField.Dimension).</summary>
    public int Dimension;

    /// <summary>Density grids written since the session started (GetDensity calls that wrote).</summary>
    public int Uploads;

    /// <summary>The shared clock the clouds' densities are evaluated at, seconds (ResponsiveSmokeWorld.CurrentTime).</summary>
    public float Time;

    /// <summary>One simulation cell's edge, cm (ResponsiveSmokeField.CellSize).</summary>
    public float CellSize;

    /// <summary>Optical depth per cm through smoke of density 1 (ResponsiveSmokeField.Extinction), as gas and AI see it.</summary>
    public float Extinction;

    /// <summary>Milliseconds the last GetDensity call took (the sampling of one grid).</summary>
    public float LastSampleMilliseconds;
}

/// <summary>One cloud of the client's world: the box its grid covers (ScctSmokeApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeSmokeField
{
    /// <summary>The client actor id of the cloud's SSmokeEmitter: the same id the events feature lists the emitter by.</summary>
    public int EmitterId;

    /// <summary>The field's revision: it changes when its cells, arrival times or holes change (not with its own growth over time).</summary>
    public uint Revision;

    /// <summary>The lowest corner of the box the grid covers, cm.</summary>
    public float OriginX, OriginY, OriginZ;

    /// <summary>The box's edge, cm (Dimension x CellSize).</summary>
    public float Size;

    /// <summary>The cloud's centre (where it blooms from, raised off a floor), cm.</summary>
    public float CenterX, CenterY, CenterZ;

    /// <summary>Seconds since it was let out, and seconds it lasts in all.</summary>
    public float Age, Lifetime;

    /// <summary>Holes shots and blasts have pushed through it that are still open or refilling.</summary>
    public int Carves;

    public int Reserved;
}

/// <summary>
/// A hole a shot pushes through a cloud (ResponsiveSmokeCarveKind.Bullet), as it is now: the segment it runs along,
/// in the cloud box's frame (cm from the box's lowest corner), its radius and soft edge, and how much smoke it holds
/// back now (1 while it is open, falling to 0 as it refills). ScctSmokeApi.h mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeSmokeHole
{
    public float StartX, StartY, StartZ, Radius;
    public float EndX, EndY, EndZ, Strength;
    public float Feather;
    public uint Sequence;
}

/// <summary>
/// The entry points for responsive smoke (the shared simulated cloud a smoke grenade lets out when the match runs
/// SvmResponsiveSmoke): whether it is on, the clouds the client's world holds and, for one cloud, its density on a grid
/// of the caller's size, evaluated at the shared clock exactly as gas, cough and AI sight read it
/// (ResponsiveSmokeField.SampleDensity: geometry, arrival front, rolling envelope, fade, and the holes shots push
/// through). The Godot client evaluates the same field on the GPU (ResponsiveSmokeVolumeCache); Unreal samples it here
/// and uploads the grid to a texture. It reads the client world and never writes it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct SmokeApi
{
    public int Version;

    /// <summary>Fills the state. Returns 1, or 0 without a session.</summary>
    public delegate* unmanaged[Cdecl]<BridgeSmokeState*, int> GetState;

    /// <summary>Writes the clouds of the client's world. Returns the count written.</summary>
    public delegate* unmanaged[Cdecl]<BridgeSmokeField*, int, int> GetFields;

    /// <summary>
    /// Writes one cloud's density on a Resolution^3 grid over its box (X fastest, then Y, then Z; 0..255 for 0..1),
    /// sampled at each grid cell's centre. Returns 1 when written, 0 when the cloud is gone or the buffer is too small.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, int, byte*, int, int> GetDensity;

    /// <summary>
    /// Writes the shot holes of one cloud as they are now, newest first. A hole is a few centimetres across, finer than
    /// any density grid, so the drawing carves it exactly where the grid only thins; a blast's clearing is wide and is
    /// in the grid. Returns the count written.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, BridgeSmokeHole*, int, int> GetHoles;
}

public static unsafe class SmokeExports
{
    public const int ApiVersion = 1;

    /// <summary>The largest grid a caller may ask for (the Godot client's High flow grid).</summary>
    public const int MaxResolution = 64;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(SmokeApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->GetState = &GetState;
        api->GetFields = &GetFields;
        api->GetDensity = &GetDensity;
        api->GetHoles = &GetHoles;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(BridgeSmokeState* state)
    {
        if (state == null || Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        try
        {
            *state = SmokeFeature.Of(session).State();
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetFields(BridgeSmokeField* into, int capacity)
    {
        if (into == null || capacity <= 0 || Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        try
        {
            return SmokeFeature.Of(session).WriteFields(new Span<BridgeSmokeField>(into, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetDensity(int emitterId, int resolution, byte* into, int bytes)
    {
        if (into == null || Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        try
        {
            return SmokeFeature.Of(session).WriteDensity(emitterId, resolution, new Span<byte>(into, bytes)) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetHoles(int emitterId, BridgeSmokeHole* into, int capacity)
    {
        if (into == null || capacity <= 0 || Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        try
        {
            return SmokeFeature.Of(session).WriteHoles(emitterId, new Span<BridgeSmokeHole>(into, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}

/// <summary>The responsive smoke of one session's client world, as the drawing reads it.</summary>
public sealed class SmokeFeature
{
    private readonly BridgeSession _bridge;
    private readonly List<KeyValuePair<SSmokeEmitter, ResponsiveSmokeField>> _live = new();

    private SmokeFeature(BridgeSession bridge)
    {
        _bridge = bridge;
    }

    public static SmokeFeature Of(BridgeSession session) => session.Feature(static s => new SmokeFeature(s));

    /// <summary>Grids written since the session started.</summary>
    public int Uploads { get; private set; }

    /// <summary>Milliseconds the last grid took to sample.</summary>
    public float LastSampleMilliseconds { get; private set; }

    private Level Level => _bridge.Session.Client.World.Level;

    public BridgeSmokeState State()
    {
        Level level = Level;
        bool enabled = ResponsiveSmokeWorld.IsEnabled(level);
        // The world service is made by the replication of the first cloud; asking for it before would make one.
        ResponsiveSmokeWorld? world = level.GetService<ResponsiveSmokeWorld>();
        return new BridgeSmokeState
        {
            Enabled = enabled ? 1 : 0,
            FieldCount = world is null ? 0 : Live(world).Count,
            Dimension = ResponsiveSmokeField.Dimension,
            Uploads = Uploads,
            Time = world?.CurrentTime ?? level.WorldTimeSeconds,
            CellSize = ResponsiveSmokeField.CellSize,
            Extinction = ResponsiveSmokeField.Extinction,
            LastSampleMilliseconds = LastSampleMilliseconds,
        };
    }

    public int WriteFields(Span<BridgeSmokeField> into)
    {
        if (Level.GetService<ResponsiveSmokeWorld>() is not { } world)
        {
            return 0;
        }

        float now = world.CurrentTime;
        float size = ResponsiveSmokeField.Dimension * ResponsiveSmokeField.CellSize;
        int count = 0;
        foreach ((SSmokeEmitter emitter, ResponsiveSmokeField field) in Live(world))
        {
            if (count >= into.Length)
            {
                break;
            }

            into[count++] = new BridgeSmokeField
            {
                EmitterId = emitter.ActorId,
                Revision = field.Revision,
                OriginX = field.Origin.X,
                OriginY = field.Origin.Y,
                OriginZ = field.Origin.Z,
                Size = size,
                CenterX = field.Center.X,
                CenterY = field.Center.Y,
                CenterZ = field.Center.Z,
                Age = now - field.CreatedAt,
                Lifetime = ResponsiveSmokeField.Lifetime,
                Carves = field.Carves.Count,
            };
        }

        return count;
    }

    /// <summary>
    /// One cloud's density on a resolution^3 grid over its box, each value the field's own SampleDensity at the grid
    /// cell's centre and the shared clock: what gas and AI read, so a gap a shot leaves is a gap on screen. The slices
    /// are sampled in parallel (the field is only read; nothing else touches the client world during the call).
    /// </summary>
    [Provenance(ProvenanceKind.Estimated, "The Godot client evaluates SampleDensity per ray step on the GPU; here it is sampled once per grid cell and interpolated by the texture")]
    public bool WriteDensity(int emitterId, int resolution, Span<byte> into)
    {
        resolution = Math.Clamp(resolution, 2, SmokeExports.MaxResolution);
        if (into.Length < resolution * resolution * resolution || Level.GetService<ResponsiveSmokeWorld>() is not { } world)
        {
            return false;
        }

        ResponsiveSmokeField? found = null;
        foreach ((SSmokeEmitter emitter, ResponsiveSmokeField field) in Live(world))
        {
            if (emitter.ActorId == emitterId)
            {
                found = field;
                break;
            }
        }

        if (found is null)
        {
            return false;
        }

        long start = Stopwatch.GetTimestamp();
        float now = world.CurrentTime;
        // Computed lazily from the cells; worked out here once so the parallel samples only read it.
        ResponsiveSmokeField cloud = found;
        _ = cloud.ShapeExtents;
        FVector origin = cloud.Origin;
        float step = ResponsiveSmokeField.Dimension * ResponsiveSmokeField.CellSize / resolution;
        int n = resolution;
        byte[] grid = System.Buffers.ArrayPool<byte>.Shared.Rent(n * n * n);
        try
        {
            Parallel.For(0, n, z =>
            {
                for (int y = 0; y < n; y++)
                {
                    int row = (z * n + y) * n;
                    for (int x = 0; x < n; x++)
                    {
                        var point = new FVector(origin.X + (x + 0.5f) * step, origin.Y + (y + 0.5f) * step, origin.Z + (z + 0.5f) * step);
                        float density = cloud.SampleDensity(point, now);
                        grid[row + x] = (byte)Math.Clamp((int)MathF.Round(density * 255f), 0, 255);
                    }
                }
            });
            grid.AsSpan(0, n * n * n).CopyTo(into);
        }
        finally
        {
            System.Buffers.ArrayPool<byte>.Shared.Return(grid);
        }

        LastSampleMilliseconds = (float)Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        Uploads++;
        return true;
    }

    /// <summary>
    /// One cloud's open shot holes, newest first, each with how much smoke it holds back now: the factor
    /// ResponsiveSmokeField.SampleDensity applies (1 - Smooth(Hold, Hold + Refill, elapsed)).
    /// </summary>
    public int WriteHoles(int emitterId, Span<BridgeSmokeHole> into)
    {
        if (Level.GetService<ResponsiveSmokeWorld>() is not { } world)
        {
            return 0;
        }

        float now = world.CurrentTime;
        foreach ((SSmokeEmitter emitter, ResponsiveSmokeField field) in Live(world))
        {
            if (emitter.ActorId != emitterId)
            {
                continue;
            }

            FVector origin = field.Origin;
            int count = 0;
            IReadOnlyList<ResponsiveSmokeCarve> carves = field.Carves;
            for (int i = carves.Count - 1; i >= 0 && count < into.Length; i--)
            {
                ResponsiveSmokeCarve c = carves[i];
                float elapsed = now - c.CreatedAt;
                if (c.Kind != ResponsiveSmokeCarveKind.Bullet || elapsed < 0f || elapsed >= c.Hold + c.Refill)
                {
                    continue;
                }

                into[count++] = new BridgeSmokeHole
                {
                    StartX = c.Start.X - origin.X,
                    StartY = c.Start.Y - origin.Y,
                    StartZ = c.Start.Z - origin.Z,
                    Radius = c.Radius,
                    EndX = c.End.X - origin.X,
                    EndY = c.End.Y - origin.Y,
                    EndZ = c.End.Z - origin.Z,
                    Strength = 1f - ResponsiveSmokeField.Smooth(c.Hold, c.Hold + c.Refill, elapsed),
                    Feather = c.Feather,
                    Sequence = c.Sequence,
                };
            }

            return count;
        }

        return 0;
    }

    /// <summary>The live clouds in a fixed order (by emitter id), the ones whose emitter is gone left out.</summary>
    private List<KeyValuePair<SSmokeEmitter, ResponsiveSmokeField>> Live(ResponsiveSmokeWorld world)
    {
        _live.Clear();
        foreach (KeyValuePair<SSmokeEmitter, ResponsiveSmokeField> pair in world.Fields)
        {
            if (!pair.Key.bDeleteMe)
            {
                _live.Add(pair);
            }
        }

        _live.Sort(static (a, b) => a.Key.ActorId.CompareTo(b.Key.ActorId));
        return _live;
    }
}
