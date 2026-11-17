#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The pose's tie to the frame's camera (Runtime/ScctPoseAimApi.h): the local spy's third-person body aims at the view
 * drawn this frame, so the arm and pistol keep up with the mouse instead of trailing the camera by a tick; the spy's
 * eye view hides the sockets a wall stands in front of; the merc's view model clips run at their own frame rates.
 *
 * Unattended switch (off by default; for runs with nobody at the keyboard):
 *   -ScctAimProbe   log the local spy's posed aim against the camera while the view turns ("Aim probe: ..."), the
 *                   upper body's view yaw as drawn and as the ticks alone would give it, and every two seconds
 *                   the mean lag of each; with -ScctAutoplay="wait 3;turn 1440 12" the player turns steadily
 */
namespace ScctPoseAim
{
	/** Before this frame's poses: hands the local spy LocalActorId the view drawn this frame (0: none). */
	void FeedDrawnView(UWorld& World, int32 LocalActorId);

	/** After this frame's poses: the -ScctAimProbe lines. */
	void LogAimProbe(UWorld& World, int32 LocalActorId, float DeltaSeconds);

	/** After GetFirstPerson: the spy eye view's sockets a wall hides (EScctHiddenSocket bits). */
	int32 ReadHiddenSockets();

	/** The muzzle socket's distance from the eye that ReadHiddenSockets last read, cm. */
	float LastMuzzleDistance();

	/** After GetFirstPerson: logs each view model clip the first time it plays, with the frame rate it runs at. */
	void LogViewModelClipRates();
}
