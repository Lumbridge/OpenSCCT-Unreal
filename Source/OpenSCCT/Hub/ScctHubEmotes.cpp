#include "Hub/ScctHubEmotes.h"

#include "Hub/ScctHubActivities.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** HubEmotes.All, in the menu's order. */
	const TCHAR* const EmoteNames[] = { TEXT("wave"), TEXT("salute"), TEXT("cheer"), TEXT("dance"), TEXT("sit"), TEXT("point"), TEXT("laugh"), TEXT("clap") };

	/** An emote lasts 4 s (8 s dance and sit); asked again a little before the shortest ends, it never lapses. */
	constexpr float RepeatSeconds = 3.5f;
}

FScctHubEmotes::FScctHubEmotes(TSharedPtr<FScctHubActivities> InActivities)
	: Activities(MoveTemp(InActivities))
{
	FString Switch;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctHubEmote="), Switch, false))
	{
		FString Number = Switch, Delay;
		Switch.Split(TEXT("@"), &Number, &Delay);
		AutoEmote = FCString::Atoi(*Number);
		if (!Delay.IsEmpty())
		{
			AutoDelay = FCString::Atof(*Delay);
		}
		if (AutoEmote < 1 || AutoEmote > UE_ARRAY_COUNT(EmoteNames))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHubEmotes: -ScctHubEmote=%s is not an emote (1 to 8)"), *Switch);
			AutoEmote = 0;
		}
	}
}

void FScctHubEmotes::Stop()
{
	InHubSeconds = 0.0f;
	RepeatIn = 0.0f;
}

void FScctHubEmotes::Tick(UWorld* World, float DeltaSeconds, bool bVerbose)
{
	if (!Activities.IsValid() || !Activities->IsValid())
	{
		return;
	}
	InHubSeconds += DeltaSeconds;
	const FScctHubActivityState& State = Activities->GetState();

	if (AutoEmote > 0 && InHubSeconds >= AutoDelay)
	{
		RepeatIn -= DeltaSeconds;
		if (RepeatIn <= 0.0f)
		{
			RepeatIn = RepeatSeconds;
			const FScctHubSocialApi* Social = ScctFeature<FScctHubSocialApi>();
			const FString Command = FString::Printf(TEXT("HubEmote%d"), AutoEmote);
			const int32 Done = Social != nullptr ? Social->RunCommand(*Command) : 0;
			++NumPlayed;
			UE_CLOG(NumPlayed == 1 || bVerbose, LogOpenSCCT, Log, TEXT("ScctHubEmotes: -ScctHubEmote: %s (%s) %s"), *Command, EmoteNames[AutoEmote - 1],
				Done != 0 ? TEXT("asked of the hub") : TEXT("refused (no social table or not in the hub)"));
		}
	}

	LogTimer -= DeltaSeconds;
	if (bVerbose && LogTimer <= 0.0f)
	{
		LogTimer = 4.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubEmotes: %d body poses laid by the emote overlay since the last line (%d in all); the local player %s"),
			State.EmotePoses - LoggedPoses, State.EmotePoses,
			(State.Flags & EScctHubActivityFlags::Emoting) != 0 ? *FString::Printf(TEXT("plays %s"), *Activities->GetLocalEmote()) : TEXT("plays no emote"));
		LoggedPoses = State.EmotePoses;
	}
}
