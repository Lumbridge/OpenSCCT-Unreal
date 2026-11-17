#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctRejoinApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctRejoinSubsystem.generated.h"

/**
 * Rejoining a round the game dropped out of (a crash, a lost connection, a quit), over the bridge's rejoin table
 * (Runtime/ScctRejoinApi.h, Bridge/Online/RejoinService.cs). While an online round is played the bridge keeps its
 * record in Saved/Online/rejoin.json; when the main menu shows (Check) the bridge asks the master or the server
 * whether that round is still being played and, if so, offers it: the home page shows a REJOIN entry and asks once
 * with a dialog (Menu/ScctRejoinPrompt.h). Rejoin joins again through the online visit, which the front end follows
 * as any join; the server restores the side, the score and the lives.
 *
 * Unattended runs (-unattended: the test scripts) neither keep nor offer a record, so one test's round never shows
 * up over another's menus, unless the command line has
 *   -ScctRejoin       keep and offer the record as a player's game does
 *   -ScctAutoRejoin   the same, and rejoin by itself a moment after a round is offered
 */
UCLASS()
class OPENSCCT_API UScctRejoinSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UScctRejoinSubsystem* Get(const UObject* WorldContext);

	/** The main menu is up: has the bridge check the remembered round (the answer comes in later frames). */
	void Check();

	/** The offered round; false when none is offered. */
	bool GetOffer(FScctRejoinChoice& Out) const;

	/** Goes up whenever the offer changes. */
	int32 GetRevision() const { return Choice.Revision; }

	/** Joins the offered round again. False when nothing is offered or the join could not start. */
	bool Rejoin();

	/** Not now: the round is not offered again. */
	void Dismiss();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool TickRejoin(float DeltaSeconds);
	const FScctRejoinApi* Connect();

	const FScctRejoinApi* Api = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;
	FScctRejoinChoice Choice = {};
	bool bEnabled = false;
	bool bCheckAsked = false;
	bool bAuto = false;
	bool bAutoDone = false;
	float AutoWait = 0.0f;
};
