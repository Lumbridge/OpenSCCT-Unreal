using System;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Hub;

// The pads table, called through its pointers as Unreal calls it, in the hosted hub of the social mode: every pad with
// its state, match, theme kit, sign and picture (the map's list picture must be a file on this PC for the pad to show
// it), and the player's queue line.
internal static unsafe partial class Probe
{
    private static bool Pads()
    {
        HubPadsApi api = default;
        delegate* unmanaged<HubPadsApi*, int> getApi = &HubPadsExports.GetApi;
        getApi(&api);
        HubPadsState state;
        api.GetState(&state);
        var rows = new HubPadRow[8];
        int count;
        fixed (HubPadRow* r = rows)
        {
            count = api.GetPads(r, rows.Length);
        }

        Console.WriteLine($"pads: valid {state.Valid}, {count} pads, queue side {state.QueueSide}, flags 0x{state.Flags:x}");
        int pictures = 0;
        for (int i = 0; i < Math.Min(count, rows.Length); i++)
        {
            fixed (HubPadRow* p = &rows[i])
            {
                bool picture = (p->Flags & HubPadsFlags.HasPicture) != 0;
                pictures += picture ? 1 : 0;
                Console.WriteLine($"  {new string(p->Id)}: state {p->State} {new string(p->Map)} '{new string(p->Title)}' {new string(p->ModeName)} {p->Spies}/{p->SpySlots} spies {p->Mercs}/{p->MercSlots} mercs,"
                    + $" theme {new string(p->Theme)}, colour {p->Rgba:X8}, floor ({p->LoX:0},{p->LoY:0})-({p->HiX:0},{p->HiY:0}) z {p->FloorZ:0},"
                    + $" sign {((p->Flags & HubPadsFlags.HasSign) != 0 ? $"({p->SignX:0},{p->SignY:0},{p->SignZ:0}){((p->Flags & HubPadsFlags.SignMounted) != 0 ? " mounted" : string.Empty)}" : "none")},"
                    + $" picture {(picture ? new string(p->Picture) : "none")}");
            }
        }

        if (count < 4)
        {
            Console.WriteLine("FAIL: the hub's four pads are not in the pads table");
            return false;
        }

        if (pictures == 0)
        {
            Console.WriteLine("WARNING: no pad has a map picture on this PC (the catalog's PreviewImage is missing)");
        }

        return true;
    }
}
