#pragma once

#include "CoreMinimal.h"

/**
 * The Performance options the settings page draws at the end of its Video tab, and what puts it into effect (registered and applied by
 * UScctDisplaySettings, Settings/ScctDisplaySettings.h):
 *
 * Vertical sync and the frame limit are the settings page's (UGameUserSettings, its Video tab); this page has:
 *
 *   Performance.LowLatency   how far the game thread may run ahead (r.GTSyncType): Off, On (waits for the RHI
 *                            thread's last frame), Ultra (waits for the GPU's last flip)
 *   Performance.Renderer     DirectX 12 or Vulkan, written to the user's Engine.ini for the next start
 *
 * -ScctPerfCheck measures the frame rate over ten seconds once a match is playing and writes
 *   "ScctPerf: cap <limit>, measured ~<fps>" to the log, with the frame limit in effect.
 */
namespace ScctPerformanceSettings
{
	void Register();

	/** Puts every Performance option into effect but the renderer (that is for the next start). bAtStart says so in the log. */
	void Apply(bool bAtStart);

	/** Writes the renderer choice to the user's Engine.ini, for the next start. */
	void WriteRenderer();

	bool Owns(FName Key);

	/** Runs -ScctPerfCheck. */
	void Tick(float DeltaSeconds);

	/** The refresh rate of the monitor the game window is on, Hz (60 when it cannot be read). */
	float MonitorHz();

	/** The frame rate limit in effect now, 0 for none. */
	int32 EffectiveLimit();
}
