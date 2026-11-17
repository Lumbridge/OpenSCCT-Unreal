using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Hud;

namespace OpenScct.UnrealBridge;

/// <summary>What a teammate's name label carries (<see cref="TeammateNameRow.Flags"/>). The header mirrors it.</summary>
[Flags]
public enum TeammateNameFlags
{
    /// <summary>The teammate is knocked out (SBasePawn.bSleeping, which replicates with the pawn).</summary>
    Sleeping = 1,

    /// <summary>The teammate carries a backpack (an SBackPack mesh whose Instigator he is; replicated to everyone).</summary>
    Backpack = 2,

    /// <summary>The backpack still holds charges of the local player's own share.</summary>
    BackpackSupplies = 4,

    /// <summary>The teammate is a mercenary (sleeping and backpack marks are the mercs' only, as in the Godot client).</summary>
    Merc = 8,
}

/// <summary>
/// One teammate a name label goes over (<see cref="TeammateNamesApi.GetTeammates"/>).
/// Source/OpenSCCT/Runtime/ScctTeammateNamesApi.h mirrors it field for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct TeammateNameRow
{
    /// <summary>The pawn's actor id, as the bridge's pawn list (GetPawns) has it, so Unreal places the label on the pawn it draws.</summary>
    public int ActorId;

    /// <summary>The player's name as a HUD text id (FScctHudApi::GetText).</summary>
    public int NameId;

    /// <summary>The pawn's health as replicated from the authority (no maximum or percentage is inferred).</summary>
    public int Health;

    /// <summary><see cref="TeammateNameFlags"/>.</summary>
    public int Flags;

    /// <summary>The pawn's location at the last tick and its collision height (the label sits 18 units over it).</summary>
    public float X, Y, Z;

    public float CollisionHeight;
}

/// <summary>
/// The teammate names' table of entry points: the Godot client's HudTeammateNames (game/Scripts/Hud/HudTeammateNames.cs)
/// without its drawing. Unreal asks for <see cref="GetApi"/> once
/// (<c>Host.GetFunction("OpenScct.UnrealBridge.TeammateNamesExports", "GetApi")</c>).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct TeammateNamesApi
{
    public int Version;

    /// <summary>
    /// GetTeammates(rows, capacity): the local player's living teammates of his own pawn type with a player name, the
    /// local pawn and spy clones left out; returns the count written. 0 while the player is dead, spectating, the game
    /// has ended or the round is a Merc Deathmatch (no friends there), as the Godot client draws none then.
    /// </summary>
    public delegate* unmanaged[Cdecl]<TeammateNameRow*, int, int> GetTeammates;
}

public static unsafe class TeammateNamesExports
{
    public const int ApiVersion = 1;

    [ThreadStatic]
    private static List<TeammateNameRow>? _rows;

    /// <summary>Fills <paramref name="api"/>. Returns the table's version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(TeammateNamesApi* api)
    {
        api->Version = ApiVersion;
        api->GetTeammates = &GetTeammates;
        return ApiVersion;
    }

    /// <summary>The rows for the local controller, by HudTeammateNames.Draw's rules (the probe calls this).</summary>
    public static void Read(SPlayerController? pc, HudFeature? hud, List<TeammateNameRow> rows)
    {
        rows.Clear();
        if (pc?.Pawn is not SBasePawn { Health: > 0, bDeleteMe: false } own || pc.bGameEnded || pc.IsSpectating() || pc.IsMercDeathMatch)
        {
            return;
        }

        bool merc = own.SPawnType == PawnType.SPAWNDEFENSE;
        foreach (SBasePawn pawn in pc.XLevel.AllActors<SBasePawn>())
        {
            if (pawn is OpenScct.Game.Svm.SpyClone || ReferenceEquals(pawn, own) || pawn.bDeleteMe || pawn.Health <= 0 ||
                pawn.SPawnType != own.SPawnType || pawn.PlayerReplicationInfo is not { bOnlySpectator: false } pri ||
                string.IsNullOrWhiteSpace(pri.PlayerName))
            {
                continue;
            }

            TeammateNameFlags flags = merc ? TeammateNameFlags.Merc : 0;
            if (merc)
            {
                if (pawn.bSleeping)
                {
                    flags |= TeammateNameFlags.Sleeping;
                }

                if (HasBackpack(pc, pawn))
                {
                    flags |= TeammateNameFlags.Backpack;
                    if (HasBackpackSupplies(pawn, own))
                    {
                        flags |= TeammateNameFlags.BackpackSupplies;
                    }
                }
            }

            rows.Add(new TeammateNameRow
            {
                ActorId = pawn.ActorId,
                NameId = hud?.Texts.Id(pri.PlayerName) ?? 0,
                Health = pawn.Health,
                Flags = (int)flags,
                X = pawn.Location.X,
                Y = pawn.Location.Y,
                Z = pawn.Location.Z,
                CollisionHeight = pawn.CollisionHeight,
            });
        }
    }

    /// <summary>Other players' gadget inventories are owner-only; the carried mesh replicates its carrier to everyone.</summary>
    private static bool HasBackpack(SPlayerController pc, SBasePawn pawn)
    {
        foreach (SBackPack backpack in pc.XLevel.AllActors<SBackPack>())
        {
            if (ReferenceEquals(backpack.Instigator, pawn))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>Each viewer has a share of his own; a full inventory does not mean that share is spent.</summary>
    private static bool HasBackpackSupplies(SBasePawn carrier, SBasePawn viewer) =>
        (ReferenceEquals(carrier.aBackPackOwner_A, viewer) &&
            (carrier.aNbChargesGad_Mate_A0 > 0 || carrier.aNbChargesGad_Mate_A1 > 0 ||
             carrier.aNbChargesGad_Mate_A2 > 0 || carrier.aNbChargesGad_Mate_A3 > 0)) ||
        (ReferenceEquals(carrier.aBackPackOwner_B, viewer) &&
            (carrier.aNbChargesGad_Mate_B0 > 0 || carrier.aNbChargesGad_Mate_B1 > 0 ||
             carrier.aNbChargesGad_Mate_B2 > 0 || carrier.aNbChargesGad_Mate_B3 > 0));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetTeammates(TeammateNameRow* rows, int capacity)
    {
        try
        {
            List<TeammateNameRow> list = _rows ??= new List<TeammateNameRow>(8);
            list.Clear();
            if (Exports.Current is { IsDisposed: false } session)
            {
                Read(session.Session.LocalController, HudFeature.Of(session), list);
            }

            int n = rows == null ? 0 : Math.Min(list.Count, capacity);
            for (int i = 0; i < n; i++)
            {
                rows[i] = list[i];
            }

            return n;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
