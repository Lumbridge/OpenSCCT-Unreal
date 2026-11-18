using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.Devices;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Events;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge;

// The run-time skins of the effect meshes: what the Godot client's EffectViews.ApplySkins draws over a thrown or placed
// gadget's mesh (game/Scripts/Fx/EffectViews.cs). The proximity mine's status light is the case: SMineProx swaps LightOn
// and LightOff into Skins[1] as it arms, is chaffed and comes back. Source/OpenSCCT/Runtime/ScctEffectSkinApi.h mirrors
// the records field for field; every field is 4 bytes.

/// <summary>One material slot of an effect actor's mesh as it is worn this frame.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeEffectSkinSlot
{
    /// <summary>
    /// Name id of the skin's object path (SGameplayObjectsTex.Object.proxy_mine_On), 0 when the slot keeps the mesh's own
    /// material: no skin set, or one that is not an additive or translucent material of the effects' texture table
    /// (EffectViews.SkinMaterial draws only those).
    /// </summary>
    public int SkinId;

    /// <summary>The cell of the skin's flipbook shown now (0 for a still skin): the level's time times Fps, wrapped.</summary>
    public int Frame;
}

public static class BridgeEffectSkinFlags
{
    /// <summary>An additive Shader.</summary>
    public const int Additive = 1;

    /// <summary>A translucent Shader (the Godot client draws it added too: black is its see-through).</summary>
    public const int Translucent = 2;

    /// <summary>The picture is a flipbook sheet of Columns by Rows cells.</summary>
    public const int Animated = 4;
}

/// <summary>How one skin is drawn (shared/fx/effects.json, the textures table).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeEffectSkin
{
    /// <summary><see cref="BridgeEffectSkinFlags"/>.</summary>
    public int Flags;

    /// <summary>
    /// Name id of the picture's file, relative to the content root (shared/fx/textures/x.png): the flipbook sheet when
    /// the skin is animated, else its still picture.
    /// </summary>
    public int FileId;

    /// <summary>The sheet's cells (1, 1, 1 for a still picture), read left to right, top to bottom.</summary>
    public int Columns, Rows, Frames;

    /// <summary>Cells a second, 0 for a still picture.</summary>
    public float Fps;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct EffectSkinApi
{
    public int Version;

    /// <summary>
    /// Writes the skins the effect actor wears now, one record per material slot from slot 0. Returns how many slots the
    /// actor's Skins has (at most <see cref="EffectSkinExports.MaxSlots"/>; only what fits is written), 0 when it has
    /// none, is gone or no session runs. A slot beyond the count keeps the mesh's own material.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, BridgeEffectSkinSlot*, int, int> GetActorSkins;

    /// <summary>Writes how the skin with this name id is drawn. Returns 1, or 0 when it is not a skin the effects draw.</summary>
    public delegate* unmanaged[Cdecl]<int, BridgeEffectSkin*, int> GetSkin;

    /// <summary>
    /// A check only (the game's -ScctMineLight switch): puts two proximity mines on the local match's server, owned by
    /// its first mercenary, side by side in the air a little ahead of the point along the direction and facing back at
    /// it, so both lights can be seen from there. Returns 1 when queued, 0 without a local server.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, int> PlaceCheckMines;

    /// <summary>
    /// A check only: locks the oldest proximity mine of the local match's server as a chaff grenade does
    /// (SetLockedByChaff), for this many seconds. Returns 1 when queued.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float, int> ChaffCheckMine;
}

public static unsafe class EffectSkinExports
{
    public const int ApiVersion = 1;

    public const int MaxSlots = 8;

    private sealed record Skin(int Flags, string File, int Columns, int Rows, int Frames, float Fps);

    private static readonly object s_lock = new();
    private static string? s_tableRoot;
    private static Dictionary<string, Skin> s_table = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(EffectSkinApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->GetActorSkins = &GetActorSkins;
        api->GetSkin = &GetSkin;
        api->PlaceCheckMines = &PlaceCheckMines;
        api->ChaffCheckMine = &ChaffCheckMine;
        return ApiVersion;
    }

    private static readonly ClassRef s_proximityMine = new("SBase.SMineProx");

    /// <summary>How far ahead of the point the check's mines hang, and how far each is from the line of sight.</summary>
    private const float CheckMineAhead = 110f, CheckMineAside = 14f;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PlaceCheckMines(float x, float y, float z, float dx, float dy, float dz) =>
        Exports.Current is { } session ? PlaceCheckMines(session, new FVector(x, y, z), new FVector(dx, dy, dz)) : 0;

    /// <summary>The managed side of the PlaceCheckMines entry (the probes call it).</summary>
    public static int PlaceCheckMines(BridgeSession session, FVector from, FVector direction) => WorldDebugExports.OnServer(session, host =>
    {
        Level level = host.World.Level;
        SBasePawn? merc = null;
        foreach (SBasePawn pawn in level.AllActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe && pawn.SPawnType == PawnType.SPAWNDEFENSE)
            {
                merc = pawn;
                break;
            }
        }

        if (merc is null)
        {
            return;
        }

        FVector forward = direction.SafeNormal();
        FRotator facing = (forward * -1f).Rotation();
        facing.GetAxes(out _, out FVector side, out _);
        for (int i = -1; i <= 1; i += 2)
        {
            // As SGadMultiMinesLauncher.SpawnMine leaves one: spawned for its owner and counted.
            if (DeviceClasses.Spawn(merc, s_proximityMine, merc, Name.None, from + (forward * CheckMineAhead) + (side * (i * CheckMineAside)), facing) is SMines mine)
            {
                mine.PcOwner = merc.Controller as SPlayerController;
                mine.AddMineCounter(1, mine.PcOwner);
            }
        }
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ChaffCheckMine(float seconds) => Exports.Current is { } session ? ChaffCheckMine(session, seconds) : 0;

    /// <summary>The managed side of the ChaffCheckMine entry (the probes call it).</summary>
    public static int ChaffCheckMine(BridgeSession session, float seconds) => WorldDebugExports.OnServer(session, host =>
    {
        foreach (SMineProx mine in host.World.Level.AllActors<SMineProx>())
        {
            if (!mine.bDeleteMe)
            {
                mine.eventSetLockedByChaff(seconds);
                return;
            }
        }
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActorSkins(int actorId, BridgeEffectSkinSlot* slots, int capacity)
    {
        try
        {
            if (slots == null || capacity <= 0)
            {
                return 0;
            }

            return Read(actorId, new Span<BridgeEffectSkinSlot>(slots, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSkin(int skinId, BridgeEffectSkin* skin)
    {
        try
        {
            if (skin == null)
            {
                return 0;
            }

            return Describe(skinId, out *skin) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The managed form of GetActorSkins, for the probes.</summary>
    public static int Read(int actorId, Span<BridgeEffectSkinSlot> slots)
    {
        if (Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        Level level = session.Session.Client.World.Level;
        if (level.FindActor(actorId) is not { bDeleteMe: false } actor || actor.Skins.Count == 0)
        {
            return 0;
        }

        Dictionary<string, Skin> table = Table(session.Session.MapFolder);
        int count = Math.Min(actor.Skins.Count, MaxSlots);
        float time = MathF.Max(level.TimeSeconds, 0f);
        for (int i = 0; i < count && i < slots.Length; i++)
        {
            string? path = actor.Skins[i].Path;
            if (path is null || !table.TryGetValue(path, out Skin? skin))
            {
                slots[i] = default;
                continue;
            }

            slots[i] = new BridgeEffectSkinSlot
            {
                SkinId = EventNames.Id(path),

                // EffectViews.SkinMaterial: the frame of the level's clock, the same on every mine.
                Frame = skin.Fps > 0f ? (int)(time * skin.Fps) % skin.Frames : 0,
            };
        }

        return count;
    }

    /// <summary>The managed form of GetSkin, for the probes.</summary>
    public static bool Describe(int skinId, out BridgeEffectSkin described)
    {
        described = default;
        if (skinId == 0 || Exports.Current is not { IsDisposed: false } session ||
            !Table(session.Session.MapFolder).TryGetValue(EventNames.Text(skinId), out Skin? skin))
        {
            return false;
        }

        described = new BridgeEffectSkin
        {
            Flags = skin.Flags,
            FileId = EventNames.Id(skin.File),
            Columns = skin.Columns,
            Rows = skin.Rows,
            Frames = skin.Frames,
            Fps = skin.Fps,
        };
        return true;
    }

    /// <summary>
    /// The skins the effects draw, by object path: the additive and translucent materials of the textures table of
    /// shared/fx/effects.json, with their flipbooks (FxLibrary's _additive, _translucent and _skinAnimations).
    /// </summary>
    private static Dictionary<string, Skin> Table(string mapFolder)
    {
        lock (s_lock)
        {
            string? root = SharedContent.FindRoot(mapFolder);
            if (string.Equals(root, s_tableRoot, StringComparison.OrdinalIgnoreCase) && s_tableRoot is not null)
            {
                return s_table;
            }

            var table = new Dictionary<string, Skin>(StringComparer.OrdinalIgnoreCase);
            string? file = root is null ? null : Path.Combine(root, "fx", "effects.json");
            if (file is not null && File.Exists(file))
            {
                using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(file));
                if (doc.RootElement.TryGetProperty("textures", out JsonElement textures))
                {
                    foreach (JsonProperty t in textures.EnumerateObject())
                    {
                        if (ReadSkin(t.Value) is { } skin)
                        {
                            table[t.Name] = skin;
                        }
                    }
                }
            }

            s_tableRoot = root;
            s_table = table;
            return table;
        }
    }

    private static Skin? ReadSkin(JsonElement texture)
    {
        if (!texture.TryGetProperty("png", out JsonElement png) || png.GetString() is not { } still ||
            !texture.TryGetProperty("material", out JsonElement material) || material.ValueKind != JsonValueKind.Object)
        {
            return null;
        }

        int flags = (IsTrue(material, "additive") ? BridgeEffectSkinFlags.Additive : 0) |
            (IsTrue(material, "translucent") ? BridgeEffectSkinFlags.Translucent : 0);
        if (flags == 0)
        {
            return null;
        }

        if (material.TryGetProperty("flipbook", out JsonElement flip) && flip.ValueKind == JsonValueKind.Object &&
            flip.TryGetProperty("texture", out JsonElement sheet) && sheet.GetString() is { } sheetFile)
        {
            int columns = flip.GetProperty("cols").GetInt32();
            int rows = flip.GetProperty("rows").GetInt32();
            int frames = flip.GetProperty("frames").GetInt32();
            float fps = flip.GetProperty("fps").GetSingle();
            if (columns > 0 && rows > 0 && frames > 0 && fps > 0f)
            {
                return new Skin(flags | BridgeEffectSkinFlags.Animated, "shared/fx/" + sheetFile, columns, rows, frames, fps);
            }
        }

        return new Skin(flags, "shared/fx/" + still, 1, 1, 1, 0f);
    }

    private static bool IsTrue(JsonElement o, string name) =>
        o.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.True;
}
