#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Test/SScctGrabAreaOverlay.h"
#include "ScctGrabAreaSubsystem.generated.h"

class AScctPlayerController;
class SWidget;
struct FScctGrabAreaApi;

/**
 * The practice grab-area overlay (Godot's practice console command grabarea, an OpenSCCT aid not in the original):
 * draws on the floor around every living mercenary where a spy can stand to grab him, coloured by what Use would do
 * now. The bridge works out the windows and the colour exactly as the Godot overlay does (Runtime/ScctFeatures.h,
 * FScctGrabAreaApi); this projects them for the frame's camera and hands them to SScctGrabAreaOverlay, which sits in
 * the viewport just below the HUD.
 *
 * It works in a practice round only (?Practice=1, or an online practice server): anywhere else a request is refused
 * with a line on the screen and in the log, as it would show the mercenaries through walls. It goes away when the
 * match ends.
 *
 * Ways to turn it on or off:
 *   scct.GrabArea [0|1]      the console command; without an argument it toggles
 *   GrabArea                 a command of the player's bindings (Controls, Interface: "Practice: show grab areas";
 *                            no key by default), which toggles it
 *   -ScctGrabArea            on the command line: asks for it at the start of every match (for screenshots)
 *
 * While it is shown the log gets "GrabArea: N mercs (green/yellow/red)" every two seconds.
 */
UCLASS()
class OPENSCCT_API UScctGrabAreaSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UScctGrabAreaSubsystem();

	/** Asks for the overlay: 1 on, 0 off, -1 the other way round. Returns what happened, for the console. */
	FString Request(int32 Mode);

	bool IsShown() const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleSessionChanged(int32 OldSessionId, int32 NewSessionId);
	void HandleMatchStopped();
	void HandleFrame(float DeltaSeconds);
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);

	/** Reads the bridge's windows and projects them for this frame's camera into the model. */
	void Project();

	void ShowWidget();
	void HideWidget();

	/** Shows Text in the middle of the screen for a few seconds. */
	void Notify(const FString& Text);

	TSharedRef<FScctGrabAreaModel> Model;
	TSharedPtr<SScctGrabAreaOverlay> Overlay;
	TSharedPtr<SWidget> ViewportContent;
	const FScctGrabAreaApi* Api = nullptr;

	/** The player wants it on (it is drawn while this holds in a practice round). */
	bool bWanted = false;

	/** -ScctGrabArea: ask for it on the first frame of the match. */
	bool bAskAtStart = false;
	bool bAskPending = false;

	TWeakObjectPtr<AScctPlayerController> CommandSource;
	FDelegateHandle CommandHandle;
	float LogTimer = 0.0f;
};
