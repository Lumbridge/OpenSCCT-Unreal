// map-shots: one imported map played as the front end starts it (a spy, no bots; the Godot client's MapShotsSmoke and
// VisitMapSmoke), for the all-maps sweep of Tools/Test/Run-MapSweep.ps1, which runs it once per map and mode.
//
//   -ScctTestMap=<Map>            the map (AquaE by default)
//   -ScctMapShotsMode=<Mode>      Story (default), Deathmatch, DiskHunt or MercDeathMatch: the URL's ?Mode=
//   -ScctMapShotsDir=<folder>     where the views go as <Map>-<Mode>-<n>.png and <Map>-<Mode>-nv<n>.png
//                                 (default Saved/ScctShots/map-shots)
//   -ScctMapShotsMagenta=<pct>    the share of magenta pixels in one view above which the test fails (default 2)
//
// It measures the time from opening the level to the first frame with the local spy drawn, counts what is missing in
// the level (mesh components without a mesh, material slots that are empty or draw the engine's default material,
// texture parameters without a texture, and the equipment meshes and skins ScctEquipmentModels reports as not
// imported), then turns the view a quarter turn at a time with the mouse and reads four frames back in plain sight and
// four in night vision. Each frame gives the share of black and of magenta pixels. One line answers:
//
//   SCCT_MAPSHOTS <Map> mode=<Mode> load=<s> missing=<meshes>/<textures>/<materials> black=<mean %> magenta=<max %> views=...
//
// The test fails when the map does not load, the spy never appears, a frame cannot be read back or a view's magenta
// share is over the threshold.

#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Hud/ScctVisionSubsystem.h"
#include "ImageUtils.h"
#include "InputKeyEventArgs.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Play/ScctPlayerController.h"
#include "Test/Features/ScctFeatureKit.h"
#include "UnrealClient.h"

#include <atomic>

namespace ScctMapShotsLocal
{
	constexpr int32 MapShotsViews = 8;         // four in plain sight, four in night vision
	constexpr int32 MapShotsNightFrom = 4;
	constexpr int32 MapShotsSettleFrames = 90;
	constexpr double MapShotsSettleSeconds = 2.0;
	constexpr int32 MapShotsAfterTurnFrames = 20;
	constexpr double MapShotsTurnSeconds = 6.0;
	constexpr float MapShotsTurnCounts = 3000.0f; // mouse counts a second while turning
	constexpr double MapShotsReadSeconds = 20.0;
	constexpr int32 MapShotsNightMode = 34;

	/** Counts the equipment models' "not imported" warnings while the test runs. */
	class FMapShotsLogCounter : public FOutputDevice
	{
	public:
		std::atomic<int32> Meshes{ 0 };
		std::atomic<int32> Skins{ 0 };

		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity > ELogVerbosity::Warning || Text == nullptr || FCString::Strncmp(Text, TEXT("Equipment: "), 11) != 0)
			{
				return;
			}
			if (FCString::Strstr(Text, TEXT("no skin material")) != nullptr)
			{
				++Skins;
			}
			else if (FCString::Strstr(Text, TEXT("no imported mesh")) != nullptr || FCString::Strstr(Text, TEXT("has no file for mesh")) != nullptr)
			{
				++Meshes;
			}
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
	};

	struct FMapShotsMissing
	{
		int32 Meshes = 0;
		int32 Textures = 0;
		int32 Materials = 0;
		TArray<FString> Examples;

		void Example(const FString& Text)
		{
			if (Examples.Num() < 6)
			{
				Examples.Add(Text);
			}
		}
	};

	/** What the level's drawn mesh components lack. */
	FMapShotsMissing CountMissing(UWorld* World)
	{
		FMapShotsMissing Missing;
		const UMaterial* Default = UMaterial::GetDefaultMaterial(MD_Surface);
		TSet<const UMaterialInterface*> Used;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UMeshComponent*> Components(*It);
			for (UMeshComponent* Component : Components)
			{
				if (Component == nullptr || !Component->IsRegistered() || !Component->IsVisible() || !Component->bRenderInMainPass)
				{
					continue;
				}
				const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Component);
				const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Component);
				if ((Static != nullptr && Static->GetStaticMesh() == nullptr) || (Skinned != nullptr && Skinned->GetSkinnedAsset() == nullptr))
				{
					++Missing.Meshes;
					Missing.Example(FString::Printf(TEXT("no mesh on %s.%s"), *It->GetName(), *Component->GetName()));
					continue;
				}
				const int32 Slots = Component->GetNumMaterials();
				for (int32 Slot = 0; Slot < Slots; ++Slot)
				{
					const UMaterialInterface* Material = Component->GetMaterial(Slot);
					if (Material == nullptr || Material->GetMaterial() == Default)
					{
						++Missing.Materials;
						Missing.Example(FString::Printf(TEXT("%s material on %s.%s slot %d"), Material == nullptr ? TEXT("no") : TEXT("the default"), *It->GetName(), *Component->GetName(), Slot));
					}
					else
					{
						Used.Add(Material);
					}
				}
			}
		}
		for (const UMaterialInterface* Material : Used)
		{
			const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
			for (int32 Depth = 0; Instance != nullptr && Depth < 8; ++Depth)
			{
				for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
				{
					if (Value.ParameterValue == nullptr)
					{
						++Missing.Textures;
						Missing.Example(FString::Printf(TEXT("no texture for %s of %s"), *Value.ParameterInfo.Name.ToString(), *Instance->GetName()));
					}
				}
				Instance = Cast<UMaterialInstance>(Instance->Parent);
			}
		}
		return Missing;
	}

	class FScctMapShotsTest : public IScctTest
	{
	public:
		virtual ~FScctMapShotsTest() override
		{
			Release();
		}

		virtual void Start(FScctTestContext& Context) override
		{
			Map = FScctTestContext::GetTestMap(TEXT("AquaE"));
			Mode = TEXT("Story");
			FParse::Value(FCommandLine::Get(), TEXT("ScctMapShotsMode="), Mode);
			ShotDir = FPaths::ProjectSavedDir() / TEXT("ScctShots") / TEXT("map-shots");
			FParse::Value(FCommandLine::Get(), TEXT("ScctMapShotsDir="), ShotDir);
			FParse::Value(FCommandLine::Get(), TEXT("ScctMapShotsMagenta="), MagentaLimit);
			GLog->AddOutputDevice(&LogCounter);
			bCounting = true;
			CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FScctMapShotsTest::OnCaptured);
		}

		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (Context.GetWorld() == nullptr)
			{
				return FScctTestResult::Running();
			}
			if (!bOpen)
			{
				bOpen = true;
				FString Error;
				if (!Stage.Open(Context, Map, FString::Printf(TEXT("?Side=spy?Bots=0?Mode=%s"), *Mode), Error))
				{
					return Finish(Context, FString::Printf(TEXT("the map did not load: %s"), *Error));
				}
				OpenedAt = Context.ElapsedSeconds;
				return FScctTestResult::Running();
			}
			if (!Stage.IsReady())
			{
				const FScctTestResult Ready = Stage.Step(Context);
				if (Ready.State == EScctTestState::Failed)
				{
					return Finish(Context, FString::Printf(TEXT("the spy never appeared: %s"), *Ready.Detail));
				}
				if (Ready.State != EScctTestState::Passed)
				{
					return FScctTestResult::Running();
				}
				LoadSeconds = Context.ElapsedSeconds - OpenedAt;
				return FScctTestResult::Running();
			}
			if (Stage.HasEnded(Context))
			{
				return Finish(Context, TEXT("the match ended while the views were taken"));
			}
			AScctPlayerController* Player = Context.GetPlayer();
			if (Player == nullptr || Player->PlayerCameraManager == nullptr)
			{
				return FScctTestResult::Running();
			}

			const float Yaw = static_cast<float>(Player->PlayerCameraManager->GetCameraRotation().Yaw);
			switch (Phase)
			{
			case EPhase::Settle:
				if (Stage.GetFrames() >= MapShotsSettleFrames && Stage.GetPlayedSeconds(Context) >= MapShotsSettleSeconds)
				{
					Missing = CountMissing(Context.GetWorld());
					Phase = EPhase::Wait;
					PhaseFrames = 0;
				}
				break;

			case EPhase::Turn:
				Turned += FMath::Abs(FMath::FindDeltaAngleDegrees(LastYaw, Yaw));
				LastYaw = Yaw;
				if (Turned >= 88.0f || Context.ElapsedSeconds - PhaseAt > MapShotsTurnSeconds)
				{
					Phase = EPhase::Wait;
					PhaseFrames = 0;
				}
				else
				{
					// Raw counts on the controller's input, the path a mouse's events take; slower near the end so it stops close.
					const float Rate = Turned > 70.0f ? MapShotsTurnCounts * 0.3f : MapShotsTurnCounts;
					Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, Rate * static_cast<float>(FApp::GetDeltaTime()), 1));
				}
				break;

			case EPhase::Wait:
				if (++PhaseFrames >= MapShotsAfterTurnFrames)
				{
					bCaptured = false;
					FScreenshotRequest::RequestScreenshot(false);
					Phase = EPhase::Read;
					PhaseAt = Context.ElapsedSeconds;
				}
				break;

			case EPhase::Read:
				if (!bCaptured)
				{
					if (Context.ElapsedSeconds - PhaseAt > MapShotsReadSeconds)
					{
						return Finish(Context, FString::Printf(TEXT("view %d could not be read back in %.0f s"), View, MapShotsReadSeconds));
					}
					break;
				}
				Views.Add(FString::Printf(TEXT("%s%d=yaw%.0f/black%.0f%%/magenta%.1f%%"), View < MapShotsNightFrom ? TEXT("v") : TEXT("nv"), View % MapShotsNightFrom,
					FRotator::ClampAxis(Yaw), LastBlack, LastMagenta));
				BlackSum += LastBlack;
				if (LastMagenta > MagentaMax)
				{
					MagentaMax = LastMagenta;
					MagentaView = Views.Last();
				}
				if (++View >= MapShotsViews)
				{
					return Finish(Context, FString());
				}
				if (View == MapShotsNightFrom)
				{
					if (UScctVisionSubsystem* Vision = Context.GetWorld()->GetSubsystem<UScctVisionSubsystem>())
					{
						Vision->ForceModeForCheck(MapShotsNightMode);
					}
					Phase = EPhase::Wait;
					PhaseFrames = 0;
				}
				else
				{
					Phase = EPhase::Turn;
					PhaseAt = Context.ElapsedSeconds;
					Turned = 0.0f;
					LastYaw = Yaw;
				}
				break;
			}
			return FScctTestResult::Running();
		}

	private:
		enum class EPhase : uint8 { Settle, Turn, Wait, Read };

		void OnCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
		{
			if (bCaptured || Width <= 0 || Height <= 0 || Bitmap.Num() < Width * Height)
			{
				return;
			}
			int32 Black = 0;
			int32 Magenta = 0;
			int32 Samples = 0;
			for (int32 Y = 0; Y < Height; Y += 4)
			{
				for (int32 X = 0; X < Width; X += 4)
				{
					const FColor& Pixel = Bitmap[Y * Width + X];
					++Samples;
					if (Pixel.R < 6 && Pixel.G < 6 && Pixel.B < 6)
					{
						++Black;
					}
					else if (Pixel.R > 204 && Pixel.B > 204 && Pixel.G < 64)
					{
						++Magenta;
					}
				}
			}
			LastBlack = 100.0f * Black / FMath::Max(1, Samples);
			LastMagenta = 100.0f * Magenta / FMath::Max(1, Samples);

			TArray<FColor> Opaque = Bitmap;
			for (FColor& Pixel : Opaque)
			{
				Pixel.A = 255;
			}
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
			const FString File = ShotDir / FString::Printf(TEXT("%s-%s-%s%d.png"), *Map, *Mode, View < MapShotsNightFrom ? TEXT("") : TEXT("nv"), View % MapShotsNightFrom);
			IFileManager::Get().MakeDirectory(*ShotDir, true);
			FFileHelper::SaveArrayToFile(Png, *File);
			bCaptured = true;
		}

		void Release()
		{
			if (bCounting)
			{
				bCounting = false;
				if (GLog != nullptr)
				{
					GLog->RemoveOutputDevice(&LogCounter);
				}
			}
			if (CaptureHandle.IsValid())
			{
				UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
				CaptureHandle.Reset();
			}
		}

		/** Writes the SCCT_MAPSHOTS line and answers: Failure empty for the views' own verdict. */
		FScctTestResult Finish(FScctTestContext& Context, const FString& Failure)
		{
			if (UWorld* World = Context.GetWorld())
			{
				if (UScctVisionSubsystem* Vision = World->GetSubsystem<UScctVisionSubsystem>())
				{
					Vision->ForceModeForCheck(INDEX_NONE);
				}
			}
			Release();
			FString Reason = Failure;
			if (Reason.IsEmpty() && MagentaMax > MagentaLimit)
			{
				Reason = FString::Printf(TEXT("%.1f%% of a view is magenta (%s), over %.1f%%"), MagentaMax, *MagentaView, MagentaLimit);
			}
			const int32 Meshes = Missing.Meshes + LogCounter.Meshes.load();
			const int32 Materials = Missing.Materials + LogCounter.Skins.load();
			const FString Line = FString::Printf(TEXT("%s mode=%s load=%.1f missing=%d/%d/%d black=%.1f magenta=%.2f views=%s"),
				*Map, *Mode, LoadSeconds, Meshes, Missing.Textures, Materials, Views.Num() > 0 ? BlackSum / Views.Num() : -1.0f, Views.Num() > 0 ? MagentaMax : -1.0f,
				Views.Num() > 0 ? *FString::Join(Views, TEXT(",")) : TEXT("none"));
			UE_LOG(LogScctTest, Log, TEXT("SCCT_MAPSHOTS %s%s"), *Line, Reason.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" FAILED %s"), *Reason));
			if (Missing.Examples.Num() > 0)
			{
				UE_LOG(LogScctTest, Log, TEXT("SCCT_MAPSHOTS_MISSING %s: %s"), *Map, *FString::Join(Missing.Examples, TEXT("; ")));
			}
			return Reason.IsEmpty() ? FScctTestResult::Pass(Line) : FScctTestResult::Fail(FString::Printf(TEXT("%s: %s"), *Map, *Reason));
		}

		FString Map;
		FString Mode;
		FString ShotDir;
		float MagentaLimit = 2.0f;
		FScctFeatureStage Stage;
		FMapShotsLogCounter LogCounter;
		FDelegateHandle CaptureHandle;
		FMapShotsMissing Missing;
		TArray<FString> Views;
		FString MagentaView;
		EPhase Phase = EPhase::Settle;
		double OpenedAt = 0.0;
		double LoadSeconds = -1.0;
		double PhaseAt = 0.0;
		int32 PhaseFrames = 0;
		int32 View = 0;
		float Turned = 0.0f;
		float LastYaw = 0.0f;
		float LastBlack = 0.0f;
		float LastMagenta = 0.0f;
		float BlackSum = 0.0f;
		float MagentaMax = 0.0f;
		bool bOpen = false;
		bool bCounting = false;
		bool bCaptured = false;
	};
}

SCCT_TEST("map-shots", ScctMapShotsLocal::FScctMapShotsTest);
