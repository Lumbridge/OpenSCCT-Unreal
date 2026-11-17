#include "Play/ScctControlSettings.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	FScctLookOptions LookFromBridge(const FScctLookSettings& In)
	{
		FScctLookOptions Out;
		Out.bWalkHold = In.WalkHold != 0;
		Out.bScopeHold = In.ScopeHold != 0;
		Out.bPerspectiveHold = In.PerspectiveHold != 0;
		Out.bCloakHold = In.CloakHold != 0;
		Out.FreeLook = In.FreeLook;
		Out.Look = In.Look;
		Out.Scope = In.Scope;
		Out.ScopeVertical = In.ScopeVertical;
		Out.Vertical = In.Vertical;
		Out.Cam = In.Cam;
		Out.LookResponse = static_cast<EScctLookResponse>(FMath::Clamp(In.LookResponse, 0, 3));
		Out.GunLookResponse = static_cast<EScctLookResponse>(FMath::Clamp(In.GunLookResponse, 0, 3));
		Out.ClutchOne = In.ClutchOne;
		Out.bClutchOneEnabled = In.ClutchOneEnabled != 0;
		Out.ClutchTwo = In.ClutchTwo;
		Out.bClutchTwoEnabled = In.ClutchTwoEnabled != 0;
		Out.bSwapLsLb = In.SwapLsLb != 0;
		Out.bSwapRsRb = In.SwapRsRb != 0;
		return Out;
	}

	FScctLookSettings LookToBridge(const FScctLookOptions& In)
	{
		FScctLookSettings Out = {};
		Out.WalkHold = In.bWalkHold ? 1 : 0;
		Out.ScopeHold = In.bScopeHold ? 1 : 0;
		Out.PerspectiveHold = In.bPerspectiveHold ? 1 : 0;
		Out.CloakHold = In.bCloakHold ? 1 : 0;
		Out.FreeLook = In.FreeLook;
		Out.Look = In.Look;
		Out.Scope = In.Scope;
		Out.ScopeVertical = In.ScopeVertical;
		Out.Vertical = In.Vertical;
		Out.Cam = In.Cam;
		Out.LookResponse = static_cast<int32>(In.LookResponse);
		Out.GunLookResponse = static_cast<int32>(In.GunLookResponse);
		Out.ClutchOne = In.ClutchOne;
		Out.ClutchOneEnabled = In.bClutchOneEnabled ? 1 : 0;
		Out.ClutchTwo = In.ClutchTwo;
		Out.ClutchTwoEnabled = In.bClutchTwoEnabled ? 1 : 0;
		Out.SwapLsLb = In.bSwapLsLb ? 1 : 0;
		Out.SwapRsRb = In.bSwapRsRb ? 1 : 0;
		return Out;
	}

	/** Reads one of the table's texts, which are handed over as "copy what fits, return the full length". */
	template <typename TRead>
	FString ReadText(TRead&& Read)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(512);
		const int32 Length = Read(Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Read(Buffer.GetData(), Buffer.Num());
		}
		Buffer.Last() = TEXT('\0');
		return FString(Buffer.GetData());
	}

	/** Makes the folder of a file the bridge is about to write. */
	void MakeFolderOf(const FString& File)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	}
}

UScctControlSettings* UScctControlSettings::Get()
{
	return GetMutableDefault<UScctControlSettings>();
}

FString UScctControlSettings::GetSettingsFile()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Controls") / TEXT("controls.json"));
}

FString UScctControlSettings::GetBindingsFile()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Controls") / TEXT("bindings.json"));
}

bool UScctControlSettings::FailFromBridge(const TCHAR* Fallback)
{
	LastError = Fallback;
	if (const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>())
	{
		const FString Text = ReadText([Controls](TCHAR* Buffer, int32 Capacity) { return Controls->LastError(Buffer, Capacity); });
		if (!Text.IsEmpty())
		{
			LastError = Text;
		}
	}
	UE_LOG(LogOpenSCCT, Warning, TEXT("Control settings: %s"), *LastError);
	return false;
}

bool UScctControlSettings::EnsureLoaded()
{
	if (bLoaded)
	{
		return true;
	}

	// A file that was refused still leaves the bridge with usable values (its defaults), so this is not tried
	// again every frame; only a bridge that is not up yet is.
	if (ScctFeature<FScctControlsApi>() == nullptr)
	{
		return false;
	}
	bLoaded = true;
	const bool bSettings = Load();
	const bool bBindings = LoadBindings();
	UE_LOG(LogOpenSCCT, Log, TEXT("Control settings loaded from %s (settings %s, bindings %s): invert Y %d, mouse scale %.2f, spy free look %.2f, spy gun look %.2f, merc look %.2f, controller %d"),
		*FPaths::GetPath(GetSettingsFile()), bSettings ? TEXT("ok") : TEXT("refused"), bBindings ? TEXT("ok") : TEXT("refused"),
		bInvertY ? 1 : 0, MouseScale, SpyMouse.FreeLook, SpyMouse.Look, MercMouse.Look, bPadEnabled ? 1 : 0);
	return bSettings && bBindings;
}

bool UScctControlSettings::Load()
{
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>(&LastError);
	if (Controls == nullptr)
	{
		return false;
	}

	const bool bRead = Controls->LoadSettings(*GetSettingsFile()) != 0;
	// Whatever the file was, the properties show what the bridge now plays with.
	FScctControlSettings Now = {};
	Controls->GetSettings(&Now);
	FromBridge(Now);
	return bRead || FailFromBridge(TEXT("The control settings file could not be read."));
}

bool UScctControlSettings::Apply()
{
	// The files first: otherwise this would act on the defaults and a save after it would write them over the
	// player's own.
	EnsureLoaded();
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>(&LastError);
	if (Controls == nullptr)
	{
		return false;
	}

	FScctControlSettings Wanted = {};
	ToBridge(Wanted);
	Controls->SetSettings(&Wanted);
	Controls->GetSettings(&Wanted);
	FromBridge(Wanted);
	return true;
}

bool UScctControlSettings::Save()
{
	if (!Apply())
	{
		return false;
	}

	const FString File = GetSettingsFile();
	MakeFolderOf(File);
	if (ScctFeature<FScctControlsApi>()->SaveSettings(*File) == 0)
	{
		return FailFromBridge(TEXT("The control settings file could not be written."));
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Control settings saved to %s"), *File);
	return true;
}

bool UScctControlSettings::Refresh()
{
	EnsureLoaded();
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>(&LastError);
	if (Controls == nullptr)
	{
		return false;
	}

	FScctControlSettings Now = {};
	Controls->GetSettings(&Now);
	FromBridge(Now);
	return true;
}

bool UScctControlSettings::LoadBindings()
{
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>(&LastError);
	if (Controls == nullptr)
	{
		return false;
	}
	return Controls->LoadBindings(*GetBindingsFile()) != 0 || FailFromBridge(TEXT("The bindings file could not be read."));
}

bool UScctControlSettings::SaveBindings()
{
	EnsureLoaded();
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>(&LastError);
	if (Controls == nullptr)
	{
		return false;
	}

	const FString File = GetBindingsFile();
	MakeFolderOf(File);
	if (Controls->SaveBindings(*File) == 0)
	{
		return FailFromBridge(TEXT("The bindings file could not be written."));
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Bindings saved to %s"), *File);
	return true;
}

FString UScctControlSettings::GetBinding(bool bMerc, bool bPad, const FString& Key) const
{
	Get()->EnsureLoaded();
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>();
	if (Controls == nullptr)
	{
		return FString();
	}
	return ReadText([&](TCHAR* Buffer, int32 Capacity) { return Controls->GetBinding(bMerc ? 1 : 0, bPad ? 1 : 0, *Key, Buffer, Capacity); });
}

bool UScctControlSettings::SetBinding(bool bMerc, bool bPad, const FString& Key, const FString& Command)
{
	EnsureLoaded();
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>(&LastError);
	if (Controls == nullptr)
	{
		return false;
	}
	if (Controls->SetBinding(bMerc ? 1 : 0, bPad ? 1 : 0, *Key, *Command) == 0)
	{
		LastError = FString::Printf(TEXT("'%s' is not the name of a %s."), *Key, bPad ? TEXT("controller control") : TEXT("key"));
		return false;
	}
	return true;
}

void UScctControlSettings::ResetBindings(bool bMerc)
{
	EnsureLoaded();
	if (const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>())
	{
		Controls->ResetBindings(bMerc ? 1 : 0);
	}
}

FString UScctControlSettings::GetBindingsJson() const
{
	Get()->EnsureLoaded();
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>();
	if (Controls == nullptr)
	{
		return FString();
	}
	return ReadText([Controls](TCHAR* Buffer, int32 Capacity) { return Controls->GetBindingsJson(Buffer, Capacity); });
}

void UScctControlSettings::ToBridge(FScctControlSettings& Out) const
{
	Out = {};
	Out.InvertY = bInvertY ? 1 : 0;
	Out.InvertXSpy = bInvertXSpy ? 1 : 0;
	Out.SpyRollFreeLook = bSpyRollFreeLook ? 1 : 0;
	Out.AutoHackComms = bAutoHackComms ? 1 : 0;
	Out.MouseWheelSpeed = bMouseWheelSpeed ? 1 : 0;
	Out.Widescreen = bWidescreen ? 1 : 0;
	Out.MercFovCap = MercFovCap;
	Out.MouseScale = MouseScale;
	Out.SpyKbm = LookToBridge(SpyMouse);
	Out.MercKbm = LookToBridge(MercMouse);
	Out.SpyPad = LookToBridge(SpyPad);
	Out.MercPad = LookToBridge(MercPad);
	Out.PadEnabled = bPadEnabled ? 1 : 0;
	Out.PadMove = bPadMove ? 1 : 0;
	Out.PadAim = bPadAim ? 1 : 0;
	Out.PadBinds = bPadBinds ? 1 : 0;
	Out.PadSlowWalk = bPadSlowWalk ? 1 : 0;
	Out.PadSpyMove8 = bPadSpyMove8 ? 1 : 0;
	Out.PadMercMove8 = bPadMercMove8 ? 1 : 0;
	Out.PadDeadzoneLeft = PadDeadzoneLeft;
	Out.PadDeadzoneRight = PadDeadzoneRight;
	Out.PadVerticalScale = PadVerticalScale;
	Out.PadRunThreshold = PadRunThreshold;
	Out.PadTriggerThreshold = PadTriggerThreshold;
	Out.PadLookScale = PadLookScale;
}

void UScctControlSettings::FromBridge(const FScctControlSettings& In)
{
	bInvertY = In.InvertY != 0;
	bInvertXSpy = In.InvertXSpy != 0;
	bSpyRollFreeLook = In.SpyRollFreeLook != 0;
	bAutoHackComms = In.AutoHackComms != 0;
	bMouseWheelSpeed = In.MouseWheelSpeed != 0;
	bWidescreen = In.Widescreen != 0;
	MercFovCap = In.MercFovCap;
	MouseScale = In.MouseScale;
	SpyMouse = LookFromBridge(In.SpyKbm);
	MercMouse = LookFromBridge(In.MercKbm);
	SpyPad = LookFromBridge(In.SpyPad);
	MercPad = LookFromBridge(In.MercPad);
	bPadEnabled = In.PadEnabled != 0;
	bPadMove = In.PadMove != 0;
	bPadAim = In.PadAim != 0;
	bPadBinds = In.PadBinds != 0;
	bPadSlowWalk = In.PadSlowWalk != 0;
	bPadSpyMove8 = In.PadSpyMove8 != 0;
	bPadMercMove8 = In.PadMercMove8 != 0;
	PadDeadzoneLeft = In.PadDeadzoneLeft;
	PadDeadzoneRight = In.PadDeadzoneRight;
	PadVerticalScale = In.PadVerticalScale;
	PadRunThreshold = In.PadRunThreshold;
	PadTriggerThreshold = In.PadTriggerThreshold;
	PadLookScale = In.PadLookScale;
}
