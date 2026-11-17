#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

/** One line of the speaker overlay. Player is the name a click or the wheel acts on, empty for the local player's lines. */
struct FScctVoiceOverlayLine
{
	FString Text;
	FString Player;
	FLinearColor Colour = FLinearColor::White;

	bool operator==(const FScctVoiceOverlayLine& Other) const
	{
		return Text == Other.Text && Player == Other.Player && Colour.Equals(Other.Colour);
	}
};

DECLARE_DELEGATE_OneParam(FScctVoiceOverlayMute, const FString& /*Player*/);
DECLARE_DELEGATE_TwoParams(FScctVoiceOverlayVolume, const FString& /*Player*/, int32 /*Steps*/);

/**
 * The voice overlay of a round (Godot's VoiceIndicator and OnlineVoiceChat's overlay, Reloaded's speaking list): in
 * the top right, one line per speaking or muted player, "name - volume n%" in his team's colour, marked "(team)" for
 * team talk, "(hacked)" for an enemy heard through hacked comms and "(muted)"; above them "Your microphone is muted"
 * or "You - Speaking". UScctVoiceSubsystem puts it on the game viewport and hands it the lines every frame.
 *
 * While the cursor is free (the match menu is open) a player's line takes the mouse: a click mutes or unmutes him, the
 * wheel turns his volume up or down by 10%. Otherwise the overlay lets every click through to the game.
 */
class OPENSCCT_API SScctVoiceOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctVoiceOverlay) {}
		SLATE_EVENT(FScctVoiceOverlayMute, OnToggleMute)
		SLATE_EVENT(FScctVoiceOverlayVolume, OnAdjustVolume)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The lines to show, top first; the rows are made again only when they changed. */
	void SetLines(const TArray<FScctVoiceOverlayLine>& InLines);
	const TArray<FScctVoiceOverlayLine>& GetLines() const { return Lines; }

	/** Whether the players' lines take clicks and the wheel (the cursor is free). */
	void SetInteractive(bool bInInteractive) { bInteractive = bInInteractive; }
	bool IsInteractive() const { return bInteractive; }

	/** The centre of a line on the screen (desktop space, as the mouse is) as last laid out; false when there is none. */
	bool GetLineScreenCentre(int32 Index, FVector2D& OutCentre) const;

private:
	void Rebuild();

	TSharedPtr<SVerticalBox> Rows;
	TArray<FScctVoiceOverlayLine> Lines;
	bool bInteractive = false;
	FScctVoiceOverlayMute OnToggleMute;
	FScctVoiceOverlayVolume OnAdjustVolume;
};
