#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctDemoApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctDemoSubsystem.generated.h"

/** Raised when the demo-saving status changed (the bridge's revision moved). */
DECLARE_MULTICAST_DELEGATE(FScctDemoChanged);

/**
 * The player's demo-saving choice (an OpenSCCT feature, docs/DEMOS.md; Bridge/OpenScct.UnrealBridge/Demo/DemoSaving.cs,
 * the Godot client's personal recording preference and --svm-record): whether local matches and online rounds are
 * saved as demos into Saved/Demos, and the status the lobby, host and results pages show.
 *
 * Two options of the settings registry on the Online tab, under Demos: "Record offline matches" (a local match records
 * the host's packets from its first tick and finishes the file when it ends) and "Record online matches" (every visit
 * asks its server for personal demos; the server records each round for this player and sends the demo, and its
 * training data when it has some, down after the round; the hub's way back waits for that download, five minutes at
 * most). Both follow the account with the settings sync. Hosting records on its own option (Online.HostRecordDemos).
 *
 * The choice is handed to the bridge at start, whenever an option changes, and just before each local match starts
 * (UScctSessionSubsystem calls PushChoice), so the match never starts on an old choice.
 *
 * The menus own the pages that show the status; they read it here: GetOnlineStatus() ("Your upcoming matches will be
 * saved to Demos on this computer.", or the server's answer while connected), GetOfflineStatus(), IsDemoDownloading()
 * and the folder and file getters.
 *
 * Switch: -ScctRecord saves both offline matches and online rounds for this run without changing the options (as the
 * Godot client's --svm-record). The log prints the file a match records into, and its size once it is finished.
 */
UCLASS()
class OPENSCCT_API UScctDemoSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static const TCHAR* const RecordOfflineSetting;
	static const TCHAR* const RecordOnlineSetting;

	static UScctDemoSubsystem* Get(const UObject* WorldContext);

	/** Saved/Demos, as a full path (the host's demos go there as well). */
	static FString DemoFolder();

	/**
	 * Hands the current choice to the bridge. False when the bridge's demo table is not up. Called before each local
	 * match starts and when the online table is first fetched (UScctOnlineSubsystem::Connect), so no match or visit
	 * starts on an old choice: a visit's demo folder is fixed when it is made.
	 */
	static bool PushChoice();

	/** Registers the two options once (also done by PushChoice, which another subsystem may call first). */
	static void RegisterOptions();

	/** The choice as it applies this run (the option, or -ScctRecord). */
	static bool SavesOffline();
	static bool SavesOnline();

	void SetRecordOffline(bool bRecord);
	void SetRecordOnline(bool bRecord);

	FString GetOnlineStatus() const { return Text(EScctDemoText::OnlineStatus); }
	FString GetOfflineStatus() const { return Text(EScctDemoText::OfflineStatus); }
	FString GetCurrentOfflineDemo() const { return Text(EScctDemoText::CurrentOfflineDemo); }
	FString GetLastOfflineDemo() const { return Text(EScctDemoText::LastOfflineDemo); }
	FString GetLastOnlineDemo() const { return Text(EScctDemoText::LastOnlineDemo); }

	/** A round of the online visit is recording, or its demo (or training data) is still downloading: leaving now loses it. */
	bool IsDemoDownloading() const { return (Info.Flags & EScctDemoFlags::PendingSave) != 0; }
	bool HasFlag(int32 Flag) const { return (Info.Flags & Flag) != 0; }
	int32 GetRevision() const { return Info.Revision; }

	FScctDemoChanged OnChanged;

	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaSeconds);
	void HandleSettingChanged(FName Key);
	FString Text(EScctDemoText Kind) const;
	void ReportFile(const TCHAR* What, const FString& Path) const;

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle SettingHandle;
	FScctDemoInfo Info = {};
	bool bPushed = false;
	FString SeenRecording;
	FString SeenOnlineDemo;
	FString SeenOnlineStatus;
	int32 SeenOnlineQuarter = -1;
	/** The offline demo whose finished size is still to be logged once its match ended. */
	FString PendingFinished;
};
