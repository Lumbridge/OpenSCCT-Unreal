#pragma once

#include "CoreMinimal.h"

class AActor;
class UPointLightComponent;
class UWorld;

/**
 * The lighting plan's dress of a level (scct.Light.Plan 1, Play/ScctLightPlan.h): what a level's lights, sky and
 * surfaces are turned into when a match starts on it, for an imported level and a level built at run time alike.
 * Nothing here runs with scct.Light.Plan 0.
 *
 * THE WORLD is lit by the original's baked light, whole: the surface material gives off its colour times the baked
 * term (Tools/Import/scct_materials.py, "the lighting plan"), switched and tuned through the parameter collection
 * MPC_ScctLight, which this file sets. Bounced and sky light reach a surface only as far as the original lit it.
 *
 * THE MAP'S LIGHTS are classified by the simulation's light table (Runtime/ScctLightPlanApi.h, EScctLightClass):
 *   projector        a live, shadowed light on world and characters
 *   dynamic          a live, shadowed light on the world: the only world light that goes when switched or shot
 *   baked            its light is in the lightmaps already: it lights no surface, and stays for the haze in the air
 *   characters only  never lit the world: off the world
 *   special lit      lights nothing
 * Pawns receive the baked and characters-only lamps through FScctPawnLighting, with an occlusion test per lamp.
 * Movers and props without baked light receive those lamps on channel 1. Channel 0 supplies live world lights.
 *
 * UNITS. A light's reach is the original's (25 x (LightRadius + 1)); it has no inverse square law. Its strength and
 * falloff exponent are solved so the original's display term (peak x S(d / R), S = 1 - 3x^2 + 2x^3) crosses 0.5 and
 * 0.15 at the same distances in the picture (FScctLightRules::Fit; Tools/Light/fit_falloff.py is the same sum, with
 * the errors it leaves). The engine's law for such a light is albedo x intensity x (1 - (d/R)^2)^exponent / pi.
 *
 * Console variables (read when a level is dressed, unless said):
 *   scct.Light.LightmapScale      what the stored baked light is multiplied by (2, as the original; followed live)
 *   scct.Light.BakedGain          a factor on the baked term (1; live)
 *   scct.Light.BakedSaturation    1 the baked light's own colour, 0 grey (1; live)
 *   scct.Light.BounceGate         the most bounced and sky light a lit surface takes, 0 to 1 (0.35; live)
 *   scct.Light.AmbientFloor       the share of the released picture's ambient floor that stays (0; live)
 *   scct.Light.LampGain           a factor on every map light's strength (1)
 *   scct.Light.LampSaturation     1 a map light's own colour, 0 grey (1)
 *   scct.Light.PeakCap            the most a light's own term shows at the light, in display units (2.5)
 *   scct.Light.CharacterLamps     1 enables ordinary actor lamps
 *   scct.Light.PawnShadow         1 suppresses ordinary lamps blocked on the path to the pawn, 0 uses retail visibility
 *   scct.Light.BodyAids           1: the own side's body fill and rim lights stay under the plan (0: removed)
 *   scct.Light.Moon               the share of the level's moon that stays on a map without a sun (0)
 *   scct.Light.SkyLight           the share of the level's sky light that stays (1: it reaches only what the gate lets it)
 *   scct.Light.FogInscatter       the share of the height fog's own glow that stays (0)
 *   scct.Light.ProjectorShadows   0: projectors cast no shadow (a check, not a look: light then passes walls)
 *   scct.Light.RulesLog           1: one log line per light as it is dressed
 */
namespace FScctLightRules
{
	/** A light's strength and falloff exponent in the engine's units. */
	struct FFit
	{
		float Intensity = 0.0f;
		float Exponent = 2.2f;
		/** The strength was held at the peak cap. */
		bool bCapped = false;
	};

	/** How the original's term falls from the light to its reach. */
	namespace EFalloff
	{
		/** An ordinary light: S(x) = 1 - 3x^2 + 2x^3. */
		constexpr int32 Smooth = 0;
		/** A projector with linear attenuation: 1 - x. */
		constexpr int32 Linear = 1;
		/** A projector without attenuation. */
		constexpr int32 Flat = 2;
		/** A projector with late attenuation: 1 up to 0.9 of the range, then to 0. */
		constexpr int32 Late = 3;
	}

	/** The two-contour solve for a light whose term is Peak (display units) at the light. Gain is scct.Light.LampGain, Cap scct.Light.PeakCap. */
	OPENSCCT_API FFit Fit(float Peak, int32 Falloff, float Cap, float Gain);

	/**
	 * Dresses the level for the plan: the parameter collection, the map's lights by class, the sky, the moon, the fog
	 * and the actor-lit meshes. Once per match start, after the level's lights are indexed by their tags and before
	 * any is bound (the level state multiplies the strength it finds then). NameOf gives a world name id's name.
	 * False while the simulation's light table is not there yet (ask again; nothing but the surfaces is dressed then).
	 */
	bool DressLevel(UWorld* World, const TMap<FName, TWeakObjectPtr<AActor>>& TaggedLights, TFunctionRef<const FString&(int32)> NameOf);

	/** Follows the console variables marked live; every frame of a match under the plan. */
	void Tick(UWorld* World);

	/** A map light's colour for a display colour the simulation gives it mid-match (its strength stays as dressed). */
	FLinearColor LightColour(const FLinearColor& Display);

	/** Apply the flare emitter's current brightness to a movable world light. */
	void SetFlareLight(UPointLightComponent& Light, float Brightness, const FLinearColor& Display);

	/** Ordinary actor lamps are enabled under the plan. */
	OPENSCCT_API bool BodiesOnLampChannel();

	/** The own side's body fill and rim lights are wanted (always without the plan). */
	bool BodyAids();
}
