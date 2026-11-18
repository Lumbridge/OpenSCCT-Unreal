#include "Play/ScctLookStickHold.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"

void FScctLookStickHold::ReadSwitch()
{
	FString Text;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ScctForceRightStick="), Text, false))
	{
		return;
	}
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() < 2)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("-ScctForceRightStick wants X,Y[,Seconds]; \"%s\" is ignored"), *Text);
		return;
	}
	bForced = true;
	Forced = FVector2f(FMath::Clamp(FCString::Atof(*Parts[0]), -1.0f, 1.0f), FMath::Clamp(FCString::Atof(*Parts[1]), -1.0f, 1.0f));
	ForcedSeconds = Parts.Num() > 2 ? FMath::Max(FCString::Atof(*Parts[2]), 0.0f) : -1.0f;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctLookHold: -ScctForceRightStick reports the right stick at (%.2f, %.2f)%s"), Forced.X, Forced.Y,
		ForcedSeconds < 0.0f ? TEXT(" for good") : *FString::Printf(TEXT(" until %.2f s after the ping wheel closes"), ForcedSeconds));
}

void FScctLookStickHold::Force(float& RightX, float& RightY) const
{
	if (!bForced)
	{
		return;
	}
	const bool bLetGo = ForcedSeconds >= 0.0f && SinceClosed >= ForcedSeconds;
	RightX = bLetGo ? 0.0f : Forced.X;
	RightY = bLetGo ? 0.0f : Forced.Y;
}

void FScctLookStickHold::Reset()
{
	bWasTaken = false;
	Remaining = 0.0f;
}

void FScctLookStickHold::Step(bool bTaken, float Deadzone, float DeltaSeconds, float& RightX, float& RightY)
{
	if (bWasTaken && !bTaken)
	{
		Remaining = RecentreSeconds;
		Held = 0.0f;
		SinceClosed = 0.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLookHold: the ping wheel closed with the look stick at (%.2f, %.2f); the stick does not turn the view until it recentres (%.1f s at most)"),
			RightX, RightY, RecentreSeconds);
	}
	else if (SinceClosed >= 0.0f)
	{
		SinceClosed += DeltaSeconds;
	}
	bWasTaken = bTaken;
	if (bTaken)
	{
		Remaining = 0.0f;
		SinceClosed = -1.0f;
		return;
	}
	if (Remaining <= 0.0f)
	{
		return;
	}
	// The stick is in the middle inside its dead zone, which is radial as the simulation's is.
	if (FVector2f(RightX, RightY).Size() <= Deadzone)
	{
		Remaining = 0.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLookHold: the look stick recentred after %.2f s and turns the view again"), Held);
		return;
	}
	Remaining -= DeltaSeconds;
	Held += DeltaSeconds;
	if (Remaining <= 0.0f)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLookHold: the look stick was still at (%.2f, %.2f) after %.2f s; it turns the view again from the next frame"), RightX, RightY, Held);
	}
	RightX = 0.0f;
	RightY = 0.0f;
}
