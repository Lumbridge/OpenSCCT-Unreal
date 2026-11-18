// The presentation checks, the second batch of Test/Features (the Godot client's HudGameplayParitySmoke, AudioSmoke,
// VoiceOutputSmoke, CloakRender, DetectorLaser and the vision smokes), on the run's map (-ScctTestMap, AquaE by default):
//
//   hud-spy, hud-merc  the HUD model against the bridge's pawn of the same frame (side, health, place, the side's own
//                      fields), the HUD's groups painted in the last frames, and staged -ScctHudCommands commands seen
//                      in the model (the scoreboard; the merc's laser sight)
//   audio              a merc's burst starts voices in the world category and no sound file fails to load (counted with
//                      or without an audio device, so it runs under -nosound)
//   voice              with -ScctVoiceTone for a microphone (tests.psd1 adds it) the microphone test captures frames,
//                      the bridge's relay hands packets back and the playback phase is reached; passes as "not
//                      applicable" when the bridge has no voice table
//   cloak              the camo suit held fully on puts the cloak material at Amount 1 on every slot of the spy's body
//                      and takes its shadow; let go, the body's own materials are back
//   lasers             detector beams are streaks only for the beams the map shows the naked eye, and in thermal and EMF
//                      every beam is a custom-depth-only cylinder and no streak is left
//   vision             each vision mode held in turn has its post-process material in the goggles' volume at Amount 1
//                      and weight 1 with every other mode at 0, and plain sight leaves none
//   fog                the imported level brings a height fog with density and an enabled post-process volume
//
// -ScctFeatureBreak leaves out the staged step of each (no command, no shots, no cloak, no mode), so each fails.

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/GameInstance.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Hud/ScctHudLayout.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudSubsystem.h"
#include "Hud/ScctVisionSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Play/ScctAudioSubsystem.h"
#include "Play/ScctEquipmentSubsystem.h"
#include "Play/ScctMarkersSubsystem.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctVoiceApi.h"
#include "Settings/ScctSettings.h"
#include "Test/Features/ScctFeatureKit.h"
#include "Voice/ScctVoiceSubsystem.h"

namespace ScctPresentationFeatureLocal
{
	constexpr double PresentationSettleSeconds = 3.0;
	constexpr double PresentationGiveUpSeconds = 40.0;

	/** Opens the run's map once for a side and waits for the match; Play is called every frame after that. */
	class FPresentationTest : public IScctTest
	{
	public:
		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (!bOpened)
			{
				if (Context.GetWorld() == nullptr)
				{
					return FScctTestResult::Running();
				}
				bOpened = true;
				FString Error;
				if (!Stage.Open(Context, FScctTestContext::GetTestMap(TEXT("AquaE")), GetOptions(), Error))
				{
					return FScctTestResult::Fail(Error);
				}
				return FScctTestResult::Running();
			}
			const FScctTestResult Ready = Stage.Step(Context);
			if (Ready.State != EScctTestState::Passed)
			{
				return End(Context, Ready);
			}
			if (Stage.GetSimSeconds(Context) < PresentationSettleSeconds)
			{
				return FScctTestResult::Running();
			}
			return End(Context, Play(Context));
		}

	protected:
		virtual FString GetOptions() const { return TEXT("?Side=spy?Bots=0"); }
		virtual FScctTestResult Play(FScctTestContext& Context) = 0;
		/** Puts back what the check held (a forced mode, a key). */
		virtual void Restore(FScctTestContext& Context) {}

		/** Seconds of the wall clock since Mark was last set by Wait. */
		bool Waited(const FScctTestContext& Context, double Seconds)
		{
			if (MarkAt < 0.0)
			{
				MarkAt = Context.ElapsedSeconds;
				MarkFrame = GFrameCounter;
			}
			// A starved run draws few frames: the eased values need frames as well as seconds.
			if (Context.ElapsedSeconds - MarkAt < Seconds || GFrameCounter - MarkFrame < 20)
			{
				return false;
			}
			MarkAt = -1.0;
			return true;
		}

		FScctTestResult Verdict(const TArray<FString>& Problems, const FString& Figures) const
		{
			return Problems.Num() == 0 ? FScctTestResult::Pass(Figures) : FScctTestResult::Fail(FString::Join(Problems, TEXT("; ")) + TEXT(" (") + Figures + TEXT(")"));
		}

		FScctFeatureStage Stage;
		FScctFeatureKeys Keys;
		int32 Phase = 0;

	private:
		FScctTestResult End(FScctTestContext& Context, const FScctTestResult& Result)
		{
			if (Result.State != EScctTestState::Running)
			{
				Keys.ReleaseAll(Context);
				Restore(Context);
			}
			return Result;
		}

		bool bOpened = false;
		double MarkAt = -1.0;
		uint64 MarkFrame = 0;
	};

	// ---------------------------------------------------------------------------------------------------------------
	// hud-spy, hud-merc
	// ---------------------------------------------------------------------------------------------------------------

	class FHudTest : public FPresentationTest
	{
	public:
		explicit FHudTest(int32 InSide) : Side(InSide) {}

	protected:
		virtual FString GetOptions() const override { return Side == 1 ? TEXT("?Side=merc?Bots=1") : TEXT("?Side=spy?Bots=1"); }

		virtual FScctTestResult Play(FScctTestContext& Context) override
		{
			UWorld* World = Context.GetWorld();
			UScctHudSubsystem* HudSubsystem = World != nullptr ? World->GetSubsystem<UScctHudSubsystem>() : nullptr;
			const FScctHudModel* Hud = ScctFeatureKit::GetHud(Context);
			const FScctBridgePawn* Pawn = ScctFeatureKit::GetLocalPawn(Context);
			if (HudSubsystem == nullptr || Hud == nullptr || Pawn == nullptr)
			{
				return Stage.GetPlayedSeconds(Context) > PresentationGiveUpSeconds ? FScctTestResult::Fail(TEXT("the HUD never had the player's state")) : FScctTestResult::Running();
			}
			const FScctHudState& State = Hud->State;
			const TCHAR* SideName = Side == 1 ? TEXT("merc") : TEXT("spy");

			if (Phase == 0)
			{
				// The model against the bridge's pawn of this frame.
				const double Apart = FVector::Dist(FVector(State.X, State.Y, State.Z), FVector(Pawn->X, Pawn->Y, Pawn->Z));
				if (State.Valid != 1)
				{
					Problems.Add(TEXT("the HUD's state is not valid though the player has a body"));
				}
				if (State.Side != Side || Pawn->Kind != Side)
				{
					Problems.Add(FString::Printf(TEXT("the HUD's side is %d and the pawn's kind %d, not %d (%s)"), State.Side, Pawn->Kind, Side, SideName));
				}
				if ((State.Flags & EScctHudFlags::HasPawn) == 0)
				{
					Problems.Add(TEXT("the HUD says the player has no live pawn"));
				}
				if (State.Health != Pawn->Health || State.Health <= 0)
				{
					Problems.Add(FString::Printf(TEXT("the HUD's health is %d, the pawn's %d"), State.Health, Pawn->Health));
				}
				if (Apart > 300.0)
				{
					Problems.Add(FString::Printf(TEXT("the HUD's place is %.0f uu from the pawn's"), Apart));
				}
				if (Hud->Gadgets.Num() != State.GadgetCount)
				{
					Problems.Add(FString::Printf(TEXT("the HUD lists %d gadgets, its state counts %d"), Hud->Gadgets.Num(), State.GadgetCount));
				}
				if (!Hud->bHasRound)
				{
					Problems.Add(TEXT("the HUD has no round state"));
				}
				if (Side == 0 && State.EnergyLevelMax <= 0)
				{
					Problems.Add(FString::Printf(TEXT("the spy's energy gauge has no full mark (%.0f of %d)"), State.EnergyLevel, State.EnergyLevelMax));
				}
				if (Side == 1 && ((State.Flags & EScctHudFlags::HasWeapon) == 0 || State.WeaponKind < 0 || State.Ammo <= 0 || State.ClipAmmo <= 0 || State.ClipAmmo > State.Ammo))
				{
					Problems.Add(FString::Printf(TEXT("the merc's weapon fields are not a loaded gun (kind %d, clip %d, rounds %d, flags 0x%x)"), State.WeaponKind, State.ClipAmmo, State.Ammo, State.Flags));
				}
				Figures = FString::Printf(TEXT("%s HUD: health %d (pawn %d), %.0f uu from the pawn, %d gadgets, energy %.0f/%d, weapon kind %d clip %d rounds %d"),
					SideName, State.Health, Pawn->Health, Apart, Hud->Gadgets.Num(), State.EnergyLevel, State.EnergyLevelMax, State.WeaponKind, State.ClipAmmo, State.Ammo);
				ScctFeatureKit::Note(GetName(), Figures);

				if (!ScctFeatureKit::IsBroken())
				{
					HudSubsystem->QueueCommandForCheck(TEXT("Scoreboard"), 1);
					if (Side == 1)
					{
						HudSubsystem->QueueCommandForCheck(TEXT("Laser"), 1);
					}
				}
				Phase = 1;
				return FScctTestResult::Running();
			}
			if (Phase == 1)
			{
				if (!Waited(Context, 2.5))
				{
					return FScctTestResult::Running();
				}
				ScctFeatureKit::Shot(GetName(), TEXT("staged"));

				// The groups the canvas painted in the last frames, for this side.
				TArray<FString> Painted;
				for (int32 Group = 0; Group < ScctHudLayout::NumGroups; ++Group)
				{
					const FScctHudGroupLive& Live = ScctHudLayout::Live(static_cast<EScctHudGroup>(Group));
					if (Live.Raw.bIsValid && Live.Side == Side && GFrameCounter - Live.PaintedFrame <= 10)
					{
						Painted.Add(ScctHudLayout::GroupId(static_cast<EScctHudGroup>(Group)));
					}
				}
				if (Painted.Num() < 2)
				{
					Problems.Add(FString::Printf(TEXT("the HUD canvas painted %d of its groups in the last frames (%s)"), Painted.Num(), *FString::Join(Painted, TEXT(" "))));
				}

				bool bLocalRow = false;
				for (const FScctHudScoreRow& Row : Hud->Scores)
				{
					bLocalRow |= (Row.Flags & EScctHudScoreFlags::Local) != 0 && Row.Team == Side;
				}
				if (Hud->ScoreboardShown < 0.5f)
				{
					Problems.Add(FString::Printf(TEXT("the Scoreboard command did not bring the scoreboard up (shown %.2f)"), Hud->ScoreboardShown));
				}
				if (!bLocalRow || Hud->Scores.Num() < 2)
				{
					Problems.Add(FString::Printf(TEXT("the scoreboard has %d rows%s; the player and one bot were expected"), Hud->Scores.Num(), bLocalRow ? TEXT("") : TEXT(" and none is the local player's on this side")));
				}
				const bool bLaser = (State.Flags & EScctHudFlags::LaserOn) != 0;
				if (Side == 1 && !bLaser)
				{
					Problems.Add(TEXT("the Laser command did not put the merc's laser sight on in the HUD"));
				}
				if (Side == 1 && bLaser && (State.Flags & EScctHudFlags::ShowReticle) != 0)
				{
					Problems.Add(TEXT("the HUD shows the reticle while the laser sight is on"));
				}
				Figures += FString::Printf(TEXT("; painted groups: %s; scoreboard shown %.2f with %d rows%s"), *FString::Join(Painted, TEXT(" ")), Hud->ScoreboardShown, Hud->Scores.Num(),
					Side == 1 ? (bLaser ? TEXT("; laser sight on") : TEXT("; laser sight off")) : TEXT(""));
				ScctFeatureKit::Note(GetName(), Figures);
				if (Problems.Num() > 0)
				{
					ScctFeatureKit::Shot(GetName(), TEXT("failed"));
				}
				return Verdict(Problems, Figures);
			}
			return FScctTestResult::Running();
		}

		virtual void Restore(FScctTestContext& Context) override
		{
			UWorld* World = Context.GetWorld();
			if (UScctHudSubsystem* HudSubsystem = World != nullptr ? World->GetSubsystem<UScctHudSubsystem>() : nullptr)
			{
				HudSubsystem->QueueCommandForCheck(TEXT("Scoreboard"), 0);
			}
		}

	private:
		int32 Side;
		TArray<FString> Problems;
		FString Figures;
	};

	class FHudSpyTest : public FHudTest { public: FHudSpyTest() : FHudTest(0) {} };
	class FHudMercTest : public FHudTest { public: FHudMercTest() : FHudTest(1) {} };

	// ---------------------------------------------------------------------------------------------------------------
	// audio
	// ---------------------------------------------------------------------------------------------------------------

	class FAudioTest : public FPresentationTest
	{
	protected:
		virtual FString GetOptions() const override { return TEXT("?Side=merc?Bots=0"); }

		virtual FScctTestResult Play(FScctTestContext& Context) override
		{
			const UWorld* World = Context.GetWorld();
			const UScctAudioSubsystem* Audio = World != nullptr ? World->GetSubsystem<UScctAudioSubsystem>() : nullptr;
			if (Audio == nullptr)
			{
				return FScctTestResult::Fail(TEXT("the world has no audio subsystem"));
			}
			PeakVoices = FMath::Max(PeakVoices, Audio->GetNumVoices());
			if (Phase == 0)
			{
				StartedBefore = Audio->GetNumStarted();
				WorldBefore = Audio->GetNumStartedIn(0);
				ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("%d voices started before the burst (%d in the world category), %d sounding or waiting%s"), StartedBefore, WorldBefore,
					Audio->GetNumVoices(), ScctFeatureKit::IsBroken() ? TEXT(" (-ScctFeatureBreak: fire is not pressed)") : TEXT("")));
				if (!ScctFeatureKit::IsBroken())
				{
					Keys.Hold(Context, TEXT("fire"), true);
				}
				FireSim = Stage.GetSimSeconds(Context);
				Phase = 1;
				return FScctTestResult::Running();
			}
			if (Phase == 1)
			{
				if (Stage.GetSimSeconds(Context) < FireSim + 1.5)
				{
					return FScctTestResult::Running();
				}
				Keys.Hold(Context, TEXT("fire"), false);
				Phase = 2;
				return FScctTestResult::Running();
			}
			if (Stage.GetSimSeconds(Context) < FireSim + 3.0)
			{
				return FScctTestResult::Running();
			}

			static const TCHAR* const CategoryNames[] = { TEXT("world"), TEXT("music"), TEXT("menu music"), TEXT("objective"), TEXT("menu sound"), TEXT("voice") };
			TArray<FString> Categories;
			for (int32 Category = 0; Category < UE_ARRAY_COUNT(CategoryNames); ++Category)
			{
				if (Audio->GetNumStartedIn(Category) > 0)
				{
					Categories.Add(FString::Printf(TEXT("%s %d"), CategoryNames[Category], Audio->GetNumStartedIn(Category)));
				}
			}
			const int32 Started = Audio->GetNumStarted() - StartedBefore;
			const int32 WorldStarted = Audio->GetNumStartedIn(0) - WorldBefore;
			const FString Figures = FString::Printf(TEXT("the burst started %d voices (%d in the world category); %d since the match began (%s), at most %d at once, %d files failed to load"),
				Started, WorldStarted, Audio->GetNumStarted(), *FString::Join(Categories, TEXT(", ")), PeakVoices, Audio->GetNumFailedFiles());
			ScctFeatureKit::Note(GetName(), Figures);
			TArray<FString> Problems;
			if (WorldStarted <= 0)
			{
				Problems.Add(TEXT("holding fire for 1.5 s started no voice in the world category"));
			}
			if (Audio->GetNumFailedFiles() > 0)
			{
				Problems.Add(FString::Printf(TEXT("%d sound files failed to load"), Audio->GetNumFailedFiles()));
			}
			return Verdict(Problems, Figures);
		}

	private:
		int32 StartedBefore = 0;
		int32 WorldBefore = 0;
		int32 PeakVoices = 0;
		double FireSim = 0.0;
	};

	// ---------------------------------------------------------------------------------------------------------------
	// voice: no map; the microphone test of -ScctVoiceEcho fed by -ScctVoiceTone.
	// ---------------------------------------------------------------------------------------------------------------

	class FVoiceTest : public IScctTest
	{
	public:
		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (ScctFeature<FScctVoiceApi>() == nullptr)
			{
				return Context.ElapsedSeconds < 10.0 ? FScctTestResult::Running() : FScctTestResult::Pass(TEXT("not applicable: the bridge has no voice table"));
			}
			float Tone = 0.0f;
			if (!FParse::Value(FCommandLine::Get(), TEXT("ScctVoiceTone="), Tone))
			{
				return FScctTestResult::Fail(TEXT("the voice check needs -ScctVoiceTone=<Hz> for a microphone (Tools/Test/tests.psd1 adds it)"));
			}
			const UScctVoiceSubsystem* Voice = Context.GameInstance != nullptr ? Context.GameInstance->GetSubsystem<UScctVoiceSubsystem>() : nullptr;
			if (Voice == nullptr)
			{
				return FScctTestResult::Fail(TEXT("the game has no voice subsystem"));
			}
			// The microphone test is started as the Voice page's switch starts it, once the front end draws steadily: one
			// begun while the game still loads has its recording time pass inside a single long frame.
			if (!bStarted)
			{
				if (Context.ElapsedSeconds < 6.0 || Context.Frame < 120)
				{
					return FScctTestResult::Running();
				}
				bStarted = true;
				StartedAt = Context.ElapsedSeconds;
				if (!ScctFeatureKit::IsBroken())
				{
					FScctSettings::SetBool(TEXT("Voice.Test"), true);
				}
				return FScctTestResult::Running();
			}
			const FScctVoiceStatus Status = Voice->GetStatus();
			PeakCaptured = FMath::Max(PeakCaptured, Status.FramesCaptured);
			PeakSent = FMath::Max(PeakSent, Status.FramesSent);
			PeakReceived = FMath::Max(PeakReceived, Status.PacketsReceived);
			PeakLevel = FMath::Max(PeakLevel, Status.InputLevel);
			bPlayed |= Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::Playing) || Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::Done);
			bRecorded |= Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::Recording);
			const FString Figures = FString::Printf(TEXT("%lld frames captured, %lld sent, %lld packets back from the relay, input level %.2f, recording %s, playback %s"),
				PeakCaptured, PeakSent, PeakReceived, PeakLevel, bRecorded ? TEXT("seen") : TEXT("not seen"), bPlayed ? TEXT("reached") : TEXT("not reached"));
			if (Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::NothingCaptured))
			{
				return FScctTestResult::Fail(TEXT("the microphone test captured nothing from the tone (") + Figures + TEXT(")"));
			}
			if (bPlayed && PeakCaptured > 0 && PeakReceived > 0)
			{
				ScctFeatureKit::Note(GetName(), Figures);
				return FScctTestResult::Pass(Figures);
			}
			if (Context.ElapsedSeconds - StartedAt > 30.0)
			{
				return FScctTestResult::Fail(TEXT("the tone did not come back through the voice path in 30 s (") + Figures + TEXT(")"));
			}
			return FScctTestResult::Running();
		}

	private:
		int64 PeakCaptured = 0;
		int64 PeakSent = 0;
		int64 PeakReceived = 0;
		float PeakLevel = 0.0f;
		bool bRecorded = false;
		bool bPlayed = false;
		bool bStarted = false;
		double StartedAt = 0.0;
	};

	// ---------------------------------------------------------------------------------------------------------------
	// cloak
	// ---------------------------------------------------------------------------------------------------------------

	struct FCloakRead
	{
		int32 Slots = 0;
		int32 CloakSlots = 0;
		float LeastAmount = 1.0f;
		bool bCastShadow = false;
	};

	FCloakRead ReadCloak(const UMeshComponent& Body)
	{
		static const FName CloakAmountName(TEXT("Amount"));
		FCloakRead Read;
		Read.Slots = Body.GetNumMaterials();
		Read.bCastShadow = Body.CastShadow;
		for (int32 Slot = 0; Slot < Read.Slots; ++Slot)
		{
			UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Body.GetMaterial(Slot));
			const UMaterialInterface* Base = Material != nullptr ? Material->GetBaseMaterial() : nullptr;
			if (Base != nullptr && Base->GetName() == TEXT("M_ScctCloak"))
			{
				++Read.CloakSlots;
				float Amount = 0.0f;
				Material->GetScalarParameterValue(CloakAmountName, Amount);
				Read.LeastAmount = FMath::Min(Read.LeastAmount, Amount);
			}
		}
		return Read;
	}

	class FCloakTest : public FPresentationTest
	{
	protected:
		virtual FScctTestResult Play(FScctTestContext& Context) override
		{
			UWorld* World = Context.GetWorld();
			UScctEquipmentSubsystem* Equipment = World != nullptr ? World->GetSubsystem<UScctEquipmentSubsystem>() : nullptr;
			const UScctSessionSubsystem* Session = Context.GetSession();
			const AScctPawnProxy* Proxy = Session != nullptr ? Session->FindLocalProxy() : nullptr;
			const UMeshComponent* Body = Proxy != nullptr ? Proxy->GetBodyMesh() : nullptr;
			if (Equipment == nullptr || Body == nullptr)
			{
				return FScctTestResult::Fail(TEXT("the spy has no drawn body or the world no equipment subsystem"));
			}
			if (Phase == 0)
			{
				Before = ReadCloak(*Body);
				if (!ScctFeatureKit::IsBroken())
				{
					Equipment->ForceCloakForCheck(1.0f);
				}
				Phase = 1;
				return FScctTestResult::Running();
			}
			if (Phase == 1)
			{
				if (!Waited(Context, 1.0))
				{
					return FScctTestResult::Running();
				}
				ScctFeatureKit::Shot(GetName(), TEXT("cloaked"));
				On = ReadCloak(*Body);
				Equipment->ForceCloakForCheck(-1.0f);
				Phase = 2;
				return FScctTestResult::Running();
			}
			if (!Waited(Context, 1.0))
			{
				return FScctTestResult::Running();
			}
			const FCloakRead After = ReadCloak(*Body);
			const FString Figures = FString::Printf(TEXT("body of %d material slots: %d cloaked before; held on, %d cloaked at Amount %.2f, shadow %s; let go, %d cloaked, shadow %s"),
				Before.Slots, Before.CloakSlots, On.CloakSlots, On.CloakSlots > 0 ? On.LeastAmount : 0.0f, On.bCastShadow ? TEXT("cast") : TEXT("gone"), After.CloakSlots, After.bCastShadow ? TEXT("cast") : TEXT("gone"));
			ScctFeatureKit::Note(GetName(), Figures);
			TArray<FString> Problems;
			if (Before.Slots <= 0)
			{
				Problems.Add(TEXT("the spy's body has no material slot"));
			}
			if (Before.CloakSlots > 0)
			{
				Problems.Add(TEXT("the body wore the cloak material before the suit was on"));
			}
			if (On.CloakSlots != On.Slots || On.Slots <= 0)
			{
				Problems.Add(FString::Printf(TEXT("the suit held on put the cloak material on %d of %d slots"), On.CloakSlots, On.Slots));
			}
			else if (On.LeastAmount < 0.99f)
			{
				Problems.Add(FString::Printf(TEXT("the cloak material's Amount is %.2f with the suit fully on"), On.LeastAmount));
			}
			else if (On.bCastShadow)
			{
				Problems.Add(TEXT("the fully cloaked body still casts its shadow"));
			}
			if (After.CloakSlots > 0)
			{
				Problems.Add(FString::Printf(TEXT("%d slots kept the cloak material after the suit came off"), After.CloakSlots));
			}
			else if (After.bCastShadow != Before.bCastShadow)
			{
				Problems.Add(TEXT("the body's shadow did not come back with its materials"));
			}
			return Verdict(Problems, Figures);
		}

		virtual void Restore(FScctTestContext& Context) override
		{
			UWorld* World = Context.GetWorld();
			if (UScctEquipmentSubsystem* Equipment = World != nullptr ? World->GetSubsystem<UScctEquipmentSubsystem>() : nullptr)
			{
				Equipment->ForceCloakForCheck(-1.0f);
			}
		}

	private:
		FCloakRead Before;
		FCloakRead On;
	};

	// ---------------------------------------------------------------------------------------------------------------
	// vision, lasers: both hold the vision modes in turn.
	// ---------------------------------------------------------------------------------------------------------------

	class FVisionHoldTest : public FPresentationTest
	{
	protected:
		UScctVisionSubsystem* GetVision(FScctTestContext& Context) const
		{
			UWorld* World = Context.GetWorld();
			return World != nullptr ? World->GetSubsystem<UScctVisionSubsystem>() : nullptr;
		}

		virtual void Restore(FScctTestContext& Context) override
		{
			if (UScctVisionSubsystem* Vision = GetVision(Context))
			{
				Vision->ForceModeForCheck(INDEX_NONE);
			}
		}
	};

	class FVisionTest : public FVisionHoldTest
	{
	protected:
		virtual FScctTestResult Play(FScctTestContext& Context) override
		{
			static const int32 Legs[] = { 34, 36, 37, 38, 0 };
			UScctVisionSubsystem* Vision = GetVision(Context);
			if (Vision == nullptr)
			{
				return FScctTestResult::Fail(TEXT("the world has no vision subsystem"));
			}
			const int32 Mode = Legs[Phase];
			if (!bHeld)
			{
				bHeld = true;
				// -ScctFeatureBreak holds plain sight throughout.
				Vision->ForceModeForCheck(ScctFeatureKit::IsBroken() ? 0 : Mode);
				if (Mode == 0)
				{
					// Sample the first frame out of EMF. By the settled-mode checks below the old
					// render-pass delay has already elapsed.
					const UScctSessionSubsystem* Session = Context.GetWorld()->GetSubsystem<UScctSessionSubsystem>();
					const AScctPawnProxy* Spy = Session ? Session->FindLocalProxy() : nullptr;
					const UPoseableMeshComponent* Body = Spy ? Spy->GetBodyMesh() : nullptr;
					if (!Body || Body->bRenderInMainPass)
					{
						return FScctTestResult::Fail(TEXT("the idle spy was not hidden from the main pass in EMF"));
					}
					Vision->Present(Mode, 1.0f / 60.0f);
					if (!Body->bRenderInMainPass || !Body->bRenderInDepthPass || Vision->GetModeCheck(3).Amount <= 0.0f)
					{
						return FScctTestResult::Fail(TEXT("the spy's normal colour and depth did not return on the first frame of EMF fade-out"));
					}
				}
				return FScctTestResult::Running();
			}
			if (!Waited(Context, 1.5))
			{
				return FScctTestResult::Running();
			}
			ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("mode-%d"), Mode));
			auto* Light = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/OpenSCCT/Materials/MPC_ScctLight.MPC_ScctLight"));
			auto* Instance = Light ? Context.GetWorld()->GetParameterCollectionInstance(Light) : nullptr;
			float Boost = 0.0f;
			if (!Instance || !Instance->GetScalarParameterValue(TEXT("Boost"), Boost)
				|| !FMath::IsNearlyEqual(Boost, Mode == 34 ? 30.0f / 255.0f : 0.0f))
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("surface night-light boost is %.4f in mode %d"), Boost, Mode));
			}
			TArray<FString> Parts;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const UScctVisionSubsystem::FModeCheck Held = Vision->GetModeCheck(Index);
				Parts.Add(FString::Printf(TEXT("%d: amount %.2f, material %.2f, weight %.0f"), Held.Mode, Held.Amount, Held.MaterialAmount, Held.BlendWeight));
				const bool bThis = Held.Mode == Mode;
				if (!Held.bMaterial)
				{
					Problems.Add(FString::Printf(TEXT("holding mode %d: mode %d has no post-process material in the goggles' volume"), Mode, Held.Mode));
				}
				else if (bThis && (Held.Amount < 0.99f || Held.MaterialAmount < 0.99f || Held.BlendWeight < 1.0f))
				{
					Problems.Add(FString::Printf(TEXT("mode %d held for 1.5 s is not applied (amount %.2f, material Amount %.2f, weight %.0f)"), Mode, Held.Amount, Held.MaterialAmount, Held.BlendWeight));
				}
				else if (!bThis && (Held.Amount > 0.0f || Held.MaterialAmount > 0.01f || Held.BlendWeight > 0.0f))
				{
					Problems.Add(FString::Printf(TEXT("holding mode %d, mode %d is still in the picture (amount %.2f, material Amount %.2f, weight %.0f)"), Mode, Held.Mode, Held.Amount, Held.MaterialAmount, Held.BlendWeight));
				}
			}
			if (Vision->GetVisionMode() != Mode)
			{
				Problems.Add(FString::Printf(TEXT("the goggles show mode %d, not the held %d"), Vision->GetVisionMode(), Mode));
			}
			const FString Line = FString::Printf(TEXT("mode %d held: %s"), Mode, *FString::Join(Parts, TEXT("; ")));
			ScctFeatureKit::Note(GetName(), Line);
			Report.Add(Line);
			bHeld = false;
			if (Problems.Num() > 0)
			{
				ScctFeatureKit::Shot(GetName(), TEXT("failed"));
				return Verdict(Problems, Line);
			}
			if (++Phase >= UE_ARRAY_COUNT(Legs))
			{
				return FScctTestResult::Pass(TEXT("night, thermal, motion and EMF each applied alone at Amount 1, and plain sight left none"));
			}
			return FScctTestResult::Running();
		}

	private:
		bool bHeld = false;
		TArray<FString> Problems;
		TArray<FString> Report;
	};

	class FLasersTest : public FVisionHoldTest
	{
	protected:
		virtual FScctTestResult Play(FScctTestContext& Context) override
		{
			static const int32 Legs[] = { 0, 36, 38, 0 };
			UWorld* World = Context.GetWorld();
			UScctVisionSubsystem* Vision = GetVision(Context);
			const UScctMarkersSubsystem* Markers = World != nullptr ? World->GetSubsystem<UScctMarkersSubsystem>() : nullptr;
			if (Vision == nullptr || Markers == nullptr)
			{
				return FScctTestResult::Fail(TEXT("the world has no vision or markers subsystem"));
			}
			const int32 Mode = Legs[Phase];
			if (!bHeld)
			{
				bHeld = true;
				Vision->ForceModeForCheck(ScctFeatureKit::IsBroken() ? 0 : Mode);
				return FScctTestResult::Running();
			}
			if (!Waited(Context, 1.5))
			{
				return FScctTestResult::Running();
			}
			ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("mode-%d"), Mode));
			const UScctMarkersSubsystem::FBeamCheck Beams = Markers->GetBeamCheck();
			const FString Line = FString::Printf(TEXT("mode %d: %d beams listed (%d for the vision modes only), %d streaks (%d components visible), %d for the goggles (%d custom depth only)"),
				Mode, Beams.Listed, Beams.VisionOnly, Beams.Streaks, Beams.VisibleCores, Beams.Goggle, Beams.StencilOnly);
			ScctFeatureKit::Note(GetName(), Line);
			Report.Add(Line);
			if (Beams.Listed <= 0)
			{
				return FScctTestResult::Pass(FString::Printf(TEXT("not applicable: %s has no detector beam to draw"), *Stage.GetMap()));
			}
			const int32 Plain = Beams.Listed - Beams.VisionOnly;
			if (Mode == 0)
			{
				if (Beams.Streaks != Plain || Beams.VisibleCores != Plain)
				{
					Problems.Add(FString::Printf(TEXT("plain sight: %d streaks drawn (%d visible) for %d beams the map shows the naked eye"), Beams.Streaks, Beams.VisibleCores, Plain));
				}
				if (Beams.Goggle != 0 || Beams.StencilOnly != 0)
				{
					Problems.Add(FString::Printf(TEXT("plain sight: %d goggle beams are still drawn"), FMath::Max(Beams.Goggle, Beams.StencilOnly)));
				}
			}
			else
			{
				if (Beams.Goggle != Beams.Listed || Beams.StencilOnly != Beams.Listed)
				{
					Problems.Add(FString::Printf(TEXT("mode %d: %d of %d beams are drawn for the goggles (%d custom depth only)"), Mode, Beams.Goggle, Beams.Listed, Beams.StencilOnly));
				}
				if (Beams.Streaks != 0 || Beams.VisibleCores != 0)
				{
					Problems.Add(FString::Printf(TEXT("mode %d: %d plain streaks are still in the picture"), Mode, FMath::Max(Beams.Streaks, Beams.VisibleCores)));
				}
			}
			bHeld = false;
			if (Problems.Num() > 0)
			{
				ScctFeatureKit::Shot(GetName(), TEXT("failed"));
				return Verdict(Problems, FString::Join(Report, TEXT(" | ")));
			}
			if (++Phase >= UE_ARRAY_COUNT(Legs))
			{
				return FScctTestResult::Pass(FString::Join(Report, TEXT(" | ")));
			}
			return FScctTestResult::Running();
		}

	private:
		bool bHeld = false;
		TArray<FString> Problems;
		TArray<FString> Report;
	};

	// ---------------------------------------------------------------------------------------------------------------
	// fog
	// ---------------------------------------------------------------------------------------------------------------

	class FFogTest : public FPresentationTest
	{
	protected:
		virtual FScctTestResult Play(FScctTestContext& Context) override
		{
			UWorld* World = Context.GetWorld();
			int32 Fogs = 0;
			int32 ShownFogs = 0;
			float Density = 0.0f;
			bool bVolumetric = false;
			for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
			{
				++Fogs;
				const UExponentialHeightFogComponent* Fog = It->GetComponent();
				if (Fog != nullptr && Fog->IsVisible() && Fog->FogDensity > 0.0f)
				{
					++ShownFogs;
					Density = FMath::Max(Density, Fog->FogDensity);
					bVolumetric |= Fog->bEnableVolumetricFog;
				}
			}
			int32 Volumes = 0;
			int32 LevelVolumes = 0;
			for (TActorIterator<APostProcessVolume> It(World); It; ++It)
			{
				++Volumes;
				// The level's own: saved with the level, not spawned by the client (the goggles', the screen effects').
				LevelVolumes += It->bEnabled && !It->HasAnyFlags(RF_Transient) && It->GetLevel() == World->PersistentLevel && It->HasAnyFlags(RF_WasLoaded) ? 1 : 0;
			}
			const FString Figures = FString::Printf(TEXT("%s: %d height fog actors, %d shown with density (most %.4f, volumetric %s); %d post-process volumes, %d of the level's own enabled"),
				*Stage.GetMap(), Fogs, ShownFogs, Density, bVolumetric ? TEXT("on") : TEXT("off"), Volumes, LevelVolumes);
			ScctFeatureKit::Note(GetName(), Figures);
			TArray<FString> Problems;
			if (ShownFogs <= 0)
			{
				Problems.Add(TEXT("the level has no visible height fog with a density"));
			}
			if (LevelVolumes <= 0)
			{
				Problems.Add(TEXT("the level has no enabled post-process volume of its own"));
			}
			if (Problems.Num() > 0)
			{
				ScctFeatureKit::Shot(GetName(), TEXT("failed"));
			}
			return Verdict(Problems, Figures);
		}
	};
}

SCCT_TEST("hud-spy", ScctPresentationFeatureLocal::FHudSpyTest);
SCCT_TEST("hud-merc", ScctPresentationFeatureLocal::FHudMercTest);
SCCT_TEST("audio", ScctPresentationFeatureLocal::FAudioTest);
SCCT_TEST("voice", ScctPresentationFeatureLocal::FVoiceTest);
SCCT_TEST("cloak", ScctPresentationFeatureLocal::FCloakTest);
SCCT_TEST("vision", ScctPresentationFeatureLocal::FVisionTest);
SCCT_TEST("lasers", ScctPresentationFeatureLocal::FLasersTest);
SCCT_TEST("fog", ScctPresentationFeatureLocal::FFogTest);
