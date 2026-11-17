#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctDashboardApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctDashboardSubsystem.generated.h"

/** One release of the changelog as the What's new page lists it. */
struct FScctDashboardRelease
{
	FString Tag;
	FString Name;
	FString Date;
	FString Notes; // plain text, bullets as "• "
	FString Url;   // its page on GitHub, built from a fixed origin
};

/**
 * The main menu's live information (Bridge/OpenScct.UnrealBridge/Social/MenuDashboard.cs, the Godot client's
 * MenuDashboardService, MenuDashboardModel and MasterVersionCheck): players online, the game and hub sessions, the
 * master's announcement and message of the day, the changelog, the next unlock and recent results of the account,
 * and the master's version line for the build label.
 *
 * The master is asked only while a page that shows the dashboard is on screen: such a page calls NoteShown() each
 * frame it is painted (from its Tick, which Slate runs only for widgets drawn this frame), and half a second without
 * one ends the watch. Each change is logged ("Menu dashboard: on screen, polling" / "off screen").
 *
 * Switches:
 *   -ScctDashboardPreview   made-up data (1,204 players online, two releases, an announcement); nothing is asked
 */
UCLASS()
class OPENSCCT_API UScctDashboardSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem of the game instance that shows the menus (any world of it). */
	static UScctDashboardSubsystem* GetCurrent();

	/** A page showing the dashboard was painted this frame. */
	void NoteShown();

	bool IsReady() const { return Api != nullptr; }
	bool IsWatched() const { return bWatched; }
	const FScctDashboardInfo& GetInfo() const { return Info; }
	bool HasFlag(int32 Flag) const { return (Info.Flags & Flag) != 0; }

	/** A text, as of the last change (cached per revision). */
	const FString& GetText(EScctDashboardText Kind) const;

	/** The changelog's releases, newest first (at most five). */
	const TArray<FScctDashboardRelease>& GetReleases() const { return Releases; }

	/** "Master 1.0.0  build ..." or what stands in for it; empty before the bridge is up. */
	const FString& GetMasterLine() const { return GetText(EScctDashboardText::MasterLine); }

	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaSeconds);
	void ReadAll();

	static TWeakObjectPtr<UScctDashboardSubsystem> Current;

	const FScctDashboardApi* Api = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;
	FScctDashboardInfo Info = {};
	int32 SeenRevision = -1;
	TArray<FString> Texts;
	TArray<FScctDashboardRelease> Releases;
	double LastShown = -1.0;
	bool bWatched = false;
	bool bPreview = false;
};
