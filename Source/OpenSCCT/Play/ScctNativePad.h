#pragma once

#include "CoreMinimal.h"

class IInputProcessor;
class ULocalPlayer;

#if PLATFORM_WINDOWS
TSharedRef<IInputProcessor> MakeScctNativePad(ULocalPlayer* Player);
#endif
