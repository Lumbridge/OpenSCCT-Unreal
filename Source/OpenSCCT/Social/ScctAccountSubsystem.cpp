#include "Social/ScctAccountSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Menu/ScctUserSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "Runtime/ScctFeatures.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/ScctSettings.h"
#include "Settings/ScctSettingsForced.h"
#include "Social/ScctSocialSubsystem.h"

const TCHAR* const UScctAccountSubsystem::TitleSetting = TEXT("Account.Title");

namespace
{
	/** Seconds between two looks at the control files' dates. */
	constexpr float FileCheckSeconds = 1.0f;

	/** A text of any length from a call that returns its full length. */
	template <typename TRead>
	FString ReadAll(TRead&& Read)
	{
		TCHAR Short[512];
		const int32 Length = Read(Short, UE_ARRAY_COUNT(Short));
		if (Length < 0)
		{
			return FString();
		}
		if (Length < UE_ARRAY_COUNT(Short))
		{
			return FString(Short);
		}
		TArray<TCHAR> Long;
		Long.SetNumZeroed(Length + 1);
		Read(Long.GetData(), Long.Num());
		return FString(Long.GetData());
	}

	FString LoadText(const FString& Path)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			Text.Reset();
		}
		return Text;
	}

	FDateTime StampOf(const FString& Path)
	{
		return IFileManager::Get().GetTimeStamp(*Path);
	}

	/**
	 * The volumes of UScctUserSettings that follow the account. The Godot client syncs every setting but the display,
	 * the frame caps and the devices; here the same goes for the user settings: the volumes, the brightness, motion
	 * blur and the HUD's distance unit travel, the render scale and the window stay on this PC.
	 */
	struct FScctAccountSyncedVolume
	{
		const TCHAR* Key;
		int32 UScctUserSettings::* Field;
	};

	const FScctAccountSyncedVolume ScctAccountSyncedVolumes[] = {
		{ TEXT("User.MasterVolume"), &UScctUserSettings::MasterVolume },
		{ TEXT("User.EffectsVolume"), &UScctUserSettings::EffectsVolume },
		{ TEXT("User.AmbientVolume"), &UScctUserSettings::AmbientVolume },
		{ TEXT("User.MusicVolume"), &UScctUserSettings::MusicVolume },
		{ TEXT("User.MenuMusicVolume"), &UScctUserSettings::MenuMusicVolume },
		{ TEXT("User.MenuSoundsVolume"), &UScctUserSettings::MenuSoundsVolume },
		{ TEXT("User.ObjectiveVolume"), &UScctUserSettings::ObjectiveVolume },
		{ TEXT("User.VoiceVolume"), &UScctUserSettings::VoiceVolume },
		{ TEXT("User.HubJukeboxVolume"), &UScctUserSettings::HubJukeboxVolume },
		{ TEXT("User.HubChimesVolume"), &UScctUserSettings::HubChimesVolume },
	};

	const TCHAR* const ScctAccountGammaKey = TEXT("User.Gamma");
	const TCHAR* const ScctAccountMotionBlurKey = TEXT("User.MotionBlur");
	const TCHAR* const ScctAccountFeetKey = TEXT("User.DistanceInFeet");

	/** The synced user settings into Object, as text like the registry's values. */
	void ScctAccountWriteUserSettings(FJsonObject& Object)
	{
		const UScctUserSettings* User = UScctUserSettings::Get();
		for (const FScctAccountSyncedVolume& Volume : ScctAccountSyncedVolumes)
		{
			Object.SetStringField(Volume.Key, FString::FromInt(User->*Volume.Field));
		}
		Object.SetStringField(ScctAccountGammaKey, FString::SanitizeFloat(User->Gamma));
		Object.SetStringField(ScctAccountMotionBlurKey, User->bMotionBlur ? TEXT("true") : TEXT("false"));
		Object.SetStringField(ScctAccountFeetKey, User->bDistanceInFeet ? TEXT("true") : TEXT("false"));
	}

	/** One synced user setting from the account's copy. False when Key is not one of them. */
	bool ScctAccountReadUserSetting(const FString& Key, const FString& Value)
	{
		UScctUserSettings* User = UScctUserSettings::Get();
		for (const FScctAccountSyncedVolume& Volume : ScctAccountSyncedVolumes)
		{
			if (Key == Volume.Key)
			{
				User->*Volume.Field = FMath::Clamp(FCString::Atoi(*Value), 0, 100);
				return true;
			}
		}
		if (Key == ScctAccountGammaKey)
		{
			User->Gamma = FMath::Clamp(FCString::Atof(*Value), 1.6f, 3.0f);
			return true;
		}
		if (Key == ScctAccountMotionBlurKey)
		{
			User->bMotionBlur = Value.ToBool();
			return true;
		}
		if (Key == ScctAccountFeetKey)
		{
			User->bDistanceInFeet = Value.ToBool();
			return true;
		}
		return false;
	}

	/** The synced user settings as one line, to tell when one of them changed. */
	FString ScctAccountUserSnapshot()
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		ScctAccountWriteUserSettings(*Object);
		FString Line;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
		{
			Line += Field.Key + TEXT("=") + Field.Value->AsString() + TEXT(";");
		}
		return Line;
	}
}

UScctAccountSubsystem* UScctAccountSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Instance = World != nullptr ? World->GetGameInstance() : nullptr;
	return Instance != nullptr ? Instance->GetSubsystem<UScctAccountSubsystem>() : nullptr;
}

void UScctAccountSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// The social subsystem runs the bridge's social Update, which moves the account service.
	Collection.InitializeDependency<UScctSocialSubsystem>();
	Super::Initialize(Collection);

	const TCHAR* CommandLine = FCommandLine::Get();
	bPreview = FParse::Param(CommandLine, TEXT("ScctAccountPreview"));
	bSyncOff = FParse::Param(CommandLine, TEXT("ScctNoAccountSync"));
	FParse::Value(CommandLine, TEXT("ScctAccountClaim="), AutoClaim);
	FString AutoSetting;
	if (FParse::Value(CommandLine, TEXT("ScctAccountSetting="), AutoSetting))
	{
		// <key>=<value>@<seconds>
		FString KeyValue = AutoSetting, Seconds = TEXT("8");
		AutoSetting.Split(TEXT("@"), &KeyValue, &Seconds, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (KeyValue.Split(TEXT("="), &AutoSettingKey, &AutoSettingValue))
		{
			AutoSettingAt = FCString::Atof(*Seconds);
		}
	}

	FScctSettingDesc Title;
	Title.Key = TitleSetting;
	Title.Page = TEXT("Online");
	Title.Label = NSLOCTEXT("ScctAccount", "Title", "Player title");
	Title.Tooltip = NSLOCTEXT("ScctAccount", "TitleTip", "The title shown under your name; chosen on the Account page.");
	Title.Type = EScctSettingType::Text;
	Title.Default = FString();
	// Chosen on the Account page from the account's unlocked titles, not typed on the settings page.
	Title.bVisible = false;
	FScctSettings::Register(Title);
	SettingHandle = FScctSettings::OnChanged().AddUObject(this, &UScctAccountSubsystem::HandleSettingChanged);

	ControlsStamp = StampOf(UScctControlSettings::GetSettingsFile());
	BindingsStamp = StampOf(UScctControlSettings::GetBindingsFile());
	UserSnapshot = ScctAccountUserSnapshot();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctAccountSubsystem::Tick));
}

void UScctAccountSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	FScctSettings::OnChanged().Remove(SettingHandle);
	Api = nullptr;
	Super::Deinitialize();
}

bool UScctAccountSubsystem::Connect()
{
	// The social table first: it brings the online bridge (the master and the sign-in) up and runs the service.
	if (ScctFeature<FScctSocialApi>() == nullptr)
	{
		return false;
	}
	Api = ScctFeature<FScctAccountApi>();
	if (Api == nullptr)
	{
		return false;
	}

	const FString StateFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Account") / TEXT("account.json"));
	Api->Start(*StateFile, (bSyncOff ? EScctAccountStartFlags::SyncOff : 0) | (bPreview ? EScctAccountStartFlags::Preview : 0));
	Api->SetChosenTitle(*FScctSettings::GetString(TitleSetting));
	SendLocalProfile(false);
	UE_LOG(LogOpenSCCT, Log, TEXT("Account: ready%s%s"), bPreview ? TEXT(" (preview account)") : TEXT(""), bSyncOff ? TEXT(", settings sync off") : TEXT(""));
	return true;
}

bool UScctAccountSubsystem::Tick(float DeltaSeconds)
{
	if (Api == nullptr && !Connect())
	{
		return true;
	}

	ApplyIncoming();

	FileCheckTimer += DeltaSeconds;
	if (FileCheckTimer >= FileCheckSeconds)
	{
		FileCheckTimer = 0.0f;
		if (ControlFilesChanged() || UserSettingsChanged())
		{
			bLocalDirty = true;
		}
	}
	if (bLocalDirty)
	{
		// Several settings changed in one frame (a page's Reset) send once.
		bLocalDirty = false;
		SendLocalProfile(true);
	}

	Api->GetInfo(&Info);
	if (Info.Revision != SeenRevision)
	{
		SeenRevision = Info.Revision;
		if (Info.View != SeenView)
		{
			SeenView = Info.View;
			static const TCHAR* const Views[] = { TEXT("no master"), TEXT("signed out"), TEXT("signing in"), TEXT("pick a name"), TEXT("signed in") };
			UE_LOG(LogOpenSCCT, Log, TEXT("Account: page view %s"), Views[FMath::Clamp(Info.View, 0, 4)]);
		}
		OnChanged.Broadcast();
	}

	RunSwitches(DeltaSeconds);
	return true;
}

void UScctAccountSubsystem::RunSwitches(float DeltaSeconds)
{
	RunSeconds += DeltaSeconds;
	if (AutoSettingAt >= 0.0f && RunSeconds >= AutoSettingAt)
	{
		AutoSettingAt = -1.0f;
		if (AutoSettingKey.StartsWith(TEXT("User.")) && ScctAccountReadUserSetting(AutoSettingKey, AutoSettingValue))
		{
			// A user setting, as the Audio or Video tab would change and save it.
			UE_LOG(LogOpenSCCT, Log, TEXT("Account: setting %s to %s (-ScctAccountSetting)"), *AutoSettingKey, *AutoSettingValue);
			UScctUserSettings::Get()->Save();
			UScctUserSettings::Get()->ApplyAll();
		}
		else if (FScctSettings::Find(FName(*AutoSettingKey)) == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Account: -ScctAccountSetting names no registered setting '%s'"), *AutoSettingKey);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Account: setting %s to %s (-ScctAccountSetting)"), *AutoSettingKey, *AutoSettingValue);
			FScctSettings::SetString(FName(*AutoSettingKey), AutoSettingValue);
			FScctSettings::Save();
		}
	}

	if (!bAutoClaimed && !AutoClaim.IsEmpty() && GetView() == EScctAccountView::PickName && HasFlag(EScctAccountFlags::Known))
	{
		bAutoClaimed = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("Account: claiming '%s' (-ScctAccountClaim)"), *AutoClaim);
		ClaimName(AutoClaim);
	}
}

// ---- the settings sync ----------------------------------------------------------------------------------------------

bool UScctAccountSubsystem::IsSynced(const FName& Page, const FName& Key)
{
	// What belongs to one PC stays on it (the Godot client's ProfileSync.LocalKeys: display, frame caps, devices).
	static const FName LocalPages[] = { TEXT("Display"), TEXT("Graphics"), TEXT("Performance") };
	for (const FName& Local : LocalPages)
	{
		if (Page == Local)
		{
			return false;
		}
	}
	// This PC's network: the master it signs in to (the sync's own way in), its ports, port mapping and connect path.
	static const FName LocalKeys[] = { TEXT("Online.MasterUrl"), TEXT("Online.InviteKey"), TEXT("Online.ConnectMode"), TEXT("Online.LanPort"),
		TEXT("Online.HostPort"), TEXT("Online.HostUpnp") };
	for (const FName& Local : LocalKeys)
	{
		if (Key == Local)
		{
			return false;
		}
	}
	// A value forced for one run (-ScctSettingSet) is not the player's choice.
	if (FScctSettingsForced::IsForced(Key))
	{
		return false;
	}
	return !Key.ToString().Contains(TEXT("Device"));
}

void UScctAccountSubsystem::HandleSettingChanged(FName Key)
{
	if (Api == nullptr)
	{
		return;
	}
	if (Key == TitleSetting)
	{
		Api->SetChosenTitle(*FScctSettings::GetString(TitleSetting));
	}
	const FScctSettingDesc* Desc = FScctSettings::Find(Key);
	if (!bApplying && Desc != nullptr && IsSynced(Desc->Page, Key))
	{
		bLocalDirty = true;
	}
}

FString UScctAccountSubsystem::BuildSettingsJson() const
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	for (const FName& Page : FScctSettings::Pages())
	{
		for (const FScctSettingDesc* Desc : FScctSettings::OptionsOf(Page))
		{
			if (IsSynced(Page, Desc->Key))
			{
				Object->SetStringField(Desc->Key.ToString(), FScctSettings::GetString(Desc->Key));
			}
		}
	}
	// Hidden from the settings page, but the player's choice all the same.
	Object->SetStringField(TitleSetting, FScctSettings::GetString(TitleSetting));
	// The volumes, brightness, motion blur and distance unit, which UScctUserSettings holds.
	ScctAccountWriteUserSettings(*Object);

	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Object, Writer);
	return Json;
}

void UScctAccountSubsystem::SendLocalProfile(bool bChanged)
{
	if (Api == nullptr)
	{
		return;
	}
	const FString Settings = BuildSettingsJson();
	const FString Controls = LoadText(UScctControlSettings::GetSettingsFile());
	const FString Bindings = LoadText(UScctControlSettings::GetBindingsFile());
	Api->SetLocalProfile(*Settings, *Controls, *Bindings, bChanged ? 1 : 0);
}

bool UScctAccountSubsystem::UserSettingsChanged()
{
	const FString Now = ScctAccountUserSnapshot();
	if (Now == UserSnapshot)
	{
		return false;
	}
	UserSnapshot = Now;
	if (!bApplying)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Account: a volume, the brightness, motion blur or the distance unit changed"));
	}
	return !bApplying;
}

bool UScctAccountSubsystem::ControlFilesChanged()
{
	const FDateTime Controls = StampOf(UScctControlSettings::GetSettingsFile());
	const FDateTime Bindings = StampOf(UScctControlSettings::GetBindingsFile());
	const bool bChanged = Controls != ControlsStamp || Bindings != BindingsStamp;
	ControlsStamp = Controls;
	BindingsStamp = Bindings;
	if (bChanged)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Account: the control settings or key layouts were saved"));
	}
	return bChanged;
}

void UScctAccountSubsystem::ApplyIncoming()
{
	const auto Part = [this](EScctProfilePart Which)
	{
		return ReadAll([this, Which](TCHAR* Buffer, int32 Capacity) { return Api->GetIncomingProfile(static_cast<int32>(Which), Buffer, Capacity); });
	};
	TCHAR Probe[2];
	if (Api->GetIncomingProfile(static_cast<int32>(EScctProfilePart::Settings), Probe, UE_ARRAY_COUNT(Probe)) < 0)
	{
		return;
	}

	const FString SettingsJson = Part(EScctProfilePart::Settings);
	const FString Controls = Part(EScctProfilePart::Controls);
	const FString Bindings = Part(EScctProfilePart::Bindings);

	// Put in without counting as the player's own change (nothing is sent back).
	bApplying = true;
	int32 Taken = 0;
	int32 TakenUser = 0;
	TSharedPtr<FJsonObject> Object;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(SettingsJson), Object) && Object.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
		{
			const FName Key(*Field.Key);
			const FScctSettingDesc* Desc = FScctSettings::Find(Key);
			FString Value;
			// Options this build does not have, and the ones that belong to one PC, are left alone.
			if (!Field.Value.IsValid() || !Field.Value->TryGetString(Value))
			{
				continue;
			}
			if (Desc != nullptr && IsSynced(Desc->Page, Key))
			{
				FScctSettings::SetString(Key, Value);
				++Taken;
			}
			else if (Desc == nullptr && ScctAccountReadUserSetting(Field.Key, Value))
			{
				++TakenUser;
			}
		}
		FScctSettings::Save();
		if (TakenUser > 0)
		{
			UScctUserSettings* User = UScctUserSettings::Get();
			User->Save();
			User->ApplyAll();
		}
		UserSnapshot = ScctAccountUserSnapshot();
	}

	bool bControls = false;
	if (!Controls.IsEmpty() && FFileHelper::SaveStringToFile(Controls, *UScctControlSettings::GetSettingsFile(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		bControls = true;
	}
	if (!Bindings.IsEmpty() && FFileHelper::SaveStringToFile(Bindings, *UScctControlSettings::GetBindingsFile(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		bControls = true;
	}
	if (bControls)
	{
		// The bridge plays with the new files from now on; a match reads them on its next frame.
		UScctControlSettings* ControlSettings = UScctControlSettings::Get();
		ControlSettings->Load();
		ControlSettings->LoadBindings();
		ControlsStamp = StampOf(UScctControlSettings::GetSettingsFile());
		BindingsStamp = StampOf(UScctControlSettings::GetBindingsFile());
	}
	bApplying = false;
	bLocalDirty = false;

	UE_LOG(LogOpenSCCT, Log, TEXT("Account: applied the account's settings (%d setting(s), %d user setting(s)%s)"), Taken, TakenUser, bControls ? TEXT(", the key layouts and control settings") : TEXT(""));
	Api->FinishIncomingProfile(1);
	SendLocalProfile(false);
}

// ---- what the page reads and does -----------------------------------------------------------------------------------

FString UScctAccountSubsystem::GetText(EScctAccountText Kind) const
{
	if (Api == nullptr)
	{
		return FString();
	}
	return ReadAll([this, Kind](TCHAR* Buffer, int32 Capacity) { return Api->GetText(static_cast<int32>(Kind), Buffer, Capacity); });
}

TArray<FScctAccountTitleRow> UScctAccountSubsystem::GetTitles() const
{
	TArray<FScctAccountTitleRow> Rows;
	if (Api == nullptr)
	{
		return Rows;
	}
	for (int32 Index = 0; Index < 256; ++Index)
	{
		FScctAccountTitle Title = {};
		TCHAR Id[64];
		TCHAR Text[128];
		TCHAR How[256];
		if (Api->GetTitle(Index, &Title, Id, UE_ARRAY_COUNT(Id), Text, UE_ARRAY_COUNT(Text), How, UE_ARRAY_COUNT(How)) == 0)
		{
			break;
		}
		FScctAccountTitleRow& Row = Rows.AddDefaulted_GetRef();
		Row.Id = Id;
		Row.Text = Text;
		Row.HowToUnlock = How;
		Row.bUnlocked = Title.Unlocked != 0;
		Row.bChosen = Title.Chosen != 0;
	}
	return Rows;
}

FLinearColor UScctAccountSubsystem::ToColour(int32 Rgb)
{
	return FLinearColor(FColor((Rgb >> 16) & 0xFF, (Rgb >> 8) & 0xFF, Rgb & 0xFF));
}

FLinearColor UScctAccountSubsystem::EmblemColour(int32 Prestige) const
{
	return ToColour(Api != nullptr ? Api->EmblemColour(Prestige) : 0xFFFFFF);
}

bool UScctAccountSubsystem::DescribeRank(int32 Packed, FString& OutText, FLinearColor& OutColour) const
{
	OutText.Reset();
	OutColour = FLinearColor::White;
	if (Api == nullptr)
	{
		return false;
	}
	FScctAccountRank Rank = {};
	TCHAR Text[128];
	Api->DescribeRank(Packed, &Rank, Text, UE_ARRAY_COUNT(Text));
	OutText = Text;
	OutColour = ToColour(Rank.Colour);
	return Rank.Level > 0;
}

void UScctAccountSubsystem::Refresh(bool bForce)
{
	if (Api != nullptr)
	{
		Api->Refresh(bForce ? 1 : 0);
	}
}

bool UScctAccountSubsystem::ClaimName(const FString& Name)
{
	return Api != nullptr && Api->ClaimName(*Name) != 0;
}

bool UScctAccountSubsystem::Prestige()
{
	return Api != nullptr && Api->Prestige() != 0;
}

bool UScctAccountSubsystem::SignOut()
{
	return Api != nullptr && Api->SignOut() != 0;
}

void UScctAccountSubsystem::BeginSignIn()
{
	if (UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>())
	{
		Online->BeginSignIn();
	}
}

void UScctAccountSubsystem::SetTitle(const FString& Id)
{
	FScctSettings::SetString(TitleSetting, Id);
	FScctSettings::Save();
}
