#pragma once

#include "CoreMinimal.h"

/**
 * The Graphics page of the Options screen and what puts it into effect (registered and applied by
 * UScctDisplaySettings, Settings/ScctDisplaySettings.h):
 *
 *   Graphics.Quality             overall quality, Low to Cinematic: Unreal's scalability level for every group
 *   Graphics.ViewDistance ...    each scalability group on its own (view distance, shadows, lighting, reflections,
 *                                post-processing, textures, effects, shading), or "Same as overall"
 *   Graphics.RenderScale         the 3D picture's resolution against the screen's, 50 to 100% (the scalability
 *                                resolution, r.ScreenPercentage); the HUD and menus stay sharp
 *   Graphics.AutoScaleFps        automatic render scale: dynamic resolution lowers the 3D resolution, down to 50%,
 *                                while frames take longer than the chosen rate allows, render scale being the most
 *   Graphics.AntiAliasing        TSR, TAA, FXAA or none (r.AntiAliasingMethod)
 *   Graphics.TextureFiltering    trilinear to 16x anisotropic (r.MaxAnisotropy)
 *   Graphics.AmbientOcclusion    contact shading: screen-space AO and Lumen's short-range AO
 *
 * The values go through UGameUserSettings (ApplyNonResolutionSettings) and console variables set at the game
 * setting priority, so they win over the scalability groups and the project's defaults but not over a console
 * command typed while playing.
 */
namespace ScctGraphicsSettings
{
	void Register();

	/** Puts every Graphics option into effect. */
	void Apply();

	/** True for the keys of this page. */
	bool Owns(FName Key);
}
