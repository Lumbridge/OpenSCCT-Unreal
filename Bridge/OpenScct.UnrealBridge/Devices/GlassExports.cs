using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The glass check of -ScctLevelDo "breakglass" (Source/OpenSCCT/Runtime/ScctGlassApi.h mirrors it): breaks one pane
/// on the local match's server with a hit at a given point and direction, as a shot does, so the pane records that
/// impact (GlassImpactLocation, GlassImpactDirection, GlassImpactSerial) and every machine shapes the same shards
/// around it. Unreal picks the point on the pane it draws. It can also stand the player beside a pane first, since a
/// pane's state reaches a client only while the pane is relevant to its player. Nothing in normal play calls these; on
/// a remote round they do nothing and return 0.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct GlassApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, float, float, float, int> BreakPane;
    public delegate* unmanaged[Cdecl]<float, float, float, int> StandBeside;
}

public static unsafe class GlassExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(GlassApi* api)
    {
        api->Version = ApiVersion;
        api->BreakPane = &BreakPane;
        api->StandBeside = &StandBeside;
        return ApiVersion;
    }

    /// <summary>
    /// Stands the local player's pawn on the server beside the pane the map placed nearest (px, py, pz), 120 uu off it
    /// along the first axis where it fits. A pane's state reaches a client only while the pane is relevant to that
    /// client's player, so a pane broken far from the player would not break on screen. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StandBeside(float px, float py, float pz)
    {
        var at = new FVector(px, py, pz);
        FVector local;
        try
        {
            // Read on the calling thread; an exception must not leave an unmanaged entry point.
            local = Exports.Current is { IsDisposed: false } session ? session.Session.LocalPawn?.Location ?? default : default;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }

        return WorldDebugExports.OnServer(host =>
        {
            Pawn? player = null;
            SVitre? pane = null;
            foreach (Pawn p in host.World.Level.AllActors<Pawn>())
            {
                if (!p.bDeleteMe && p.Controller is PlayerController &&
                    (player is null || FVector.DistSquared(p.Location, local) < FVector.DistSquared(player.Location, local)))
                {
                    player = p;
                }
            }

            foreach (SVitre v in host.World.Level.AllActors<SVitre>())
            {
                if (!v.bDeleteMe && (pane is null || FVector.DistSquared(v.Location, at) < FVector.DistSquared(pane.Location, at)))
                {
                    pane = v;
                }
            }

            if (player is null || pane is null)
            {
                return;
            }

            foreach (FVector offset in new[] { new FVector(120f, 0f, 20f), new FVector(-120f, 0f, 20f), new FVector(0f, 120f, 20f), new FVector(0f, -120f, 20f) })
            {
                if (player.SetLocation(pane.Location + offset))
                {
                    return;
                }
            }
        });
    }

    /// <summary>
    /// Breaks the unbroken pane (SVitre) the map placed at (px, py, pz) (its Location, within 2 uu) with one heavy hit
    /// at (hx, hy, hz) going along (dx, dy, dz), on the server's next world step. The server's actors carry no map
    /// names, so the pane is found by where it stands. Returns 1 when queued, 0 without a local server.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int BreakPane(float px, float py, float pz, float hx, float hy, float hz, float dx, float dy, float dz)
    {
        var at = new FVector(px, py, pz);
        var hit = new FVector(hx, hy, hz);
        var direction = new FVector(dx, dy, dz);
        return WorldDebugExports.OnServer(host =>
        {
            SVitre? found = null;
            float best = 4f;
            foreach (SVitre pane in host.World.Level.AllActors<SVitre>())
            {
                float d = FVector.DistSquared(pane.Location, at);
                if (!pane.bDeleteMe && !pane.bAlreadyDestroyed && pane.bDamageable && d < best)
                {
                    best = d;
                    found = pane;
                }
            }

            if (found is null)
            {
                return;
            }

            // The momentum gives the pane its impact direction (SGamePlayObject.TakeDamage), as a bullet's does.
            SDamageType type = found.TakeDamageTypes.Count > 0 ? found.TakeDamageTypes[0] : SDamageType.SDTBase;
            found.eventTakeDamage(10000, null, hit, direction.SafeNormal() * 300f, type);
        });
    }
}
