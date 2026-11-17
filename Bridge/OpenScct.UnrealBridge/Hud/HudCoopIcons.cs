using System;
using System.Collections.Generic;
using System.Text.Json;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// The spy page's coop icons as the content draws them: A_Coop of the Hud Magma package holds four instances (Coop1 to
/// Coop4) of the clips Move_coop to Move_coop4, each a few posed figures from Move_coop and Move_coop02 that fade and
/// loop at the clip's frame rate. The Godot client plays the clip through its Magma player (SpyHudPage.DriveCoop:
/// one clip shown from frame 0 when it appears, its console button art hidden); this evaluates the same keyframes at
/// a time since the icon appeared, so Unreal draws the same figures with the same fades.
/// </summary>
public sealed class HudCoopIcons
{
    /// <summary>One image of a clip at one moment, in page units (the 640 x 480 Magma page) and texels.</summary>
    public readonly record struct Layer(string Texture, float L, float T, float R, float B, int U0, int V0, int U1, int V1,
        float Red, float Green, float Blue, float Alpha);

    private sealed record Key(int Frame, int Interp, float L, float T, float R, float B, float[] Colour);

    private sealed record Image(string Texture, int U0, int V0, int U1, int V1, List<Key> Keys);

    private sealed record Clip(string Name, float FrameRate, List<Image> Images, int LoopFrom, int LoopTo, int StopAt);

    private const int InterpNone = 0, InterpLinear = 1, InterpCircle = 2, InterpSquare = 3, InterpRoot = 4;

    private readonly Clip?[] _clips = new Clip?[4];

    /// <summary>The package the textures of every layer belong to.</summary>
    public const string Package = "Hud";

    private readonly (float L, float T, float R, float B)[] _bounds = new (float, float, float, float)[5];

    /// <summary>The box every pose of the four clips stays in, in page units: where A_Coop sits on P_HudATT.</summary>
    public (float L, float T, float R, float B) Bounds => _bounds[0];

    /// <summary>The box every pose of clip <paramref name="icon"/> (1..4) stays in; 0 gives the box of all four.</summary>
    public (float L, float T, float R, float B) BoundsOf(int icon) => _bounds[Math.Clamp(icon, 0, 4)];

    private HudCoopIcons()
    {
    }

    private static HudCoopIcons? _kept;
    private static HudTextureLibrary? _keptFor;

    /// <summary>The icons of the configured content, or null with the reason.</summary>
    public static HudCoopIcons? Current(out string? error)
    {
        HudTextureLibrary? library = HudTextureLibrary.Current(out error);
        if (library is null)
        {
            return null;
        }

        if (_kept is not null && ReferenceEquals(_keptFor, library))
        {
            return _kept;
        }

        _keptFor = library;
        _kept = Load(library, out error);
        return _kept;
    }

    /// <summary>Reads the four clips from the Hud package of <paramref name="library"/>.</summary>
    public static HudCoopIcons? Load(HudTextureLibrary library, out string? error)
    {
        if (library.Package(Package) is not { } doc)
        {
            error = "the UI content has no Hud package";
            return null;
        }

        JsonElement top = doc.RootElement;
        JsonElement areas = top.GetProperty("areas");
        JsonElement materials = top.GetProperty("materials");
        int coop = -1;
        for (int i = 0; i < areas.GetArrayLength(); i++)
        {
            if (areas[i].TryGetProperty("name", out JsonElement n) && n.GetString() == "A_Coop")
            {
                coop = i;
                break;
            }
        }

        if (coop < 0)
        {
            error = "the Hud package has no A_Coop area";
            return null;
        }

        var icons = new HudCoopIcons();
        float l = float.MaxValue, t = float.MaxValue, r = float.MinValue, b = float.MinValue;
        foreach (JsonElement w in areas[coop].GetProperty("widgets").EnumerateArray())
        {
            string name = w.TryGetProperty("name", out JsonElement wn) ? wn.GetString() ?? "" : "";
            if (!name.StartsWith("Coop", StringComparison.Ordinal) || !int.TryParse(name.AsSpan(4), out int slot) ||
                slot is < 1 or > 4 || !w.TryGetProperty("area", out JsonElement areaIndex))
            {
                continue;
            }

            int index = areaIndex.GetInt32();
            Clip clip = ReadClip(areas[index], index, materials);
            icons._clips[slot - 1] = clip;
            float cl = float.MaxValue, ct = float.MaxValue, cr = float.MinValue, cb = float.MinValue;
            foreach (Image image in clip.Images)
            {
                foreach (Key k in image.Keys)
                {
                    cl = Math.Min(cl, Math.Min(k.L, k.R));
                    ct = Math.Min(ct, Math.Min(k.T, k.B));
                    cr = Math.Max(cr, Math.Max(k.L, k.R));
                    cb = Math.Max(cb, Math.Max(k.T, k.B));
                }
            }

            icons._bounds[slot] = (cl, ct, cr, cb);
            (l, t, r, b) = (Math.Min(l, cl), Math.Min(t, ct), Math.Max(r, cr), Math.Max(b, cb));
        }

        if (l > r)
        {
            error = "A_Coop has no coop clips";
            return null;
        }

        icons._bounds[0] = (l, t, r, b);
        error = null;
        return icons;
    }

    private static Clip ReadClip(JsonElement area, int areaIndex, JsonElement materials)
    {
        var images = new List<Image>();
        int loopFrom = -1, loopTo = -1, stopAt = -1;
        foreach (JsonElement w in area.GetProperty("widgets").EnumerateArray())
        {
            List<Key> keys = [];
            if (w.TryGetProperty("keys", out JsonElement ks))
            {
                foreach (JsonElement k in ks.EnumerateArray())
                {
                    int frame = k.GetProperty("f").GetInt32();
                    if (k.TryGetProperty("actions", out JsonElement actions))
                    {
                        // A GotoFrame or Stop on the clip itself loops or ends it. One aimed at another clip (Move_coop2's
                        // goto lands in Move_coop3) restarts that hidden clip in the original, so this one plays on to
                        // its last keyframe and holds it.
                        foreach (JsonElement a in actions.EnumerateArray())
                        {
                            string op = a.TryGetProperty("op", out JsonElement o) ? o.GetString() ?? "" : "";
                            int target = a.TryGetProperty("area", out JsonElement at) ? at.GetInt32() : -1;
                            if (target != areaIndex)
                            {
                                continue;
                            }

                            if (op == "goto" && (loopFrom < 0 || frame < loopFrom))
                            {
                                loopFrom = frame;
                                loopTo = a.TryGetProperty("frame", out JsonElement f) ? f.GetInt32() : 0;
                            }
                            else if (op == "stop" && (stopAt < 0 || frame < stopAt))
                            {
                                stopAt = frame;
                            }
                        }
                    }

                    if (!k.TryGetProperty("rect", out JsonElement rect))
                    {
                        continue;
                    }

                    keys.Add(new Key(frame, InterpOf(k), rect[0].GetSingle(), rect[1].GetSingle(), rect[2].GetSingle(),
                        rect[3].GetSingle(), ColourOf(k)));
                }
            }

            // The button art instance (console only) and anything without a picture are left out, as the Godot page does.
            if (w.GetProperty("kind").GetString() != "image" || !w.TryGetProperty("material", out JsonElement mi) ||
                mi.GetInt32() < 0 || mi.GetInt32() >= materials.GetArrayLength() || keys.Count == 0 ||
                !HudTextureLibrary.MaterialAt(materials[mi.GetInt32()], out string texture, out int u0, out int v0, out int u1, out int v1))
            {
                continue;
            }

            images.Add(new Image(texture, u0, v0, u1, v1, keys));
        }

        float rate = area.TryGetProperty("frameRate", out JsonElement fr) ? Math.Max(1f, fr.GetSingle()) : 10f;
        return new Clip(area.GetProperty("name").GetString() ?? "", rate, images, loopFrom, loopTo, stopAt);
    }

    private static int InterpOf(JsonElement k) => (k.TryGetProperty("interp", out JsonElement i) ? i.GetString() : null) switch
    {
        "linear" => InterpLinear,
        "circle" => InterpCircle,
        "square" => InterpSquare,
        "root" => InterpRoot,
        _ => InterpNone,
    };

    /// <summary>The key's colour: the mean of its four corner colours (COLORn, 0xAABBGGRR) times its state (RGBA 0..255).</summary>
    private static float[] ColourOf(JsonElement k)
    {
        float[] c = [0f, 0f, 0f, 0f];
        int n = 0;
        if (k.TryGetProperty("colors", out JsonElement colours))
        {
            foreach (JsonElement v in colours.EnumerateArray())
            {
                uint abgr = v.GetUInt32();
                c[0] += (abgr & 0xFF) / 255f;
                c[1] += ((abgr >> 8) & 0xFF) / 255f;
                c[2] += ((abgr >> 16) & 0xFF) / 255f;
                c[3] += ((abgr >> 24) & 0xFF) / 255f;
                n++;
            }
        }

        if (n == 0)
        {
            c = [1f, 1f, 1f, 1f];
            n = 1;
        }

        for (int i = 0; i < 4; i++)
        {
            c[i] /= n;
        }

        if (k.TryGetProperty("state", out JsonElement state) && state.GetArrayLength() >= 4)
        {
            for (int i = 0; i < 4; i++)
            {
                c[i] *= state[i].GetSingle() / 255f;
            }
        }

        return c;
    }

    /// <summary>The clip's frame <paramref name="seconds"/> after it appeared: looping on its own GotoFrame, held at a Stop.</summary>
    public static float FrameAt(float seconds, float rate, int loopFrom, int loopTo, int stopAt)
    {
        float frame = Math.Max(seconds, 0f) * rate;
        if (stopAt >= 0 && (loopFrom < 0 || stopAt <= loopFrom) && frame >= stopAt)
        {
            return stopAt;
        }

        if (loopFrom > 0 && loopTo >= 0 && loopTo < loopFrom && frame >= loopFrom)
        {
            frame = loopTo + (frame - loopFrom) % (loopFrom - loopTo);
        }

        return frame;
    }

    /// <summary>
    /// The layers of coop icon <paramref name="icon"/> (1 offer a ladder .. 4 be boosted up, as HudState.CoopIcon)
    /// <paramref name="seconds"/> after it appeared, in drawing order. Images before their first keyframe are not there yet.
    /// </summary>
    public void Evaluate(int icon, float seconds, List<Layer> into)
    {
        into.Clear();
        if (icon is < 1 or > 4 || _clips[icon - 1] is not { } clip)
        {
            return;
        }

        float frame = FrameAt(seconds, clip.FrameRate, clip.LoopFrom, clip.LoopTo, clip.StopAt);
        foreach (Image image in clip.Images)
        {
            List<Key> keys = image.Keys;
            if (frame < keys[0].Frame)
            {
                continue;
            }

            int i = 0;
            while (i + 1 < keys.Count && keys[i + 1].Frame <= frame)
            {
                i++;
            }

            // A segment eases with its destination keyframe's interpolation; "none" holds the earlier key.
            Key a = keys[i];
            Key b = i + 1 < keys.Count ? keys[i + 1] : a;
            float s = 0f;
            if (!ReferenceEquals(a, b) && b.Interp != InterpNone)
            {
                float x = Math.Clamp((frame - a.Frame) / Math.Max(1, b.Frame - a.Frame), 0f, 1f);
                s = b.Interp switch
                {
                    InterpCircle => 1f - MathF.Sqrt(Math.Max(0f, 1f - x * x)),
                    InterpSquare => x * x,
                    InterpRoot => MathF.Sqrt(x),
                    _ => x,
                };
            }

            float Lerp(float p, float q) => p + (q - p) * s;
            into.Add(new Layer(image.Texture, Lerp(a.L, b.L), Lerp(a.T, b.T), Lerp(a.R, b.R), Lerp(a.B, b.B),
                image.U0, image.V0, image.U1, image.V1,
                Lerp(a.Colour[0], b.Colour[0]), Lerp(a.Colour[1], b.Colour[1]), Lerp(a.Colour[2], b.Colour[2]), Lerp(a.Colour[3], b.Colour[3])));
        }
    }

    /// <summary>The clip's name for a log line (Move_coop .. Move_coop4), or empty.</summary>
    public string ClipName(int icon) => icon is >= 1 and <= 4 && _clips[icon - 1] is { } clip ? clip.Name : "";
}
