#include "Hub/ScctHubVoice.h"

#include "Hub/ScctHubActivities.h"
#include "Hub/ScctHubSubsystem.h"
#include "OpenSCCT.h"

namespace
{
	constexpr float UpdateSeconds = 0.1f;
	constexpr int32 MaxSpeakers = 64;

	// ProximityVoice: the low-pass in the open and through a wall.
	constexpr float OpenHz = 20000.0f;
	constexpr float OccludedHz = 1200.0f;
}

FScctHubVoice::FScctHubVoice(UScctHubSubsystem* InOwner, TSharedPtr<FScctHubActivities> InActivities)
	: Owner(InOwner)
	, Activities(MoveTemp(InActivities))
{
}

void FScctHubVoice::Stop()
{
	Speakers.Reset();
	SinceUpdate = 0.0f;
}

void FScctHubVoice::Tick(UWorld* World, float DeltaSeconds, bool bVerbose)
{
	if (!Activities.IsValid() || !Activities->IsValid() || Owner == nullptr)
	{
		return;
	}
	const FScctHubActivityApi* Api = Activities->GetApi();
	SinceUpdate += DeltaSeconds;
	if (Api == nullptr || SinceUpdate < UpdateSeconds)
	{
		return;
	}
	const float Dt = SinceUpdate;
	SinceUpdate = 0.0f;

	if (!bToldNoTransport)
	{
		bToldNoTransport = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubVoice: this client has no voice transport (no microphone capture, codec or relay): each speaker's position, gain and wall muffle are worked out and kept, no voice is played"));
	}

	FScctHubVoiceMix Rows[MaxSpeakers];
	const FVector& Ear = Activities->GetEye();
	const int32 Count = FMath::Min(Api->GetVoiceMixes(static_cast<float>(Ear.X), static_cast<float>(Ear.Y), static_cast<float>(Ear.Z), Dt, Rows, MaxSpeakers), MaxSpeakers);
	Speakers.Reset(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctHubVoiceMix& Row = Rows[Index];
		FScctHubVoiceSpeaker& Speaker = Speakers.AddDefaulted_GetRef();
		Speaker.PlayerId = Row.PlayerId;
		Speaker.Name = Row.Name;
		Speaker.Location = FVector(Row.X, Row.Y, Row.Z);
		Speaker.Distance = Row.Distance;
		Speaker.ProximityGain = Row.Gain;
		Speaker.Volume = Owner->GetVoiceVolume(Speaker.Name);
		Speaker.bMuted = Owner->IsVoiceMuted(Speaker.Name);
		Speaker.Gain = Owner->GetVoiceGain(Speaker.Name) * Row.Gain;
		Speaker.LowPassHz = Row.LowPassHz;
		Speaker.Muffle = FMath::Clamp((OpenHz - Row.LowPassHz) / (OpenHz - OccludedHz), 0.0f, 1.0f);
		Speaker.bOccluded = (Row.Flags & EScctHubVoiceMixFlags::Occluded) != 0;
	}

	LogTimer -= Dt;
	if (bVerbose && LogTimer <= 0.0f)
	{
		LogTimer = 4.0f;
		FString Lines;
		for (const FScctHubVoiceSpeaker& Speaker : Speakers)
		{
			Lines += FString::Printf(TEXT(" [%s: %.0f uu, gain %.2f (proximity %.2f x volume %.2f%s), muffle %.2f (low-pass %.0f Hz)%s]"), *Speaker.Name, Speaker.Distance,
				Speaker.Gain, Speaker.ProximityGain, Speaker.Volume, Speaker.bMuted ? TEXT(", muted") : TEXT(""), Speaker.Muffle, Speaker.LowPassHz,
				Speaker.bOccluded ? TEXT(", behind a wall") : TEXT(""));
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubVoice: %d speakers heard from (%.0f, %.0f, %.0f):%s"), Speakers.Num(), Ear.X, Ear.Y, Ear.Z, Speakers.Num() > 0 ? *Lines : TEXT(" none"));
	}
}
