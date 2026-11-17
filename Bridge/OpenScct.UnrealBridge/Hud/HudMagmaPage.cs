using System;
using System.Collections.Generic;
using System.Text.Json;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// A page of a converted Magma package (shared/ui/magma/&lt;package&gt;.json) evaluated at a moment, as the Godot client's
/// MagmaRenderer draws it: each instance of the page is an area with its own clock (its frame from the seconds since it
/// was shown, looping on its own GotoFrame and held at a Stop), its widgets' keyframes are interpolated with the
/// destination key's easing, an instance's position and state move and tint everything in it, and images and texts come
/// out in drawing order with their page rectangles (640 x 480), texel regions, rotation and colours. Unreal paints the
/// layers; the page logic (what is shown, the texts written, a widget moved) is the caller's.
/// </summary>
public sealed class HudMagmaPage
{
    /// <summary>A picture or a text of the page at one moment, in page units.</summary>
    public readonly record struct Layer(
        bool IsText,
        string Package,
        string Texture,
        string Widget,
        string Text,
        float L, float T, float R, float B,
        float Rotation,
        float U0, float V0, float U1, float V1,
        float Red, float Green, float Blue, float Alpha,
        float Height,
        int Align,
        float ShadowRed, float ShadowGreen, float ShadowBlue, float ShadowAlpha,
        float ShadowX, float ShadowY,
        float Tracking);

    /// <summary>What the caller decides each moment: a widget's text or place, and which widgets are hidden.</summary>
    public sealed class Overrides
    {
        public Dictionary<string, string> Texts { get; } = new(StringComparer.Ordinal);

        /// <summary>A widget's top-left corner moved to this page point (MagmaNode.Widget(...).Pos).</summary>
        public Dictionary<string, (float X, float Y)> Positions { get; } = new(StringComparer.Ordinal);

        public HashSet<string> Hidden { get; } = new(StringComparer.Ordinal);

        public void Clear()
        {
            Texts.Clear();
            Positions.Clear();
            Hidden.Clear();
        }
    }

    private const int InterpNone = 0, InterpLinear = 1, InterpCircle = 2, InterpSquare = 3, InterpRoot = 4;
    private const int KindImage = 0, KindText = 1, KindInstance = 2, KindOther = 3;

    private sealed class Key
    {
        public int Frame;
        public int Interp;
        public bool HasRect;
        public float L, T, R, B;
        public float PosX, PosY;
        public float Rot;
        public float[] State = [1f, 1f, 1f, 1f];
        public float[] Colour = [1f, 1f, 1f, 1f];     // image: the mean of the four corner colours
        public float[] TextColour = [1f, 1f, 1f, 1f];
        public float[] Shadow = [0f, 0f, 0f, 0f];
        public float ShadowX, ShadowY;
        public float Height = 12f;
        public float Tracking;
        public float TilingX = 1f, TilingY = 1f;
        public float OffsetX, OffsetY;
    }

    private sealed class Widget
    {
        public string Name = "";
        public int Kind;
        public int Material = -1;
        public int Area = -1;
        public bool Hidden;
        public int Align;
        public string Text = "";
        public readonly List<Key> Keys = [];
    }

    private sealed class Area
    {
        public string Name = "";
        public float FrameRate = 10f;
        public int LoopFrom = -1, LoopTo = -1, StopAt = -1;
        public readonly List<Widget> Widgets = [];
    }

    private readonly string _package;
    private readonly Area[] _areas;
    private readonly (string Texture, int U0, int V0, int U1, int V1)?[] _materials;
    private readonly int _page;

    private HudMagmaPage(string package, Area[] areas, (string, int, int, int, int)?[] materials, int page)
    {
        _package = package;
        _areas = areas;
        _materials = materials;
        _page = page;
    }

    /// <summary>The page's name as the package lists it.</summary>
    public string Name => _areas[_page].Name;

    /// <summary>The names of the page's instances (its top-level areas), in drawing order.</summary>
    public IEnumerable<string> Instances
    {
        get
        {
            foreach (Widget w in _areas[_page].Widgets)
            {
                if (w.Kind == KindInstance)
                {
                    yield return w.Name;
                }
            }
        }
    }

    /// <summary>Reads page <paramref name="pageName"/> of <paramref name="package"/> from the configured content, or null with the reason.</summary>
    public static HudMagmaPage? Load(HudTextureLibrary library, string package, string pageName, out string? error)
    {
        if (library.Package(package) is not { } doc)
        {
            error = $"the UI content has no {package} package";
            return null;
        }

        JsonElement top = doc.RootElement;
        if (!top.TryGetProperty("pages", out JsonElement pages) || !pages.TryGetProperty(pageName, out JsonElement pageIndex))
        {
            error = $"the {package} package has no page {pageName}";
            return null;
        }

        JsonElement areasJson = top.GetProperty("areas");
        var areas = new Area[areasJson.GetArrayLength()];
        for (int i = 0; i < areas.Length; i++)
        {
            areas[i] = ReadArea(areasJson[i], i);
        }

        JsonElement materialsJson = top.GetProperty("materials");
        var materials = new (string, int, int, int, int)?[materialsJson.GetArrayLength()];
        for (int i = 0; i < materials.Length; i++)
        {
            materials[i] = HudTextureLibrary.MaterialAt(materialsJson[i], out string texture, out int u0, out int v0, out int u1, out int v1)
                ? (texture, u0, v0, u1, v1)
                : null;
        }

        int page = pageIndex.GetInt32();
        if (page < 0 || page >= areas.Length)
        {
            error = $"page {pageName} points outside the package";
            return null;
        }

        error = null;
        return new HudMagmaPage(package, areas, materials, page);
    }

    private static Area ReadArea(JsonElement a, int index)
    {
        var area = new Area
        {
            Name = a.TryGetProperty("name", out JsonElement n) ? n.GetString() ?? "" : "",
            FrameRate = a.TryGetProperty("frameRate", out JsonElement fr) ? Math.Max(1f, fr.GetSingle()) : 10f,
        };
        if (!a.TryGetProperty("widgets", out JsonElement widgets))
        {
            return area;
        }

        foreach (JsonElement w in widgets.EnumerateArray())
        {
            string kind = w.TryGetProperty("kind", out JsonElement k) ? k.GetString() ?? "" : "";
            var widget = new Widget
            {
                Name = w.TryGetProperty("name", out JsonElement wn) ? wn.GetString() ?? "" : "",
                Kind = kind switch { "image" => KindImage, "text" => KindText, "inst" => KindInstance, _ => KindOther },
                Material = Int(w, "material"),
                Area = Int(w, "area"),
                Hidden = w.TryGetProperty("hidden", out JsonElement h) && h.ValueKind == JsonValueKind.True,
                Align = (w.TryGetProperty("align", out JsonElement al) ? al.GetString() : null) switch { "center" => 1, "right" => 2, _ => 0 },
                Text = w.TryGetProperty("text", out JsonElement t) ? t.GetString() ?? "" : "",
            };

            if (w.TryGetProperty("keys", out JsonElement keys))
            {
                foreach (JsonElement kj in keys.EnumerateArray())
                {
                    int frame = kj.GetProperty("f").GetInt32();
                    ReadLoop(kj, frame, index, area);
                    widget.Keys.Add(ReadKey(kj, frame));
                }
            }

            area.Widgets.Add(widget);
        }

        return area;
    }

    /// <summary>A whole-number property, or -1 when it is missing or null (a widget without a material or an area).</summary>
    private static int Int(JsonElement e, string name) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? v.GetInt32() : -1;

    /// <summary>A GotoFrame or Stop aimed at the area itself loops or ends its clip (as HudCoopIcons reads them).</summary>
    private static void ReadLoop(JsonElement k, int frame, int areaIndex, Area area)
    {
        if (!k.TryGetProperty("actions", out JsonElement actions))
        {
            return;
        }

        foreach (JsonElement a in actions.EnumerateArray())
        {
            string op = a.TryGetProperty("op", out JsonElement o) ? o.GetString() ?? "" : "";
            int target = a.TryGetProperty("area", out JsonElement at) ? at.GetInt32() : -1;
            if (target != areaIndex)
            {
                continue;
            }

            if (op == "goto" && (area.LoopFrom < 0 || frame < area.LoopFrom))
            {
                area.LoopFrom = frame;
                area.LoopTo = a.TryGetProperty("frame", out JsonElement f) ? f.GetInt32() : 0;
            }
            else if (op == "stop" && (area.StopAt < 0 || frame < area.StopAt))
            {
                area.StopAt = frame;
            }
        }
    }

    private static Key ReadKey(JsonElement k, int frame)
    {
        var key = new Key
        {
            Frame = frame,
            Interp = (k.TryGetProperty("interp", out JsonElement i) ? i.GetString() : null) switch
            {
                "linear" => InterpLinear,
                "circle" => InterpCircle,
                "square" => InterpSquare,
                "root" => InterpRoot,
                _ => InterpNone,
            },
        };
        if (k.TryGetProperty("rect", out JsonElement rect) && rect.GetArrayLength() >= 4)
        {
            key.HasRect = true;
            key.L = rect[0].GetSingle();
            key.T = rect[1].GetSingle();
            key.R = rect[2].GetSingle();
            key.B = rect[3].GetSingle();
        }

        if (k.TryGetProperty("pos", out JsonElement pos) && pos.GetArrayLength() >= 2)
        {
            key.PosX = pos[0].GetSingle();
            key.PosY = pos[1].GetSingle();
        }

        key.Rot = k.TryGetProperty("rot", out JsonElement rot) ? rot.GetSingle() : 0f;
        if (k.TryGetProperty("state", out JsonElement state) && state.GetArrayLength() >= 4)
        {
            key.State = [state[0].GetSingle() / 255f, state[1].GetSingle() / 255f, state[2].GetSingle() / 255f, state[3].GetSingle() / 255f];
        }

        if (k.TryGetProperty("colors", out JsonElement colours) && colours.GetArrayLength() > 0)
        {
            // COLORn as 0xAABBGGRR; the corners of these pages are alike, so their mean stands for the quad.
            float[] c = [0f, 0f, 0f, 0f];
            int n = 0;
            foreach (JsonElement v in colours.EnumerateArray())
            {
                uint abgr = v.GetUInt32();
                c[0] += (abgr & 0xFF) / 255f;
                c[1] += ((abgr >> 8) & 0xFF) / 255f;
                c[2] += ((abgr >> 16) & 0xFF) / 255f;
                c[3] += ((abgr >> 24) & 0xFF) / 255f;
                n++;
            }

            key.Colour = [c[0] / n, c[1] / n, c[2] / n, c[3] / n];
        }

        key.TextColour = Rgba(k, "color", key.TextColour);
        key.Shadow = Rgba(k, "shadow", key.Shadow);
        if (k.TryGetProperty("shadowOffset", out JsonElement so) && so.GetArrayLength() >= 2)
        {
            key.ShadowX = so[0].GetSingle();
            key.ShadowY = so[1].GetSingle();
        }

        key.Height = k.TryGetProperty("height", out JsonElement height) ? height.GetSingle() : key.Height;
        key.Tracking = k.TryGetProperty("tracking", out JsonElement tracking) ? tracking.GetSingle() : 0f;
        if (k.TryGetProperty("tiling", out JsonElement tiling) && tiling.GetArrayLength() >= 2)
        {
            key.TilingX = tiling[0].GetSingle();
            key.TilingY = tiling[1].GetSingle();
        }

        if (k.TryGetProperty("offset", out JsonElement offset) && offset.GetArrayLength() >= 2)
        {
            key.OffsetX = offset[0].GetSingle();
            key.OffsetY = offset[1].GetSingle();
        }

        return key;
    }

    private static float[] Rgba(JsonElement k, string name, float[] fallback)
    {
        if (!k.TryGetProperty(name, out JsonElement c) || c.GetArrayLength() < 4)
        {
            return fallback;
        }

        return [c[0].GetSingle() / 255f, c[1].GetSingle() / 255f, c[2].GetSingle() / 255f, c[3].GetSingle() / 255f];
    }

    /// <summary>
    /// The page's layers in drawing order. <paramref name="shownFor"/> gives each top-level instance the seconds since it
    /// was shown (its clock), or null to leave it out; an instance the content hides is left out too. Instances inside
    /// an instance run on their parent's clock.
    /// </summary>
    public void Evaluate(Func<string, float?> shownFor, Overrides? overrides, List<Layer> into)
    {
        into.Clear();
        Area page = _areas[_page];
        foreach (Widget w in page.Widgets)
        {
            if (w.Kind != KindInstance || w.Hidden || w.Area < 0 || w.Area >= _areas.Length || shownFor(w.Name) is not { } seconds)
            {
                continue;
            }

            Key k = At(w, 0f);
            DrawArea(w.Area, seconds, k.PosX, k.PosY, k.State, overrides, into, 0);
        }
    }

    private void DrawArea(int index, float seconds, float originX, float originY, float[] tint, Overrides? overrides, List<Layer> into, int depth)
    {
        Area area = _areas[index];
        float frame = HudCoopIcons.FrameAt(seconds, area.FrameRate, area.LoopFrom, area.LoopTo, area.StopAt);
        foreach (Widget w in area.Widgets)
        {
            if (w.Keys.Count == 0 || frame < w.Keys[0].Frame || (overrides?.Hidden.Contains(w.Name) ?? false))
            {
                continue;
            }

            Key k = At(w, frame);
            if (w.Kind == KindInstance)
            {
                if (!w.Hidden && depth < 8 && w.Area >= 0 && w.Area < _areas.Length)
                {
                    DrawArea(w.Area, seconds, originX + k.PosX, originY + k.PosY, Mul(tint, k.State), overrides, into, depth + 1);
                }

                continue;
            }

            if (!k.HasRect || (w.Kind != KindImage && w.Kind != KindText))
            {
                continue;
            }

            float l = k.L, t = k.T, r = k.R, b = k.B;
            if (overrides is not null && overrides.Positions.TryGetValue(w.Name, out (float X, float Y) moved))
            {
                (l, t, r, b) = (moved.X, moved.Y, moved.X + (r - l), moved.Y + (b - t));
            }

            l += originX;
            r += originX;
            t += originY;
            b += originY;
            if (w.Kind == KindText)
            {
                string text = overrides is not null && overrides.Texts.TryGetValue(w.Name, out string? written) ? written : w.Text;
                if (text.Length == 0)
                {
                    continue;
                }

                float[] c = Mul(Mul(k.TextColour, k.State), tint);
                float[] s = Mul(Mul(k.Shadow, k.State), tint);
                // Magma's shadow offset counts y upwards: (1, -1) is one unit right and one down.
                into.Add(new Layer(true, "", "", w.Name, text, l, t, r, b, 0f, 0f, 0f, 0f, 0f, c[0], c[1], c[2], c[3],
                    k.Height, w.Align, s[0], s[1], s[2], s[3], k.ShadowX, -k.ShadowY, k.Tracking));
                continue;
            }

            if (w.Material < 0 || w.Material >= _materials.Length || _materials[w.Material] is not { } m)
            {
                continue;
            }

            // MagmaRenderer's image path: the box reaches one unit past its right and bottom keys; the tiling scales the
            // region (a negative factor mirrors it) and the offset shifts it.
            float su0 = (k.TilingX >= 0f ? 0f : 1f) + k.OffsetX, su1 = (k.TilingX >= 0f ? k.TilingX : 1f + k.TilingX) + k.OffsetX;
            float sv0 = (k.TilingY >= 0f ? 0f : 1f) + k.OffsetY, sv1 = (k.TilingY >= 0f ? k.TilingY : 1f + k.TilingY) + k.OffsetY;
            float[] colour = Mul(Mul(k.Colour, k.State), tint);
            into.Add(new Layer(false, _package, m.Texture, w.Name, "", l, t, r + 1f, b + 1f, k.Rot,
                m.U0 + su0 * (m.U1 - m.U0), m.V0 + sv0 * (m.V1 - m.V0), m.U0 + su1 * (m.U1 - m.U0), m.V0 + sv1 * (m.V1 - m.V0),
                Math.Clamp(colour[0], 0f, 1f), Math.Clamp(colour[1], 0f, 1f), Math.Clamp(colour[2], 0f, 1f), Math.Clamp(colour[3], 0f, 1f),
                0f, 0, 0f, 0f, 0f, 0f, 0f, 0f, 0f));
        }
    }

    /// <summary>The widget's key at <paramref name="frame"/>: a segment eases with its destination key's interpolation; "none" holds the earlier key.</summary>
    private static Key At(Widget w, float frame)
    {
        List<Key> keys = w.Keys;
        int i = 0;
        while (i + 1 < keys.Count && keys[i + 1].Frame <= frame)
        {
            i++;
        }

        Key a = keys[i];
        Key? b = i + 1 < keys.Count ? keys[i + 1] : null;
        if (b is null || b.Interp == InterpNone || frame <= a.Frame)
        {
            return a;
        }

        float x = Math.Clamp((frame - a.Frame) / Math.Max(1, b.Frame - a.Frame), 0f, 1f);
        float s = b.Interp switch
        {
            InterpCircle => 1f - MathF.Sqrt(Math.Max(0f, 1f - x * x)),
            InterpSquare => x * x,
            InterpRoot => MathF.Sqrt(x),
            _ => x,
        };

        float L(float p, float q) => p + (q - p) * s;
        float[] L4(float[] p, float[] q) => [L(p[0], q[0]), L(p[1], q[1]), L(p[2], q[2]), L(p[3], q[3])];
        return new Key
        {
            Frame = a.Frame,
            Interp = b.Interp,
            HasRect = a.HasRect && b.HasRect,
            L = L(a.L, b.L), T = L(a.T, b.T), R = L(a.R, b.R), B = L(a.B, b.B),
            PosX = L(a.PosX, b.PosX), PosY = L(a.PosY, b.PosY),
            Rot = L(a.Rot, b.Rot),
            State = L4(a.State, b.State),
            Colour = L4(a.Colour, b.Colour),
            TextColour = L4(a.TextColour, b.TextColour),
            Shadow = L4(a.Shadow, b.Shadow),
            ShadowX = L(a.ShadowX, b.ShadowX), ShadowY = L(a.ShadowY, b.ShadowY),
            Height = L(a.Height, b.Height),
            Tracking = L(a.Tracking, b.Tracking),
            TilingX = L(a.TilingX, b.TilingX), TilingY = L(a.TilingY, b.TilingY),
            OffsetX = L(a.OffsetX, b.OffsetX), OffsetY = L(a.OffsetY, b.OffsetY),
        };
    }

    private static float[] Mul(float[] a, float[] b) => [a[0] * b[0], a[1] * b[1], a[2] * b[2], a[3] * b[3]];

    /// <summary>The home rectangle of a widget of one of the page's instances (its first key), for moving it from there.</summary>
    public bool Home(string instance, string widget, out float l, out float t)
    {
        l = t = 0f;
        foreach (Widget i in _areas[_page].Widgets)
        {
            if (i.Kind != KindInstance || i.Name != instance || i.Area < 0 || i.Area >= _areas.Length)
            {
                continue;
            }

            foreach (Widget w in _areas[i.Area].Widgets)
            {
                if (w.Name == widget && w.Keys.Count > 0 && w.Keys[0].HasRect)
                {
                    l = w.Keys[0].L;
                    t = w.Keys[0].T;
                    return true;
                }
            }
        }

        return false;
    }
}
