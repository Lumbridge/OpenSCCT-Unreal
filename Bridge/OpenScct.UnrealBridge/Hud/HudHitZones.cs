using OpenScct.Core;
using OpenScct.Game.Combat;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// Gives the client's level its own hit zone tracer. A level without an <see cref="IHitZones"/> service traces through
/// the shared <see cref="TraceHitZones.Instance"/>, whose shape list is reused by every trace; in a local match the
/// in-process server ticks on its own thread, so the merc's laser sight traced on both sides at once (the client's
/// SGadTorcheLaser tick and the server's) and the match failed with "Collection was modified" within seconds of the
/// laser finding a pawn. The laser's target is what the HUD's own tracker and the target name hold, so the HUD sets this
/// up on its first tick of a level: the tracer is built exactly as the shared one is, only not shared.
/// </summary>
public static class HudHitZones
{
    /// <summary>Puts a tracer of its own on <paramref name="level"/> when it has none.</summary>
    public static void Ensure(Level? level)
    {
        if (level is not null && level.GetService<IHitZones>() is null)
        {
            level.SetService<IHitZones>(new TraceHitZones(EstimatedPillPose.Instance));
        }
    }
}
