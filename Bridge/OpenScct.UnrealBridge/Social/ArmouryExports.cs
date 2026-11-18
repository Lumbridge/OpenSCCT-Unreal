using System;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Net.Cosmetics;
using OpenScct.UnrealBridge.Pose;
using OpenScct.UnrealBridge.Social;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The Armoury's table (Source/OpenSCCT/Runtime/ScctArmouryApi.h mirrors it): the cosmetics catalogue with the
/// account's unlocks, the chosen look per side, the appearance sync's status and its conflict choices (Social/Appearance.cs).
/// Every entry catches its exceptions. Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ArmouryApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<ArmouryInfoBlock*, int> GetInfo;
    public delegate* unmanaged[Cdecl]<int, ArmouryItemBlock*, char*, int, char*, int, char*, int, int> GetItem;
    public delegate* unmanaged[Cdecl]<int, int, int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<int, char*, void> SetChosen;
    public delegate* unmanaged[Cdecl]<int, int, char*, int, int> Choose;
    public delegate* unmanaged[Cdecl]<int, int, void> Command;
}

public static unsafe class ArmouryExports
{
    public const int ApiVersion = 1;

    private static AccountService Account => SocialBridge.Instance.Account;

    [UnmanagedCallersOnly]
    public static int GetApi(ArmouryApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetInfo = &GetInfo;
        api->GetItem = &GetItem;
        api->GetText = &GetText;
        api->SetChosen = &SetChosen;
        api->Choose = &Choose;
        api->Command = &Command;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetInfo(ArmouryInfoBlock* info)
    {
        if (info == null)
        {
            return 0;
        }

        try
        {
            AccountService a = Account;
            *info = new ArmouryInfoBlock
            {
                Revision = a.Revision,
                Flags = (AccountService.MasterUrl is not null ? 1 : 0) | (a.Account is not null ? 2 : 0) | (a.AppearanceManaged ? 4 : 0) |
                    (a.AppearanceConflict ? 8 : 0) | (PawnLooks.HideMine ? 16 : 0),
                Items = CosmeticCatalog.All.Count,
                TitleSerial = a.AppearanceTitleSerial,
            };
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            *info = default;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetItem(int index, ArmouryItemBlock* item, char* id, int idCapacity, char* name, int nameCapacity, char* tip, int tipCapacity)
    {
        try
        {
            if (!Account.Cosmetic(index, out ArmouryItemBlock block, out string i, out string n, out string t))
            {
                return 0;
            }

            if (item != null)
            {
                *item = block;
            }

            OnlineTextCopy.Buffer(id, idCapacity, i);
            OnlineTextCopy.Buffer(name, nameCapacity, n);
            OnlineTextCopy.Buffer(tip, tipCapacity, t);
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Kind 0 the sync's status; 1 the chosen ids of side a, joined by commas; 2 the name of slot b of side a; 3 the
    /// slots of side a (numbers joined by commas, in the page's order); 4 the look of side a sent to servers (hex);
    /// 5 the account's title to store; 6 every chosen id of both sides joined by "+" (the own look for "hide mine").
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, int a, int b, char* buffer, int capacity)
    {
        try
        {
            CosmeticSide side = a == 1 ? CosmeticSide.Merc : CosmeticSide.Spy;
            string text = kind switch
            {
                0 => Account.AppearanceStatus,
                1 => string.Join(",", ArmouryLook.Of(side)),
                2 => CosmeticCatalog.SlotName((CosmeticSlot)b, side),
                3 => string.Join(",", CosmeticCatalog.SlotsOf(side).Select(s => ((int)s).ToString(System.Globalization.CultureInfo.InvariantCulture))),
                4 => ArmouryLook.Shared(side).ToHex(),
                5 => Account.AppearanceTitle,
                6 => string.Join("+", ArmouryLook.Spy.Concat(ArmouryLook.Merc)),
                _ => string.Empty,
            };
            return OnlineTextCopy.Buffer(buffer, capacity, text);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>The saved choice of a side at start (ids joined by commas).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetChosen(int side, char* ids)
    {
        try
        {
            Account.SetChosenCosmetics(side == 1 ? CosmeticSide.Merc : CosmeticSide.Spy, ids == null ? string.Empty : new string(ids));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    /// <summary>Puts an item (null or empty id: retail) on a slot of a side. Returns 1 when taken; 0 for a locked item unless forced.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Choose(int side, int slot, char* id, int force)
    {
        try
        {
            return Account.ChooseCosmetic(side == 1 ? CosmeticSide.Merc : CosmeticSide.Spy, (CosmeticSlot)slot, id == null ? null : new string(id), force != 0) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>0 reload the account's appearance, 1 reapply this PC's choices, 2 ask the master for the account, 3 "hide my cosmetics" (value).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Command(int command, int value)
    {
        try
        {
            switch (command)
            {
                case 0: Account.ReloadAppearance(); break;
                case 1: Account.ReapplyAppearance(); break;
                case 2: Account.Refresh(); break;
                case 3: PawnLooks.HideMine = value != 0; break;
                case 4: Account.CancelPreparedJoin(); break;
            }
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }
}
