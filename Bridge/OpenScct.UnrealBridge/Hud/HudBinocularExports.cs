using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Hud;

namespace OpenScct.UnrealBridge;

/// <summary>
/// A picture or a text of a Magma page (<see cref="HudBinocularApi.GetSpyPage"/>), in units of the 640 x 480 page.
/// Source/OpenSCCT/Runtime/ScctHudBinocularApi.h mirrors it field for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudPageLayer
{
    public const int PackageLength = 16, TextureLength = 32, TextLength = 96;

    /// <summary>
    /// 0 an image, 1 a text, 2 a mask (not drawn: it hides what the masked images after it have under its box),
    /// 3 a masked image (drawn only outside the mask before it; as an image when there is none).
    /// </summary>
    public int Kind;

    /// <summary>A text's alignment in its box: 0 left, 1 centre, 2 right.</summary>
    public int Align;

    public fixed char Package[PackageLength];

    public fixed char Texture[TextureLength];

    public fixed char Text[TextLength];

    /// <summary>The box before its rotation, in page units.</summary>
    public float L, T, R, B;

    /// <summary>Degrees about the box's centre, clockwise on screen.</summary>
    public float Rotation;

    /// <summary>The texel region shown (U1 left of U0 shows it mirrored).</summary>
    public float U0, V0, U1, V1;

    /// <summary>The colour, 0..1 as the content gives it (sRGB), the parents' tint applied.</summary>
    public float Red, Green, Blue, Alpha;

    /// <summary>A text's height in page units.</summary>
    public float Height;

    public float ShadowRed, ShadowGreen, ShadowBlue, ShadowAlpha;

    /// <summary>Where a text's shadow sits from it, in page units (y down).</summary>
    public float ShadowX, ShadowY;

    /// <summary>A text's letter spacing in thousandths of its height.</summary>
    public float Tracking;

    public void Set(in HudMagmaPage.Layer l)
    {
        Kind = l.IsText ? 1 : l.Mask == HudMagmaPage.MaskSet ? 2 : l.Mask == HudMagmaPage.MaskUse ? 3 : 0;
        Align = l.Align;
        fixed (char* p = Package)
        {
            Copy(l.Package, p, PackageLength);
        }

        fixed (char* t = Texture)
        {
            Copy(l.Texture, t, TextureLength);
        }

        fixed (char* x = Text)
        {
            Copy(l.Text, x, TextLength);
        }

        (L, T, R, B, Rotation) = (l.L, l.T, l.R, l.B, l.Rotation);
        (U0, V0, U1, V1) = (l.U0, l.V0, l.U1, l.V1);
        (Red, Green, Blue, Alpha) = (l.Red, l.Green, l.Blue, l.Alpha);
        Height = l.Height;
        (ShadowRed, ShadowGreen, ShadowBlue, ShadowAlpha) = (l.ShadowRed, l.ShadowGreen, l.ShadowBlue, l.ShadowAlpha);
        (ShadowX, ShadowY, Tracking) = (l.ShadowX, l.ShadowY, l.Tracking);
    }

    private static void Copy(string s, char* into, int capacity)
    {
        int n = Math.Min(s.Length, capacity - 1);
        for (int i = 0; i < n; i++)
        {
            into[i] = s[i];
        }

        into[n] = '\0';
    }
}

/// <summary>What the spy binocular page shows this frame beyond its own keyframes (<see cref="HudBinocularApi.GetSpyPage"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudBinocularPageInput
{
    public const int KeyLength = 64;

    /// <summary>Seconds since the binoculars came up: the frame, sound meter and zoom areas start then.</summary>
    public float Seconds;

    /// <summary>Seconds since the speak box (the Hack prompt) appeared, or below 0 while it is not shown.</summary>
    public float SpeakSeconds;

    /// <summary>The zoom step, trunc(fBinocularVisionFOV x 0.1): 1 is x6, 2 x4, anything else x2.</summary>
    public int ZoomStep;

    public int Reserved;

    /// <summary>The key help's key names ("Mouse Wheel Up / Mouse Wheel Down", the exit key) and the speak box's text.</summary>
    public fixed char ZoomKeys[KeyLength];

    public fixed char ExitKey[KeyLength];

    public fixed char SpeakText[KeyLength];
}

/// <summary>What the merc's scope page shows this frame beyond its own keyframes (<see cref="HudBinocularApi.GetMercPage"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudScopePageInput
{
    public const int KeyLength = 64, MessageLength = 96;

    /// <summary><see cref="Flags"/>: the breath is held (the sights and axes stay on the page's centre).</summary>
    public const int HoldBreath = 1;

    /// <summary><see cref="Flags"/>: a shot hit a pawn less than 0.4 s ago (red sights).</summary>
    public const int Hit = 2;

    /// <summary>Seconds since the scope page came up: the zoom and reticle message areas start then.</summary>
    public float Seconds;

    /// <summary>Seconds since the rings' clip of the last zoom step started, or below 0 before the first.</summary>
    public float ZoomSeconds;

    /// <summary>The rings' clip: 1 closing in (a step in), 2 opening out (a step out, and the page's first frame).</summary>
    public int ZoomAnimation;

    /// <summary>The zoom step, trunc(fBinocularVisionFOV x 0.1): 1 is x6, 2 x4, anything else x2.</summary>
    public int ZoomStep;

    /// <summary>The aim point on the 640 x 480 page (the page's centre when it is not known).</summary>
    public float SpotX, SpotY;

    public int Flags;

    /// <summary>SPlayerController.BinocularHoldBreathTime: breath left, 5 s when full.</summary>
    public float BreathTime;

    /// <summary>The range finder's distance in uu (0 when nothing is hit) and the units of the text per uu.</summary>
    public float Distance, DistanceScale;

    /// <summary>Seconds since the speak box (the Hack prompt) appeared, or below 0 while it is not shown.</summary>
    public float SpeakSeconds;

    /// <summary>Seconds since the alert box appeared (an objective's countdown runs), or below 0 while it is not shown.</summary>
    public float AlertSeconds;

    /// <summary>The key help's key names: the zoom keys ("Mouse Wheel Up / Mouse Wheel Down"), the exit key, the hold breath key.</summary>
    public fixed char ZoomKeys[KeyLength];

    public fixed char ExitKey[KeyLength];

    public fixed char BreathKey[KeyLength];

    public fixed char SpeakText[KeyLength];

    /// <summary>The message under the reticle while it lasts, or empty.</summary>
    public fixed char Message[MessageLength];
}

/// <summary>
/// The binocular pages' table of entry points, beside the HUD's own (<see cref="HudExports"/>, whose snapshot carries the
/// mode, the zoom and the objective targets): the spy's P_BinocularsATT evaluated from the content at a moment, and an
/// unattended check's way to turn the view onto a target. Unreal asks for <see cref="GetApi"/> once
/// (<c>Host.GetFunction("OpenScct.UnrealBridge.HudBinocularExports", "GetApi")</c>).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudBinocularApi
{
    public int Version;

    /// <summary>
    /// Writes up to <c>capacity</c> layers of P_BinocularsATT for <c>input</c> in drawing order and returns how many there
    /// are (0 when the content has no such page).
    /// </summary>
    public delegate* unmanaged[Cdecl]<HudBinocularPageInput*, HudPageLayer*, int, int> GetSpyPage;

    /// <summary>
    /// Unattended checks only: turns the local player's view and aim to look from his eye at the world point (X, Y, Z),
    /// as a mouse turn would leave them. Returns 1 when there is a pawn to turn.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float, float, float, int> AimAt;

    /// <summary>
    /// Writes up to <c>capacity</c> layers of P_BinocularsDEF, the merc's rifle scope, for <c>input</c> in drawing order and
    /// returns how many there are (0 when the content has no such page).
    /// </summary>
    public delegate* unmanaged[Cdecl]<HudScopePageInput*, HudPageLayer*, int, int> GetMercPage;

    /// <summary>
    /// The scoped merc's aim point in the world at the drawn moment between the last two ticks (three floats: X, Y, Z).
    /// Returns 1 when there is one. Project it with the drawn camera for <see cref="HudScopePageInput.SpotX"/>.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float*, int> GetMercAim;
}

public static unsafe class HudBinocularExports
{
    public const int ApiVersion = 2;

    public const string Package = "Hud";
    public const string SpyPageName = "P_BinocularsATT";
    public const string MercPageName = "P_BinocularsDEF";

    /// <summary>The rings' clip of a zoom step in (frames 10 to 19) and of a step out (20 to 29) (P_BinocularsDEF update 0x10b342c0).</summary>
    public const int ZoomInFrame = 10, ZoomOutFrame = 20;

    /// <summary>How many frames a rings' clip runs before its GotoFrame puts the area back on frame 0.</summary>
    public const int ZoomClipFrames = 9;

    /// <summary>The frame rate of the page's areas.</summary>
    public const float PageFrameRate = 10f;

    /// <summary>The breath gauge's frame: trunc((5 - BinocularHoldBreathTime) x 4) (P_BinocularsDEF update 0x10b342c0).</summary>
    public const float BreathFull = 5f, BreathFramesPerSecond = 4f;

    /// <summary>The page's centre, where the sights rest.</summary>
    public const float CentreX = 320f, CentreY = 240f;

    /// <summary>The widgets the merc page hides at its creation: the console's key help and the comms hack help.</summary>
    private static readonly string[] MercHiddenWidgets =
        ["IconZoomUp", "IconZoomDown", "textZoomDef1", "textZoomDef2", "textZoomDef3", "PCtextZoomDef4", "PCtextZoomDef4Binding", "Speak/Element4"];

    private static HudMagmaPage? _mercPage;
    private static HudTextureLibrary? _mercPageFor;

    /// <summary>How far the zoom marker moves from its home for x6 (right) and x2 (left) (P_BinocularsATT init 0x10b35190).</summary>
    public const float ZoomMarkerStep = 35f;

    /// <summary>The widgets the page hides at its creation: the console's key help and the comms hack help (spec rule 2).</summary>
    private static readonly string[] HiddenWidgets =
        ["IconZoomUp", "IconZoomDown", "textZoomAtt1", "textZoomAtt2", "PCtextZoomAtt3", "PCtextZoomAtt3Binding", "Element4"];

    private static HudMagmaPage? _page;
    private static HudTextureLibrary? _pageFor;
    private static readonly HudMagmaPage.Overrides Overrides = new();
    private static readonly List<HudMagmaPage.Layer> Layers = new(96);

    /// <summary>Fills <paramref name="api"/>. Returns the binocular API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudBinocularApi* api)
    {
        api->Version = ApiVersion;
        api->GetSpyPage = &GetSpyPage;
        api->AimAt = &AimAt;
        api->GetMercPage = &GetMercPage;
        api->GetMercAim = &GetMercAim;
        return ApiVersion;
    }

    /// <summary>The spy page of the configured content, or null with the reason.</summary>
    public static HudMagmaPage? SpyPage(out string? error)
    {
        HudTextureLibrary? library = HudTextureLibrary.Current(out error);
        if (library is null)
        {
            return null;
        }

        if (_page is null || !ReferenceEquals(_pageFor, library))
        {
            _pageFor = library;
            _page = HudMagmaPage.Load(library, Package, SpyPageName, out error);
        }

        error = _page is null ? error : null;
        return _page;
    }

    /// <summary>The zoom label of a step: 1 is "x6" (FOV 10), 2 is "x4" (FOV 20), anything else "x2" (FOV 30).</summary>
    public static string ZoomLabel(int step) => step switch
    {
        1 => "x6",
        2 => "x4",
        _ => "x2",
    };

    /// <summary>
    /// The layers of P_BinocularsATT as <see cref="GetSpyPage"/> writes them (the probe calls this): the goggle frame, the
    /// self-looping sound meter and the zoom scale from the moment the binoculars came up, the zoom label and marker at
    /// the step (spec/hud/binoculars.md rules 2, 3, 5), the key help with the player's keys (rule 6) and the speak box
    /// with the Hack prompt while it shows (rule 7).
    /// </summary>
    public static List<HudMagmaPage.Layer> ReadSpyPage(float seconds, float speakSeconds, int zoomStep, string zoomKeys, string exitKey, string speakText)
    {
        Layers.Clear();
        if (SpyPage(out _) is not { } page)
        {
            return Layers;
        }

        Overrides.Clear();
        foreach (string w in HiddenWidgets)
        {
            Overrides.Hidden.Add(w);
        }

        Overrides.Texts["ZoomValue"] = ZoomLabel(zoomStep);
        Overrides.Texts["PCtextZoomAtt1Binding"] = zoomKeys;
        Overrides.Texts["PCtextZoomAtt2Binding"] = exitKey;
        Overrides.Texts["_TextSpeakATT"] = speakText;
        if (page.Home("ZoomAtt", "zoomInside", out float homeL, out float homeT))
        {
            float dx = zoomStep switch
            {
                1 => ZoomMarkerStep,
                2 => 0f,
                _ => -ZoomMarkerStep,
            };
            Overrides.Positions["zoomInside"] = (homeL + dx, homeT);
        }

        page.Evaluate(name => name switch
        {
            "frame" or "SoundAnimation" or "ZoomAtt" => Math.Max(seconds, 0f),
            "Speak" => speakSeconds >= 0f ? speakSeconds : null,
            _ => null,
        }, Overrides, Layers);
        return Layers;
    }

    /// <summary>The merc's scope page of the configured content, or null with the reason.</summary>
    public static HudMagmaPage? MercPage(out string? error)
    {
        HudTextureLibrary? library = HudTextureLibrary.Current(out error);
        if (library is null)
        {
            return null;
        }

        if (_mercPage is null || !ReferenceEquals(_mercPageFor, library))
        {
            _mercPageFor = library;
            _mercPage = HudMagmaPage.Load(library, Package, MercPageName, out error);
        }

        error = _mercPage is null ? error : null;
        return _mercPage;
    }

    /// <summary>The range finder's text (spec/hud/binoculars.md rule 9): "%02i.%02i" of the whole distance units and the uu left over.</summary>
    public static string DistanceText(float d, float unit)
    {
        int a = (int)(d * unit);
        int b = (int)(d - a / unit);
        return string.Create(System.Globalization.CultureInfo.InvariantCulture, $"{a:00}.{b:00}");
    }

    /// <summary>
    /// The layers of P_BinocularsDEF as <see cref="GetMercPage"/> writes them (the probe calls this), as the Godot client's
    /// MercBinocularsPage drives the page (spec/hud/binoculars.md rules 2, 3 and 8 to 11): the scope rings, which close in
    /// or open out for nine frames on a zoom step (and open out when the page comes up); the sights at the aim point, or
    /// on the centre while the breath is held, red after a hit; the two axes through that point; the breath gauge at the
    /// frame of the breath used; the range finder; the zoom label; the key help with the player's keys; the reticle
    /// message moved with the aim point; the speak box and the alert box while they show.
    /// </summary>
    public static List<HudMagmaPage.Layer> ReadMercPage(float seconds, float zoomSeconds, int zoomAnimation, int zoomStep, float spotX, float spotY,
        bool holdBreath, bool hit, float breathTime, float distance, float distanceScale, float speakSeconds, float alertSeconds,
        string zoomKeys, string exitKey, string breathKey, string speakText, string message)
    {
        Layers.Clear();
        if (MercPage(out _) is not { } page)
        {
            return Layers;
        }

        Overrides.Clear();
        foreach (string w in MercHiddenWidgets)
        {
            Overrides.Hidden.Add(w);
        }

        // The rings' clip runs from its first frame; its last frame's GotoFrame puts the area back on frame 0, where it rests.
        float clip = zoomSeconds >= 0f && zoomAnimation is 1 or 2 ? zoomSeconds * PageFrameRate : ZoomClipFrames;
        Overrides.Frames["frame"] = clip < ZoomClipFrames ? (zoomAnimation == 1 ? ZoomInFrame : ZoomOutFrame) + clip : 0f;
        Overrides.Frames["breathGauge"] = (int)((BreathFull - breathTime) * BreathFramesPerSecond);

        Overrides.Texts["zoomValue"] = ZoomLabel(zoomStep);
        Overrides.Texts["distance"] = DistanceText(distance, distanceScale > 0f ? distanceScale : 0.01f);
        Overrides.Texts["PCtextZoomDef1Binding"] = zoomKeys;
        Overrides.Texts["PCtextZoomDef2Binding"] = exitKey;
        Overrides.Texts["PCtextZoomDef3Binding"] = breathKey;
        Overrides.Texts["_TextSpeakDEF"] = speakText;
        Overrides.Texts["messageReticule"] = message;

        // The sights' corners 0xff44b2fd, 0xff0000ff for 0.4 s after a hit (P_BinocularsDEF update 0x10b342c0).
        Overrides.Colours["sights"] = hit ? (1f, 0f, 0f, 1f) : (253f / 255f, 178f / 255f, 68f / 255f, 1f);
        (float atX, float atY) = holdBreath ? (CentreX, CentreY) : (spotX, spotY);
        if (page.Home("ZoomDef", "sights", out float sightsL, out float sightsT))
        {
            float halfW = MathF.Truncate(CentreX - sightsL), halfH = MathF.Truncate(CentreY - sightsT);
            Overrides.Positions["sights"] = holdBreath ? (CentreX - halfW, CentreY - halfH) : (MathF.Truncate(spotX - halfW), MathF.Truncate(spotY - halfH));
        }

        Overrides.Positions["verticalAxis"] = (MathF.Truncate(atX), 0f);
        Overrides.Positions["horizontalAxis"] = (0f, MathF.Truncate(atY));
        if (message.Length > 0 && page.Home("Reload", "messageReticule", out float messageL, out float messageT))
        {
            // The message keeps its place from the aim point, breath held or not.
            Overrides.Positions["messageReticule"] = (messageL + MathF.Truncate(spotX - CentreX), messageT + MathF.Truncate(spotY - CentreY));
        }

        page.Evaluate(name => name switch
        {
            "frame" or "ZoomDef" or "Reload" => Math.Max(seconds, 0f),
            "Speak" => speakSeconds >= 0f ? speakSeconds : null,
            "Alert" => alertSeconds >= 0f ? alertSeconds : null,
            _ => null,
        }, Overrides, Layers);
        return Layers;
    }

    /// <summary>What <see cref="AimAt"/> does: the view and aim of <paramref name="pc"/> turned to look from the eye at the point.</summary>
    public static bool Aim(SPlayerController? pc, FVector target)
    {
        if (pc?.Pawn is not { bDeleteMe: false } pawn)
        {
            return false;
        }

        FVector eye = pawn.Location + OpenScct.Game.Combat.CombatServices.Ports(pc).EyePosition(pawn);
        FVector d = target - eye;
        float flat = MathF.Sqrt(d.X * d.X + d.Y * d.Y);
        const float UnitsPerRadian = 32768f / MathF.PI;
        int yaw = (int)MathF.Round(MathF.Atan2(d.Y, d.X) * UnitsPerRadian) & 0xFFFF;
        int pitch = (int)MathF.Round(MathF.Atan2(d.Z, Math.Max(flat, 1f)) * UnitsPerRadian) & 0xFFFF;
        var view = new FRotator(pitch, yaw, 0);

        // As the bots' input does: SetViewRotation turns the view and the aim that follows it, and the desired rotation
        // is what the binocular and scope views read.
        pc.SetViewRotation(view);
        pc.MyDesiredRotation = view;
        pc.LocalMyDesiredRotation = view;
        return true;
    }

    private static SPlayerController? Controller => Exports.Current?.Session.LocalController is { bDeleteMe: false } pc ? pc : null;

    private static string Read(char* s, int capacity)
    {
        int n = 0;
        while (n < capacity && s[n] != '\0')
        {
            n++;
        }

        return new string(s, 0, n);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSpyPage(HudBinocularPageInput* input, HudPageLayer* rows, int capacity)
    {
        try
        {
            if (input == null)
            {
                return 0;
            }

            List<HudMagmaPage.Layer> layers = ReadSpyPage(input->Seconds, input->SpeakSeconds, input->ZoomStep,
                Read(input->ZoomKeys, HudBinocularPageInput.KeyLength), Read(input->ExitKey, HudBinocularPageInput.KeyLength),
                Read(input->SpeakText, HudBinocularPageInput.KeyLength));
            int n = rows == null ? 0 : Math.Min(Math.Max(capacity, 0), layers.Count);
            for (int i = 0; i < n; i++)
            {
                rows[i].Set(layers[i]);
            }

            return layers.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMercPage(HudScopePageInput* input, HudPageLayer* rows, int capacity)
    {
        try
        {
            if (input == null)
            {
                return 0;
            }

            List<HudMagmaPage.Layer> layers = ReadMercPage(input->Seconds, input->ZoomSeconds, input->ZoomAnimation, input->ZoomStep,
                Finite(input->SpotX, CentreX), Finite(input->SpotY, CentreY),
                (input->Flags & HudScopePageInput.HoldBreath) != 0, (input->Flags & HudScopePageInput.Hit) != 0,
                Finite(input->BreathTime, BreathFull), Finite(input->Distance, 0f), input->DistanceScale, input->SpeakSeconds, input->AlertSeconds,
                Read(input->ZoomKeys, HudScopePageInput.KeyLength), Read(input->ExitKey, HudScopePageInput.KeyLength),
                Read(input->BreathKey, HudScopePageInput.KeyLength), Read(input->SpeakText, HudScopePageInput.KeyLength),
                Read(input->Message, HudScopePageInput.MessageLength));
            int n = rows == null ? 0 : Math.Min(Math.Max(capacity, 0), layers.Count);
            for (int i = 0; i < n; i++)
            {
                rows[i].Set(layers[i]);
            }

            return layers.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    private static float Finite(float value, float fallback) => float.IsFinite(value) ? value : fallback;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMercAim(float* xyz)
    {
        try
        {
            if (xyz == null || Exports.Current is not { } bridge || HudScopeAim.Of(bridge).At(bridge.Session.Alpha) is not { } aim)
            {
                return 0;
            }

            xyz[0] = aim.X;
            xyz[1] = aim.Y;
            xyz[2] = aim.Z;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int AimAt(float x, float y, float z)
    {
        try
        {
            return Aim(Controller, new FVector(x, y, z)) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
