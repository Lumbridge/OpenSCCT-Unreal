using OpenScct.Core.Engine;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.LightPlan;

internal static class FlareLighting
{
    /// <summary>The Unreal renderer doubles the reach of a gadget flare. World reach is proportional to radius + 1.</summary>
    public static float RenderRadius(Actor actor) => actor is SFlareEmitter or SFlareCourseEmitter
        ? 2f * (actor.LightRadius + 1f) - 1f
        : actor.LightRadius;
}
