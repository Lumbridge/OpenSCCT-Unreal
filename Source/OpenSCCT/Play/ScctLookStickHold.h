#pragma once

#include "CoreMinimal.h"

/**
 * The look stick after the team ping wheel closes (Godot's GamepadInput, _lookHold).
 *
 * On a controller the right stick chooses on the wheel, so it is still pushed over when the wheel closes. It turns
 * the view again only once it is back in the middle (inside its dead zone), or after RecentreSeconds for a stick
 * held on purpose. The mouse is not held back.
 *
 * UNATTENDED SWITCH (off by default; a run without a controller):
 *   -ScctForceRightStick=X,Y[,Seconds]
 * reports the right stick at X,Y (each -1 to 1) from the start; Seconds after the wheel closes the stick is reported
 * in the middle again (left out: it stays pushed). Each hold's start and end is a LogOpenSCCT line "ScctLookHold: ...".
 */
struct FScctLookStickHold
{
	/** The longest the look stick is ignored after the wheel closes while it has not recentred. */
	static constexpr float RecentreSeconds = 0.5f;

	/** Reads -ScctForceRightStick. */
	void ReadSwitch();

	/**
	 * Once a frame with play input, after the look sink had the frame. bTaken: the sink took the look (the wheel is
	 * open). Zeroes RightX and RightY while the stick is held back.
	 */
	void Step(bool bTaken, float Deadzone, float DeltaSeconds, float& RightX, float& RightY);

	/** The forced stick, where the switch is given; before the sink sees the frame. */
	void Force(float& RightX, float& RightY) const;

	/** Play input went away (menu, no focus): nothing is held over. */
	void Reset();

private:
	bool bWasTaken = false;
	float Remaining = 0.0f;
	float Held = 0.0f;

	bool bForced = false;
	FVector2f Forced = FVector2f::ZeroVector;
	float ForcedSeconds = -1.0f;   // after the wheel closed; below 0: for good
	float SinceClosed = -1.0f;     // below 0: the wheel has not closed yet
};
