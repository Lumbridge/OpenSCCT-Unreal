using System;
using System.IO;
using OpenScct.Content;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Pose;

/// <summary>
/// The map's outfits on the drawn pawns (render/outfits.json, spec/svmap/1.2/outfits.md, spec/additions/map-outfits.md),
/// read once per session: for each team the model posed and drawn in place of its stock mesh (a glTF the map ships under
/// render/, or a stock mesh of the shared packs by asset path), the slot textures over its body and head, the goggle
/// lights' bone and offset and the cosmetics policy. A port of the Godot client's PawnOutfits without its drawing: the
/// bridge poses the outfit's skeleton with the team's stock clips, and Unreal draws the model and puts the textures on.
/// A model that is missing or cannot be read leaves the team's stock mesh (with a warning in <see cref="Warnings"/>).
/// </summary>
public sealed class PawnOutfits
{
    private readonly Team?[] _teams = new Team?[2];

    private PawnOutfits(MapOutfits outfits, string mapFolder, string? sharedRoot)
    {
        Outfits = outfits;
        MapFolder = mapFolder;
        SharedRoot = sharedRoot;
        for (int kind = 0; kind < 2; kind++)
        {
            _teams[kind] = Dress(kind, outfits.For(spy: kind == 0));
        }
    }

    /// <summary>A team as the map dresses it.</summary>
    /// <param name="Outfit">The outfit as the file has it.</param>
    /// <param name="Stock">The team's stock character file (its clips pose the model).</param>
    /// <param name="Model">The outfit's own model, or null when the team keeps its stock mesh.</param>
    /// <param name="ModelName">
    /// The model's name for Unreal: the stock asset path ("SPerso.DEF_01") of a stock model, or the map glb's path under
    /// render/ ("outfits/agent.glb") of the map's own, which Tools/Import/import_outfits.py imports; null without a model.
    /// </param>
    /// <param name="MapGlb">The model is the map's own glb (not a stock mesh).</param>
    /// <param name="BodyTexture">The body slot's texture (full path) when its file exists.</param>
    /// <param name="HeadTexture">The head slot's texture, likewise.</param>
    public sealed record Team(TeamOutfit Outfit, GlbCharacter Stock, GlbCharacter? Model, string? ModelName, bool MapGlb, string? BodyTexture,
        string? HeadTexture);

    /// <summary>The file as read (<see cref="MapOutfits.None"/> when the map has none).</summary>
    public MapOutfits Outfits { get; }

    public string MapFolder { get; }

    public string? SharedRoot { get; }

    /// <summary>What could not be used: a model that is missing or unreadable, a slot texture that is not there.</summary>
    public string Warnings { get; private set; } = string.Empty;

    /// <summary>The outfits of a map folder; an unreadable outfits.json counts as none (with the reason in <see cref="Warnings"/>).</summary>
    public static PawnOutfits For(string mapFolder, string? sharedRoot)
    {
        ArgumentException.ThrowIfNullOrEmpty(mapFolder);
        MapOutfits outfits = MapOutfits.None;
        string problem = string.Empty;
        try
        {
            outfits = OutfitsDoc.Load(mapFolder) ?? MapOutfits.None;
        }
        catch (Exception e) when (e is InvalidDataException or IOException or FormatException or ArgumentException)
        {
            problem = $"{OutfitsDoc.RelativePath} left out: {e.Message}. ";
        }

        var made = new PawnOutfits(outfits, mapFolder, sharedRoot);
        made.Warnings = problem + made.Warnings;
        return made;
    }

    /// <summary>A team (0 spies, 1 mercs) as the map dresses it, or null when the map leaves it stock.</summary>
    public Team? Of(int kind) => kind is 0 or 1 ? _teams[kind] : null;

    /// <summary>The character posed for a pawn kind: the outfit's model, else the team's stock file (null when that is missing).</summary>
    public GlbCharacter? Character(int kind) => Of(kind) is { Model: { } model } ? model : StockCharacter(kind);

    /// <summary>The team's stock character file: SPerso.ATT_01 for the spies, SPerso.DEF_01 for the mercs.</summary>
    public GlbCharacter? StockCharacter(int kind) =>
        kind is 0 or 1 && StockPath(StockAsset(kind == 0), SharedRoot) is { } path ? GlbCharacter.Load(path) : null;

    /// <summary>The team's stock mesh: SPerso.ATT_01 for the spies, SPerso.DEF_01 for the mercs (PawnOutfits.StockAsset).</summary>
    public static string StockAsset(bool spy) => spy ? "SPerso.ATT_01" : "SPerso.DEF_01";

    /// <summary>A stock mesh's file by asset path ("SPerso.DEF_01": shared/chars/SPerso/DEF_01.glb), or null when missing.</summary>
    public static string? StockPath(string asset, string? sharedRoot)
    {
        int dot = asset.IndexOf('.', StringComparison.Ordinal);
        if (sharedRoot is null || dot <= 0 || dot == asset.Length - 1)
        {
            return null;
        }

        string path = Path.Combine(sharedRoot, "chars", asset[..dot], asset[(dot + 1)..] + ".glb");
        return File.Exists(path) ? path : null;
    }

    /// <summary>The folder a model's textures are in (its glb's folder plus textures), for the camo files.</summary>
    public string? ModelTextureFolder(int kind) =>
        Of(kind) is { Model: { } model } ? Path.Combine(Path.GetDirectoryName(model.Path) ?? ".", "textures") : null;

    private Team? Dress(int kind, TeamOutfit? outfit)
    {
        // A team without slots or a model still has its cosmetics policy ("none" draws it retail).
        if (outfit is null || StockCharacter(kind) is not { } stock)
        {
            return null;
        }

        GlbCharacter? model = null;
        string? name = null;
        bool mapGlb = false;
        if (outfit.Model is { } wanted)
        {
            string? path = wanted.Glb is { } own ? MapRenderFile(own) : wanted.Stock is { } asset ? StockPath(asset, SharedRoot) : null;
            if (path is null)
            {
                Warnings += $"outfit model {wanted.Glb ?? wanted.Stock} of the {(kind == 0 ? "spies" : "mercs")} is not there; the stock mesh is drawn. ";
            }
            else if (!string.Equals(Path.GetFullPath(path), Path.GetFullPath(stock.Path), StringComparison.OrdinalIgnoreCase))
            {
                try
                {
                    model = GlbCharacter.Load(path);
                    name = wanted.Glb ?? wanted.Stock;
                    mapGlb = wanted.Glb is not null;
                }
                catch (Exception e) when (e is InvalidDataException or IOException)
                {
                    Warnings += $"outfit model {path} left out: {e.Message}. ";
                }
            }
        }

        return new Team(outfit, stock, model, name, mapGlb, SlotPath(outfit.Body, "body", kind), SlotPath(outfit.Head, "head", kind));
    }

    private string? SlotPath(OutfitSlot? slot, string what, int kind)
    {
        if (slot is null)
        {
            return null;
        }

        string? path = MapRenderFile(slot.Texture);
        if (path is null)
        {
            Warnings += $"the {(kind == 0 ? "spies'" : "mercs'")} {what} texture {slot.Texture} is not there. ";
        }

        return path;
    }

    private string? MapRenderFile(string relative)
    {
        string path = Path.Combine(MapFolder, "render", relative.Replace('/', Path.DirectorySeparatorChar));
        return File.Exists(path) ? path : null;
    }

    /// <summary>The goggle lights' bone and offset (uu, in the bone's axes) of a team's outfit model; null without one.</summary>
    public (string Bone, FVector Offset)? Goggles(int kind) =>
        Of(kind) is { Model: not null, Outfit.Model: { } model } ? (model.GoggleBone, model.GoggleOffset) : null;
}
