#include "Settings/ScctDisplaySettings.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Menu/SScctBrightnessPage.h"
#include "Menu/SScctKeepDisplay.h"
#include "Menu/SScctOptionsPage.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Play/ScctControlSettings.h"
#include "Settings/ScctGraphicsSettings.h"
#include "Settings/ScctPerformanceSettings.h"
#include "Settings/ScctSettingsForced.h"
#include "Settings/ScctVideoOptions.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

using ScctVideoOptions::Option;

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	const TCHAR* const DisplayPage = TEXT("Display");

	/** Where the registry keeps its values (Settings/ScctSettings.cpp). */
	const TCHAR* const SettingsSection = TEXT("OpenSCCT.Settings");

	const FName ModeKey(TEXT("Display.Mode"));
	const FName MonitorKey(TEXT("Display.Monitor"));
	const FName ResolutionKey(TEXT("Display.Resolution"));
	const FName PositionKey(TEXT("Display.WindowPosition"));
	const FName WidescreenKey(TEXT("Display.Widescreen"));
	const FName FovCapKey(TEXT("Display.MercFovCap"));
	const FName BrightnessKey(TEXT("Display.Brightness"));

	const FName WindowKeys[] = { ModeKey, MonitorKey, ResolutionKey, PositionKey };

	/** Display.Mode's choices, in order. */
	const EWindowMode::Type Modes[] = { EWindowMode::WindowedFullscreen, EWindowMode::Fullscreen, EWindowMode::Windowed };
	const TCHAR* const ModeNames[] = { TEXT("Borderless window"), TEXT("Fullscreen"), TEXT("Windowed") };

	/**
	 * Display.Resolution's sizes after "Desktop" (Godot's list, OptionsMenu.Resolutions). A fixed list, so a stored
	 * choice means the same size on every monitor; a size the monitor cannot take is fitted when it is applied.
	 */
	const FIntPoint WindowSizes[] = { { 1280, 720 }, { 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }, { 1024, 768 }, { 1600, 1200 } };

	enum class EPlace : int32 { Centred, TopLeft, Fill };

	/** How long a changed display waits for KEEP before it goes back. */
	constexpr float KeepSeconds = 15.0f;

	/** The display gamma at a brightness of 1 (UEngine's default). */
	constexpr float NeutralGamma = 2.2f;

	/** The resolution change goes through on a later frame; the window is placed after it. */
	constexpr double PlaceDelaySeconds = 0.3;

	/** -ScctDisplayChange: how long after the start apply its values are set. */
	constexpr double ChangeDelaySeconds = 5.0;

	bool StoredOrForced(FName Key)
	{
		FString Value;
		return FScctSettingsForced::IsForced(Key)
			|| (GConfig != nullptr && GConfig->GetString(SettingsSection, *Key.ToString(), Value, GGameUserSettingsIni));
	}

	bool AnyWindowForced()
	{
		for (const FName Key : WindowKeys)
		{
			if (FScctSettingsForced::IsForced(Key))
			{
				return true;
			}
		}
		return false;
	}

	FString SizeText(const FIntPoint& Size)
	{
		return FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);
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

	FString DescribeWindow()
	{
		const int32 Mode = FMath::Clamp(FScctSettings::GetInt(ModeKey), 0, 2);
		const int32 Resolution = FScctSettings::GetInt(ResolutionKey);
		const FMonitor Monitor = FindMonitor(FScctSettings::GetInt(MonitorKey));
		return FString::Printf(TEXT("%s, %s on monitor %d"), ModeNames[Mode],
			Mode == 0 || Resolution <= 0 || Resolution > UE_ARRAY_COUNT(WindowSizes) ? *FString::Printf(TEXT("desktop size %s"), *SizeText(Monitor.Desktop)) : *SizeText(WindowSizes[Resolution - 1]),
			Monitor.Index + 1);
	}
}

void UScctDisplaySettings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Play in editor shares the editor's window, scalability and frame rate: the options are shown, never applied.
	bApplies = !GIsEditor;

	RegisterDisplay();
	ScctGraphicsSettings::Register();
	ScctPerformanceSettings::Register();
	RegisterSections();
	FScctSettingsForced::Report();

	ChangedHandle = FScctSettings::OnChanged().AddUObject(this, &UScctDisplaySettings::HandleChanged);
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
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

void UScctDisplaySettings::RegisterDisplay()
{
	int32 Order = 0;
	{
		FScctSettingDesc Desc = Option(TEXT("Display.Mode"), DisplayPage, TEXT("Screen"), TEXT("Display mode"),
			TEXT("Borderless covers the monitor with a window, so switching away is quick. Fullscreen takes the monitor over and can use another resolution. A change asks to be kept."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		for (const TCHAR* Name : ModeNames)
		{
			Desc.Choices.Add(FText::FromString(Name));
		}
		FScctSettings::Register(Desc);
	}
	{
		FDisplayMetrics Metrics;
		FDisplayMetrics::RebuildDisplayMetrics(Metrics);
		int32 Primary = 0;
		FScctSettingDesc Desc = Option(TEXT("Display.Monitor"), DisplayPage, TEXT("Screen"), TEXT("Monitor"),
			TEXT("The monitor the game is shown on."), EScctSettingType::Choice, TEXT("0"), Order++);
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
		FScctSettingDesc Desc = Option(TEXT("Display.Resolution"), DisplayPage, TEXT("Screen"), TEXT("Resolution"),
			TEXT("The window's size, or the fullscreen mode's (the nearest one the monitor offers). A borderless window is always the monitor's size. A change asks to be kept."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices.Add(FText::FromString(TEXT("Desktop")));
		for (const FIntPoint& Size : WindowSizes)
		{
			Desc.Choices.Add(FText::FromString(SizeText(Size)));
		}
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Display.WindowPosition"), DisplayPage, TEXT("Screen"), TEXT("Window position (windowed)"),
			TEXT("Where the window goes on its monitor. Fill the screen sizes it to the monitor's work area, above the taskbar."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("Centred"), TEXT("Top left"), TEXT("Fill the screen") });
		FScctSettings::Register(Desc);
	}

	FScctSettings::Register(Option(TEXT("Display.Widescreen"), DisplayPage, TEXT("Widescreen layout"), TEXT("Wider field of view on widescreen"),
		TEXT("Keeps the original 4:3 picture's height and shows more at the sides; off stretches the 4:3 picture across the screen."),
		EScctSettingType::Bool, TEXT("true"), Order++));
	{
		FScctSettingDesc Desc = Option(TEXT("Display.MercFovCap"), DisplayPage, TEXT("Widescreen layout"), TEXT("Mercenary field of view cap"),
			TEXT("The widest the mercenary's horizontal field of view may get on a wide screen, in degrees."),
			EScctSettingType::Float, TEXT("112"), Order++);
		Desc.Min = 95.0f;
		Desc.Max = 112.0f;
		Desc.Step = 1.0f;
		Desc.FormatValue = [](float Value) { return FString::Printf(TEXT("%d deg"), FMath::RoundToInt(Value)); };
		FScctSettings::Register(Desc);
	}

	{
		FScctSettingDesc Desc = Option(TEXT("Display.Brightness"), DisplayPage, TEXT("Picture"), TEXT("Brightness"),
			TEXT("The brightness of the 3D picture (its display gamma). Set it against the test pattern: CALIBRATE BRIGHTNESS above."),
			EScctSettingType::Float, TEXT("1.0"), Order++);
		Desc.Min = 0.5f;
		Desc.Max = 1.5f;
		Desc.Step = 0.05f;
		Desc.FormatValue = [](float Value)
		{
			const int32 Percent = FMath::RoundToInt((Value - 1.0f) * 100.0f);
			return Percent == 0 ? FString(TEXT("0%")) : FString::Printf(TEXT("%+d%%"), Percent);
		};
		FScctSettings::Register(Desc);
	}
}

void UScctDisplaySettings::RegisterSections()
{
	// The brightness test pattern is a button, which the registry has no type for: the page's own section.
	FScctOptionsSection Display;
	Display.Build = []
	{
		return StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)
			[
				Ui::Text(Ui::ToText(TEXT("Set the brightness against a test pattern of near-black squares.")), 12.5f, FSlateColor(ScctHudStyle::TextDim()),
					ScctHudStyle::EWeight::Regular, true)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Ui::Button(Ui::ToText(TEXT("CALIBRATE BRIGHTNESS")), FOnClicked::CreateLambda([] { SScctBrightnessPage::Open(); return FReply::Handled(); }),
					false, Ui::Accent(), 220.0f)
			]);
	};
	SScctOptionsPage::RegisterSection(TEXT("Display"), Display);

	// What the Performance page's options do on this machine right now.
	FScctOptionsSection Performance;
	Performance.Build = []
	{
		return Ui::Text(TAttribute<FText>::CreateLambda([]
		{
			const int32 Limit = ScctPerformanceSettings::EffectiveLimit();
			return Ui::ToText(FString::Printf(TEXT("Drawing with %s on a %.0f Hz monitor; frame rate limit now %s."), *FApp::GetGraphicsRHI(),
				ScctPerformanceSettings::MonitorHz(), Limit > 0 ? *FString::Printf(TEXT("%d FPS"), Limit) : TEXT("none (vertical sync or unlimited)")));
		}), 12.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true);
	};
	SScctOptionsPage::RegisterSection(TEXT("Performance"), Performance);
}

void UScctDisplaySettings::HandleChanged(FName Key)
{
	if (Key == WidescreenKey || Key == FovCapKey)
	{
		if (!bSyncingControls)
		{
			WriteControls();
		}
		return;
	}
	if (!bApplies)
	{
		return;
	}
	if (Key == BrightnessKey)
	{
		ApplyBrightness();
	}
	else if (Key == ModeKey || Key == MonitorKey || Key == ResolutionKey || Key == PositionKey)
	{
		bWindowDirty = true;
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
		bStartPending = false;
		SyncControlsFromFile();
		if (bApplies)
		{
			ApplyBrightness();
			ScctGraphicsSettings::Apply();
			ScctPerformanceSettings::Apply(true);
			bool bWindowSet = false;
			for (const FName Key : WindowKeys)
			{
				bWindowSet |= StoredOrForced(Key);
			}
			if (bWindowSet)
			{
				ApplyWindow(true);
			}
			else if (const UGameUserSettings* Settings = GEngine->GetGameUserSettings())
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: no window setting set yet; the window stays as Unreal opened it (mode %d, %s, system %ux%u)"),
					static_cast<int32>(Settings->GetFullscreenMode()), *SizeText(Settings->GetScreenResolution()), GSystemResolution.ResX, GSystemResolution.ResY);
			}
		}
		RememberKept();
		if (ChangeAt < 0.0)
		{
			ChangeAt = Now + ChangeDelaySeconds;
		}
		bWindowDirty = bGraphicsDirty = bPerformanceDirty = false;
	}

	if (bGraphicsDirty)
	{
		bGraphicsDirty = false;
		ScctGraphicsSettings::Apply();
	}
	if (bPerformanceDirty)
	{
		bPerformanceDirty = false;
		ScctPerformanceSettings::Apply(false);
	}
	if (bWindowDirty)
	{
		bWindowDirty = false;
		ApplyWindow(false);
		if (bGoingBack)
		{
			bGoingBack = false;
		}
		else
		{
			// Only the window's place changed (or nothing did): nothing to lose, nothing to ask.
			TMap<FName, FString> Changed = WindowSnapshot();
			Changed.Add(PositionKey, Kept.FindRef(PositionKey));
			if (Changed.OrderIndependentCompareEqual(Kept))
			{
				Keep();
			}
			else
			{
				AskToKeep();
			}
		}
	}
	if (PlaceAt > 0.0 && Now >= PlaceAt)
	{
		PlaceAt = 0.0;
		PlaceWindow();
	}
	// The question can vanish without an answer (a map change clears the viewport): its time still runs out.
	if (KeepDeadline > 0.0 && Now > KeepDeadline + 1.0 && !SScctKeepDisplay::IsOpen())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: the keep question is gone and its time is up"));
		GoBack();
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

namespace
{
	/** What the window settings come to on this machine: the mode, the size and the monitor Unreal is given. */
	struct FWindowPlan
	{
		int32 ModeIndex = 0;
		EWindowMode::Type Mode = EWindowMode::WindowedFullscreen;
		FIntPoint Size;
		FMonitor Monitor;
		/** Why the size is not the one asked for, if it is not. */
		FString Fitted;
	};

	FWindowPlan PlanWindow()
	{
		FWindowPlan Plan;
		Plan.ModeIndex = FMath::Clamp(FScctSettings::GetInt(ModeKey), 0, 2);
		Plan.Mode = Modes[Plan.ModeIndex];
		Plan.Monitor = FindMonitor(FScctSettings::GetInt(MonitorKey));
		const int32 Choice = FScctSettings::GetInt(ResolutionKey);
		const EPlace Place = static_cast<EPlace>(FMath::Clamp(FScctSettings::GetInt(PositionKey), 0, 2));
		const FMonitor& Monitor = Plan.Monitor;
		const FIntPoint WorkSize(Monitor.Work.Right - Monitor.Work.Left, Monitor.Work.Bottom - Monitor.Work.Top);

		FIntPoint& Size = Plan.Size;
		Size = Choice > 0 && Choice <= UE_ARRAY_COUNT(WindowSizes) ? WindowSizes[Choice - 1] : Monitor.Desktop;
		if (Plan.Mode == EWindowMode::WindowedFullscreen)
		{
			Size = Monitor.Desktop;
		}
		else if (Plan.Mode == EWindowMode::Windowed)
		{
			if (Place == EPlace::Fill)
			{
				Size = WorkSize;
			}
			// Without a list of monitors (off-screen) the "desktop" is only the picture's own size: nothing to fit in.
			else if (Monitor.Index >= 0 && (Size.X > WorkSize.X || Size.Y > WorkSize.Y))
			{
				Plan.Fitted = FString::Printf(TEXT(" (asked %s, bigger than the work area)"), *SizeText(Size));
				const float Scale = FMath::Min(static_cast<float>(WorkSize.X) / Size.X, static_cast<float>(WorkSize.Y) / Size.Y);
				Size = FIntPoint(FMath::FloorToInt(Size.X * Scale), FMath::FloorToInt(Size.Y * Scale));
			}
		}
		else
		{
			// Fullscreen: a mode the monitor offers, the nearest in size to the one asked for.
			TArray<FIntPoint> Supported;
			if (UKismetSystemLibrary::GetSupportedFullscreenResolutions(Supported) && Supported.Num() > 0 && !Supported.Contains(Size))
			{
				const FIntPoint Asked = Size;
				Size = Supported[0];
				for (const FIntPoint& Offered : Supported)
				{
					if (FMath::Abs(Offered.X * Offered.Y - Asked.X * Asked.Y) + FMath::Abs(Offered.X - Asked.X) <
						FMath::Abs(Size.X * Size.Y - Asked.X * Asked.Y) + FMath::Abs(Size.X - Asked.X))
					{
						Size = Offered;
					}
				}
				Plan.Fitted = FString::Printf(TEXT(" (asked %s, not offered by the monitor)"), *SizeText(Asked));
			}
		}
		return Plan;
	}

	void WriteFields(UGameUserSettings& Settings, const FWindowPlan& Plan)
	{
		if (Plan.Monitor.Index >= 0)
		{
			Settings.SetDisplayProperties(Plan.Monitor.Id, Plan.Monitor.Index);
		}
		Settings.SetFullscreenMode(Plan.Mode);
		Settings.SetScreenResolution(Plan.Size);
	}
}

void UScctDisplaySettings::ApplyWindow(bool bAtStart)
{
	UGameUserSettings* Settings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (Settings == nullptr)
	{
		return;
	}
	const FWindowPlan Plan = PlanWindow();
	const FMonitor& Monitor = Plan.Monitor;

	// Fullscreen takes the monitor the window is on: the window goes there first.
	if (const TSharedPtr<SWindow> Window = GameWindow(); Window.IsValid() && Plan.Mode != EWindowMode::Windowed && Settings->GetFullscreenMode() == EWindowMode::Windowed)
	{
		Window->MoveWindowTo(FVector2D(Monitor.Work.Left + 32, Monitor.Work.Top + 32));
	}
	WriteFields(*Settings, Plan);
	Settings->ApplyResolutionSettings(false);
	if (Plan.Mode == EWindowMode::Windowed)
	{
		PlaceAt = FPlatformTime::Seconds() + PlaceDelaySeconds;
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: applied%s %s %s%s on monitor %d '%s' (desktop %s); UGameUserSettings mode %d, resolution %s, display %d"),
		bAtStart ? TEXT(" at start:") : TEXT(":"), ModeNames[Plan.ModeIndex], *SizeText(Plan.Size), *Plan.Fitted, Monitor.Index + 1, *Monitor.Name,
		*SizeText(Monitor.Desktop), static_cast<int32>(Settings->GetFullscreenMode()), *SizeText(Settings->GetScreenResolution()), Settings->GetDisplayIndex());

	// Unreal writes its user settings when the game ends (UGameEngine::PreExit) and opens the next start's window from
	// them. They hold the last kept display: the player's own at start or going back; a change waiting for KEEP, or a
	// display forced for this run only, is put back in them at once (the window stays as applied).
	if ((bAtStart || bGoingBack) && !AnyWindowForced())
	{
		Settings->ConfirmVideoMode();
	}
	else
	{
		Settings->RevertVideoMode();
	}
}

void UScctDisplaySettings::PlaceWindow()
{
	const TSharedPtr<SWindow> Window = GameWindow();
	UGameUserSettings* Settings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (Settings == nullptr)
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
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: window placed at %.0f,%.0f (%s) on monitor %d%s; system resolution %ux%u"),
		Position.X, Position.Y, Place == EPlace::Centred ? TEXT("centred") : Place == EPlace::TopLeft ? TEXT("top left") : TEXT("filling the work area"),
		Monitor.Index + 1, Window.IsValid() ? TEXT("") : TEXT(" (no window to move: off-screen)"), GSystemResolution.ResX, GSystemResolution.ResY);
}

void UScctDisplaySettings::ApplyBrightness()
{
	if (GEngine == nullptr)
	{
		return;
	}
	const float Brightness = FMath::Clamp(FScctSettings::GetFloat(BrightnessKey), 0.5f, 1.5f);
	GEngine->DisplayGamma = NeutralGamma * Brightness;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: brightness %.2f, display gamma %.2f"), Brightness, GEngine->DisplayGamma);
}

void UScctDisplaySettings::SyncControlsFromFile()
{
	UScctControlSettings* Controls = UScctControlSettings::Get();
	if (Controls == nullptr || !Controls->EnsureLoaded())
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctDisplay: the controls' settings could not be read (%s); the widescreen options follow them later"),
			Controls != nullptr ? *Controls->GetLastError() : TEXT("no controls"));
		return;
	}
	// The controls' file holds these two; the registry follows it, unless the run forces a value.
	TGuardValue<bool> Guard(bSyncingControls, true);
	if (!FScctSettingsForced::IsForced(WidescreenKey) && FScctSettings::GetBool(WidescreenKey) != Controls->bWidescreen)
	{
		FScctSettings::SetBool(WidescreenKey, Controls->bWidescreen);
	}
	if (!FScctSettingsForced::IsForced(FovCapKey) && !FMath::IsNearlyEqual(FScctSettings::GetFloat(FovCapKey), Controls->MercFovCap, 0.01f))
	{
		FScctSettings::SetFloat(FovCapKey, Controls->MercFovCap);
	}
	if (FScctSettingsForced::IsForced(WidescreenKey) || FScctSettingsForced::IsForced(FovCapKey))
	{
		Controls->bWidescreen = FScctSettings::GetBool(WidescreenKey);
		Controls->MercFovCap = FScctSettings::GetFloat(FovCapKey);
		Controls->Apply();
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: widescreen %s, merc field of view cap %.0f (controls.json)"),
		Controls->bWidescreen ? TEXT("on") : TEXT("off"), Controls->MercFovCap);
}

void UScctDisplaySettings::WriteControls()
{
	UScctControlSettings* Controls = UScctControlSettings::Get();
	if (Controls == nullptr || !Controls->EnsureLoaded())
	{
		return;
	}
	Controls->bWidescreen = FScctSettings::GetBool(WidescreenKey);
	Controls->MercFovCap = FScctSettings::GetFloat(FovCapKey);
	// A value forced for the run reaches the match but not the file.
	const bool bForced = FScctSettingsForced::IsForced(WidescreenKey) || FScctSettingsForced::IsForced(FovCapKey);
	const bool bDone = bForced ? Controls->Apply() : Controls->Save();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: widescreen %s, merc field of view cap %.0f %s"), Controls->bWidescreen ? TEXT("on") : TEXT("off"),
		Controls->MercFovCap, !bDone ? *FString::Printf(TEXT("refused: %s"), *Controls->GetLastError()) : bForced ? TEXT("applied") : TEXT("saved to controls.json"));
}

TMap<FName, FString> UScctDisplaySettings::WindowSnapshot() const
{
	TMap<FName, FString> Snapshot;
	for (const FName Key : WindowKeys)
	{
		Snapshot.Add(Key, FScctSettings::GetString(Key));
	}
	return Snapshot;
}

void UScctDisplaySettings::AskToKeep()
{
	KeepDeadline = FPlatformTime::Seconds() + KeepSeconds;
	if (!SScctKeepDisplay::Open(DescribeWindow(), KeepSeconds, FSimpleDelegate::CreateUObject(this, &UScctDisplaySettings::Keep),
		FSimpleDelegate::CreateUObject(this, &UScctDisplaySettings::GoBack)))
	{
		// No viewport to ask in: nothing can be seen to be wrong either.
		Keep();
	}
}

void UScctDisplaySettings::Keep()
{
	KeepDeadline = 0.0;
	RememberKept();
	// So the next start opens the window this way at once; not a display forced for this run only.
	if (UGameUserSettings* Settings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr; Settings != nullptr && bApplies && !AnyWindowForced())
	{
		WriteFields(*Settings, PlanWindow());
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: kept %s"), *DescribeWindow());
}

void UScctDisplaySettings::GoBack()
{
	KeepDeadline = 0.0;
	bGoingBack = true;
	// Each window setting held as it was when last kept: the text in the user settings file (or none, for the
	// default) and the value forced for the run (or none). Setting the values through the registry would store
	// them and drop what was forced.
	for (const FName Key : WindowKeys)
	{
		const FString Before = FScctSettings::GetString(Key);
		if (GConfig != nullptr)
		{
			if (const TOptional<FString>& Stored = KeptStored.FindRef(Key); Stored.IsSet())
			{
				GConfig->SetString(SettingsSection, *Key.ToString(), *Stored.GetValue(), GGameUserSettingsIni);
			}
			else
			{
				GConfig->RemoveKey(SettingsSection, *Key.ToString(), GGameUserSettingsIni);
			}
		}
		if (const TOptional<FString>& Forced = KeptForced.FindRef(Key); Forced.IsSet())
		{
			FScctSettingsForced::Force(Key, Forced.GetValue());
		}
		else
		{
			FScctSettingsForced::Drop(Key);
		}
		if (FScctSettings::GetString(Key) != Before)
		{
			FScctSettings::OnChanged().Broadcast(Key);
		}
	}
	bWindowDirty = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctDisplay: going back to %s"), *DescribeWindow());
}

void UScctDisplaySettings::RememberKept()
{
	Kept = WindowSnapshot();
	KeptStored.Reset();
	KeptForced.Reset();
	for (const FName Key : WindowKeys)
	{
		FString Stored;
		KeptStored.Add(Key, GConfig != nullptr && GConfig->GetString(SettingsSection, *Key.ToString(), Stored, GGameUserSettingsIni)
			? TOptional<FString>(Stored) : TOptional<FString>());
		const FString* Forced = FScctSettingsForced::Find(Key);
		KeptForced.Add(Key, Forced != nullptr ? TOptional<FString>(*Forced) : TOptional<FString>());
	}
}
