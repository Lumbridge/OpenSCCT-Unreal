#include "Online/ScctTokenStore.h"

#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincrypt.h>
#include <dpapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	/** The section and key earlier builds kept the token under, in plain text. */
	const TCHAR* const ScctTokenLegacySection = TEXT("OpenSCCT.Online");
	const TCHAR* const ScctTokenLegacyKey = TEXT("AccountToken");

	FString ScctTokenFile()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Account") / TEXT("AccountToken.dpapi"));
	}

#if PLATFORM_WINDOWS
	/** DPAPI's extra secret: a file sealed by another program for the same user does not open as ours, nor ours as theirs. */
	DATA_BLOB ScctTokenEntropy()
	{
		static const char Entropy[] = "OpenSCCT.AccountToken.v1";
		DATA_BLOB Blob;
		Blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(Entropy));
		Blob.cbData = sizeof(Entropy) - 1;
		return Blob;
	}

	/** The token sealed with DPAPI for the Windows user. */
	class FScctDpapiTokenStore final : public IScctTokenStore
	{
	public:
		virtual bool Load(FString& OutToken) override
		{
			OutToken.Reset();
			TArray<uint8> Sealed;
			if (!FFileHelper::LoadFileToArray(Sealed, *ScctTokenFile(), FILEREAD_Silent) || Sealed.Num() == 0)
			{
				return false;
			}

			DATA_BLOB In;
			In.pbData = Sealed.GetData();
			In.cbData = static_cast<DWORD>(Sealed.Num());
			DATA_BLOB Entropy = ScctTokenEntropy();
			DATA_BLOB Out = {};
			if (!CryptUnprotectData(&In, nullptr, &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Out))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Account token: the saved sign-in cannot be opened by this Windows user (error %u); sign in again"),
					static_cast<uint32>(GetLastError()));
				return false;
			}
			const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Out.pbData), static_cast<int32>(Out.cbData));
			OutToken = FString(Text.Length(), Text.Get());
			SecureZeroMemory(Out.pbData, Out.cbData);
			LocalFree(Out.pbData);
			return !OutToken.IsEmpty();
		}

		virtual bool Save(const FString& Token) override
		{
			FTCHARToUTF8 Utf8(*Token);
			DATA_BLOB In;
			In.pbData = reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(Utf8.Get()));
			In.cbData = static_cast<DWORD>(Utf8.Length());
			DATA_BLOB Entropy = ScctTokenEntropy();
			DATA_BLOB Out = {};
			if (!CryptProtectData(&In, L"OpenSCCT account sign-in", &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Out))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Account token: the sign-in could not be sealed (error %u); it is kept for this run only"),
					static_cast<uint32>(GetLastError()));
				return false;
			}
			const TArray<uint8> Sealed(Out.pbData, static_cast<int32>(Out.cbData));
			LocalFree(Out.pbData);
			const bool bSaved = FFileHelper::SaveArrayToFile(Sealed, *ScctTokenFile());
			if (!bSaved)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Account token: could not write %s; the sign-in is kept for this run only"), *ScctTokenFile());
			}
			return bSaved;
		}

		virtual void Clear() override
		{
			IFileManager::Get().Delete(*ScctTokenFile(), false, true, true);
		}

		virtual FString Describe() const override
		{
			return TEXT("DPAPI, sealed for this Windows user");
		}
	};
#endif

	/** Platforms without a sealed store yet keep nothing: the player signs in each run. */
	class FScctNoTokenStore final : public IScctTokenStore
	{
	public:
		virtual bool Load(FString& OutToken) override { OutToken.Reset(); return false; }
		virtual bool Save(const FString&) override { return false; }
		virtual void Clear() override {}
		virtual FString Describe() const override { return TEXT("none on this platform (sign in each run)"); }
	};

	bool bScctTokenMigrated = false;

	/** A token earlier builds left in plain text moves into the store, and leaves the file. */
	void ScctTokenMigrateLegacy(IScctTokenStore& Store)
	{
		if (bScctTokenMigrated || GConfig == nullptr)
		{
			return;
		}
		bScctTokenMigrated = true;
		FString Plain;
		if (!GConfig->GetString(ScctTokenLegacySection, ScctTokenLegacyKey, Plain, GGameUserSettingsIni))
		{
			return;
		}
		if (!Plain.IsEmpty())
		{
			FString Existing;
			if (!Store.Load(Existing))
			{
				Store.Save(Plain);
			}
		}
		GConfig->RemoveKey(ScctTokenLegacySection, ScctTokenLegacyKey, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		UE_LOG(LogOpenSCCT, Log, TEXT("Account token: moved the sign-in out of GameUserSettings.ini into the sealed store"));
	}
}

namespace ScctAccountToken
{
	IScctTokenStore& Store()
	{
#if PLATFORM_WINDOWS
		static FScctDpapiTokenStore Instance;
#else
		static FScctNoTokenStore Instance;
#endif
		return Instance;
	}

	bool HasRunOverride()
	{
		FString Token;
		return FParse::Value(FCommandLine::Get(), TEXT("ScctAccountToken="), Token) && !Token.IsEmpty();
	}

	FString Load()
	{
		FString Token;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctAccountToken="), Token) && !Token.IsEmpty())
		{
			static bool bLogged = false;
			if (!bLogged)
			{
				bLogged = true;
				UE_LOG(LogOpenSCCT, Log, TEXT("Account token: using -ScctAccountToken for this run (never saved)"));
			}
			return Token;
		}

		IScctTokenStore& Sealed = Store();
		ScctTokenMigrateLegacy(Sealed);
		if (!Sealed.Load(Token))
		{
			Token.Reset();
		}
		return Token;
	}

	void Save(const FString& Token)
	{
		if (Token.IsEmpty())
		{
			return;
		}
		if (HasRunOverride())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Account token: not saved (-ScctAccountToken is in use for this run)"));
			return;
		}
		if (Store().Save(Token))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Account token: sign-in saved (%s)"), *Store().Describe());
		}
	}

	void Clear()
	{
		if (HasRunOverride())
		{
			return;
		}
		Store().Clear();
		UE_LOG(LogOpenSCCT, Log, TEXT("Account token: the saved sign-in was forgotten"));
	}
}
