#pragma once

#include "CoreMinimal.h"

/** Where a chat line was said: to everyone, to the sender's team, or by the server. */
enum class EScctChatChannel : uint8
{
	All,
	Team,
	System,
};

/** One received chat line. The sender stays apart from the text, so the panel can colour the name by team. */
struct FScctChatEntry
{
	int64 Id = 0;
	EScctChatChannel Channel = EScctChatChannel::All;
	FString Sender;
	int32 SenderTeam = -1;
	FString Text;
	/**
	 * The chat clock when it arrived (UScctChatSubsystem: the seconds of the level's frames, as the HUD ages its lines);
	 * negative for lines that were there before the level opened, which the passive view does not show.
	 */
	double ReceivedAt = -1.0;
};

/**
 * The chat lines of the connection, oldest first, at most Capacity (Godot's ChatTranscript). It belongs to the chat
 * subsystem and is drawn by the chat panel; it does not depend on the pawn or the simulation's clock.
 */
class FScctChatTranscript
{
public:
	static constexpr int32 Capacity = 64;

	/** How long a line shows in the passive view, and how long it then takes to fade (ChatTranscript's 8 s and 1 s). */
	static constexpr double DefaultVisibleSeconds = 8.0;
	static constexpr double FadeSeconds = 1.0;

	/**
	 * Adds a line. The same line from the same sender on the same channel within EchoSeconds of the last one is an
	 * echo of it (the server relays our own line back, and a visit hears it on two paths) and is dropped: false.
	 */
	bool Add(EScctChatChannel Channel, const FString& Sender, int32 SenderTeam, const FString& Text, double Now)
	{
		constexpr double EchoSeconds = 1.5;
		if (Entries.Num() > 0)
		{
			const FScctChatEntry& Last = Entries.Last();
			if (Now >= 0.0 && Last.ReceivedAt >= 0.0 && Now - Last.ReceivedAt <= EchoSeconds && Last.Channel == Channel && Last.Sender == Sender && Last.Text == Text)
			{
				return false;
			}
		}
		if (Entries.Num() == Capacity)
		{
			Entries.RemoveAt(0);
		}
		FScctChatEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Id = ++NextId;
		Entry.Channel = Channel;
		Entry.Sender = Sender;
		Entry.SenderTeam = SenderTeam;
		Entry.Text = Text;
		Entry.ReceivedAt = Now;
		return true;
	}

	const TArray<FScctChatEntry>& GetEntries() const { return Entries; }

	/** 1 while the line is shown in the passive view, then down to 0 over FadeSeconds. */
	static float Opacity(const FScctChatEntry& Entry, double Now, double VisibleSeconds)
	{
		if (Entry.ReceivedAt < 0.0)
		{
			return 0.0f;
		}
		return static_cast<float>(FMath::Clamp((Entry.ReceivedAt + VisibleSeconds + FadeSeconds - Now) / FadeSeconds, 0.0, 1.0));
	}

private:
	TArray<FScctChatEntry> Entries;
	int64 NextId = 0;
};
