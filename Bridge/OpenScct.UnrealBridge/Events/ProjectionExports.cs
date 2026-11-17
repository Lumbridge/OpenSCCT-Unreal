using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Physics.Collision;
using OpenScct.Provenance;
using OpenScct.UnrealBridge.Events;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge;

/// <summary>
/// One abandoned projection of the client world, placed on the surface it covers (Source/OpenSCCT/Runtime/
/// ScctProjectionsApi.h mirrors it field for field).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeProjection
{
    /// <summary>The projection's serial (AbandonedProjection.Serial): the same projection keeps it until it goes.</summary>
    public int Serial;

    /// <summary>Name id (EventsExports.GetName) of the projected texture's object path (sfx.FootPrints.step01).</summary>
    public int TextureId;

    /// <summary>
    /// Name id of the texture's converted picture without folder or extension (sfx_step01): the name of the imported
    /// texture asset (Tools/Effects/build_effect_assets.py). 0 when the content does not list the texture.
    /// </summary>
    public int AssetId;

    /// <summary>Name id of the projector's class (SBase.SFootPrint).</summary>
    public int ClassId;

    /// <summary>Simulation id of the actor the projection lies on (a door, a lift), 0 for the level's own geometry.</summary>
    public int SurfaceActorId;

    /// <summary><see cref="BridgeProjectionFlags"/>.</summary>
    public int Flags;

    /// <summary>Where the projection lies on the surface.</summary>
    public float X, Y, Z;

    /// <summary>The surface's normal there, out of the surface.</summary>
    public float NormalX, NormalY, NormalZ;

    /// <summary>The direction on the surface the texture's U runs (along the walk, toes first, for a footprint).</summary>
    public float UX, UY, UZ;

    /// <summary>The size of the projection on the surface, cm (the texture's size times the projector's DrawScale).</summary>
    public float Width, Height;

    /// <summary>The colour and opacity a tinted projection is drawn in (sRGB, 0..1); white for one drawn as its texture.</summary>
    public float TintR, TintG, TintB, TintA;

    /// <summary>Seconds it is drawn in all, and seconds left.</summary>
    public float Life, Remaining;

    /// <summary>How much of it is drawn now, 0..1: it fades out over the end of its life.</summary>
    public float Opacity;
}

public static class BridgeProjectionFlags
{
    /// <summary>Drawn in the tint's colour with the texture's alpha times the tint's (SFootPrint's Combiner).</summary>
    public const int Tinted = 1;

    /// <summary>The texture is upside down (SFootPrint's right foot).</summary>
    public const int FlipV = 2;
}

/// <summary>
/// The entry points for the abandoned projections of the client world (Projector.AbandonProjector): the flare's burn
/// mark, the shotgun's pellet marks and the footprints, as the Godot client's CombatFxView.TrackProjections draws them.
/// Each is placed once, when first seen, on the first surface along its forward axis within its MaxTraceDistance.
/// It reads the client world and never writes it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ProjectionApi
{
    public int Version;

    /// <summary>Writes the projections drawn now, oldest first. Returns the count written.</summary>
    public delegate* unmanaged[Cdecl]<BridgeProjection*, int, int> GetProjections;

    /// <summary>Projections placed since the session started, and ones whose line met no surface (not drawn).</summary>
    public delegate* unmanaged[Cdecl]<int*, int*, int> GetCounts;
}

public static unsafe class ProjectionExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(ProjectionApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->GetProjections = &GetProjections;
        api->GetCounts = &GetCounts;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetProjections(BridgeProjection* into, int capacity)
    {
        if (into == null || capacity <= 0 || Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        try
        {
            return ProjectionFeature.Of(session).Write(new Span<BridgeProjection>(into, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetCounts(int* placed, int* missed)
    {
        if (Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        try
        {
            ProjectionFeature feature = ProjectionFeature.Of(session);
            if (placed != null)
            {
                *placed = feature.Placed;
            }

            if (missed != null)
            {
                *missed = feature.Missed;
            }

            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}

/// <summary>The abandoned projections of one session's client world, each placed on its surface once.</summary>
public sealed class ProjectionFeature
{
    /// <summary>The trace the Godot client's particles and projections use (ParticleK.TraceFlags).</summary>
    private const ETraceFlags TraceFlags = (ETraceFlags)0x84;

    /// <summary>A projection fades out over this part of its life, at most <see cref="MaxFadeSeconds"/>.</summary>
    [Provenance(ProvenanceKind.Estimated, "Stock UE2 drops an abandoned projection at its time; a short fade-out stands in for the pop")]
    private const float FadeShare = 0.25f;

    private const float MaxFadeSeconds = 4f;

    /// <summary>The shotgun's pellet marks are capped at the class default's scale (CombatFxView.ProjectionNode).</summary>
    private const float ShotgunMaxDrawScale = 0.3f;

    private sealed class Placement
    {
        public required BridgeProjection Record;
        public required float Expires;
    }

    private readonly PlaySession _session;
    private readonly Dictionary<string, (int Width, int Height, string Asset)> _textures = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<int, Placement> _placed = new();
    private readonly List<int> _gone = new();
    private AbandonedProjectors? _list;
    private int _lastSerial;

    private ProjectionFeature(BridgeSession bridge)
    {
        _session = bridge.Session;
        if (SharedContent.FindRoot(_session.MapFolder) is { } sharedRoot)
        {
            LoadTextures(Path.Combine(sharedRoot, "fx", "effects.json"));
        }
    }

    public static ProjectionFeature Of(BridgeSession session) => session.Feature(static s => new ProjectionFeature(s));

    /// <summary>Projections placed on a surface since the session started.</summary>
    public int Placed { get; private set; }

    /// <summary>Projections whose line met no surface, or whose texture the content does not list: not drawn.</summary>
    public int Missed { get; private set; }

    /// <summary>The texture sizes and pictures of shared/fx/effects.json (the Godot client's FxLibrary texture table).</summary>
    private void LoadTextures(string path)
    {
        if (!File.Exists(path))
        {
            return;
        }

        using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(path));
        if (!doc.RootElement.TryGetProperty("textures", out JsonElement textures) || textures.ValueKind != JsonValueKind.Object)
        {
            return;
        }

        foreach (JsonProperty t in textures.EnumerateObject())
        {
            JsonElement v = t.Value;
            if (v.TryGetProperty("width", out JsonElement w) && v.TryGetProperty("height", out JsonElement h) &&
                v.TryGetProperty("png", out JsonElement png) && png.GetString() is { } file)
            {
                _textures[t.Name] = (w.GetInt32(), h.GetInt32(), Path.GetFileNameWithoutExtension(file));
            }
        }
    }

    public int Write(Span<BridgeProjection> into)
    {
        Level level = _session.Client.World.Level;
        float now = level.Info.TimeSeconds;
        AbandonedProjectors? list = level.GetService<AbandonedProjectors>();
        if (!ReferenceEquals(list, _list))
        {
            // A new level: its serials start again.
            _placed.Clear();
            _list = list;
            _lastSerial = 0;
        }

        if (list is not null)
        {
            foreach (AbandonedProjection p in list.Live)
            {
                if (p.Serial <= _lastSerial)
                {
                    continue;
                }

                _lastSerial = p.Serial;
                if (p.Expires > now)
                {
                    if (Place(level, p, now) is { } placed)
                    {
                        _placed[p.Serial] = placed;
                        Placed++;
                    }
                    else
                    {
                        Missed++;
                    }
                }
            }
        }

        _gone.Clear();
        int count = 0;
        foreach ((int serial, Placement placed) in _placed)
        {
            float remaining = placed.Expires - now;
            if (remaining <= 0f)
            {
                _gone.Add(serial);
                continue;
            }

            if (count >= into.Length)
            {
                continue;
            }

            BridgeProjection r = placed.Record;
            r.Remaining = remaining;
            float fade = MathF.Min(MaxFadeSeconds, r.Life * FadeShare);
            r.Opacity = fade > 0f ? Math.Clamp(remaining / fade, 0f, 1f) : 1f;
            into[count++] = r;
        }

        foreach (int serial in _gone)
        {
            _placed.Remove(serial);
        }

        return count;
    }

    /// <summary>
    /// CombatFxView.ProjectionNode without the drawing: the first surface along the projector's X axis within its
    /// MaxTraceDistance, the projection's size (the texture's size times DrawScale: a projector of FOV near 0 is
    /// orthographic) and the texture's U on the surface (CombatFxView.ProjectionBasis). Null when nothing is hit.
    /// </summary>
    [Provenance(ProvenanceKind.Estimated, "As the Godot client: the projection is not decoded, a flat mark of the texture's size x DrawScale on the surface hit along the projector's axis")]
    private Placement? Place(Level level, AbandonedProjection p, float now)
    {
        string? texturePath = p.ProjTexture.Path;
        if (texturePath is null || !_textures.TryGetValue(texturePath, out var texture) ||
            GadgetWorld.Collision(level.Info) is not { } world)
        {
            return null;
        }

        p.Rotation.GetAxes(out FVector x, out FVector y, out FVector z);
        FVector end = p.Location + (x * p.MaxTraceDistance);
        if (!world.LineCheck(out FCheckResult hit, null, end, p.Location, FVector.Zero, TraceFlags) || !hit.bHit)
        {
            return null;
        }

        string cls = p.Class.ToString();
        float drawScale = cls is "SShotGunProjector" or "SBase.SShotGunProjector" ? Math.Clamp(p.DrawScale, 0f, ShotgunMaxDrawScale) : p.DrawScale;
        FVector n = hit.Normal.SafeNormal();
        FVector u = OnSurface(z * -1f, n);
        if (u.SizeSquared() < 1e-6f)
        {
            u = OnSurface(y, n);
        }

        if (u.SizeSquared() < 1e-6f)
        {
            u = OnSurface(MathF.Abs(n.Z) > 0.99f ? new FVector(0f, 1f, 0f) : new FVector(0f, 0f, 1f), n);
        }

        u = u.SafeNormal();
        FColor tint = p.Tint ?? new FColor { R = 255, G = 255, B = 255, A = 255 };
        Actor? surface = hit.Actor is null or LevelInfo ? null : hit.Actor;
        float life = p.Expires - now;
        return new Placement
        {
            Expires = p.Expires,
            Record = new BridgeProjection
            {
                Serial = p.Serial,
                TextureId = EventNames.Id(texturePath),
                AssetId = EventNames.Id(texture.Asset),
                ClassId = EventNames.Id(cls),
                SurfaceActorId = surface?.ActorId ?? 0,
                Flags = (p.Tint is not null ? BridgeProjectionFlags.Tinted : 0) | (p.FlipV ? BridgeProjectionFlags.FlipV : 0),
                X = hit.Location.X,
                Y = hit.Location.Y,
                Z = hit.Location.Z,
                NormalX = n.X,
                NormalY = n.Y,
                NormalZ = n.Z,
                UX = u.X,
                UY = u.Y,
                UZ = u.Z,
                Width = texture.Width * drawScale,
                Height = texture.Height * drawScale,
                TintR = tint.R / 255f,
                TintG = tint.G / 255f,
                TintB = tint.B / 255f,
                TintA = tint.A / 255f,
                Life = life,
                Remaining = life,
                Opacity = 1f,
            },
        };
    }

    private static FVector OnSurface(FVector d, FVector n) => d - (n * (d | n));
}
