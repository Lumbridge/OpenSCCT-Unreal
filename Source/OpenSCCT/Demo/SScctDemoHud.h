#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctDemoPlayApi.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SScctSelectBox;

/**
 * The playback bar over a demo being watched (Demo/ScctDemoPlayback.h), ported from the Godot client's
 * game/Scripts/Play/Demo/DemoHud.cs and its DemoSeekBar: the demo's map and mode, PLAYING / PAUSED / SEEKING /
 * FINISHED, who is watched; the timeline with its round, death and event markers (click or drag to seek; a marker
 * seeks three seconds before it), the time; Restart, -5 s, Pause, +5 s, the speeds, Controls (the shortcuts),
 * Hide and Back to demos; the players to watch and the cameras (first person, third person, free, as played).
 *
 * Keys (as DemoHud): Space play / pause, Left / Right 5 s (Shift 30 s), Up / Down speed, Home restart, End the end,
 * C camera, X X-ray, [ / ] or the wheel the player, 1-9 a player, H hide / show, ? the shortcuts, Escape close them or ask to
 * go back to the demos. The right mouse button orbits the third-person camera or looks with the free one; W A S D,
 * Q / E and Shift fly the free camera. The bar hides after four idle seconds while playing (not in an unattended run).
 */
class SScctDemoHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctDemoHud) {}
		/** Back to demos was confirmed. */
		SLATE_EVENT(FSimpleDelegate, OnLeave)
		/** Whether X-ray is on, and its toggle (the X-RAY chip and the X key). */
		SLATE_ATTRIBUTE(bool, XRayOn)
		SLATE_EVENT(FSimpleDelegate, OnToggleXRay)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Once a frame from the playback (before the bridge's Frame): the free camera's keys and the bar's state. */
	void DriveFrame(float DeltaSeconds);

	const FScctDemoPlayInfo& GetInfo() const { return Info; }
	const TArray<FScctDemoMarker>& GetMarkers() const { return Markers; }

	/** Sends a command to the playback. */
	void Command(EScctDemoCommand Which, int32 Arg = 0);

	/** The timeline's hover text, or its default. */
	void SetPreview(const FString& Text);

	/** "1:05" for a number of ticks of this demo. */
	FString Clock(int32 Ticks) const;

	// SWidget
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;

private:
	TSharedRef<SWidget> BuildBar();
	void RebuildPlayers();
	bool IsBarShown() const;
	void SetLooking(bool bLooking);
	void AskLeave();
	void AnswerLeave(bool bLeave);
	void ToggleHelp();

	FSimpleDelegate OnLeave;
	TAttribute<bool> XRayOn;
	FSimpleDelegate OnToggleXRay;
	FScctDemoPlayInfo Info = {};
	TArray<FScctDemoMarker> Markers;
	TArray<FScctDemoTarget> Targets;
	FString RosterSignature;
	TSharedPtr<SBox> PlayerBox;
	TSharedPtr<SScctSelectBox> PlayerSelect;

	FString Preview;
	float Idle = 0.0f;
	bool bHidden = false;
	bool bHelp = false;
	bool bLooking = false;
	bool bConfirmingLeave = false;
	bool bWasPausedBeforeConfirm = false;
	bool bAlwaysShown = false;

	/** The keys held, for the free camera's flight. */
	TSet<FKey> Held;
};
