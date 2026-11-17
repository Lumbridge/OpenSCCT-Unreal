using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScct.UnrealBridge.Hud;

namespace OpenScct.UnrealBridge;

/// <summary>
/// A picture the HUD draws from the converted content: which texture of which package, the texel region of it, where
/// it goes on the 640 x 480 Magma page and its colour. Source/OpenSCCT/Runtime/ScctHudTextureApi.h mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudTextureLayer
{
    public const int PackageLength = 16, TextureLength = 32;

    /// <summary>The package and texture name (UTF-16, zero ended), as <see cref="HudTextureApi.GetTexture"/> takes them.</summary>
    public fixed char Package[PackageLength];

    public fixed char Texture[TextureLength];

    /// <summary>Where it is drawn, in page units (left, top, right, bottom; a mirrored image has right left of left).</summary>
    public float L, T, R, B;

    /// <summary>The texel region of the texture shown (left, top, right, bottom).</summary>
    public int U0, V0, U1, V1;

    /// <summary>The colour it is drawn in, 0..1 as the content gives it (sRGB), alpha included.</summary>
    public float Red, Green, Blue, Alpha;

    public void SetNames(string package, string texture)
    {
        fixed (char* p = Package)
        {
            Copy(package, p, PackageLength);
        }

        fixed (char* t = Texture)
        {
            Copy(texture, t, TextureLength);
        }
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

/// <summary>
/// The HUD's pictures, beside the HUD's own table (<see cref="HudExports"/>): the converted interface textures by
/// package and name (SHudTextures.Icones_Weapons, Hud.Move_coop, Hud.heart ...) as BGRA8 texels, a Magma material's
/// texture and region, and the coop icon clips evaluated at a time. Unreal asks for <see cref="GetApi"/> once
/// (<c>Host.GetFunction("OpenScct.UnrealBridge.HudTextureExports", "GetApi")</c>).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudTextureApi
{
    public int Version;

    /// <summary>
    /// GetTexture(package, name, width, height, bgra, capacity): 1 when the content has the texture, with its size; its
    /// texels (4 bytes each, top row first) are copied when <c>bgra</c> holds width x height x 4 bytes. 0 when missing.
    /// </summary>
    public delegate* unmanaged[Cdecl]<char*, char*, int*, int*, byte*, int, int> GetTexture;

    /// <summary>GetMaterial(package, material, layer): fills the layer's names and texel region; 1 when found.</summary>
    public delegate* unmanaged[Cdecl]<char*, char*, HudTextureLayer*, int> GetMaterial;

    /// <summary>
    /// GetCoopLayers(icon, seconds, rows, capacity): coop icon 1..4 (HudState.CoopIcon) that long after it appeared,
    /// as layers in drawing order. Returns how many were written.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, float, HudTextureLayer*, int, int> GetCoopLayers;

    /// <summary>
    /// GetCoopBounds(icon, ltrb): the page box (4 floats) every pose of coop icon 1..4 stays in, or with 0 the box of all
    /// four (where A_Coop sits); 1 when known.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, float*, int> GetCoopBounds;

    /// <summary>
    /// Unattended checks only: the local player's pawn on the local match's server dies by its own hand (lives are left
    /// as they are, unlike Resign), so a check can see what a dead player sees: the Merc Deathmatch respawn weapon pick.
    /// Returns 1 when queued for the server's next tick, 0 on a remote round or without a pawn.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int> KillLocalPawn;
}

public static unsafe class HudTextureExports
{
    public const int ApiVersion = 1;

    [ThreadStatic]
    private static List<HudCoopIcons.Layer>? _layers;

    /// <summary>Fills <paramref name="api"/>. Returns the texture API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudTextureApi* api)
    {
        api->Version = ApiVersion;
        api->GetTexture = &GetTexture;
        api->GetMaterial = &GetMaterial;
        api->GetCoopLayers = &GetCoopLayers;
        api->GetCoopBounds = &GetCoopBounds;
        api->KillLocalPawn = &KillLocalPawn;
        return ApiVersion;
    }

    /// <summary>The texture as <see cref="GetTexture"/> hands it over, or null with the reason (the probe calls this).</summary>
    public static HudPng.Image? ReadTexture(string package, string name, out string? error)
    {
        if (HudTextureLibrary.Current(out error) is not { } library)
        {
            return null;
        }

        HudPng.Image? image = library.Texture(package, name);
        error = library.LastError;
        return image;
    }

    /// <summary>The coop icon's layers as <see cref="GetCoopLayers"/> writes them (the probe calls this).</summary>
    public static List<HudCoopIcons.Layer> ReadCoopLayers(int icon, float seconds)
    {
        List<HudCoopIcons.Layer> layers = _layers ??= new List<HudCoopIcons.Layer>(8);
        layers.Clear();
        HudCoopIcons.Current(out _)?.Evaluate(icon, seconds, layers);
        return layers;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetTexture(char* package, char* name, int* width, int* height, byte* bgra, int capacity)
    {
        try
        {
            if (package == null || name == null || ReadTexture(new string(package), new string(name), out _) is not { } image)
            {
                return 0;
            }

            if (width != null)
            {
                *width = image.Width;
            }

            if (height != null)
            {
                *height = image.Height;
            }

            if (bgra != null && capacity >= image.Bgra.Length)
            {
                image.Bgra.AsSpan().CopyTo(new Span<byte>(bgra, image.Bgra.Length));
            }

            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMaterial(char* package, char* material, HudTextureLayer* layer)
    {
        try
        {
            if (package == null || material == null || layer == null || HudTextureLibrary.Current(out _) is not { } library)
            {
                return 0;
            }

            string p = new(package);
            if (!library.Material(p, new string(material), out string texture, out int u0, out int v0, out int u1, out int v1))
            {
                return 0;
            }

            *layer = default;
            layer->SetNames(p, texture);
            layer->U0 = u0;
            layer->V0 = v0;
            layer->U1 = u1;
            layer->V1 = v1;
            layer->Red = layer->Green = layer->Blue = layer->Alpha = 1f;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetCoopLayers(int icon, float seconds, HudTextureLayer* rows, int capacity)
    {
        try
        {
            List<HudCoopIcons.Layer> layers = ReadCoopLayers(icon, seconds);
            int n = rows == null ? 0 : Math.Min(layers.Count, capacity);
            for (int i = 0; i < n; i++)
            {
                HudCoopIcons.Layer l = layers[i];
                HudTextureLayer* row = rows + i;
                *row = default;
                row->SetNames(HudCoopIcons.Package, l.Texture);
                (row->L, row->T, row->R, row->B) = (l.L, l.T, l.R, l.B);
                (row->U0, row->V0, row->U1, row->V1) = (l.U0, l.V0, l.U1, l.V1);
                (row->Red, row->Green, row->Blue, row->Alpha) = (l.Red, l.Green, l.Blue, l.Alpha);
            }

            return n;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetCoopBounds(int icon, float* ltrb)
    {
        try
        {
            if (ltrb == null || HudCoopIcons.Current(out _) is not { } icons)
            {
                return 0;
            }

            (ltrb[0], ltrb[1], ltrb[2], ltrb[3]) = icons.BoundsOf(icon);
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>What <see cref="KillLocalPawn"/> does on the server's thread, for the probe: true when a pawn was hit.</summary>
    public static bool KillOnServer(ServerHost host, int playerId)
    {
        foreach (SPlayerController pc in host.World.Level.AllActors<SPlayerController>())
        {
            if (pc.bDeleteMe || pc.PlayerReplicationInfo?.PlayerID != playerId || pc.Pawn is not { Health: > 0 } pawn)
            {
                continue;
            }

            pc.PawnTakeDamage(pawn.Health + 100, pawn, pawn.Location, default, SDamageType.Suicided);
            return true;
        }

        return false;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int KillLocalPawn()
    {
        try
        {
            if (Exports.Current is not { IsRemote: false, IsDisposed: false } session || session.Session.Host is not { } host ||
                session.Session.LocalController?.PlayerReplicationInfo is not { } pri)
            {
                return 0;
            }

            int id = pri.PlayerID;
            void Run(ServerHost running)
            {
                running.BeforeWorldTick -= Run;
                try
                {
                    KillOnServer(running, id);
                }
                catch (Exception)
                {
                    // A check that fails shows as nothing happening; it must not take the server down.
                }
            }

            host.BeforeWorldTick += Run;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
