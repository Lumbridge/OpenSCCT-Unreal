#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctTeamPingApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctTeamPingSubsystem.generated.h"

class AScctPlayerController;

/**
 * The team ping of a match level (Godot's TeamPingInput with its TeamPingHud): the local player marks a spot for his
 * team, and every teammate's mark is drawn on the HUD.
 *
 * THE GESTURE. The player's TeamPing binding (unbound by default, as in the Godot client) arrives as an interface
 * command, pressed and released (AScctPlayerController::OnLocalCommand). A tap, let go within Hud.TeamPingHoldSeconds
 * (0.25 s), marks the aimed spot ("here"). Held longer, it opens the wheel at the screen's centre: the mouse or the right
 * stick chooses (up Watch here, right Check here, and for the spies down Co-op boost and left Mine) and does not turn
 * the view meanwhile (AScctPlayerController::LookSink); letting go sends the choice, or nothing from the middle. The
 * ping is aimed when it is sent, not when the key went down. A change of pawn, of the actor giving the view (his
 * sticky cam, the camera network) or of team drops the gesture, and so does losing the keys (a menu, the chat, focus).
 *
 * SENDING AND RECEIVING. A ping goes through the session's own connection (Runtime/ScctTeamPingApi.h); the server
 * checks it, at most one a second, and sends it to the whole team, the sender included, so a ping is drawn when it
 * comes back. Each teammate has one ping at a time, living 4 s and fading over the last 0.75 s.
 *
 * THE BADGES (ScctHudElementsTeamPing.cpp): a disc with the kind's icon above the marked spot, with its distance, or at
 * the screen's edge with an arrow when the spot is off the screen or behind. Not drawn while a menu, the chat or the
 * scoreboard is open, the HUD is hidden, or no ping can be placed (dead, spectating, Merc Deathmatch).
 *
 * OPTIONS (page Hud, group Team ping): Hud.TeamPingHoldSeconds, how long the key is held before the wheel opens;
 * Hud.ShowTeamPings, whether the team's pings are drawn.
 *
 * Unattended switches (off by default):
 *   -ScctTeamPing=kind[,At]   sends one ping of that kind (generic, watch, check, boost, mine; enemy is taken as watch,
 *                             the protocol having no enemy kind) aimed through the camera, At seconds into the match
 *                             (default 8), or as soon after as a ping can be placed
 *   -ScctHudCommands "PingKey 1/0" and "PingWheel D" hold the TeamPing key and point the wheel (see UScctHudSubsystem).
 * Sends, refusals, receptions, the wheel and the badges on screen write "ScctTeamPing:" lines to the log.
 */
UCLASS()
class OPENSCCT_API UScctTeamPingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A timed HUD command of the team ping (PingKey, PingWheel); false for a name that is not one. */
	bool RunCommand(const FString& Name, int32 Argument);

	/** The wheel is open now. */
	bool IsWheelOpen() const { return Gesture.bWheelOpen; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	static void RegisterSettings();

	void HandleSessionChanged(int32 OldSessionId, int32 NewSessionId);
	void HandleFrame(float DeltaSeconds);
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);
	bool HandleLook(float MouseX, float MouseY, float RightX, float RightY);

	/** Listens to the current local player's commands and look. */
	void Attach(AScctPlayerController* Player);
	void Detach();

	/** The ping's context this frame: the bridge's, or 0 while the HUD's own blocks are up (menu, chat, scoreboard, hidden HUD). */
	int32 CurrentContext(const FScctTeamPingState& State) const;

	/** Godot's TeamPingInput: press, release, and once a frame the wheel's opening, the queued send and the cancels. */
	void Press(double Now);
	void Release(double Now);
	void Advance(double Now, bool bInputAvailable);
	void CancelGesture(const TCHAR* Why);
	void CloseGesture();
	void Choose(const FVector2f& Direction);

	/** Aims a ping of Kind through the camera and sends it; logs the result. */
	bool Send(int32 Kind, const TCHAR* Why);

	/** The pings into the HUD model's badges, placed on the screen for this frame's camera, and the wheel. */
	void Present(bool bShow);

	void LogReceived();

	struct FGestureState
	{
		bool bDown = false;          // the TeamPing key is held (a gesture runs)
		int32 Context = 0;           // the context it began under
		double PressedAt = 0.0;
		bool bMerc = false;          // the wheel has the mercs' two kinds
		bool bWheelOpen = false;
		int32 Selection = -1;        // EScctTeamPingKind, -1 none
		FVector2f MouseDirection = FVector2f::ZeroVector;
		FVector2f StickDirection = FVector2f::ZeroVector;
		bool bPending = false;       // a release waits for the next frame's check to send it
		int32 PendingContext = 0;
		int32 PendingKind = 0;
		bool bPendingFromWheel = false;
	} Gesture;

	FScctTeamPingState State = {};
	int32 Context = 0;
	int32 LastReceived = 0;
	uint32 LastBadgeSignature = 0;
	double Clock = 0.0;

	TWeakObjectPtr<AScctPlayerController> CommandSource;
	FDelegateHandle CommandHandle;

	// -ScctTeamPing=kind[,At]
	int32 DebugKind = -1;
	float DebugAt = 8.0f;
	bool bDebugDone = false;
	bool bDebugWaitLogged = false;
	bool bTableMissingLogged = false;
};
