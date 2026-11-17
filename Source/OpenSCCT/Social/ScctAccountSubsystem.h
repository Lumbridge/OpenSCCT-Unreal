#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctAccountApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctAccountSubsystem.generated.h"

/** One title of the catalogue as the Account page lists it. */
struct FScctAccountTitleRow
{
	FString Id;
	FString Text;
	FString HowToUnlock;
	bool bUnlocked = false;
	bool bChosen = false;
};

/** Raised when anything the Account page shows changed (the bridge's revision moved). */
DECLARE_MULTICAST_DELEGATE(FScctAccountChanged);

/**
 * The player's account on the master (Bridge/OpenScct.UnrealBridge/Social/AccountService.cs, the Godot client's
 * AccountService, AccountSettings and ProfileSync): what the Account page shows and does, and the settings sync.
 *
 * The sign-in itself stays with UScctOnlineSubsystem (its token is kept in the user settings). Once signed in, the
 * bridge asks the master for the account (after each sign-in, at start-up with a saved one, and a few seconds after
 * each round's results) and keeps its name, pronouns, rank, XP, prestige, titles and career. A 401 from the master
 * signs the player out on this PC (the social subsystem deletes the saved token).
 *
 * Settings sync: the settings registry's values (every visible option except the Display, Graphics and Performance
 * pages and device choices, which belong to one PC, plus the chosen title) and both key layouts (Saved/Controls/
 * controls.json and bindings.json) follow the account. A changed setting, heard on FScctSettings::OnChanged, or a
 * key layout file saved anew, is sent 3 s later; after a sign-in the account's copy is taken when it is newer and put
 * in through FScctSettings and the control settings. The chosen title is the hidden setting Account.Title.
 *
 * Switches (off by default):
 *   -ScctAccountPreview                  the page shows a made-up account (prestige 2, level 37); nothing is sent
 *   -ScctNoAccountSync                   never send or take settings this run
 *   -ScctAccountSetting=<key>=<value>@S  set a registered setting S seconds in, as the settings page would (sync checks)
 *   -ScctAccountClaim=<name>             claim that name once the signed-in account is known and has none
 */
UCLASS()
class OPENSCCT_API UScctAccountSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The hidden setting that holds the chosen title's id (empty: none). */
	static const TCHAR* const TitleSetting;

	static UScctAccountSubsystem* Get(const UObject* WorldContext);

	/** False until the bridge's account table is up. */
	bool IsReady() const { return Api != nullptr; }

	const FScctAccountInfo& GetInfo() const { return Info; }
	EScctAccountView GetView() const { return static_cast<EScctAccountView>(Info.View); }
	bool HasFlag(int32 Flag) const { return (Info.Flags & Flag) != 0; }
	FString GetText(EScctAccountText Kind) const;

	/** Every title of the catalogue with the account's unlocks and choice. */
	TArray<FScctAccountTitleRow> GetTitles() const;

	/** The pronoun presets ("they/them" first, the default). */
	TArray<FString> GetPronounPresets() const;

	/** The prestige emblem's colour (1 to 10; 11 the master emblem). */
	FLinearColor EmblemColour(int32 Prestige) const;

	/** A packed lobby rank (FScctOnlinePlayer::Rank) as a line ("P3 · 42 - Field Agent") and its colour; false for no rank. */
	bool DescribeRank(int32 Packed, FString& OutText, FLinearColor& OutColour) const;

	static FLinearColor ToColour(int32 Rgb);

	void Refresh(bool bForce = false);
	bool ClaimName(const FString& Name);
	bool SetPronouns(int32 Preset);
	bool Prestige();
	bool SignOut();

	/** Starts a sign-in in the browser through the online subsystem. */
	void BeginSignIn();

	/** Shows the title (its id; empty: none) under the player's name; kept in the settings and synced. */
	void SetTitle(const FString& Id);

	/** Goes up whenever anything above changed. */
	int32 GetRevision() const { return Info.Revision; }
	FScctAccountChanged OnChanged;

	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaSeconds);
	bool Connect();
	void HandleSettingChanged(FName Key);

	/** The registry's synced values as a JSON object, and the control files as they are on disk. */
	FString BuildSettingsJson() const;
	void SendLocalProfile(bool bChanged);

	/** Puts the account's settings in, when the bridge has some waiting. */
	void ApplyIncoming();

	/** A key layout or the control settings were saved since the last look: they count as a change. */
	bool ControlFilesChanged();

	void RunSwitches(float DeltaSeconds);

	static bool IsSynced(const FName& Page, const FName& Key);

	const FScctAccountApi* Api = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle SettingHandle;
	FScctAccountInfo Info = {};
	int32 SeenRevision = -1;
	int32 SeenView = -1;

	bool bApplying = false;
	bool bLocalDirty = false;
	float FileCheckTimer = 0.0f;
	FDateTime ControlsStamp;
	FDateTime BindingsStamp;

	// Switches.
	bool bPreview = false;
	bool bSyncOff = false;
	FString AutoSettingKey;
	FString AutoSettingValue;
	float AutoSettingAt = -1.0f;
	FString AutoClaim;
	bool bAutoClaimed = false;
	float RunSeconds = 0.0f;
};
