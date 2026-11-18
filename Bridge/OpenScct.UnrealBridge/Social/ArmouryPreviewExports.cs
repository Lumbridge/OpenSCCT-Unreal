using System;
using System.IO;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Net.Cosmetics;
using OpenScct.Server;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge;

/// <summary>How one cosmetic is drawn on the Armoury's preview (Source/OpenSCCT/Runtime/ScctArmouryPreviewApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct ArmouryPreviewItem
{
    public int Side;   // 0 spy, 1 merc
    public int Slot;   // CosmeticSlot
    public int Kind;   // 0 tint, 1 camo texture, 2 light hue
    public int HasHue; // 1 when HueTurn is set (a light item)
    public float HueTurn; // radians from the side's retail glow colour to the item's
    public float Hue;     // degrees
    public int Reserved0;
    public int Reserved1;
}

/// <summary>
/// The Armoury's preview table (a port of what the Godot client's CosmeticPreview and CosmeticThumbs ask of the
/// content): which model a slot is seen on, an item's pattern and flat picture, the content builder's camo texture of a
/// pattern for a retail texture without a session (the configured content), and the stock characters' skeleton with a
/// standing idle pose, so the preview stands as a pawn does. Every entry catches its exceptions. Strings are UTF-16,
/// zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ArmouryPreviewApi
{
    public int Version;

    /// <summary>An item by id: its record, its pattern ("woodland", "tint_5a6ea8"; empty for a light) and its flat picture's file (empty when none was built). Returns 0 for an unknown id.</summary>
    public delegate* unmanaged[Cdecl]<char*, ArmouryPreviewItem*, char*, int, char*, int, int> GetItem;

    /// <summary>Writes the model a slot of a side is seen on ("SPerso.ATT_01", "SWeaponFirst.M160"). Returns 0 the character, 1 the character's head, 2 a gun.</summary>
    public delegate* unmanaged[Cdecl]<int, int, char*, int, int> SlotModel;

    /// <summary>Writes the full path of the camo texture of a pattern for a retail texture of a package folder. Returns its length, 0 when none was built.</summary>
    public delegate* unmanaged[Cdecl]<char*, char*, char*, char*, int, int> CosmeticTexture;

    /// <summary>The stock character's bones (0 spy, 1 merc), in the order of <see cref="GetIdlePose"/>. Returns the bone count, 0 without the content.</summary>
    public delegate* unmanaged[Cdecl]<int, BridgeBoneInfo*, int, int> GetSkeleton;

    /// <summary>The stock character standing idle at a time in seconds (the clip loops). Returns the bone count, 0 without the content.</summary>
    public delegate* unmanaged[Cdecl]<int, float, BridgeBone*, int, int> GetIdlePose;
}

public static unsafe class ArmouryPreviewExports
{
    public const int ApiVersion = 1;

    private static readonly string[] s_idleClips = ["WaitStAlFd0", "WaitStSpFd2"];
    private static readonly GlbCharacter?[] s_characters = new GlbCharacter?[2];
    private static readonly GlbClip?[] s_idle = new GlbClip?[2];
    private static readonly bool[] s_tried = new bool[2];

    [UnmanagedCallersOnly]
    public static int GetApi(ArmouryPreviewApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetItem = &GetItem;
        api->SlotModel = &SlotModel;
        api->CosmeticTexture = &CosmeticTexture;
        api->GetSkeleton = &GetSkeleton;
        api->GetIdlePose = &GetIdlePose;
        return ApiVersion;
    }

    private static string Text(char* text) => text == null ? string.Empty : new string(text);

    /// <summary>The model a slot is seen on (CosmeticPreview.AssetFor): the side's character for a body or light, else that gun.</summary>
    private static string AssetFor(CosmeticSide side, CosmeticSlot slot) => slot switch
    {
        CosmeticSlot.Suppressed => "OpenScctSidearms.suppressed",
        CosmeticSlot.Unsuppressed => "OpenScctSidearms.unsuppressed",
        CosmeticSlot.Automatic => "OpenScctSidearms.automatic",
        CosmeticSlot.Magnum44 => "OpenScctSidearms.magnum44",
        CosmeticSlot.DesertEagle => "OpenScctSidearms.desert_eagle",
        CosmeticSlot.Pistol => "SWeaponFirst.p190",
        CosmeticSlot.GunAr => "SWeaponFirst.M160",
        CosmeticSlot.GunSmg => "SWeaponFirst.Hk81",
        CosmeticSlot.GunShotgun => "SWeaponFirst.BM401",
        _ => side == CosmeticSide.Spy ? "SPerso.ATT_01" : "SPerso.DEF_01",
    };

    /// <summary>The flat picture of an item (CosmeticThumbs): the content builder's thumb of its pattern, else the built camo texture of the slot's main texture.</summary>
    private static string? ThumbOf(CosmeticItem item)
    {
        if (ContentPaths.Shared is not { } shared || PawnLooks.PatternOf(item) is not { Length: > 0 } pattern)
        {
            return null;
        }

        string thumb = Path.Combine(shared, "chars", "thumbs", "cosmetic_" + pattern + ".png");
        if (File.Exists(thumb))
        {
            return thumb;
        }

        (string package, string stem) = (item.Slot, item.Side) switch
        {
            (CosmeticSlot.Body, CosmeticSide.Spy) => ("SPerso", "SPersoTextures_Shadw_agent"),
            (CosmeticSlot.Body, CosmeticSide.Merc) => ("SPerso", "SPersoTextures_DEF_01_A_Body"),
            (CosmeticSlot.Pistol, _) => ("SWeaponFirst", "SWeaponTex_p190"),
            (CosmeticSlot.GunAr, _) => ("SWeaponFirst", "SWeaponTex_M160"),
            (CosmeticSlot.GunSmg, _) => ("SWeaponFirst", "SWeaponTex_Hk81"),
            (CosmeticSlot.GunShotgun, _) => ("SWeaponFirst", "SWeaponTex_shotgun"),
            _ => (string.Empty, string.Empty),
        };
        if (item.Kind != CosmeticKind.Texture || package.Length == 0)
        {
            return null;
        }

        string camo = Path.Combine(shared, "chars", package, "textures", "cosmetic_" + pattern + "_" + stem + ".png");
        return File.Exists(camo) ? camo : null;
    }

    private static GlbCharacter? Character(int kind)
    {
        if (kind is < 0 or > 1)
        {
            return null;
        }

        if (!s_tried[kind])
        {
            s_tried[kind] = true;
            s_characters[kind] = BridgeSession.ConfiguredCharacter(kind);
            if (s_characters[kind] is { } loaded)
            {
                foreach (string name in s_idleClips)
                {
                    if (loaded.Find(name) is { } clip)
                    {
                        s_idle[kind] = clip;
                        break;
                    }
                }
            }
        }

        return s_characters[kind];
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetItem(char* id, ArmouryPreviewItem* item, char* pattern, int patternCapacity, char* thumb, int thumbCapacity)
    {
        try
        {
            OnlineTextCopy.Buffer(pattern, patternCapacity, string.Empty);
            OnlineTextCopy.Buffer(thumb, thumbCapacity, string.Empty);
            if (CosmeticCatalog.Find(Text(id)) is not { } found)
            {
                return 0;
            }

            float? turn = PawnLooks.HueTurnOf(found, found.Side);
            if (item != null)
            {
                *item = new ArmouryPreviewItem
                {
                    Side = (int)found.Side,
                    Slot = (int)found.Slot,
                    Kind = found.Kind switch { CosmeticKind.Tint => 0, CosmeticKind.Texture => 1, _ => 2 },
                    HasHue = turn is null ? 0 : 1,
                    HueTurn = turn ?? 0f,
                    Hue = found.Hue,
                };
            }

            OnlineTextCopy.Buffer(pattern, patternCapacity, PawnLooks.PatternOf(found));
            OnlineTextCopy.Buffer(thumb, thumbCapacity, ThumbOf(found));
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SlotModel(int side, int slot, char* buffer, int capacity)
    {
        try
        {
            var where = (CosmeticSlot)slot;
            OnlineTextCopy.Buffer(buffer, capacity, AssetFor(side == 1 ? CosmeticSide.Merc : CosmeticSide.Spy, where));
            return where switch { CosmeticSlot.Body => 0, CosmeticSlot.Light => 1, _ => 2 };
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CosmeticTexture(char* pattern, char* package, char* retail, char* into, int capacity)
    {
        try
        {
            string p = Text(pattern), folder = Text(package), r = Text(retail);
            OnlineTextCopy.Buffer(into, capacity, string.Empty);
            if (p.Length == 0 || folder.Length == 0 || r.Length == 0 || ContentPaths.Shared is not { } shared)
            {
                return 0;
            }

            string path = Path.Combine(shared, "chars", folder, "textures", "cosmetic_" + p + "_" + r + ".png");
            return File.Exists(path) ? OnlineTextCopy.Buffer(into, capacity, path) : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSkeleton(int kind, BridgeBoneInfo* bones, int capacity)
    {
        try
        {
            return Character(kind) is { } character
                ? BridgeSession.GetSkeleton(character, bones == null || capacity <= 0 ? default : new Span<BridgeBoneInfo>(bones, capacity))
                : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetIdlePose(int kind, float time, BridgeBone* bones, int capacity)
    {
        try
        {
            if (Character(kind) is not { } character)
            {
                return 0;
            }

            int count = character.BoneCount;
            Vector3[] position = (Vector3[])character.RestPosition.Clone();
            Quaternion[] rotation = (Quaternion[])character.RestRotation.Clone();
            Vector3[] scale = (Vector3[])character.RestScale.Clone();
            if (s_idle[kind] is { } clip)
            {
                float length = clip.Length;
                double at = length > 0f ? time % length : 0.0;
                GlbTrack[] tracks = clip.Tracks;
                for (int track = 0; track < tracks.Length; track++)
                {
                    int bone = tracks[track].Bone;
                    if (bone < 0 || bone >= count || bone >= position.Length)
                    {
                        continue;
                    }

                    switch (tracks[track].Type)
                    {
                        case GlbTrackType.Position: position[bone] = clip.Position(track, at); break;
                        case GlbTrackType.Rotation: rotation[bone] = Quaternion.Normalize(clip.Rotation(track, at)); break;
                        case GlbTrackType.Scale: scale[bone] = clip.Scale(track, at); break;
                    }
                }
            }

            int written = bones == null ? 0 : Math.Min(Math.Min(capacity, count), position.Length);
            for (int b = 0; b < written; b++)
            {
                bones[b] = new BridgeBone
                {
                    Px = position[b].X, Py = position[b].Y, Pz = position[b].Z,
                    Qx = rotation[b].X, Qy = rotation[b].Y, Qz = rotation[b].Z, Qw = rotation[b].W,
                    Sx = scale[b].X, Sy = scale[b].Y, Sz = scale[b].Z,
                };
            }

            return count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
