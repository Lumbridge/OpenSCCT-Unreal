#pragma once

#include "CoreMinimal.h"

// The fixture levels the named tests run on. Each is made by a script in Tools/Test, not by hand.
namespace ScctTestLevels
{
	/**
	 * TestBox: OpenSCCT's synthetic test map (a walled box with four player starts, no original content). The bridge
	 * ships its simulation files in Binaries/Bridge/maps/TestBox; the Unreal level is drawn from the same fixture by
	 * Tools/Test/make_testbox.py. The bridge still reads the shared character files from the content folder, so a run
	 * on it needs -ScctContent, but none of the original maps.
	 */
	inline const TCHAR* const TestBoxMap = TEXT("TestBox");
	inline const TCHAR* const TestBoxLevel = TEXT("/Game/OpenSCCT/Test/TestBox");
}
