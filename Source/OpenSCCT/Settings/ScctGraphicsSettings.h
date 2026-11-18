#pragma once

#include "CoreMinimal.h"

/**
 * The Graphics options the settings page draws at the end of its Video tab, and what puts it into effect (registered and applied by
 * UScctDisplaySettings, Settings/ScctDisplaySettings.h):
 *
 * The overall quality, its groups and the render scale are the settings page's (UGameUserSettings and
 * UScctUserSettings, its Video tab); this page has what it has no row for:
 *
 *   Graphics.AutoScaleFps        automatic render scale: dynamic resolution lowers the 3D resolution, down to 50%,
 *                                while frames take longer than the chosen rate allows, the render scale being the most
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

	/** Seeds a new player's quality groups and render scale before startup writes the first video settings. */
	void DetectFirstLaunch();

	/** Puts every Graphics option into effect. */
	void Apply();

	/** True for the keys of this page. */
	bool Owns(FName Key);
}
