#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctSocialApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctSocialSubsystem.generated.h"

class SScctSocialToast;

/** A join that waits for the player to type the server's password (the front end's password page shows it). */
struct FScctSocialPasswordPrompt
{
	/** EScctSocialRequest::JoinMaster or JoinAddress. */
	EScctSocialRequest Kind = EScctSocialRequest::None;
	/** The server's master id, or its address. */
	FString Value;
	/** The server's name as the page shows it. */
	FString Name;
	int32 Flags = 0;
};

/**
 * The game's social services for the whole run (Bridge/OpenScct.UnrealBridge/Social, the Godot client's
 * DiscordPresence, WebsiteJoinService and ClientPresenceService):
 *   - the player's Discord status: each second this subsystem describes what the player does (the front end's page,
 *     an online visit with its lobby, hosting, map, mode, side and team scores, the hub, an offline match against
 *     bots, the party and how friends join) and the bridge shows it through the Discord app on this PC. The Online
 *     page's "Discord status" option turns it on and off at once. A friend's Join from Discord starts once the
 *     player is in the front end, as the server list, the address page or the hub page would start it.
 *   - the website's Join button: -svm-join-server=<id> (or --svm-join-server=<id>) joins that listed server or hub
 *     once the front end is up and idle, through the official master with the player's own sign-in; a game already
 *     running takes the id over the named pipe OpenSCCT.WebJoin.Game, and the process the site started quits.
 *   - the master's presence lease, every 30 s while the game runs.
 * The bridge asks for the joins and notices (FScctSocialRequest); this subsystem carries them out on the online and
 * hub subsystems, puts notices up as a toast at the top of the screen, and keeps the password prompt and the page to
 * open for the front end, which takes them in its Tick.
 *
 * The Discord status is never started in unattended or -RenderOffscreen runs (they must not touch the player's
 * Discord) unless -ScctDiscord forces it. The game registers itself with Discord (discord-<app>://, so a friend's Join
 * can start it) only in a packaged game, or with -ScctDiscordRegister: a development run would otherwise take the
 * protocol over from an installed game.
 *
 * Unattended switches (off by default):
 *   -ScctDiscord               start the Discord status even in an unattended or off-screen run
 *   -ScctDiscordRegister       register this (development) game with Discord for friends' Joins
 *   -ScctDiscordJoin=<secret>  a friend's Join with that secret ("oscct1|m|<id>", "oscct1|a|host:port", "oscct1|h|host:port")
 *   -ScctWebJoin=<id>          a website join of that id as if it came over the pipe, once the bridge is up
 *   -ScctDiscordToggle=S       turn the Discord status option off S seconds in and back on 5 s later (not saved)
 *   -ScctNoPresenceLease       no presence lease (runs that must not count as a player online)
 *   -ScctSocialLog             log every status sent and every lease (on by default in unattended runs)
 */
UCLASS()
class OPENSCCT_API UScctSocialSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The Online page's option. */
	static const TCHAR* const DiscordStatusSetting;

	static UScctSocialSubsystem* Get(const UObject* WorldContext);

	/** The front end says which page it shows (SScctFrontEnd::ShowPage), for the status. */
	void SetFrontEndPage(EScctSocialPage Page) { FrontEndPage = Page; }

	/** The front end takes a password prompt to show, once (false when none waits). */
	bool TakePasswordPrompt(FScctSocialPasswordPrompt& OutPrompt);

	/** The password page's JOIN: the waiting join goes on with this password and side. */
	void SubmitPassword(const FScctSocialPasswordPrompt& Prompt, const FString& Password, int32 Team);

	/** The front end takes a page it is asked to open ("servers" for a sign-in), once; empty when none. */
	FString TakePageToOpen();

	/** What Discord shows (or would show with the status on), as one line. */
	FString GetPresenceText() const;

	const FScctSocialStatus& GetStatus() const { return Status; }

	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	const FScctSocialApi* Connect();
	bool Tick(float DeltaSeconds);

	/** Whether the Discord status should run now (the option, and not an unattended run unless forced). */
	bool WantsDiscord() const;
	void ApplyDiscord();
	void HandleSettingChanged(FName Key);

	/** EScctSocialReadiness for this frame. */
	int32 FindReadiness() const;

	/** Describes the player's state to the bridge. */
	void SendPresence();

	void DrainLog();
	void HandleRequests();
	void HandleRequest(const FScctSocialRequest& Request, const FString& Value, const FString& Text);
	void JoinMaster(const FString& Id, const FString& Name, const FString& Password, int32 Team, bool bRequireAccount);

	/** Leaves an online or hub visit before a friend's Join goes elsewhere. */
	void LeaveVisits(bool bOnline, bool bHub);

	void ShowNotice(const FString& Text);

	/** How Discord starts this game for a friend's Join: the exe and its command, or empty when the game is not registered. */
	void FindLaunchCommand(FString& OutExe, FString& OutCommand) const;

	bool IsFrontEndOpen() const;

	const FScctSocialApi* Api = nullptr;
	bool bConnectTried = false;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle SettingHandle;
	FScctSocialStatus Status = {};
	EScctSocialPage FrontEndPage = EScctSocialPage::Other;
	float PresenceTimer = 0.0f;
	bool bDiscordOn = false;
	int32 LastDiscordState = -1;
	FString LastPresenceText;

	TOptional<FScctSocialPasswordPrompt> PendingPrompt;
	FString PageToOpen;
	TSharedPtr<SScctSocialToast> Toast;

	// Switches.
	FString StartupWebJoin;
	FString AutoWebJoin;
	FString AutoDiscordJoin;
	bool bForceDiscord = false;
	bool bRegister = false;
	bool bLease = true;
	bool bVerbose = false;
	bool bAutoQueued = false;
	float ToggleAt = -1.0f;
	float RunSeconds = 0.0f;
	int32 ToggleStep = 0;
};
