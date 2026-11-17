#pragma once

#include "CoreMinimal.h"

/**
 * The Performance page of the Options screen and what puts it into effect (registered and applied by
 * UScctDisplaySettings, Settings/ScctDisplaySettings.h):
 *
 *   Performance.VSync        vertical sync (UGameUserSettings, r.VSync)
 *   Performance.MaxFps       the frame rate limit: 30 to 500, or "Match refresh rate" (the slider's lowest notch,
 *                            the refresh rate of the monitor the window is on, followed when the window moves to
 *                            another one), or "Unlimited" (its highest). With vertical sync on and a limit at or
 *                            above the refresh rate the swap paces the frames and the limiter is off, as Godot's
 *                            DisplayFrameCap does it (a sleeping limiter at the swap's own rate misses refreshes).
 *   Performance.LowLatency   how far the game thread may run ahead (r.GTSyncType): Off, On (waits for the RHI
 *                            thread's last frame), Ultra (waits for the GPU's last flip)
 *   Performance.Renderer     DirectX 12 or Vulkan, written to the user's Engine.ini for the next start
 *
 * -ScctPerfCheck measures the frame rate over ten seconds once a match is playing and writes
 *   "ScctPerf: cap <limit>, measured ~<fps>" to the log, so a forced cap (-ScctSettingSet=Performance.MaxFps=30)
 *   can be checked unattended.
 */
namespace ScctPerformanceSettings
{
	void Register();

	/** Puts every Performance option into effect but the renderer (that is for the next start). bAtStart says so in the log. */
	void Apply(bool bAtStart);

	/** Writes the renderer choice to the user's Engine.ini, for the next start. */
	void WriteRenderer();

	bool Owns(FName Key);

	/** Every second or so: follows the window to another monitor's refresh rate; runs -ScctPerfCheck. */
	void Tick(float DeltaSeconds);

	/** The refresh rate of the monitor the game window is on, Hz (60 when it cannot be read). */
	float MonitorHz();

	/** The frame rate limit in effect now, 0 for none. */
	int32 EffectiveLimit();
}
