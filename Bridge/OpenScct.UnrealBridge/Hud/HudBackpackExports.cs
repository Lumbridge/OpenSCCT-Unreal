using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScct.UnrealBridge.Hud;

namespace OpenScct.UnrealBridge;

/// <summary>
/// What the merc's backpack panels show (<see cref="HudBackpackApi.GetBackpack"/>), as the Godot client's
/// BackpackDriver reads it. Source/OpenSCCT/Runtime/ScctHudBackpackApi.h mirrors it field for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudBackpackState
{
    /// <summary>0 hidden, 1 the merc holds his backpack (his share and his two mates'), 2 the refill entry is offered.</summary>
    public int Mode;

    /// <summary>The team's gadget slots (STeamInfo.iNBGadgets) clamped to 1..4: the rows each panel shows.</summary>
    public int Rows;

    /// <summary>The panels shown: bit 0 the own panel (in mode 2 the share the mate carries), bit 1 mate A, bit 2 mate B.</summary>
    public int Panels;

    /// <summary>The content's panel titles (HUD text ids): "You" (A_BackPackSelf) and "Teammate" (A_BackPackFriend).</summary>
    public int SelfLabelId;

    public int MateLabelId;

    /// <summary>The player names (HUD text ids) of mate A and mate B; in mode 2 mate A is the mate carrying the share.</summary>
    public int MateANameId;

    public int MateBNameId;

    /// <summary>How many rows <see cref="HudBackpackApi.GetBackpack"/> wrote.</summary>
    public int RowCount;
}

/// <summary>One gadget row of a backpack panel. Source/OpenSCCT/Runtime/ScctHudBackpackApi.h mirrors it.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudBackpackRow
{
    public const int PackageLength = 16, TextureLength = 32;

    /// <summary>The panel (owner slot): 0 the own panel or the share, 1 mate A, 2 mate B.</summary>
    public int Panel;

    /// <summary>The row in the panel, 0..3 (the original's Ico1..Ico4: top, left, right, bottom).</summary>
    public int Row;

    /// <summary>The gadget's unique id (SGadget.GetUniqueIDByClass), 0 when the slot has no gadget.</summary>
    public int GadgetId;

    /// <summary>The charges the backpack holds for it; below 0 the slot is not filled (drawn grey), 0 drawn red.</summary>
    public int Charges;

    /// <summary>What the backpack holds for that gadget when full (SGadBackPack.GetBackPackCharge), -1 when unknown.</summary>
    public int Max;

    /// <summary>The gadget's icon: the texel region of the texture named below (the Options package's material).</summary>
    public int U0, V0, U1, V1;

    public fixed char Package[PackageLength];

    public fixed char Texture[TextureLength];

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

    public string TextureName()
    {
        fixed (char* t = Texture)
        {
            return new string(t);
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
/// The merc backpack panels' table of entry points, beside the HUD's own (<see cref="HudExports"/>): the own share
/// and both mates' shares while the merc holds the backpack, or the share a mate carries for him while the refill
/// entry is offered, each row with its gadget's icon; and unattended checks' ways to select the backpack and to set up
/// a refill. Unreal asks for <see cref="GetApi"/> once
/// (<c>Host.GetFunction("OpenScct.UnrealBridge.HudBackpackExports", "GetApi")</c>).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudBackpackApi
{
    public int Version;

    /// <summary>GetBackpack(state, rows, capacity): fills the state and up to capacity rows; returns the rows written.</summary>
    public delegate* unmanaged[Cdecl]<HudBackpackState*, HudBackpackRow*, int, int> GetBackpack;

    /// <summary>Unattended checks only: selects the local merc's backpack with the gadget keys; 1 when it is selected.</summary>
    public delegate* unmanaged[Cdecl]<int> SelectBackpack;

    /// <summary>
    /// Unattended checks only, on the local match's server: for <c>seconds</c> the mate carrying the local merc's share
    /// is held 110 units in front of him, facing him, and the merc's first gadget with charges loses one, so the refill
    /// entry is offered as in play. 1 when queued, 0 on a remote round or without a pawn.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float, int> ForceRefill;

    /// <summary>
    /// Unattended checks only, on the local match's server: gives the backpack (SGadBackPack) to the local merc, and with
    /// a non-zero argument to every merc of his side, as a gear change to it would (the client has no gear screen yet,
    /// and the fixed merc kit has no backpack). 1 when queued, 0 on a remote round or without a pawn.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, int> GiveBackpacks;
}

public static unsafe class HudBackpackExports
{
    public const int ApiVersion = 1;

    /// <summary>The package whose materials name the gadget icons (MagmaIconName), as BackpackDriver looks them up.</summary>
    public const string IconPackage = "Options";

    private const string DefaultIcon = "C_Default_2";

    private static readonly Dictionary<int, SGadget?> GadgetsById = [];

    [ThreadStatic]
    private static List<HudBackpackRow>? _rows;

    /// <summary>Fills <paramref name="api"/>. Returns the backpack API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudBackpackApi* api)
    {
        api->Version = ApiVersion;
        api->GetBackpack = &GetBackpack;
        api->SelectBackpack = &SelectBackpack;
        api->ForceRefill = &ForceRefill;
        api->GiveBackpacks = &GiveBackpacks;
        return ApiVersion;
    }

    /// <summary>
    /// The panels for the local controller, as BackpackDriver.Drive decides them: the refill share while the fixed
    /// Refill entry (interaction 0) has the focus and can be used, else the backpack's three panels while it is the
    /// selected gadget, else nothing (the probe calls this).
    /// </summary>
    public static HudBackpackState Read(SPlayerController? pc, HudFeature? hud, List<HudBackpackRow> rows)
    {
        rows.Clear();
        HudBackpackState s = default;
        if (pc is not { bDeleteMe: false } || pc.SBPawn is not { bDeleteMe: false, Health: > 0 } pawn)
        {
            return s;
        }

        STeamInfo? team = pc.PlayerReplicationInfo?.Team as STeamInfo;
        s.Rows = team is not null ? Math.Clamp(team.iNBGadgets, 1, 4) : 4;
        if (hud is not null)
        {
            s.SelfLabelId = hud.Texts.Id(hud.Strings.Get("Magma", "A_BackPackSelf", "Element", "You"));
            s.MateLabelId = hud.Texts.Id(hud.Strings.Get("Magma", "A_BackPackFriend", "Nom_Friend", "Teammate"));
        }

        Span<int> charges = stackalloc int[4];
        Span<int> ids = stackalloc int[4];
        if (pc.IManager is { bDeleteMe: false } manager && manager.Interactions.Length > 0 &&
            manager.Interactions[0] is SBackPackInteraction { bHasFocus: true } interaction &&
            interaction.CanUse(pc) && interaction.Mate is { } mate)
        {
            // The share the mate carries for this player, against this player's own gadgets.
            s.Mode = 2;
            s.Panels = 1;
            s.MateANameId = NameId(hud, mate);
            MateCharges(mate, ReferenceEquals(mate.aBackPackOwner_A, pawn), charges, ids);
            // What a full share holds is the same for every merc of a side (GetBackPackCharge reads the owner's side and team size),
            // so the player's own backpack answers when the mate's gadgets are not replicated to this machine.
            SelfRows(rows, pawn, BackpackOf(mate) ?? BackpackOf(pawn), charges, s.Rows);
        }
        else if (pawn.SelectedGadget is SGadBackPack pack)
        {
            s.Mode = 1;
            s.Panels = 1;
            pack.aNbChargesGad_Self.AsSpan(0, 4).CopyTo(charges);
            SelfRows(rows, pawn, pack, charges, s.Rows);
            if (pawn.aBackPackOwner_A is { } a && pawn.aIDGad_Mate_A0 > 0)
            {
                s.Panels |= 2;
                s.MateANameId = NameId(hud, a);
                MateCharges(pawn, true, charges, ids);
                MateRows(rows, 1, team, pack, charges, ids, s.Rows);
            }

            if (pawn.aBackPackOwner_B is { } b && pawn.aIDGad_Mate_B0 > 0)
            {
                s.Panels |= 4;
                s.MateBNameId = NameId(hud, b);
                MateCharges(pawn, false, charges, ids);
                MateRows(rows, 2, team, pack, charges, ids, s.Rows);
            }
        }

        s.RowCount = rows.Count;
        return s;
    }

    private static int NameId(HudFeature? hud, SBasePawn pawn) =>
        hud?.Texts.Id((pawn.Controller?.PlayerReplicationInfo ?? pawn.PlayerReplicationInfo)?.PlayerName) ?? 0;

    /// <summary>The backpack among a pawn's gadgets, if he carries one.</summary>
    private static SGadBackPack? BackpackOf(SBasePawn pawn)
    {
        for (SGadget? g = pawn.MyGadget; g is not null; g = g.MyGadget)
        {
            if (g is SGadBackPack pack)
            {
                return pack;
            }
        }

        return null;
    }

    /// <summary>The four slots a backpack's owner carries for mate A or B: charges and gadget ids.</summary>
    private static void MateCharges(SBasePawn owner, bool a, Span<int> charges, Span<int> ids)
    {
        if (a)
        {
            (charges[0], charges[1], charges[2], charges[3]) = (owner.aNbChargesGad_Mate_A0, owner.aNbChargesGad_Mate_A1, owner.aNbChargesGad_Mate_A2, owner.aNbChargesGad_Mate_A3);
            (ids[0], ids[1], ids[2], ids[3]) = (owner.aIDGad_Mate_A0, owner.aIDGad_Mate_A1, owner.aIDGad_Mate_A2, owner.aIDGad_Mate_A3);
        }
        else
        {
            (charges[0], charges[1], charges[2], charges[3]) = (owner.aNbChargesGad_Mate_B0, owner.aNbChargesGad_Mate_B1, owner.aNbChargesGad_Mate_B2, owner.aNbChargesGad_Mate_B3);
            (ids[0], ids[1], ids[2], ids[3]) = (owner.aIDGad_Mate_B0, owner.aIDGad_Mate_B1, owner.aIDGad_Mate_B2, owner.aIDGad_Mate_B3);
        }
    }

    /// <summary>The own panel: the player's refillable gadgets in order (torches left out), as BackpackDriver.SelfIcons.</summary>
    private static void SelfRows(List<HudBackpackRow> rows, SBasePawn pawn, SGadBackPack? pack, ReadOnlySpan<int> charges, int count)
    {
        int row = 0;
        for (SGadget? g = pawn.MyGadget; g is not null && row < count; g = g.MyGadget)
        {
            if (!SBackPackInteraction.Refillable(g))
            {
                continue;
            }

            rows.Add(Row(0, row, SGadget.GetUniqueIDByClass(new ClassRef(g.GetUcClass().Path)), g.MagmaIconName, charges[row], pack, g));
            row++;
        }

        // Slots past the gadgets the player has keep the default icon, as the original's panel does.
        for (; row < count; row++)
        {
            rows.Add(Row(0, row, 0, DefaultIcon, charges[row], null, null));
        }
    }

    /// <summary>A mate's panel: each slot's gadget found by its id among the team's gadget classes, as BackpackDriver.Icons.</summary>
    private static void MateRows(List<HudBackpackRow> rows, int panel, STeamInfo? team, SGadBackPack pack, ReadOnlySpan<int> charges, ReadOnlySpan<int> ids, int count)
    {
        for (int row = 0; row < count; row++)
        {
            SGadget? gadget = GadgetOf(team, ids[row]);
            rows.Add(Row(panel, row, ids[row], gadget?.MagmaIconName ?? DefaultIcon, charges[row], pack, gadget));
        }
    }

    /// <summary>A gadget of the class with that id (from the team's class inventory), kept for the session's lifetime.</summary>
    private static SGadget? GadgetOf(STeamInfo? team, int id)
    {
        if (id <= 0 || team is null)
        {
            return null;
        }

        if (GadgetsById.TryGetValue(id, out SGadget? known))
        {
            return known;
        }

        foreach (ClassRef cls in team.ClassInventory)
        {
            if (SGadget.GetUniqueIDByClass(cls) != id || cls.Path is not { } path ||
                OpenScct.Game.Devices.DeviceClasses.Index.Find(path)?.Create() is not SGadget gadget)
            {
                continue;
            }

            GadgetsById[id] = gadget;
            return gadget;
        }

        // Not cached: the team's classes may not have arrived yet.
        return null;
    }

    private static HudBackpackRow Row(int panel, int row, int id, string icon, int charges, SGadBackPack? pack, SGadget? gadget)
    {
        HudBackpackRow r = default;
        r.Panel = panel;
        r.Row = row;
        r.GadgetId = id;
        r.Charges = charges;
        r.Max = pack is not null && gadget is not null ? pack.GetBackPackCharge(gadget) : -1;
        if (HudTextureLibrary.Current(out _) is { } library &&
            (library.Material(IconPackage, string.IsNullOrEmpty(icon) ? DefaultIcon : icon, out string texture, out int u0, out int v0, out int u1, out int v1) ||
             library.Material(IconPackage, DefaultIcon, out texture, out u0, out v0, out u1, out v1)))
        {
            r.SetNames(IconPackage, texture);
            (r.U0, r.V0, r.U1, r.V1) = (u0, v0, u1, v1);
        }

        return r;
    }

    /// <summary>What <see cref="SelectBackpack"/> does: the gadget keys' own selection, slot by slot, until the backpack is selected.</summary>
    public static bool Select(SPlayerController? pc)
    {
        if (pc?.SBPawn is not { } pawn)
        {
            return false;
        }

        for (int slot = 0; slot < 8 && pawn.SelectedGadget is not SGadBackPack; slot++)
        {
            pc.SelectGadget(slot);
        }

        return pawn.SelectedGadget is SGadBackPack;
    }

    /// <summary>
    /// One server tick of <see cref="ForceRefill"/>: the mate whose backpack holds a share for the player stands 110
    /// units in front of him, facing him; with <paramref name="drain"/> the player's first refillable gadget with
    /// charges loses one. Returns the mate's name, or null when no mate carries a share for him.
    /// </summary>
    public static string? HoldMate(ServerHost host, int playerId, bool drain)
    {
        SBasePawn? me = null;
        foreach (SPlayerController pc in host.World.Level.AllActors<SPlayerController>())
        {
            if (!pc.bDeleteMe && pc.PlayerReplicationInfo?.PlayerID == playerId && pc.Pawn is SBasePawn { Health: > 0 } p)
            {
                me = p;
            }
        }

        if (me is null)
        {
            return null;
        }

        SBasePawn? carrier = null;
        foreach (SBasePawn other in host.World.Level.AllActors<SBasePawn>())
        {
            if (!other.bDeleteMe && other.Health > 0 && !ReferenceEquals(other, me) &&
                ((ReferenceEquals(other.aBackPackOwner_A, me) && other.aIDGad_Mate_A0 > 0) ||
                 (ReferenceEquals(other.aBackPackOwner_B, me) && other.aIDGad_Mate_B0 > 0)))
            {
                carrier = other;
                break;
            }
        }

        if (carrier is null)
        {
            return null;
        }

        float yaw = me.Rotation.Yaw * (MathF.PI / 32768f);
        var forward = new FVector(MathF.Cos(yaw), MathF.Sin(yaw), 0f);
        FVector spot = me.Location + forward * 110f;
        carrier.SetLocation(spot);
        carrier.Velocity = default;
        carrier.Acceleration = default;
        var facing = new FRotator(0, (me.Rotation.Yaw + 32768) & 0xFFFF, 0);
        carrier.Rotation = facing;
        if (carrier.Controller is { } c)
        {
            c.Rotation = facing;
        }

        if (drain)
        {
            for (SGadget? g = me.MyGadget; g is not null; g = g.MyGadget)
            {
                if (SBackPackInteraction.Refillable(g) && g.Charge > 0)
                {
                    g.Charge -= 1;
                    break;
                }
            }
        }

        return (carrier.Controller?.PlayerReplicationInfo ?? carrier.PlayerReplicationInfo)?.PlayerName ?? "?";
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetBackpack(HudBackpackState* state, HudBackpackRow* rows, int capacity)
    {
        try
        {
            List<HudBackpackRow> list = _rows ??= new List<HudBackpackRow>(12);
            HudBackpackState read = default;
            if (Exports.Current is { IsDisposed: false } session)
            {
                if (_focusUntil > 0 && session.Session.LocalController is { bDeleteMe: false } pc)
                {
                    if (pc.Level.TimeSeconds >= _focusUntil)
                    {
                        _focusUntil = 0;
                    }
                    else
                    {
                        FocusRefill(pc);
                    }
                }

                read = Read(session.Session.LocalController, HudFeature.Of(session), list);
            }
            int n = rows == null ? 0 : Math.Min(read.RowCount, capacity);
            for (int i = 0; i < n; i++)
            {
                rows[i] = list[i];
            }

            read.RowCount = n;
            if (state != null)
            {
                *state = read;
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
    private static int SelectBackpack()
    {
        try
        {
            return Select(Exports.Current?.Session.LocalController is { bDeleteMe: false } pc ? pc : null) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ForceRefill(float seconds)
    {
        try
        {
            if (Exports.Current is not { IsRemote: false, IsDisposed: false } session || session.Session.Host is not { } host ||
                session.Session.LocalController?.PlayerReplicationInfo is not { } pri)
            {
                return 0;
            }

            Queue(host, pri.PlayerID, seconds);
            _focusUntil = session.Session.LocalController is { } local ? local.Level.TimeSeconds + seconds : 0;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// What <see cref="GiveBackpacks"/> does on the server's thread: a backpack spawned and given to the player's merc
    /// (and with <paramref name="allMercs"/> to every live pawn of his side) that has none. Returns how many were given.
    /// </summary>
    public static int GiveOnServer(ServerHost host, int playerId, bool allMercs)
    {
        SBasePawn? me = null;
        foreach (SPlayerController pc in host.World.Level.AllActors<SPlayerController>())
        {
            if (!pc.bDeleteMe && pc.PlayerReplicationInfo?.PlayerID == playerId && pc.Pawn is SBasePawn { Health: > 0 } p)
            {
                me = p;
            }
        }

        if (me is null)
        {
            return 0;
        }

        int given = 0;
        var backpack = new ClassRef("SBase.SGadBackPack");
        foreach (SBasePawn pawn in host.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe || pawn.Health <= 0 || pawn.SPawnType != me.SPawnType || (!allMercs && !ReferenceEquals(pawn, me)) ||
                BackpackOf(pawn) is not null)
            {
                continue;
            }

            // The kit's slots are full: the backpack takes the last selectable gadget's slot, as choosing it in that
            // slot of the gear would (a gadget past the team's slots cannot be selected).
            int selectable = 0;
            SGadget? last = null;
            for (SGadget? g = pawn.MyGadget; g is not null; g = g.MyGadget)
            {
                if (OpenScct.Game.Equipment.PawnInventory.IsSelectable(g))
                {
                    selectable++;
                    last = g;
                }
            }

            if (last is not null && selectable >= OpenScct.Game.Equipment.PawnInventory.TeamSlots(pawn))
            {
                OpenScct.Game.Equipment.PawnInventory.DeleteGadget(pawn, last);
            }

            if (OpenScct.Game.Devices.DeviceClasses.Spawn(pawn, backpack, pawn, default, pawn.Location, pawn.Rotation) is SGadget gadget)
            {
                gadget.GiveTo(pawn);
                given++;
            }
        }

        return given;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GiveBackpacks(int allMercs)
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
                    GiveOnServer(running, id, allMercs != 0);
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

    /// <summary>Client level time until which <see cref="GetBackpack"/> keeps the refill entry focused (a forced refill).</summary>
    private static double _focusUntil;

    /// <summary>
    /// The interaction menu's own selection (SelectFirstViewAbleItem, what s_MenuInteraction.BeginState runs when the
    /// player holds Use): with the refill entry usable it takes the focus, as it does when the player opens the menu next
    /// to the mate. Unattended checks only (a forced refill), so the share shows without a held key.
    /// </summary>
    public static void FocusRefill(SPlayerController pc)
    {
        if (pc.IManager is { bDeleteMe: false } manager && manager.Interactions.Length > 0 &&
            manager.Interactions[0] is SBackPackInteraction { bHasFocus: false } entry && entry.CanUse(pc))
        {
            manager.SelectFirstViewAbleItem();
        }
    }

    /// <summary>Runs <see cref="HoldMate"/> before each server tick for that long (the probe calls this too).</summary>
    public static void Queue(ServerHost host, int playerId, float seconds)
    {
        int ticks = 0;
        double until = -1;
        void Run(ServerHost running)
        {
            double now = running.World.Level.TimeSeconds;
            if (until < 0)
            {
                until = now + Math.Max(seconds, 0.1f);
            }

            if (now > until)
            {
                running.BeforeWorldTick -= Run;
                return;
            }

            try
            {
                HoldMate(running, playerId, ticks++ == 0);
            }
            catch (Exception)
            {
                // A check that fails shows as nothing happening; it must not take the server down.
                running.BeforeWorldTick -= Run;
            }
        }

        host.BeforeWorldTick += Run;
    }
}
