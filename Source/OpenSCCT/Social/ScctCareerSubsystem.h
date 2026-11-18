#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctCareerApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctCareerSubsystem.generated.h"

/**
 * The player's career for the Stats, Match history and Achievements pages (Bridge/OpenScct.UnrealBridge/Social/
 * CareerModel.cs), and the capture of every round the player plays into the scoreboard archive
 * (Social/ScoreboardCapture.cs, the Godot client's ScoreboardCapture): once the bridge is up this subsystem names the
 * archive's folder, Saved/Scoreboards, and from then on each session that takes the bridge's main slot (a local
 * match, an online round) saves the final scoreboard of each round the player played, or the last one received
 * marked partial and left early when the player left first. The bridge logs each save ("Scoreboard saved: ...").
 *
 * The match history joins the saved rounds with the signed-in account's history on the master (page by page); the
 * stats and the achievements' progress are the account the master last described (UScctAccountSubsystem).
 */
UCLASS()
class OPENSCCT_API UScctCareerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem of the game instance that shows the menus. */
	static UScctCareerSubsystem* GetCurrent();

	/** Saved/Scoreboards, where the rounds played are kept. */
	static FString ArchiveFolder();

	bool IsReady() const { return Api != nullptr; }

	/** The table's state as of this frame (read again on each call when the bridge's answers moved). */
	const FScctCareerInfo& GetInfo() const;
	bool HasFlag(int32 Flag) const { return (GetInfo().Flags & Flag) != 0; }

	FString GetString(EScctCareerString Kind, int32 Index = 0, int32 Sub = 0) const;
	bool GetMatch(int32 Index, FScctCareerMatch& OutMatch) const;
	bool GetPlayer(int32 Index, int32 Row, FScctCareerPlayer& OutPlayer) const;
	bool GetAchievement(int32 Index, FScctCareerAchievement& OutAchievement) const;
	void Command(EScctCareerCommand Kind, int32 Index = 0);

	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaSeconds);

	static TWeakObjectPtr<UScctCareerSubsystem> Current;

	const FScctCareerApi* Api = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;
	mutable FScctCareerInfo Info = {};
	mutable uint64 InfoFrame = MAX_uint64;
	int32 LoggedSaved = 0;
};
