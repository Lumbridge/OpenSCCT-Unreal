using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using OpenScct.Content;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net.Cosmetics;
using OpenScct.Server;

namespace OpenScct.UnrealBridge.Pose;

/// <summary><see cref="BridgePawnLook.Flags"/>.</summary>
public static class BridgePawnLookFlags
{
    /// <summary>The pawn of the player at this machine.</summary>
    public const int Mine = 1;

    /// <summary>The light item turns the goggle and lamp glow (<see cref="BridgePawnLook.HueTurn"/> is set).</summary>
    public const int Hue = 2;

    /// <summary>The team wears a map outfit (any part of it).</summary>
    public const int Outfit = 4;

    /// <summary>The outfit's own model is drawn in place of the stock mesh.</summary>
    public const int OutfitModel = 8;

    /// <summary>The look is retail: no item in any slot.</summary>
    public const int Retail = 16;
}

/// <summary>
/// One pawn's look as it is drawn on this machine (Source/OpenSCCT/Runtime/ScctPawnLookApi.h mirrors it): the player's
/// cosmetics after the hide-mine / hide-others options and the map outfit's policy. Names are ids into the session's
/// name table (0: none).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgePawnLook
{
    public int ActorId;

    /// <summary><see cref="BridgePawnLookFlags"/>.</summary>
    public int Flags;

    /// <summary>0 spy, 1 merc: whose items and stock mesh.</summary>
    public int Side;

    /// <summary>The body item's id ("spy.body.woodland") and the pattern its texture is made with ("woodland",
    /// "tint_9aa0a8": the content builder's cosmetic_&lt;pattern&gt;_&lt;retail texture&gt;.png); 0 for retail.</summary>
    public int BodyItem, BodyPattern;

    /// <summary>0 a camo texture, 1 a tint (also drawn from a made texture), -1 none.</summary>
    public int BodyKind;

    /// <summary>A tint item's colour, 0xRRGGBB (0xFFFFFF otherwise).</summary>
    public uint BodyRgb;

    /// <summary>The light item's id (0 for retail) and its hue, degrees.</summary>
    public int LightItem;

    public float LightHue;

    /// <summary>Radians from the side's retail glow colour to the item's (PawnCosmetics.HueTurn); 0 without <see cref="BridgePawnLookFlags.Hue"/>.</summary>
    public float HueTurn;

    /// <summary>The item of the gun in the pawn's hands (the spy's pistol, the merc's gun class) and its pattern; 0 for retail.</summary>
    public int GunItem, GunPattern;
}

/// <summary>A team's map outfit (Source/OpenSCCT/Runtime/ScctPawnLookApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeKindOutfit
{
    /// <summary>1 when the team wears an outfit (any part), else 0 and nothing else is set.</summary>
    public int Dressed;

    /// <summary>1 when the model is the map's own glb under render/ (imported by Tools/Import/import_outfits.py), 0 a stock mesh.</summary>
    public int MapGlb;

    /// <summary>0 map, 1 player, 2 none (OutfitCosmetics).</summary>
    public int Policy;

    /// <summary>The model's name: "SPerso.DEF_01" for a stock mesh, the glb's path under render/ for the map's own; 0 without a model.</summary>
    public int Model;

    /// <summary>The map's folder name (for the imported map glb's place).</summary>
    public int Map;

    /// <summary>Full paths of the body and head slot textures (0 when the outfit has none or the file is missing).</summary>
    public int BodyTexture, HeadTexture;

    /// <summary>The goggle lights' bone on the model and their offset in its axes (uu).</summary>
    public int GoggleBone;

    public float GoggleX, GoggleY, GoggleZ;

    /// <summary>Bones of the model's skeleton (GetSkeleton for the kind lists them), 0 without a model.</summary>
    public int ModelBones;
}

/// <summary>
/// Each pawn's look on this machine, as the Godot client's PawnCosmetics.LookOf reads it: the look replicated on its
/// PlayerReplicationInfo (?SvmLook at login, SvmLookLow/High/ExtraLow/ExtraHigh), the local player's own choice while
/// "hide mine" keeps it off the servers, retail for others while "hide others" is on, then the map outfit's policy for
/// the team (spec/additions/map-outfits.md): "map" (the default) takes the body item off when the outfit dresses the
/// body or swaps the model, "none" draws the team retail, "player" keeps the look. Also the debug entries that give
/// looks on the local server, so what is drawn comes back through replication.
/// </summary>
public sealed class PawnLooks
{
    /// <summary>The retail goggle glow's hue (PawnCosmetics.SpyRetailHue) and the merc lamp's (RenderK.TorchHue 21 of 255).</summary>
    public const float SpyRetailHue = 71f;

    public const float MercRetailHue = 21f * 360f / 255f;

    /// <summary>Looks -ScctLookOthers hands out, in turn, to every other player on the local server.</summary>
    public static readonly string[] OtherPresets =
    [
        "desert+red", "digital+cyan", "navy+violet", "red_tiger+magenta", "woodland+blue", "olive+amber", "galaxy+green",
    ];

    private readonly BridgeSession _session;
    private readonly List<string> _names = [string.Empty];
    private readonly Dictionary<string, int> _nameIds = new(StringComparer.Ordinal) { [string.Empty] = 0 };

    private PawnLooks(BridgeSession session)
    {
        _session = session;
    }

    public static PawnLooks Of(BridgeSession session) => session.Feature(static s => new PawnLooks(s));

    /// <summary>The player's own looks shown on his pawn while <see cref="HideMine"/> is on (the Armoury's choice).</summary>
    public static CosmeticLook OwnSpy { get; set; } = CosmeticLook.Default;

    public static CosmeticLook OwnMerc { get; set; } = CosmeticLook.Default;

    /// <summary>"Hide my cosmetics": the own look stays off the servers and is drawn here only.</summary>
    public static bool HideMine { get; set; }

    /// <summary>"Hide others' cosmetics": every other player's pawn is drawn retail.</summary>
    public static bool HideOthers { get; set; }

    /// <summary>The name of an id of the table (empty for 0 or an unknown id).</summary>
    public string Name(int id) => id > 0 && id < _names.Count ? _names[id] : string.Empty;

    /// <summary>The id of a name in the table (0 for null or empty), added on first use.</summary>
    public int Id(string? name)
    {
        if (string.IsNullOrEmpty(name))
        {
            return 0;
        }

        if (!_nameIds.TryGetValue(name, out int id))
        {
            id = _names.Count;
            _names.Add(name);
            _nameIds[name] = id;
        }

        return id;
    }

    /// <summary>The look drawn on <paramref name="pawn"/> (port of PawnCosmetics.LookOf).</summary>
    public CosmeticLook LookOf(SBasePawn pawn)
    {
        if (pawn.PlayerReplicationInfo is not { } pri)
        {
            return CosmeticLook.Default;
        }

        bool mine = IsMine(pawn);
        CosmeticSide side = SideOf(pawn);
        CosmeticLook look = mine
            ? HideMine ? (side == CosmeticSide.Spy ? OwnSpy : OwnMerc) : new CosmeticLook(pri.SvmLook, pri.SvmLookExtra)
            : HideOthers ? CosmeticLook.Default : new CosmeticLook(pri.SvmLook, pri.SvmLookExtra);
        TeamOutfit? outfit = _session.Outfits.Outfits.For(spy: side == CosmeticSide.Spy);
        return outfit?.Cosmetics switch
        {
            OutfitCosmetics.Map when outfit.Body is not null || outfit.Model is not null => look.With(CosmeticSlot.Body, null),
            OutfitCosmetics.None => CosmeticLook.Default,
            _ => look,
        };
    }

    public static CosmeticSide SideOf(SBasePawn pawn) => pawn is SPAWNDEFENSE ? CosmeticSide.Merc : CosmeticSide.Spy;

    private bool IsMine(SBasePawn pawn) =>
        pawn.PlayerReplicationInfo is { } pri && ReferenceEquals(_session.Session.LocalController?.PlayerReplicationInfo, pri);

    /// <summary>The look's item for the gun in the pawn's hands (port of PawnCosmetics.GunItem).</summary>
    public static CosmeticItem? GunItem(SBasePawn pawn, CosmeticLook look)
    {
        if (pawn is SPawnAttaque or OpenScct.Game.Svm.SpyClone)
        {
            return look[CosmeticSlot.Pistol];
        }

        return SGameInfo.GunClassOf(pawn.MyWeapon) switch
        {
            "suppressed" => look[CosmeticSlot.Suppressed],
            "unsuppressed" => look[CosmeticSlot.Unsuppressed],
            "automatic" => look[CosmeticSlot.Automatic],
            "magnum44" => look[CosmeticSlot.Magnum44],
            "desert_eagle" => look[CosmeticSlot.DesertEagle],
            "ar" => look[CosmeticSlot.GunAr],
            "smg" => look[CosmeticSlot.GunSmg],
            "shotgun" => look[CosmeticSlot.GunShotgun],
            _ => null,
        };
    }

    /// <summary>The pattern an item's texture is made with (PawnCosmetics.Dress): its camo, or "tint_rrggbb" for a tint.</summary>
    public static string? PatternOf(CosmeticItem? item) => item?.Kind switch
    {
        CosmeticKind.Texture => item.Pattern,
        CosmeticKind.Tint => "tint_" + item.Rgb.ToString("x6", CultureInfo.InvariantCulture),
        _ => null,
    };

    /// <summary>The hue turn (radians) from the side's retail glow to the light item's, or null for retail (PawnCosmetics.HueTurn).</summary>
    public static float? HueTurnOf(CosmeticItem? light, CosmeticSide side)
    {
        if (light is not { Kind: CosmeticKind.Hue })
        {
            return null;
        }

        float from = side == CosmeticSide.Spy ? SpyRetailHue : MercRetailHue;
        return (light.Hue - from) * MathF.PI / 180f;
    }

    /// <summary>Writes the looks of the client world's pawns. Returns how many there are (only the first that fit are written).</summary>
    public int Looks(Span<BridgePawnLook> into)
    {
        int count = 0;
        foreach (SBasePawn pawn in _session.Session.Client.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            if (count < into.Length)
            {
                into[count] = Record(pawn);
            }

            count++;
        }

        return count;
    }

    /// <summary>One pawn's record.</summary>
    public BridgePawnLook Record(SBasePawn pawn)
    {
        CosmeticLook look = LookOf(pawn);
        CosmeticSide side = SideOf(pawn);
        CosmeticItem? body = look[CosmeticSlot.Body], light = look[CosmeticSlot.Light], gun = GunItem(pawn, look);
        float? turn = HueTurnOf(light, side);
        int kind = side == CosmeticSide.Merc ? 1 : 0;
        PawnOutfits.Team? team = _session.Outfits.Of(kind);
        int flags = (IsMine(pawn) ? BridgePawnLookFlags.Mine : 0) | (turn is not null ? BridgePawnLookFlags.Hue : 0) |
            (team is { Outfit.IsEmpty: false } ? BridgePawnLookFlags.Outfit : 0) | (team?.Model is not null ? BridgePawnLookFlags.OutfitModel : 0) |
            (look.IsDefault ? BridgePawnLookFlags.Retail : 0);
        return new BridgePawnLook
        {
            ActorId = pawn.ActorId,
            Flags = flags,
            Side = kind,
            BodyItem = Id(body?.Id),
            BodyPattern = Id(PatternOf(body)),
            BodyKind = body?.Kind switch { CosmeticKind.Texture => 0, CosmeticKind.Tint => 1, _ => -1 },
            BodyRgb = body is { Kind: CosmeticKind.Tint } ? body.Rgb : 0xFFFFFFu,
            LightItem = Id(light?.Id),
            LightHue = light?.Hue ?? 0f,
            HueTurn = turn ?? 0f,
            GunItem = Id(gun?.Id),
            GunPattern = Id(PatternOf(gun)),
        };
    }

    /// <summary>A team's outfit record.</summary>
    public BridgeKindOutfit Outfit(int kind)
    {
        if (_session.Outfits.Of(kind) is not { } team)
        {
            return default;
        }

        (string Bone, OpenScct.Core.FVector Offset)? goggles = _session.Outfits.Goggles(kind);
        return new BridgeKindOutfit
        {
            Dressed = 1,
            MapGlb = team.MapGlb ? 1 : 0,
            Policy = (int)team.Outfit.Cosmetics,
            Model = Id(team.ModelName),
            Map = Id(_session.Map),
            BodyTexture = Id(team.BodyTexture),
            HeadTexture = Id(team.HeadTexture),
            GoggleBone = Id(goggles?.Bone),
            GoggleX = goggles?.Offset.X ?? 0f,
            GoggleY = goggles?.Offset.Y ?? 0f,
            GoggleZ = goggles?.Offset.Z ?? 0f,
            ModelBones = team.Model?.BoneCount ?? 0,
        };
    }

    /// <summary>
    /// The content builder's camo texture of a pattern for a retail texture, by the package folder the retail texture is
    /// in: shared/chars/&lt;package&gt;/textures/cosmetic_&lt;pattern&gt;_&lt;retail&gt;.png, or for "outfit:&lt;kind&gt;" the textures
    /// folder of that team's outfit model. Null when no such file was built.
    /// </summary>
    public string? CosmeticTexture(string pattern, string package, string retail)
    {
        if (string.IsNullOrEmpty(pattern) || string.IsNullOrEmpty(retail) || string.IsNullOrEmpty(package))
        {
            return null;
        }

        string? folder = package.StartsWith("outfit:", StringComparison.Ordinal) && int.TryParse(package[7..], out int kind)
            ? _session.Outfits.ModelTextureFolder(kind)
            : _session.Outfits.SharedRoot is { } shared ? Path.Combine(shared, "chars", package, "textures") : null;
        return folder is null ? null : FindCosmeticTexture(folder, pattern, retail);
    }

    internal static string? FindCosmeticTexture(string folder, string pattern, string retail)
    {
        string prefix = Path.Combine(folder, "cosmetic_" + pattern + "_");
        string path = prefix + retail + ".png";
        if (File.Exists(path))
        {
            return path;
        }

        // Unreal replaces the dot in Blender's numbered names on import (e.g. g18.001_baseColor).
        // The loose colour texture and its finish mask keep the original name.
        string original = Regex.Replace(retail, @"_([0-9]{3})(?=_|$)", ".$1");
        string originalPath = prefix + original + ".png";
        return original != retail && File.Exists(originalPath) ? originalPath : null;
    }

    /// <summary>
    /// A look of one side from text: "hex:&lt;hex&gt;" as ?SvmLook carries it, or names joined by "+" or ",". A name with a
    /// dot is an item id ("spy.body.woodland", kept when it is the side's); any other name puts on every slot of the side
    /// that has an item "&lt;side&gt;.&lt;slot&gt;.&lt;name&gt;" ("woodland" the suit and the guns, "blue" the light, "navy" the tint).
    /// Unknown names are left out; the count of names used comes back in <paramref name="used"/>.
    /// </summary>
    public static CosmeticLook Parse(string? text, CosmeticSide side, out int used)
    {
        used = 0;
        if (string.IsNullOrWhiteSpace(text))
        {
            return CosmeticLook.Default;
        }

        text = text.Trim();
        if (text.StartsWith("hex:", StringComparison.OrdinalIgnoreCase))
        {
            CosmeticLook hex = CosmeticLook.ParseHex(text[4..]);
            used = hex.IsDefault ? 0 : 1;
            return hex.Validate(side, CosmeticUnlocks.Of(AllIds()));
        }

        CosmeticLook look = CosmeticLook.Default;
        foreach (string raw in text.Split(['+', ','], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            bool any = false;
            if (raw.Contains('.', StringComparison.Ordinal))
            {
                if (CosmeticCatalog.Find(raw) is { } item && item.Side == side)
                {
                    look = look.With(item.Slot, item);
                    any = true;
                }
            }
            else
            {
                foreach (CosmeticSlot slot in CosmeticCatalog.SlotsOf(side))
                {
                    foreach (CosmeticItem item in CosmeticCatalog.For(side, slot))
                    {
                        if (item.Id.EndsWith("." + raw, StringComparison.OrdinalIgnoreCase))
                        {
                            look = look.With(slot, item);
                            any = true;
                            break;
                        }
                    }
                }
            }

            used += any ? 1 : 0;
        }

        return look;
    }

    private static IEnumerable<string> AllIds()
    {
        foreach (CosmeticItem item in CosmeticCatalog.All)
        {
            yield return item.Id;
        }
    }

    /// <summary>
    /// Debug (-ScctLook): writes a look on the local server onto the player's own PlayerReplicationInfo, for the side of
    /// his team, as ?SvmLook at login does, so it reaches this machine through replication. The text is read per side
    /// (<see cref="Parse"/>). Also kept as the player's own look for "hide mine". Returns 1 when queued.
    /// </summary>
    public static int ForceLocal(BridgeSession session, string text)
    {
        OwnSpy = Parse(text, CosmeticSide.Spy, out _);
        OwnMerc = Parse(text, CosmeticSide.Merc, out _);
        // The bots join the local server as clients too: the player is told apart by the name he logged in with.
        if (session.Session.LocalController?.PlayerReplicationInfo?.PlayerName is not { Length: > 0 } name)
        {
            return 0;
        }

        return OnServer(session, host =>
        {
            foreach (PlayerReplicationInfo pri in host.World.Level.AllActors<PlayerReplicationInfo>())
            {
                if (!pri.bDeleteMe && pri.PlayerName == name)
                {
                    Put(pri, text);
                }
            }
        });
    }

    /// <summary>
    /// Debug (-ScctLookOthers): gives every other player on the local server (the bots) a look of <see cref="OtherPresets"/>
    /// in turn, for the side of each one's team. Returns 1 when queued.
    /// </summary>
    public static int GiveOthers(BridgeSession session)
    {
        if (session.Session.LocalController?.PlayerReplicationInfo?.PlayerName is not { Length: > 0 } name)
        {
            return 0;
        }

        return OnServer(session, host =>
        {
            int next = 0;
            foreach (PlayerReplicationInfo pri in host.World.Level.AllActors<PlayerReplicationInfo>())
            {
                if (!pri.bDeleteMe && pri.PlayerName != name && pri.Team is not null)
                {
                    Put(pri, OtherPresets[next++ % OtherPresets.Length]);
                }
            }
        });
    }



    private static void Put(PlayerReplicationInfo pri, string text)
    {
        CosmeticSide side = pri.Team?.TeamIndex == 1 ? CosmeticSide.Merc : CosmeticSide.Spy;
        CosmeticLook look = Parse(text, side, out _);
        (pri.SvmLookLow, pri.SvmLookHigh) = look.Split();
        pri.SvmLookExtraLow = (int)look.Extra;
        pri.SvmLookExtraHigh = (int)(look.Extra >> 32);
    }

    /// <summary>Runs <paramref name="action"/> once on the local match's server thread before its next world step (as WorldDebugExports).</summary>
    private static int OnServer(BridgeSession session, Action<ServerHost> action)
    {
        if (session is not { IsRemote: false, IsDisposed: false } || session.Session.Host is not { } host)
        {
            return 0;
        }

        void Run(ServerHost running)
        {
            running.BeforeWorldTick -= Run;
            try
            {
                action(running);
            }
            catch (Exception e)
            {
                LastFailure = e;
            }
        }

        host.BeforeWorldTick += Run;
        return 1;
    }

    /// <summary>The last exception a debug entry threw on the server thread (for the probe); null when none did.</summary>
    public static Exception? LastFailure { get; private set; }
}
