#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctHubApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctHubSubsystem.generated.h"

class UScctSessionSubsystem;

/** Raised when what the hub page shows changed: the state, a text, the roster count. */
DECLARE_MULTICAST_DELEGATE(FScctHubChanged);

/**
 * The player's visit to an OpenSCCT hub (Runtime/ScctHubApi.h), and the hub this PC can host for it. It belongs to
 * the game instance, so it lives across level changes: the visit connects from wherever the player is (the front
 * end), then the hub is shown in its imported level, /Game/Imported/Maps/Hub/Hub (Tools/Import/Import-HubMap.ps1),
 * opened with ?ScctHub=1, which tells AScctGameMode to start no local match and to present the bridge's session.
 *
 * Every frame it runs the bridge's Hub.Update: in the hub level at the start of the session subsystem's frame (its
 * OnBeforeTicks: the visit puts its session into the bridge's slot, which the session subsystem then finds), in any
 * other level, and while no level ticks, from a core ticker.
 *
 * Which level is open follows the bridge:
 *   - the visit names a map to load (welcomed, loading, in the hub): the hub level is opened. Once it has begun play
 *     and presents the visit's session, ReportLoaded is called and the hub spawns the player's pawn.
 *   - the visit failed: the front end level is opened and GetNotice says why.
 *   - the hub level was left another way (the in-match menu's leave button opens the front end): the visit is left.
 *
 * Unattended switches (off by default; for runs with nobody at the keyboard, used with -RenderOffscreen):
 *   -ScctHub=<host[:port]>        join that hub as soon as the game is up (port 28886 when none is given)
 *   -ScctHostHub[=lan|loopback]   host a hub on this PC (its own server thread, UDP 28886, not listed on any
 *                                 master) and join it; lan (the default) opens the port on every interface so others
 *                                 on the network can join, loopback keeps it to this PC
 *   -ScctHubPort=<port>           the port -ScctHostHub opens
 *   -ScctSide=spy|merc            the side to enter the hub on (default spy)
 *   -ScctName=<name>              the player's name for this run
 *   -ScctHubLog=S                 seconds between the status lines in the log (default 2 with the switches above)
 * Started in the hub level itself (Tools\Run-Offscreen.ps1 -Map Hub -Extra "-ScctHostHub") the level presents the
 * visit at once instead of starting a local match on the hub map.
 */
UCLASS()
class OPENSCCT_API UScctHubSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The hub's imported level. */
	static const TCHAR* const HubLevel;

	/** The URL option AScctGameMode reads: the level presents the hub visit, it starts no local match. */
	static const TCHAR* const HubOption;

	static UScctHubSubsystem* Get(const UObject* WorldContext);

	// ---- joining, hosting and leaving ------------------------------------------------------------------------

	/** Joins the hub at "host", "host:port" or "[v6]:port" on Team (0 spies, 1 mercs). False when the visit could not start (GetNotice says why). */
	bool Join(const FString& Address, int32 Team = 0);

	/** Hosts a hub on this PC on Port (0: the hub's default) and joins it. False when it could not (GetNotice says why). */
	bool HostAndJoin(int32 Port = 0, int32 Team = 0, bool bLoopbackOnly = false);

	/** Leaves the hub (and closes the hub this PC hosts), or dismisses a failure. The state is Idle afterwards. */
	void Leave();

	// ---- what the hub page shows ------------------------------------------------------------------------------

	EScctHubState GetState() const { return static_cast<EScctHubState>(Status.State); }
	const FScctHubStatus& GetStatus() const { return Status; }
	bool HasFlag(int32 Flag) const { return (Status.Flags & Flag) != 0; }

	/** A visit is under way, or has failed and not been dismissed. */
	bool IsVisiting() const { return GetState() != EScctHubState::Idle || !Notice.IsEmpty(); }

	const FString& GetHubName() const { return HubName; }
	const FString& GetStatusText() const { return StatusText; }
	const FString& GetAddress() const { return Address; }

	/** Why the visit failed or could not start, as a player reads it; empty when there is nothing to say. */
	const FString& GetNotice() const { return Notice; }

	int32 GetRevision() const { return Revision; }
	FScctHubChanged OnChanged;

	// ---- the levels ------------------------------------------------------------------------------------------

	/**
	 * True when a level starting play should present the hub visit instead of starting a local match: it was opened
	 * with the hub option, or it is the hub level while a visit is under way or about to start (the unattended
	 * switches).
	 */
	bool ShouldPresent(const UWorld* World) const;

	/** Called by the game mode of a level that presents the hub: that world's session subsystem shows the visit's session. */
	void PresentIn(UScctSessionSubsystem* Session);

	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	/** The table, fetched on first use; null (the reason is in Notice) when the bridge cannot be loaded. */
	const FScctHubApi* Connect();

	/** Once a frame, whoever calls first: Hub.Update, the status, and everything that follows from it. */
	void TickVisit();
	bool TickFromTicker(float DeltaSeconds);
	void HandleBeforeTicks(float DeltaSeconds);

	void RefreshTexts();
	void DrainLog();
	void HandleStateChange(EScctHubState Old, EScctHubState New);
	void ForgetFinishedTravel();
	void FollowLevel();
	void ReportLoadedWhenReady();
	void RunUnattended();
	void LogStatus(float DeltaSeconds);
	void Touch();

	FString ReadText(EScctHubText Kind) const;
	FString FindPlayerName() const;
	void OpenLevel(const FString& LevelUrl);

	/** The world presents the hub visit (PresentIn was called for it). */
	bool IsPresenting(const UWorld* World) const { return World != nullptr && PresentedWorld.Get() == World; }

	const FScctHubApi* Api = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;
	uint64 LastTickFrame = 0;
	double LastTickSeconds = 0.0;

	FScctHubStatus Status = {};
	int32 BridgeRevision = -1;
	int32 Revision = 0;
	FString HubName, StatusText, Address, MapToLoad;
	FString Notice;

	/** The world that presents the visit, and whether one did since the visit started. */
	TWeakObjectPtr<const UWorld> PresentedWorld;
	bool bWasPresented = false;

	/** The level a travel was asked for, and the world it was asked in; empty once another world is open. */
	FString PendingLevel;
	TWeakObjectPtr<const UWorld> PendingFrom;

	/** ReportLoaded was sent for this bridge session id. */
	int32 ReportedSessionId = 0;

	// Unattended switches.
	FString AutoJoin;
	bool bAutoHost = false;
	bool bAutoHostLoopback = false;
	int32 AutoHostPort = 0;
	int32 AutoTeam = 0;
	bool bAutoStarted = false;
	float LogEvery = 0.0f;
	float LogTimer = 0.0f;
};
