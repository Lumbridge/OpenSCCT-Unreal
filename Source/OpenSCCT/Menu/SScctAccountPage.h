#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SEditableTextBox;
class SVerticalBox;
class UScctAccountSubsystem;
class UScctOnlineSubsystem;

/**
 * The menus' Account page (the Godot client's AccountScreen), drawn from
 * UScctAccountSubsystem:
 *   - signed out: what an account gives, and SIGN IN WITH DISCORD (the browser opens the master's page; the page
 *     shows the code to check there and waits, with a way to give up);
 *   - signed in without a name: the name to claim (3 to 16 letters, digits, _ - and .);
 *   - signed in: the name with CHANGE NAME (locked with the date while the cooldown runs) and SIGN OUT, the
 *     rank with its XP bar, lifetime XP, the prestige button (pressed twice: the second press confirms)
 *     and the prestige emblems, the career snapshot, and the title to show under the name (unlocked ones to pick, the
 *     locked ones on request with how to unlock them).
 * Settings and key layouts follow the account; the page says where the sync stands.
 *
 * Unattended (for screenshots): -ScctAccountPress=changename|prestige|locked presses that once, two seconds after the
 * page is shown (prestige only arms the button: nothing is sent).
 */
class SScctAccountPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctAccountPage) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctAccountSubsystem>, Account)
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		SLATE_EVENT(FSimpleDelegate, OnBack)
		/** Opens another page of the menus by name (the Stats, Match history and Achievements links). */
		SLATE_ARGUMENT(TFunction<void(const FString&)>, OnOpen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SScctAccountPage() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void Rebuild();

	TSharedRef<SWidget> BuildNoMaster();
	TSharedRef<SWidget> BuildSignedOut();
	TSharedRef<SWidget> BuildSigningIn();
	TSharedRef<SWidget> BuildPickName();
	TSharedRef<SWidget> BuildSignedIn();

	TSharedRef<SWidget> BuildIdentity();
	TSharedRef<SWidget> BuildRank();
	TSharedRef<SWidget> BuildCareer();
	TSharedRef<SWidget> BuildTitles();

	void Claim();
	void PressPrestige();

	FText StatusLine() const;

	TWeakObjectPtr<UScctAccountSubsystem> Account;
	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	FSimpleDelegate OnBack;
	TFunction<void(const FString&)> OnOpen;
	FDelegateHandle ChangedHandle;

	TSharedPtr<SBox> ContentBox;
	/** Kept across rebuilds so what the player typed survives the account's updates. */
	TSharedPtr<SEditableTextBox> NameBox;

	bool bDirty = true;
	bool bChangingName = false;
	bool bPrestigeArmed = false;
	bool bShowLocked = false;

	FString AutoPress;
	float AutoPressTimer = 2.0f;
};
