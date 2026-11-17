#pragma once

#include "CoreMinimal.h"

/**
 * Where the master's sign-in (the account's session token) is kept between runs. Never in plain text: on Windows the
 * token is sealed with DPAPI for the Windows user (CryptProtectData, with OpenSCCT's own entropy) in
 * Saved/Account/AccountToken.dpapi, so another user of the PC, or a copy of the file on another PC, cannot read it.
 * Other platforms get a store of their own later; until then they keep nothing and the player signs in each run.
 */
class IScctTokenStore
{
public:
	virtual ~IScctTokenStore() = default;

	/** The saved token, or false when none is saved (or it cannot be read on this PC by this user). */
	virtual bool Load(FString& OutToken) = 0;

	/** Seals and saves the token. False when it could not be. */
	virtual bool Save(const FString& Token) = 0;

	/** Forgets the saved token. */
	virtual void Clear() = 0;

	/** What the store is, for the log ("DPAPI (Windows user)"). */
	virtual FString Describe() const = 0;
};

/**
 * The account token as the online and hub subsystems use it, over the platform's IScctTokenStore.
 *   - -ScctAccountToken=<token> uses that token for this run only: it is never saved, and signing out leaves the
 *     saved one alone (a developer's override);
 *   - a token an earlier build left in plain text in GameUserSettings.ini ([OpenSCCT.Online] AccountToken) is moved
 *     into the store once and removed from the file.
 * Game thread only.
 */
namespace ScctAccountToken
{
	IScctTokenStore& Store();

	/** The token to sign in with: the run's override, else the saved one; empty when there is none. */
	FString Load();

	/** Keeps a new sign-in for the next run (not while the run's override is in use). */
	void Save(const FString& Token);

	/** Forgets the saved sign-in (not while the run's override is in use). */
	void Clear();

	/** -ScctAccountToken= gave this run's token. */
	bool HasRunOverride();
}
