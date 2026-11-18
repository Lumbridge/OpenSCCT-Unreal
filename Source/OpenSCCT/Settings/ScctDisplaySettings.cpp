#include "Settings/ScctDisplaySettings.h"

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/PlatformTime.h"
#include "Menu/ScctUserSettings.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Settings/ScctGraphicsSettings.h"
#include "Settings/ScctPerformanceSettings.h"
#include "Settings/ScctSettingsForced.h"
#include "Settings/ScctVideoOptions.h"
#include "UnrealEngine.h"
#include "Widgets/SWindow.h"

using ScctVideoOptions::Option;

namespace
{
	const TCHAR* const DisplayPage = TEXT("Display");

	/** Where the registry keeps its values (Settings/ScctSettings.cpp). */
	const TCHAR* const DisplaySettingsSection = TEXT("OpenSCCT.Settings");

	const FName MonitorKey(TEXT("Display.Monitor"));
	const FName PositionKey(TEXT("Display.WindowPosition"));

	enum class EPlace : int32 { Centred, TopLeft, Fill };

	/** The resolution change goes through on a later frame; the window is placed after it. */
	constexpr double PlaceDelaySeconds = 0.3;

	/** -ScctDisplayChange: how long after the start apply its values are set. */
	constexpr double ChangeDelaySeconds = 5.0;

	bool IsStoredOrForced(FName Key)
	{
		FString Value;
		return FScctSettingsForced::IsForced(Key)
			|| (GConfig != nullptr && GConfig->GetString(DisplaySettingsSection, *Key.ToString(), Value, GGameUserSettingsIni));
	}

	/** The monitor an option names, its current size and its work area (the primary display when there is no list). */
	struct FMonitor
	{
		int32 Index = -1;
		FString Id;
		FString Name;
		FIntPoint Desktop = FIntPoint(1920, 1080);
		FPlatformRect Work = FPlatformRect(0, 0, 1920, 1080);
	};

	FMonitor FindMonitor(int32 Wanted)
	{
		FDisplayMetrics Metrics;
		FDisplayMetrics::RebuildDisplayMetrics(Metrics);
		FMonitor Result;
		Result.Desktop = FIntPoint(Metrics.PrimaryDisplayWidth, Metrics.PrimaryDisplayHeight);
		Result.Work = Metrics.PrimaryDisplayWorkAreaRect;
		if (Metrics.MonitorInfo.Num() > 0)
		{
			Result.Index = FMath::Clamp(Wanted, 0, Metrics.MonitorInfo.Num() - 1);
			const FMonitorInfo& Info = Metrics.MonitorInfo[Result.Index];
			Result.Id = Info.ID;
			Result.Name = Info.Name;
			Result.Desktop = FIntPoint(Info.DisplayRect.Right - Info.DisplayRect.Left, Info.DisplayRect.Bottom - Info.DisplayRect.Top);
			Result.Work = Info.WorkArea;
		}
		return Result;
	}

	TSharedPtr<SWindow> GameWindow()
	{
		return GEngine != nullptr && GEngine->GameViewport != nullptr ? GEngine->GameViewport->GetWindow() : nullptr;
	}

	const FMonitorInfo* TargetMonitor(const UGameUserSettings& Settings)
	{
		if (!FSlateApplication::IsInitialized()) { return nullptr; }
		const FDisplayMetrics& Metrics = FSlateApplication::Get().GetCachedDisplayMetricsByRef();
		if (Settings.GetDisplayIndex() == INDEX_NONE && Settings.GetDisplayID().IsEmpty())
		{
			if (const TSharedPtr<SWindow> Window = GameWindow())
			{
				const FSlateRect Rect = Window->GetFullScreenInfo();
				for (const FMonitorInfo& Info : Metrics.MonitorInfo)
				{
					if (Rect.GetSize().X > 0 && Info.DisplayRect.Left == Rect.Left && Info.DisplayRect.Top == Rect.Top)
					{
						return &Info;
					}
				}
			}
		}
		const int32 Index = Metrics.GetClosestMonitorFromIDAndIndex(Settings.GetDisplayID(), Settings.GetDisplayIndex());
		return Metrics.MonitorInfo.IsValidIndex(Index) ? &Metrics.MonitorInfo[Index] : nullptr;
	}

	bool HasLaunchWindowOverride()
	{
		const TCHAR* Cmd = FCommandLine::Get();
		FString Value;
		return FParse::Param(Cmd, TEXT("Windowed")) || FParse::Param(Cmd, TEXT("Fullscreen"))
			|| FParse::Param(Cmd, TEXT("RenderOffscreen")) || FParse::Param(Cmd, TEXT("ForceRes"))
			|| FParse::Value(Cmd, TEXT("Res="), Value) || FParse::Value(Cmd, TEXT("ResX="), Value)
			|| FParse::Value(Cmd, TEXT("ResY="), Value);
	}
}

FIntPoint UScctDisplaySettings::DesktopSize(const UGameUserSettings& Settings)
{
	if (const FMonitorInfo* Info = TargetMonitor(Settings))
	{
		return FIntPoint(Info->DisplayRect.Right - Info->DisplayRect.Left, Info->DisplayRect.Bottom - Info->DisplayRect.Top);
	}
	return Settings.GetDesktopResolution();
}

TArray<FIntPoint> UScctDisplaySettings::ResolutionChoices(const UGameUserSettings& Settings, EWindowMode::Type Mode, FIntPoint Current)
{
	TArray<FIntPoint> Sizes;
	const FMonitorInfo* Info = TargetMonitor(Settings);
	FScreenResolutionArray Modes;
	if (GDynamicRHI && Info && RHIGetAvailableResolutionsForDisplay(Modes, true, Info->NativeHandle))
	{
		for (const FScreenResolutionRHI& Available : Modes)
		{
			if (Available.Width > 0 && Available.Height > 0)
			{
				Sizes.AddUnique(FIntPoint(Available.Width, Available.Height));
			}
		}
	}
	const FIntPoint Desktop = DesktopSize(Settings);
	if (Desktop.X > 0 && Desktop.Y > 0) { Sizes.AddUnique(Desktop); }
	if (Mode == EWindowMode::Windowed)
	{
		for (FIntPoint Size : { FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160) })
		{
			if (Size.X <= Desktop.X && Size.Y <= Desktop.Y) { Sizes.AddUnique(Size); }
		}
		if (Current.X > 0 && Current.Y > 0) { Sizes.AddUnique(Current); }
	}
	if (Sizes.IsEmpty() && Current.X > 0 && Current.Y > 0) { Sizes.Add(Current); }
	Sizes.Sort([](FIntPoint A, FIntPoint B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
	return Sizes;
}

void UScctDisplaySettings::ApplyResolution(UGameUserSettings& Settings)
{
	if (Settings.GetFullscreenMode() == EWindowMode::WindowedFullscreen)
	{
		const FIntPoint Desktop = DesktopSize(Settings);
		if (Desktop.X > 0 && Desktop.Y > 0) { Settings.SetScreenResolution(Desktop); }
		// The requested mode can already match r.SetRes even though the actual window was created at another size.
		if (!FApp::IsUnattended()) { GSystemResolution.ForceRefresh(); }
	}
	if (!FApp::IsUnattended()) { Settings.ApplyResolutionSettings(false); }
}

void UScctDisplaySettings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Play in editor shares the editor's window, scalability and frame rate: the options are shown, never applied.
	bApplies = !GIsEditor && !IsRunningDedicatedServer() && FApp::CanEverRender();

	RegisterDisplay();
	ScctGraphicsSettings::Register();
	ScctPerformanceSettings::Register();
	FScctSettingsForced::Report();

	ChangedHandle = FScctSettings::OnChanged().AddUObject(this, &UScctDisplaySettings::HandleChanged);
	// The render scale is UScctUserSettings' and the automatic render scale goes up to it.
	UserSettingsHandle = UScctUserSettings::Get()->OnApplied.AddWeakLambda(this, [this] { bGraphicsDirty = true; });
	// Everything is applied on the first tick: Unreal applies its own user settings during its start, after the
	// game instance, and the game viewport the window belongs to is made after it too.
	bStartPending = true;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctDisplaySettings::Tick));

	if (FParse::Value(FCommandLine::Get(), TEXT("ScctDisplayChange="), ChangeSets, false) && !ChangeSets.IsEmpty())
	{
		ChangeAt = -1.0;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: Display, Graphics and Performance options registered%s"), bApplies ? TEXT("") : TEXT(" (editor: not applied)"));
}

void UScctDisplaySettings::Deinitialize()
{
	FScctSettings::OnChanged().Remove(ChangedHandle);
	UScctUserSettings::Get()->OnApplied.Remove(UserSettingsHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

void UScctDisplaySettings::RegisterDisplay()
{
	// The window mode, the resolution, the widescreen field of view and the brightness are the settings page's own
	// (UGameUserSettings, UScctControlSettings, UScctUserSettings): only what it has no row for is registered here.
	int32 Order = 0;
	{
		FDisplayMetrics Metrics;
		FDisplayMetrics::RebuildDisplayMetrics(Metrics);
		int32 Primary = 0;
		FScctSettingDesc Desc = Option(TEXT("Display.Monitor"), DisplayPage, TEXT("Window"), TEXT("Monitor"),
			TEXT("The monitor the game is shown on. The window mode and the resolution above are kept."), EScctSettingType::Choice, TEXT("0"), Order++);
		for (int32 Index = 0; Index < Metrics.MonitorInfo.Num(); ++Index)
		{
			const FMonitorInfo& Info = Metrics.MonitorInfo[Index];
			Primary = Info.bIsPrimary ? Index : Primary;
			Desc.Choices.Add(FText::FromString(FString::Printf(TEXT("%d: %s (%dx%d)%s"), Index + 1, *Info.Name,
				Info.DisplayRect.Right - Info.DisplayRect.Left, Info.DisplayRect.Bottom - Info.DisplayRect.Top, Info.bIsPrimary ? TEXT(", main") : TEXT(""))));
		}
		if (Desc.Choices.Num() == 0)
		{
			Desc.Choices.Add(FText::FromString(TEXT("1: Main display")));
		}
		Desc.Default = FString::FromInt(Primary);
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Display.WindowPosition"), DisplayPage, TEXT("Window"), TEXT("Window position (windowed)"),
			TEXT("Where the window goes on its monitor. Fill the screen sizes it to the monitor's work area, above the taskbar."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("Centred"), TEXT("Top left"), TEXT("Fill the screen") });
		FScctSettings::Register(Desc);
	}
}

void UScctDisplaySettings::HandleChanged(FName Key)
{
	if (!bApplies)
	{
		return;
	}
	if (Key == MonitorKey)
	{
		bMonitorDirty = true;
	}
	else if (Key == PositionKey)
	{
		PlaceAt = FPlatformTime::Seconds();
	}
	else if (ScctGraphicsSettings::Owns(Key))
	{
		bGraphicsDirty = true;
	}
	else if (Key == TEXT("Performance.Renderer"))
	{
		ScctPerformanceSettings::WriteRenderer();
	}
	else if (ScctPerformanceSettings::Owns(Key))
	{
		bPerformanceDirty = true;
	}
}

bool UScctDisplaySettings::Tick(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();
	if (bStartPending)
	{
		if (bApplies && !FApp::IsUnattended() && !GameWindow().IsValid()) { return true; }
		bStartPending = false;
		if (bApplies)
		{
			ScctGraphicsSettings::Apply();
			ScctPerformanceSettings::Apply(true);
			if (!HasLaunchWindowOverride())
			{
				UGameUserSettings* Settings = GEngine->GetGameUserSettings();
				if (IsStoredOrForced(MonitorKey))
				{
					ApplyMonitor(true);
				}
				else if (Settings && Settings->GetFullscreenMode() == EWindowMode::WindowedFullscreen && !FApp::IsUnattended())
				{
					const FIntPoint Saved = Settings->GetScreenResolution();
					ApplyResolution(*Settings);
					if (Saved != Settings->GetScreenResolution())
					{
						Settings->ConfirmVideoMode();
						Settings->SaveSettings();
					}
				}
				else if (IsStoredOrForced(PositionKey)) { PlaceAt = Now + PlaceDelaySeconds; }
			}
		}
		if (ChangeAt < 0.0)
		{
			ChangeAt = Now + ChangeDelaySeconds;
		}
		bMonitorDirty = bGraphicsDirty = bPerformanceDirty = false;
	}

	if (bGraphicsDirty)
	{
		bGraphicsDirty = false;
		if (bApplies)
		{
			ScctGraphicsSettings::Apply();
		}
	}
	if (bPerformanceDirty)
	{
		bPerformanceDirty = false;
		ScctPerformanceSettings::Apply(false);
	}
	if (bMonitorDirty)
	{
		bMonitorDirty = false;
		ApplyMonitor(false);
	}
	if (PlaceAt > 0.0 && Now >= PlaceAt)
	{
		PlaceAt = 0.0;
		PlaceWindow();
	}
	if (ChangeAt > 0.0 && Now >= ChangeAt)
	{
		ChangeAt = 0.0;
		TArray<FString> Pairs;
		ChangeSets.ParseIntoArray(Pairs, TEXT(";"));
		for (const FString& Pair : Pairs)
		{
			FString Key, Value;
			if (Pair.Split(TEXT("="), &Key, &Value))
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("-ScctDisplayChange: setting %s to %s"), *Key.TrimStartAndEnd(), *Value.TrimStartAndEnd());
				FScctSettings::SetString(FName(*Key.TrimStartAndEnd()), Value.TrimStartAndEnd());
			}
		}
	}

	if (bApplies)
	{
		ScctPerformanceSettings::Tick(DeltaSeconds);
	}
	return true;
}

void UScctDisplaySettings::ApplyMonitor(bool bAtStart)
{
	UGameUserSettings* Settings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (Settings == nullptr)
	{
		return;
	}
	const FMonitor Monitor = FindMonitor(FScctSettings::GetInt(MonitorKey));
	if (Monitor.Index < 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: no list of monitors (off-screen): the window stays where it is"));
		return;
	}
	// Fullscreen takes the monitor the window is on: the window goes there first. The mode and the size stay the
	// settings page's (UGameUserSettings); a borderless window takes the new monitor's size.
	const EWindowMode::Type Mode = Settings->GetFullscreenMode();
	if (const TSharedPtr<SWindow> Window = GameWindow(); Window.IsValid())
	{
		Window->MoveWindowTo(FVector2D(Monitor.Work.Left + 32, Monitor.Work.Top + 32));
	}
	Settings->SetDisplayProperties(Monitor.Id, Monitor.Index);
	ApplyResolution(*Settings);
	if (!FScctSettingsForced::IsForced(MonitorKey))
	{
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
	}
	if (Mode == EWindowMode::Windowed)
	{
		PlaceAt = FPlatformTime::Seconds() + PlaceDelaySeconds;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: monitor %d '%s' (desktop %dx%d)%s, window mode %d"), Monitor.Index + 1, *Monitor.Name,
		Monitor.Desktop.X, Monitor.Desktop.Y, bAtStart ? TEXT(" at start") : TEXT(""), static_cast<int32>(Mode));
}

void UScctDisplaySettings::PlaceWindow()
{
	const TSharedPtr<SWindow> Window = GameWindow();
	UGameUserSettings* Settings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (Settings == nullptr || Settings->GetFullscreenMode() != EWindowMode::Windowed)
	{
		return;
	}
	const FMonitor Monitor = FindMonitor(FScctSettings::GetInt(MonitorKey));
	const EPlace Place = static_cast<EPlace>(FMath::Clamp(FScctSettings::GetInt(PositionKey), 0, 2));
	const FVector2D WorkOrigin(Monitor.Work.Left, Monitor.Work.Top);
	const FVector2D WorkSize(Monitor.Work.Right - Monitor.Work.Left, Monitor.Work.Bottom - Monitor.Work.Top);
	const FVector2D WindowSize = Window.IsValid() ? Window->GetSizeInScreen() : FVector2D(Settings->GetScreenResolution());

	FVector2D Position = WorkOrigin;
	if (Place == EPlace::Centred)
	{
		Position = WorkOrigin + ((WorkSize - WindowSize) * 0.5f).ComponentMax(FVector2D::ZeroVector);
	}
	if (Window.IsValid())
	{
		if (Place == EPlace::Fill)
		{
			Window->ReshapeWindow(WorkOrigin, WorkSize);
		}
		else
		{
			Window->MoveWindowTo(Position);
		}
	}
	Settings->SetWindowPosition(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y));
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: window placed at %.0f,%.0f (%s) on monitor %d%s"),
		Position.X, Position.Y, Place == EPlace::Centred ? TEXT("centred") : Place == EPlace::TopLeft ? TEXT("top left") : TEXT("filling the work area"),
		Monitor.Index + 1, Window.IsValid() ? TEXT("") : TEXT(" (no window to move: off-screen)"));
}
