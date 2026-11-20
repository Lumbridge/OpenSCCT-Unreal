using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct PawnCarriedLookApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, int> GetPattern;
}

/// <summary>Supplies the holstered weapon's pattern in the same name table as the pawn's selected look.</summary>
public static unsafe class PawnCarriedLookExports
{
    [UnmanagedCallersOnly]
    public static int GetApi(PawnCarriedLookApi* api)
    {
        if (api == null) return 0;
        api->Version = 1;
        api->GetPattern = &GetPattern;
        return api->Version;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPattern(int actorId)
    {
        try
        {
            if (Exports.Current is not { IsDisposed: false } session ||
                session.Session.Client.World.Level.FindActor(actorId) is not SPAWNDEFENSE { bDeleteMe: false } pawn) return 0;
            var looks = PawnLooks.Of(session);
            return looks.Id(PawnLooks.PatternOf(PawnLooks.GunItem(pawn, looks.LookOf(pawn), carried: true)));
        }
        catch (Exception ex) { Exports.ReportFailure(ex.ToString()); return 0; }
    }
}
