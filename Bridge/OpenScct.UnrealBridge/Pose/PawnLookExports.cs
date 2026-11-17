using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Net.Cosmetics;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The entry points for how each pawn looks (Source/OpenSCCT/Runtime/ScctPawnLookApi.h mirrors the table): the players'
/// cosmetics (camo or tint on the body and the gun, the light hue of the goggles and lamp) after the cosmetics options
/// and the map outfit's policy, the map outfits themselves (render/outfits.json: model, slot textures, goggle bone) and
/// the content builder's camo textures by retail texture. Unreal puts these on the pawn bodies, their attachments and
/// the first-person sleeves. The heat and EMF masks of an outfit model are the vision feature's (they read the outfit's
/// model name from <see cref="GetOutfit"/>).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct PawnLookApi
{
    public int Version;

    /// <summary>Writes the pawns' looks. Returns how many pawns there are (only the first that fit are written).</summary>
    public delegate* unmanaged[Cdecl]<BridgePawnLook*, int, int> GetLooks;

    /// <summary>Fills a team's (0 spies, 1 mercs) map outfit. Returns 1 when the team wears one, else 0.</summary>
    public delegate* unmanaged[Cdecl]<int, BridgeKindOutfit*, int> GetOutfit;

    /// <summary>Writes a name of the session's table (UTF-16, no terminator). Returns its length (0 for none).</summary>
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetName;

    /// <summary>
    /// Writes the full path of the camo texture of pattern (a name id) for a retail texture (UTF-16 text and length) of a
    /// package folder ("SPerso", "SWeaponFirst"; "outfit:0" or "outfit:1" for a team's outfit model). Returns its length,
    /// 0 when no such texture was built.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, char*, int, char*, int, char*, int, int> CosmeticTexture;

    /// <summary>The cosmetics options: "hide mine" (the own look is drawn here only) and "hide others". Returns 1.</summary>
    public delegate* unmanaged[Cdecl]<int, int, int> SetOptions;

    /// <summary>
    /// The player's own look for "hide mine", from text (PawnLooks.Parse: "hex:..." or names such as "woodland+blue"),
    /// for both sides. Returns how many names were used for either side.
    /// </summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> SetOwnLook;

    /// <summary>Unattended checks: puts a look (text as SetOwnLook) on the player's info on the local server. Returns 1 when queued.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> ForceLocalLook;

    /// <summary>Unattended checks: every bot on the local server gets a look of its own. Returns 1 when queued.</summary>
    public delegate* unmanaged[Cdecl]<int> GiveOthersLooks;
}

public static unsafe class PawnLookExports
{
    public const int ApiVersion = 1;

    [UnmanagedCallersOnly]
    public static int GetApi(PawnLookApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->GetLooks = &GetLooks;
        api->GetOutfit = &GetOutfit;
        api->GetName = &GetName;
        api->CosmeticTexture = &CosmeticTexture;
        api->SetOptions = &SetOptions;
        api->SetOwnLook = &SetOwnLook;
        api->ForceLocalLook = &ForceLocalLook;
        api->GiveOthersLooks = &GiveOthersLooks;
        return ApiVersion;
    }

    private static PawnLooks? Feature() => Exports.Current is { IsDisposed: false } session ? PawnLooks.Of(session) : null;

    private static string Text(char* text, int length) => text == null || length <= 0 ? string.Empty : new string(text, 0, length);

    private static int Write(string value, char* into, int capacity)
    {
        if (into == null || capacity <= 0)
        {
            return 0;
        }

        int length = Math.Min(value.Length, capacity);
        value.AsSpan(0, length).CopyTo(new Span<char>(into, capacity));
        return length;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLooks(BridgePawnLook* into, int capacity)
    {
        try
        {
            return Feature() is { } f ? f.Looks(into == null || capacity <= 0 ? default : new Span<BridgePawnLook>(into, capacity)) : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetOutfit(int kind, BridgeKindOutfit* into)
    {
        try
        {
            if (into == null || Feature() is not { } f)
            {
                return 0;
            }

            *into = f.Outfit(kind);
            return into->Dressed;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetName(int id, char* into, int capacity)
    {
        try
        {
            return Feature() is { } f ? Write(f.Name(id), into, capacity) : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CosmeticTexture(int pattern, char* package, int packageLength, char* retail, int retailLength, char* into, int capacity)
    {
        try
        {
            return Feature() is { } f && f.CosmeticTexture(f.Name(pattern), Text(package, packageLength), Text(retail, retailLength)) is { } path
                ? Write(path, into, capacity)
                : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetOptions(int hideMine, int hideOthers)
    {
        PawnLooks.HideMine = hideMine != 0;
        PawnLooks.HideOthers = hideOthers != 0;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetOwnLook(char* text, int length)
    {
        try
        {
            string look = Text(text, length);
            PawnLooks.OwnSpy = PawnLooks.Parse(look, CosmeticSide.Spy, out int spy);
            PawnLooks.OwnMerc = PawnLooks.Parse(look, CosmeticSide.Merc, out int merc);
            return Math.Max(spy, merc);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ForceLocalLook(char* text, int length)
    {
        try
        {
            return Exports.Current is { IsDisposed: false } session ? PawnLooks.ForceLocal(session, Text(text, length)) : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GiveOthersLooks()
    {
        try
        {
            return Exports.Current is { IsDisposed: false } session ? PawnLooks.GiveOthers(session) : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
