using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text.Json;
using OpenScct.Bots;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.AI;
using OpenScct.Game.Equipment;
using OpenScct.Physics.Collision;
using OpenScct.Server;
using OpenScct.UnrealBridge.Pose.OutfitArms;

namespace OpenScct.UnrealBridge.Test;

/// <summary>What the simulation and the original's baked light say about one point (LightProbeExports hands it over).</summary>
public struct LightProbeReading
{
    /// <summary>The bots' level for a pawn here lit from every side, animated lights at their mean (routes, hiding spots).</summary>
    public float Level;

    /// <summary>The bots' level on the side of a pawn here that faces the viewer, now (what their sight uses).</summary>
    public float LevelFacing;

    /// <summary>Luminance of the zone ambient here.</summary>
    public float Ambient;

    /// <summary>The baked light term of the floor under the point (2 x the stored lightmap, display units), and its luminance.</summary>
    public float BakedR, BakedG, BakedB, Baked;

    public float FloorZ;
    public int Zone;

    /// <summary>The floor under the point is lightmapped level geometry, so the Baked values are known.</summary>
    public bool HasBaked;
}

/// <summary>A walkable spot chosen for the light parity measurement.</summary>
public readonly record struct LightProbeSpot(FVector Floor, float Level, int Kind);

/// <summary>
/// The light parity measurement's view of a running session's map: the bots' light field (the same
/// <see cref="AiLightField"/> a bot match builds, following the trigger lights of the client's map) and the original's
/// baked light under a point, read from the level geometry of render/world.glb and its lightmap pages. It also picks
/// the sample spots: walkable floor found by a <see cref="NavGrid"/> over the box of the map's lights, the darkest and
/// brightest by the field, a spread between and pairs of spots on either side of the sharpest steps in the level.
/// It reads the client world and never writes it. The play floor, the predicted picture of the grey sphere and the
/// spots of the switch test are in LightProbeChecks.cs.
/// </summary>
public sealed partial class LightProbe
{
    /// <summary>Height of a standing pawn's centre above the floor, and its radius (AiK).</summary>
    public const float CentreHeight = AiK.StandingHeight;

    public const float PawnRadius = AiK.PawnRadius;

    /// <summary>Spot kinds: among the darkest, among the brightest, the spread between, the lit and the dark side of a step.</summary>
    public const int KindDark = 0, KindBright = 1, KindSpread = 2, KindEdgeLit = 3, KindEdgeDark = 4;

    private const float GridSpacing = 48f;
    private const float Separation = 420f;
    private const float FloorCell = 128f;

    private readonly BridgeSession _session;
    private readonly ICollisionWorld? _collision;
    private readonly AiLightField _field;
    private readonly List<FVector> _lightPlaces = [];
    private readonly List<FloorTriangle> _floor = [];
    private readonly Dictionary<long, List<int>> _floorCells = new();
    private readonly List<Lightmap> _pages = [];

    public LightProbe(BridgeSession session)
    {
        _session = session;
        PlaySessionMap map = new(session);
        _collision = map.Collision;
        string folder = session.Session.MapFolder;
        _field = MapAiLight.Load(folder, _collision, session.Session.Client.Map?.Loaded);
        ReadLightPlaces(folder);
        try
        {
            ReadBaked(folder);
        }
        catch (Exception e) when (e is IOException or InvalidDataException or JsonException or KeyNotFoundException)
        {
            // No baked data for this map: the readings say so (HasBaked false).
            _floor.Clear();
            _floorCells.Clear();
            BakedError = e.Message;
        }
    }

    public string? BakedError { get; }

    public int LightCount => _field.LightCount + _field.SpotLightCount;

    public bool HasData => _field.HasData;

    public int FloorTriangleCount => _floor.Count;

    /// <summary>The reading for a pawn whose centre is at <paramref name="centre"/>, seen from <paramref name="viewer"/>.</summary>
    public LightProbeReading Read(FVector centre, float radius, FVector viewer)
    {
        Level level = _session.Session.Client.World.Level;
        _field.Time = level.Info.TimeSeconds;
        _field.FollowSwitches();
        var reading = new LightProbeReading
        {
            Level = _field.LevelAround(centre, radius),
            LevelFacing = _field.LevelAt(centre, radius, viewer),
            Ambient = _field.AmbientAt(centre).Luminance,
            Zone = _collision?.GetZoneIndex(centre) ?? -1,
            FloorZ = centre.Z - CentreHeight,
        };
        if (_collision is not null && _collision.LineCheck(out FCheckResult hit, null, centre - new FVector(0f, 0f, 400f), centre, default,
                ETraceFlags.TRACE_World | ETraceFlags.TRACE_StopAtFirstHit) && hit.bHit)
        {
            reading.FloorZ = hit.Location.Z;
        }

        if (BakedAt(centre.X, centre.Y, reading.FloorZ, out AiRgb baked))
        {
            reading.HasBaked = true;
            reading.BakedR = baked.R;
            reading.BakedG = baked.G;
            reading.BakedB = baked.B;
            reading.Baked = baked.Luminance;
        }

        return reading;
    }

    // ---- sample spots -------------------------------------------------------------------------------------------

    /// <summary>
    /// The sample spots: <paramref name="perKind"/> of the darkest, of the brightest and of the spread between, and
    /// <paramref name="perKind"/> more in pairs across the sharpest steps of the level, each at least a few metres from
    /// the others of its kind. Empty without collision or lights. Only the play floor is sampled (what can be walked
    /// or dropped to from a start, an objective, a flag zone or a switch: <see cref="PlayFloor"/>), unless
    /// <paramref name="allFloor"/> asks for the first version's rule: any connected floor a map light hangs over.
    /// </summary>
    public List<LightProbeSpot> FindSpots(int perKind, bool allFloor = false)
    {
        var spots = new List<LightProbeSpot>();
        if (_collision is null || _lightPlaces.Count == 0 || perKind <= 0)
        {
            return spots;
        }

        _field.Time = _session.Session.Client.World.Level.Info.TimeSeconds;
        _field.FollowSwitches();
        var min = new FVector(float.MaxValue, float.MaxValue, float.MaxValue);
        var max = new FVector(float.MinValue, float.MinValue, float.MinValue);
        foreach (FVector p in _lightPlaces)
        {
            min = new FVector(MathF.Min(min.X, p.X), MathF.Min(min.Y, p.Y), MathF.Min(min.Z, p.Z));
            max = new FVector(MathF.Max(max.X, p.X), MathF.Max(max.Y, p.Y), MathF.Max(max.Z, p.Z));
        }

        min -= new FVector(300f, 300f, 600f);
        max += new FVector(300f, 300f, 100f);
        NavGrid grid = NavGrid.Build(_collision, min, max, new NavGridSettings { Spacing = GridSpacing });
        IReadOnlyList<FVector> nodes = grid.Nodes;
        int[] component = grid.Components(out int componentCount);

        // The floor people play on: the connected pieces a map light hangs over, and the one the local player stands on.
        var size = new int[componentCount];
        var lit = new bool[componentCount];
        var cells = new Dictionary<long, List<int>>();
        for (int i = 0; i < nodes.Count; i++)
        {
            size[component[i]]++;
            long key = CellKey(nodes[i].X, nodes[i].Y, min, GridSpacing);
            if (!cells.TryGetValue(key, out List<int>? list))
            {
                cells[key] = list = new List<int>(1);
            }

            list.Add(i);
        }

        foreach (FVector p in _lightPlaces)
        {
            if (cells.TryGetValue(CellKey(p.X, p.Y, min, GridSpacing), out List<int>? under))
            {
                foreach (int n in under)
                {
                    float dz = p.Z - nodes[n].Z;
                    if (dz > 30f && dz < 700f)
                    {
                        lit[component[n]] = true;
                    }
                }
            }
        }

        if (_session.Session.LocalPawn is { } pawn)
        {
            int at = grid.NearestNode(pawn.Location, pawn.CollisionHeight);
            if (at >= 0)
            {
                lit[component[at]] = true;
                size[component[at]] = Math.Max(size[component[at]], 200);
            }
        }

        bool[]? play = allFloor ? null : PlayFloor(grid, component, componentCount);
        var use = new List<int>();
        var level = new float[nodes.Count];
        int litFloor = 0;
        for (int i = 0; i < nodes.Count; i++)
        {
            bool underLight = lit[component[i]] && size[component[i]] >= 200;
            litFloor += underLight ? 1 : 0;
            if (play is not null ? !play[i] : !underLight)
            {
                continue;
            }

            use.Add(i);
            level[i] = _field.LevelAround(nodes[i] + new FVector(0f, 0f, CentreHeight), PawnRadius);
        }

        _grid = grid;
        _playNodes = use;
        FloorNodes = nodes.Count;
        LitFloorNodes = litFloor;
        PlayNodes = use.Count;
        PlayRule = play is not null;
        if (use.Count == 0)
        {
            return spots;
        }

        // Ties (many spots read exactly 0) are broken by a fixed scramble of the node's number, so the choice is the same each run.
        static uint Scramble(int n) => unchecked((uint)n * 2654435761u);
        var byLevel = new List<int>(use);
        byLevel.Sort((a, b) => level[a] != level[b] ? level[a].CompareTo(level[b]) : Scramble(a).CompareTo(Scramble(b)));

        var taken = new List<FVector>();
        bool Free(FVector p, float apart)
        {
            foreach (FVector t in taken)
            {
                if (FVector.DistSquared(t, p) < apart * apart)
                {
                    return false;
                }
            }

            return true;
        }

        void Take(int node, int kind)
        {
            taken.Add(nodes[node]);
            spots.Add(new LightProbeSpot(nodes[node], level[node], kind));
        }

        // Steps first (they need two free spots close together): neighbours four cells apart on one floor with an open line.
        var steps = new List<(float Step, int Lit, int Dark)>();
        ReadOnlySpan<(int X, int Y)> reach = [(4, 0), (0, 4), (3, 3), (3, -3)];
        foreach (int a in use)
        {
            foreach ((int dx, int dy) in reach)
            {
                var q = new FVector(nodes[a].X + (dx * GridSpacing), nodes[a].Y + (dy * GridSpacing), 0f);
                if (!cells.TryGetValue(CellKey(q.X, q.Y, min, GridSpacing), out List<int>? there))
                {
                    continue;
                }

                foreach (int b in there)
                {
                    if (component[b] != component[a] || MathF.Abs(nodes[b].Z - nodes[a].Z) > 40f)
                    {
                        continue;
                    }

                    float step = MathF.Abs(level[a] - level[b]);
                    if (step >= 0.15f && MathF.Min(level[a], level[b]) < 0.15f)
                    {
                        steps.Add(level[a] > level[b] ? (step, a, b) : (step, b, a));
                    }
                }
            }
        }

        steps.Sort((x, y) => y.Step.CompareTo(x.Step));
        int pairs = 0;
        var up = new FVector(0f, 0f, CentreHeight);
        foreach ((float _, int litNode, int darkNode) in steps)
        {
            if (pairs >= perKind / 2)
            {
                break;
            }

            if (!Free(nodes[litNode], Separation) || !Free(nodes[darkNode], Separation) ||
                _collision.LineCheck(out _, null, nodes[darkNode] + up, nodes[litNode] + up, default, ETraceFlags.TRACE_World | ETraceFlags.TRACE_StopAtFirstHit))
            {
                continue;
            }

            Take(litNode, KindEdgeLit);
            Take(darkNode, KindEdgeDark);
            pairs++;
        }

        int count = 0;
        for (int i = 0; i < byLevel.Count && count < perKind; i++)
        {
            if (Free(nodes[byLevel[i]], Separation))
            {
                Take(byLevel[i], KindDark);
                count++;
            }
        }

        count = 0;
        for (int i = byLevel.Count - 1; i >= 0 && count < perKind; i--)
        {
            if (Free(nodes[byLevel[i]], Separation))
            {
                Take(byLevel[i], KindBright);
                count++;
            }
        }

        // The spread: levels spaced evenly (on a square-root scale, so the dim end is covered) between the two ends.
        float low = MathF.Sqrt(MathF.Max(0f, level[byLevel[0]]));
        float high = MathF.Sqrt(MathF.Max(0f, level[byLevel[^1]]));
        for (int k = 0; k < perKind; k++)
        {
            float root = low + ((high - low) * (k + 1) / (perKind + 1));
            float target = root * root;
            int best = -1;
            float bestOff = float.MaxValue;
            foreach (int n in use)
            {
                float off = MathF.Abs(level[n] - target);
                if (off < bestOff && Free(nodes[n], Separation))
                {
                    bestOff = off;
                    best = n;
                }
            }

            if (best >= 0)
            {
                Take(best, KindSpread);
            }
        }

        return spots;
    }

    private static long CellKey(float x, float y, FVector min, float cell) =>
        ((long)(int)MathF.Round((x - min.X) / cell) << 32) | (uint)(int)MathF.Round((y - min.Y) / cell);

    private void ReadLightPlaces(string folder)
    {
        string path = Path.Combine(folder, "render", "lighting.json");
        if (!File.Exists(path))
        {
            return;
        }

        using JsonDocument json = JsonDocument.Parse(File.ReadAllBytes(path));
        if (!json.RootElement.TryGetProperty("lights", out JsonElement lights) || lights.ValueKind != JsonValueKind.Array)
        {
            return;
        }

        foreach (JsonElement l in lights.EnumerateArray())
        {
            if (l.TryGetProperty("location", out JsonElement v) && v.ValueKind == JsonValueKind.Array && v.GetArrayLength() >= 3)
            {
                _lightPlaces.Add(new FVector(v[0].GetSingle(), v[1].GetSingle(), v[2].GetSingle()));
            }
        }
    }

    // ---- baked light ---------------------------------------------------------------------------------------------

    private readonly record struct FloorTriangle(FVector A, FVector B, FVector C, float Ua, float Va, float Ub, float Vb, float Uc, float Vc, int Page);

    private sealed record Lightmap(int Width, int Height, byte[] Rgb);

    /// <summary>
    /// The baked light term (2 x the stored lightmap, as the original modulates it) of the lightmapped floor at
    /// (x, y) within 30 uu of <paramref name="floorZ"/>. False when the floor there is not lightmapped level geometry
    /// (a static mesh, which is vertex lit, or nothing).
    /// </summary>
    public bool BakedAt(float x, float y, float floorZ, out AiRgb baked)
    {
        baked = default;
        long key = ((long)(int)MathF.Floor(x / FloorCell) << 32) | (uint)(int)MathF.Floor(y / FloorCell);
        if (!_floorCells.TryGetValue(key, out List<int>? candidates))
        {
            return false;
        }

        float bestOff = 30f;
        bool found = false;
        foreach (int i in candidates)
        {
            FloorTriangle t = _floor[i];
            float d = ((t.B.Y - t.C.Y) * (t.A.X - t.C.X)) + ((t.C.X - t.B.X) * (t.A.Y - t.C.Y));
            if (MathF.Abs(d) < 1e-6f)
            {
                continue;
            }

            float wa = (((t.B.Y - t.C.Y) * (x - t.C.X)) + ((t.C.X - t.B.X) * (y - t.C.Y))) / d;
            float wb = (((t.C.Y - t.A.Y) * (x - t.C.X)) + ((t.A.X - t.C.X) * (y - t.C.Y))) / d;
            float wc = 1f - wa - wb;
            if (wa < -0.001f || wb < -0.001f || wc < -0.001f)
            {
                continue;
            }

            float off = MathF.Abs((wa * t.A.Z) + (wb * t.B.Z) + (wc * t.C.Z) - floorZ);
            if (off < bestOff)
            {
                bestOff = off;
                found = true;
                baked = Sample(_pages[t.Page], (wa * t.Ua) + (wb * t.Ub) + (wc * t.Uc), (wa * t.Va) + (wb * t.Vb) + (wc * t.Vc));
            }
        }

        return found;
    }

    private static AiRgb Sample(Lightmap page, float u, float v)
    {
        float fx = Math.Clamp((u * page.Width) - 0.5f, 0f, page.Width - 1f);
        float fy = Math.Clamp((v * page.Height) - 0.5f, 0f, page.Height - 1f);
        int x0 = (int)fx, y0 = (int)fy;
        int x1 = Math.Min(x0 + 1, page.Width - 1), y1 = Math.Min(y0 + 1, page.Height - 1);
        float tx = fx - x0, ty = fy - y0;
        AiRgb Texel(int x, int y)
        {
            int at = ((y * page.Width) + x) * 3;
            return new AiRgb(page.Rgb[at] / 255f, page.Rgb[at + 1] / 255f, page.Rgb[at + 2] / 255f);
        }

        AiRgb top = (Texel(x0, y0) * (1f - tx)) + (Texel(x1, y0) * tx);
        AiRgb bottom = (Texel(x0, y1) * (1f - tx)) + (Texel(x1, y1) * tx);

        // Stored at half intensity and modulated 2x (docs/RENDERING.md "Baked light").
        return ((top * (1f - ty)) + (bottom * ty)) * 2f;
    }

    private void ReadBaked(string folder)
    {
        string path = Path.Combine(folder, "render", "world.glb");
        if (!File.Exists(path))
        {
            return;
        }

        using GlbDocument glb = GlbDocument.Load(path);
        JsonElement root = glb.Root;
        var pageOf = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        JsonElement materials = root.GetProperty("materials");
        JsonElement meshes = root.GetProperty("meshes");
        foreach (JsonElement node in root.GetProperty("nodes").EnumerateArray())
        {
            // The level geometry (role "bsp"), not its sky zone.
            if (!node.TryGetProperty("mesh", out JsonElement meshIndex) || !node.TryGetProperty("extras", out JsonElement extras) ||
                !extras.TryGetProperty("svmap", out JsonElement svmap) || !svmap.TryGetProperty("role", out JsonElement role) ||
                role.GetString() != "bsp" || (svmap.TryGetProperty("sky", out JsonElement sky) && sky.ValueKind == JsonValueKind.True))
            {
                continue;
            }

            foreach (JsonElement primitive in meshes[meshIndex.GetInt32()].GetProperty("primitives").EnumerateArray())
            {
                JsonElement attributes = primitive.GetProperty("attributes");
                if (!attributes.TryGetProperty("TEXCOORD_1", out JsonElement uvAccessor) || !primitive.TryGetProperty("material", out JsonElement material) ||
                    !primitive.TryGetProperty("indices", out JsonElement indexAccessor) ||
                    !materials[material.GetInt32()].TryGetProperty("extras", out JsonElement materialExtras) ||
                    !materialExtras.TryGetProperty("scct_lightmap", out JsonElement pageName) || pageName.GetString() is not { Length: > 0 } name)
                {
                    continue;
                }

                if (!pageOf.TryGetValue(name, out int page))
                {
                    string file = Path.Combine(folder, "render", name.Replace('/', Path.DirectorySeparatorChar));
                    if (!File.Exists(file))
                    {
                        continue;
                    }

                    page = _pages.Count;
                    _pages.Add(ReadPng(File.ReadAllBytes(file)));
                    pageOf[name] = page;
                }

                float[] positions = glb.ReadFloats(attributes.GetProperty("POSITION").GetInt32());
                float[] uvs = glb.ReadFloats(uvAccessor.GetInt32());
                int[] indices = glb.ReadIndices(indexAccessor.GetInt32());

                // The file is in metres with Y up; the simulation's point is (x, z, y) x 100.
                FVector Point(int i) => new(positions[i * 3] * 100f, positions[(i * 3) + 2] * 100f, positions[(i * 3) + 1] * 100f);
                for (int i = 0; i + 2 < indices.Length; i += 3)
                {
                    int ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
                    FVector a = Point(ia), b = Point(ib), c = Point(ic);
                    FVector ab = b - a, ac = c - a;
                    var normal = new FVector((ab.Y * ac.Z) - (ab.Z * ac.Y), (ab.Z * ac.X) - (ab.X * ac.Z), (ab.X * ac.Y) - (ab.Y * ac.X));
                    float length = normal.Size();

                    // Floors either way up in the file's winding; the nearest one under the feet is taken when sampling.
                    if (length < 1e-3f || MathF.Abs(normal.Z) / length < 0.7f)
                    {
                        continue;
                    }

                    int index = _floor.Count;
                    _floor.Add(new FloorTriangle(a, b, c, uvs[ia * 2], uvs[(ia * 2) + 1], uvs[ib * 2], uvs[(ib * 2) + 1], uvs[ic * 2], uvs[(ic * 2) + 1], page));
                    int x0 = (int)MathF.Floor(MathF.Min(a.X, MathF.Min(b.X, c.X)) / FloorCell);
                    int x1 = (int)MathF.Floor(MathF.Max(a.X, MathF.Max(b.X, c.X)) / FloorCell);
                    int y0 = (int)MathF.Floor(MathF.Min(a.Y, MathF.Min(b.Y, c.Y)) / FloorCell);
                    int y1 = (int)MathF.Floor(MathF.Max(a.Y, MathF.Max(b.Y, c.Y)) / FloorCell);
                    for (int x = x0; x <= x1; x++)
                    {
                        for (int y = y0; y <= y1; y++)
                        {
                            long key = ((long)x << 32) | (uint)y;
                            if (!_floorCells.TryGetValue(key, out List<int>? list))
                            {
                                _floorCells[key] = list = new List<int>(4);
                            }

                            list.Add(index);
                        }
                    }
                }
            }
        }
    }

    /// <summary>An 8-bit, non-interlaced RGB, RGBA or grey PNG as RGB bytes (the lightmap pages are 8-bit RGB).</summary>
    private static Lightmap ReadPng(ReadOnlySpan<byte> png)
    {
        if (png.Length < 33 || png[0] != 137 || png[1] != 80 || png[24] != 8 || png[28] != 0)
        {
            throw new InvalidDataException("The lightmap page is not an 8-bit non-interlaced PNG.");
        }

        int width = BinaryPrimitives.ReadInt32BigEndian(png[16..]);
        int height = BinaryPrimitives.ReadInt32BigEndian(png[20..]);
        int channels = png[25] switch { 0 => 1, 2 => 3, 6 => 4, _ => 0 };
        if (channels == 0 || width <= 0 || height <= 0 || (long)width * height > 67108864)
        {
            throw new InvalidDataException("The lightmap page must be grey, RGB or RGBA.");
        }

        using var compressed = new MemoryStream();
        for (int at = 8; at + 12 <= png.Length;)
        {
            int length = BinaryPrimitives.ReadInt32BigEndian(png[at..]);
            if (length < 0 || length > png.Length - at - 12)
            {
                throw new InvalidDataException("Truncated lightmap page.");
            }

            if (png.Slice(at + 4, 4).SequenceEqual("IDAT"u8))
            {
                compressed.Write(png.Slice(at + 8, length));
            }

            at += length + 12;
        }

        compressed.Position = 0;
        using var z = new ZLibStream(compressed, CompressionMode.Decompress);
        int stride = width * channels;
        var previous = new byte[stride];
        var row = new byte[stride];
        var encoded = new byte[stride];
        var rgb = new byte[width * height * 3];
        for (int y = 0; y < height; y++)
        {
            int filter = z.ReadByte();
            if (filter < 0 || filter > 4)
            {
                throw new InvalidDataException("Invalid lightmap page filter.");
            }

            z.ReadExactly(encoded);
            for (int x = 0; x < stride; x++)
            {
                int a = x < channels ? 0 : row[x - channels], b = previous[x], c = x < channels ? 0 : previous[x - channels], p = a + b - c;
                int pa = Math.Abs(p - a), pb = Math.Abs(p - b), pc = Math.Abs(p - c);
                int prediction = filter switch { 0 => 0, 1 => a, 2 => b, 3 => (a + b) / 2, _ => pa <= pb && pa <= pc ? a : pb <= pc ? b : c };
                row[x] = unchecked((byte)(encoded[x] + prediction));
            }

            for (int x = 0; x < width; x++)
            {
                int to = ((y * width) + x) * 3;
                if (channels == 1)
                {
                    rgb[to] = rgb[to + 1] = rgb[to + 2] = row[x];
                }
                else
                {
                    rgb[to] = row[x * channels];
                    rgb[to + 1] = row[(x * channels) + 1];
                    rgb[to + 2] = row[(x * channels) + 2];
                }
            }

            (previous, row) = (row, previous);
        }

        return new Lightmap(width, height, rgb);
    }

    /// <summary>The client world's collision of a session.</summary>
    private readonly struct PlaySessionMap(BridgeSession session)
    {
        public ICollisionWorld? Collision => GadgetWorld.Collision(session.Session.Client.World.Level.Info);
    }
}
