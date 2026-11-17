#include "Hud/ScctScreenEffectsSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Hud/ScctHudModel.h"
#include "Misc/CommandLine.h"
#include "OpenSCCT.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "RendererInterface.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "TextureResource.h"

#include <atomic>

/**
 * Copies the player's picture out of the post-processing chain once when asked: straight after tone mapping, so it is
 * the frame as shown (display values, the eye's exposure applied) without the HUD, as the original's post-render
 * capture took it. The goggles' materials come after this point, which is where the original drew the after-image
 * in normal and night vision.
 */
class FScctAfterImageCapture final : public FSceneViewExtensionBase
{
public:
	explicit FScctAfterImageCapture(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	/** Render thread: the next player frame goes into this target (null cancels). */
	void SetTarget_RenderThread(FTextureRenderTargetResource* InTarget)
	{
		Target = InTarget;
	}

	/** Frames copied so far (the game thread reads it). */
	int32 GetCaptures() const
	{
		return Captures.load(std::memory_order_acquire);
	}

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override
	{
		// The player's own view only: a scene capture or a thumbnail is not what the player saw.
		if (Pass != EPostProcessingPass::Tonemap || !bIsPassEnabled || Target == nullptr || !InView.bIsGameView || InView.bIsSceneCapture)
		{
			return;
		}
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateSP(StaticCastSharedRef<FScctAfterImageCapture>(AsShared()), &FScctAfterImageCapture::CopyFrame));
	}

private:
	FScreenPassTexture CopyFrame(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
	{
		FTextureRenderTargetResource* Into = Target;
		Target = nullptr;
		FRHITexture* Texture = Into != nullptr ? Into->GetRenderTargetTexture() : nullptr;
		const FScreenPassTextureSlice SceneColor = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
		if (Texture != nullptr && SceneColor.IsValid())
		{
			FRDGTextureRef Output = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Texture, TEXT("ScctAfterImage")));
			// Drawn, not copied: the target is smaller than the picture and may differ in format.
			AddDrawTexturePass(GraphBuilder, FScreenPassViewInfo(View), SceneColor, FScreenPassRenderTarget(Output, ERenderTargetLoadAction::ENoAction));
			Captures.fetch_add(1, std::memory_order_release);
		}
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	// Render thread only.
	FTextureRenderTargetResource* Target = nullptr;
	std::atomic<int32> Captures{ 0 };
};

bool UScctScreenEffectsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctScreenEffectsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// -ScctForceFlash or -ScctForceFlash=seconds.
	float Seconds = 4.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctForceFlash="), Seconds) || FParse::Param(FCommandLine::Get(), TEXT("ScctForceFlash")))
	{
		ForceDelay = FMath::Max(Seconds, 0.0f);
	}
	float CrossEye = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctForceCrossEye="), CrossEye))
	{
		ForcedCrossEye = FMath::Clamp(CrossEye, 0.0f, 1.0f);
	}
	ScctVisorEffects::ParseCommandLine(Visor);
}

void UScctScreenEffectsSubsystem::Deinitialize()
{
	if (Capture.IsValid())
	{
		// The target is released after this on the render thread, so the capture must forget it first.
		ENQUEUE_RENDER_COMMAND(ScctAfterImageForget)([Extension = Capture](FRHICommandListImmediate&)
		{
			Extension->SetTarget_RenderThread(nullptr);
		});
		Capture.Reset();
	}
	Clear();
	Super::Deinitialize();
}

float UScctScreenEffectsSubsystem::FlashWhiteOut(const FScctHudState& State)
{
	float White = 0.0f;

	// The merc page's Flash: white, alpha 255 to 0 over 8 s from the hit (screen-effects.md rule 14).
	if (State.Side == 1 && State.FlashTime >= 0.0f)
	{
		const float Age = State.TimeSeconds - State.FlashTime;
		if (Age >= 0.0f && Age < FlashSeconds)
		{
			White = 1.0f - Age / FlashSeconds;
		}
	}

	// The FlashEffect slot runs 1 to 0 over the same 8 s. It is what blinds a player without the merc page's
	// white-out; for a merc the two coincide, so the screen is never whitened twice.
	for (int32 Slot = 0; Slot < ScctHudSfxSlots; ++Slot)
	{
		if (State.SfxType[Slot] == EScctHudSfx::Flash)
		{
			White = FMath::Max(White, FMath::Clamp(State.SfxValue[Slot], 0.0f, 1.0f));
		}
	}
	return White;
}

float UScctScreenEffectsSubsystem::CrossEyeFactor(const FScctHudState& State)
{
	// The bridge already sets CrossEyedFactor from the slots as ProcessSFX does; the slots are read too so a slot
	// value is never lost to an update order between the two.
	float Factor = FMath::Clamp(State.CrossEyedFactor, 0.0f, 1.0f);
	for (int32 Slot = 0; Slot < ScctHudSfxSlots; ++Slot)
	{
		if (State.SfxType[Slot] == EScctHudSfx::CrossEye || State.SfxType[Slot] == EScctHudSfx::Smoke)
		{
			Factor = FMath::Max(Factor, FMath::Clamp(State.SfxValue[Slot], 0.0f, 1.0f));
		}
	}
	return Factor;
}

FVector2f UScctScreenEffectsSubsystem::DoubleVisionSpread(float Factor, double Seconds)
{
	const double F = FMath::Clamp(Factor, 0.0f, 1.0f);
	const double A = (20.0 * FMath::Cos(1.8849557195089517 * Seconds) + 5.0 * FMath::Cos(4.826742963254986 * Seconds) + 50.0) * F * 0.0015625;
	const double B = (20.0 * FMath::Cos(2.8274335792634275 * Seconds) + 5.0 * FMath::Cos(4.826742963254986 * (Seconds + 0.12999999523162842)) + 50.0) * F * 0.0015625;
	return FVector2f(static_cast<float>(A), static_cast<float>(B));
}

bool UScctScreenEffectsSubsystem::EnsureDoubleVision()
{
	if (IsValid(DoubleVolume) && IsValid(DoubleMaterial))
	{
		return true;
	}
	if (bTriedDoubleVision)
	{
		return false;
	}
	bTriedDoubleVision = true;

	static const TCHAR* MaterialPath = TEXT("/Game/OpenSCCT/UI/Vision/M_ScctDoubleVision.M_ScctDoubleVision");
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
	if (Material == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScreenFx: %s is missing; run Tools/Materials/Build-VisionMaterials.ps1. Gas and smoke will not double the view."), MaterialPath);
		return false;
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	DoubleVolume = GetWorld()->SpawnActor<APostProcessVolume>(Parameters);
	if (!IsValid(DoubleVolume))
	{
		return false;
	}
	DoubleVolume->bUnbound = true;
	DoubleVolume->BlendWeight = 1.0f;
	// Over the goggles' volume (1000); the material's own blendable priority puts it after their materials.
	DoubleVolume->Priority = 1001.0f;
	DoubleMaterial = UMaterialInstanceDynamic::Create(Material, this);
	DoubleMaterial->SetScalarParameterValue(TEXT("Amount"), 0.0f);
	DoubleVolume->Settings.WeightedBlendables.Array.Emplace(0.0f, DoubleMaterial);
	return true;
}

void UScctScreenEffectsSubsystem::PresentDoubleVision(float Factor)
{
	// Factors at or under the threshold draw no doubling (the spreads are next to nothing there anyway).
	const bool bOn = Factor > AfterImageThreshold;
	if (bOn != bDoubled)
	{
		bDoubled = bOn;
		if (bOn)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScreenFx: double vision %.2f"), Factor);
			NextDoubleLog = 0.0;
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScreenFx: double vision over"));
		}
	}

	if (!bOn)
	{
		if (IsValid(DoubleVolume))
		{
			DoubleMaterial->SetScalarParameterValue(TEXT("Amount"), 0.0f);
			DoubleVolume->Settings.WeightedBlendables.Array[0].Weight = 0.0f;
		}
		return;
	}
	if (!EnsureDoubleVision())
	{
		return;
	}

	// The viewport's time, taken as wall-clock seconds as the Godot port does.
	const double Seconds = GetWorld()->GetRealTimeSeconds();
	const FVector2f Spread = DoubleVisionSpread(Factor, Seconds);
	DoubleMaterial->SetScalarParameterValue(TEXT("Amount"), 1.0f);
	DoubleMaterial->SetScalarParameterValue(TEXT("SpreadA"), Spread.X);
	DoubleMaterial->SetScalarParameterValue(TEXT("SpreadB"), Spread.Y);
	DoubleVolume->Settings.WeightedBlendables.Array[0].Weight = 1.0f;

	if (Seconds >= NextDoubleLog)
	{
		NextDoubleLog = Seconds + 2.0;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScreenFx: double vision %.2f, spread %.4f %.4f at %.1f s"), Factor, Spread.X, Spread.Y, Seconds);
	}
}

void UScctScreenEffectsSubsystem::Present(FScctHudModel& Model, float DeltaSeconds)
{
	ApplyForcedFlash(Model, DeltaSeconds);
	if (ForcedCrossEye >= 0.0f && Model.bHasState)
	{
		Model.State.CrossEyedFactor = FMath::Max(Model.State.CrossEyedFactor, ForcedCrossEye);
	}
	ScctVisorEffects::Update(Visor, Model, DeltaSeconds);
	Model.Visor = &Visor;

	const FScctHudState& State = Model.State;
	PresentDoubleVision(Model.bHasState ? CrossEyeFactor(State) : 0.0f);
	if (Model.bHasState)
	{
		// bCaptureRemanence: each new request freezes the next frame (the first value seen is only remembered).
		if (LastCaptures != INDEX_NONE && State.RemanenceCaptures != LastCaptures)
		{
			RequestCapture();
		}
		LastCaptures = State.RemanenceCaptures;
	}

	if (Capture.IsValid() && Capture->GetCaptures() != CapturesSeen)
	{
		CapturesSeen = Capture->GetCaptures();
		bHeld = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScreenFx: after-image captured (%dx%d)"), AfterImage->SizeX, AfterImage->SizeY);
	}

	const float Factor = Model.bHasState ? State.RemanenceBlendFactor : 0.0f;
	const bool bDraw = bHeld && IsValid(AfterImage) && Factor > AfterImageThreshold;
	Model.AfterImage = bDraw ? &AfterImageBrush : nullptr;
	Model.AfterImageAlpha = bDraw ? FMath::Clamp(Factor, 0.0f, 1.0f) : 0.0f;

	const float White = Model.bHasState ? FlashWhiteOut(State) : 0.0f;
	if ((White > 0.0f) != bWhiteOut)
	{
		bWhiteOut = White > 0.0f;
		if (bWhiteOut)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScreenFx: flash white-out (%s, %.2f, after-image %.2f)"),
				State.Side == 1 && State.FlashTime >= 0.0f ? TEXT("merc page") : TEXT("flash slot"), White, Factor);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScreenFx: flash white-out over"));
		}
	}
}

void UScctScreenEffectsSubsystem::Clear()
{
	bHeld = false;
	bWhiteOut = false;
	LastCaptures = INDEX_NONE;
	ScctVisorEffects::Reset(Visor);
	PresentDoubleVision(0.0f);
}

void UScctScreenEffectsSubsystem::RequestCapture()
{
	if (GEngine == nullptr || !GEngine->ViewExtensions.IsValid())
	{
		return;
	}
	if (!Capture.IsValid())
	{
		Capture = FSceneViewExtensions::NewExtension<FScctAfterImageCapture>();
		CapturesSeen = Capture->GetCaptures();
	}

	// Half the viewport is plenty for a picture that is only ever seen fading.
	FIntPoint Size(960, 540);
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		FVector2D ViewportSize;
		Viewport->GetViewportSize(ViewportSize);
		if (ViewportSize.X >= 2.0 && ViewportSize.Y >= 2.0)
		{
			Size = FIntPoint(FMath::RoundToInt32(ViewportSize.X * 0.5), FMath::RoundToInt32(ViewportSize.Y * 0.5));
		}
	}

	if (!IsValid(AfterImage))
	{
		AfterImage = NewObject<UTextureRenderTarget2D>(this, TEXT("ScctAfterImage"));
		AfterImage->ClearColor = FLinearColor::Black;
		// The copy holds display values (after tone mapping); read as they are, they show as they were shown.
		AfterImage->InitCustomFormat(Size.X, Size.Y, PF_B8G8R8A8, true);
		AfterImage->UpdateResourceImmediate(true);
		AfterImageBrush = FSlateBrush();
		AfterImageBrush.DrawAs = ESlateBrushDrawType::Image;
		AfterImageBrush.SetResourceObject(AfterImage);
	}
	else if (AfterImage->SizeX != Size.X || AfterImage->SizeY != Size.Y)
	{
		AfterImage->ResizeTarget(Size.X, Size.Y);
	}
	AfterImageBrush.ImageSize = FVector2D(Size);

	// The old picture stays hidden until the new one is in (the next frame).
	bHeld = false;
	FTextureRenderTargetResource* Resource = AfterImage->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(ScctAfterImageRequest)([Extension = Capture, Resource](FRHICommandListImmediate&)
	{
		Extension->SetTarget_RenderThread(Resource);
	});
}

void UScctScreenEffectsSubsystem::ApplyForcedFlash(FScctHudModel& Model, float DeltaSeconds)
{
	if (!Model.bHasState)
	{
		return;
	}
	FScctHudState& State = Model.State;

	if (ForceDelay >= 0.0f)
	{
		ForceDelay -= DeltaSeconds;
		if (ForceDelay < 0.0f)
		{
			ForcedAt = State.TimeSeconds;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScreenFx: forced flash-bang hit at t=%.1f (side %d)"), ForcedAt, State.Side);
			RequestCapture();
		}
	}
	if (ForcedAt < 0.0f)
	{
		return;
	}

	// As ClientDamageHudEffect type 8 sets it: the FlashEffect slot 1 to 0 over 8 s drives RemanenceBlendFactor, and a
	// merc's page gets its white-out from the hit's time. The values are absolute, so writing them every frame over a
	// snapshot that was not read again does not add up.
	const float Age = State.TimeSeconds - ForcedAt;
	if (Age >= FlashSeconds)
	{
		ForcedAt = -1.0f;
		return;
	}
	const float Value = 1.0f - FMath::Max(Age, 0.0f) / FlashSeconds;
	State.RemanenceBlendFactor = FMath::Max(State.RemanenceBlendFactor, Value);
	int32 Free = INDEX_NONE;
	for (int32 Slot = 0; Slot < ScctHudSfxSlots; ++Slot)
	{
		if (State.SfxType[Slot] == EScctHudSfx::Flash || (Free == INDEX_NONE && State.SfxType[Slot] == EScctHudSfx::None))
		{
			Free = Slot;
			if (State.SfxType[Slot] == EScctHudSfx::Flash)
			{
				break;
			}
		}
	}
	if (Free != INDEX_NONE)
	{
		State.SfxType[Free] = EScctHudSfx::Flash;
		State.SfxValue[Free] = Value;
	}
	if (State.Side == 1)
	{
		State.FlashTime = ForcedAt;
	}
}
