#include "Hud/ScctPoisonOverlay.h"

#include "Engine/PostProcessVolume.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudPainter.h"
#include "Hud/ScctHudTextures.h"
#include "Hud/ScctVisorEffects.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OpenSCCT.h"
#include "Runtime/ScctHudApi.h"

namespace ScctPoisonLocal
{
	/** The page the layers are placed on, and the area's frame rate. */
	constexpr float PageWidth = 640.0f;
	constexpr float PageHeight = 480.0f;
	constexpr float FramesPerSecond = 10.0f;

	/** The area starts at frame 1 (frame 0 holds a stop) and frame 10 sends it back to frame 1. */
	constexpr float LoopStart = 1.0f;
	constexpr float LoopEnd = 10.0f;

	/** A rectangle of the area: left, top, right, bottom in page units from the health area's place. */
	struct FPoisonRect
	{
		float L, T, R, B;
	};

	/** effet01: its two keys (frames 0 and 10, a straight line between) and its colour (150, 185, 138, 128). */
	constexpr FPoisonRect WashFrom = { -568.0f, -249.0f, 72.0f, 231.0f };
	constexpr FPoisonRect WashTo = { -665.0f, -320.0f, 135.0f, 280.0f };
	constexpr float WashColour[4] = { 150.0f, 185.0f, 138.0f, 128.0f };

	/** effet02_dodge: its one key and its colour (50, 62, 46; a Dodge layer's alpha is not used). */
	constexpr FPoisonRect DodgeAt = { -571.0f, -249.0f, 69.0f, 231.0f };
	constexpr float DodgeColour[3] = { 50.0f, 62.0f, 46.0f };

	/** The health area on a side's page: where it stands and the colour everything in it is multiplied by. */
	struct FHealthArea
	{
		float X, Y;
		float Colour[3];
	};
	constexpr FHealthArea SpyHealth = { 566.0f, 230.0f, { 150.0f, 185.0f, 138.0f } };
	constexpr FHealthArea MercHealth = { 40.0f, 230.0f, { 255.0f, 182.0f, 68.0f } };

	const FHealthArea& HealthOf(int32 Side)
	{
		return Side == 1 ? MercHealth : SpyHealth;
	}

	/** The area's frame so long after it started: 1 to 10, then from 1 again. */
	float FrameAt(float Seconds)
	{
		return LoopStart + FMath::Fmod(FMath::Max(Seconds, 0.0f) * FramesPerSecond, LoopEnd - LoopStart);
	}

	/**
	 * A rectangle of the area as fractions of the view: moved to the health area's place, its right and bottom one
	 * unit further (the page's images are drawn to the far side of their last unit, MagmaRenderer.DrawImage), and
	 * stretched over the view (x / 640, y / 480).
	 */
	FBox2f OnView(const FPoisonRect& Rect, const FHealthArea& Health)
	{
		return FBox2f(
			FVector2f((Rect.L + Health.X) / PageWidth, (Rect.T + Health.Y) / PageHeight),
			FVector2f((Rect.R + 1.0f + Health.X) / PageWidth, (Rect.B + 1.0f + Health.Y) / PageHeight));
	}

	FBox2f WashOnView(float Frame, const FHealthArea& Health)
	{
		const float Share = FMath::Clamp(Frame / LoopEnd, 0.0f, 1.0f);
		const FPoisonRect Rect = {
			FMath::Lerp(WashFrom.L, WashTo.L, Share), FMath::Lerp(WashFrom.T, WashTo.T, Share),
			FMath::Lerp(WashFrom.R, WashTo.R, Share), FMath::Lerp(WashFrom.B, WashTo.B, Share) };
		return OnView(Rect, Health);
	}

	/** A layer's channel times the health area's, 0 to 1 as the screen shows it. */
	float Tinted(float Channel, float AreaChannel)
	{
		return (Channel / 255.0f) * (AreaChannel / 255.0f);
	}

	/** A colour of the screen (0 to 1) as the linear value Slate multiplies an sRGB texture by. */
	float ToLinear(float Shown)
	{
		return Shown <= 0.04045f ? Shown / 12.92f : FMath::Pow((Shown + 0.055f) / 1.055f, 2.4f);
	}

	UTexture* PatternTexture()
	{
		const UScctHudTextures::FEntry* Entry = UScctHudTextures::Find(TEXT("Hud"), TEXT("stickshock"));
		return Entry != nullptr ? Cast<UTexture>(Entry->Brush.GetResourceObject()) : nullptr;
	}
}

void ScctPoisonOverlay::Update(FScctVisorState& Visor, const FScctHudState& State)
{
	// As the health gauge shows: a live pawn whose health element is not switched off, and a HUD that is drawn at all
	// (an EMP keeps the merc's own blocks down for its 12 s).
	const bool bGauge = State.Valid != 0
		&& (State.Flags & EScctHudFlags::HasPawn) != 0
		&& (State.Flags & (EScctHudFlags::LifeOff | EScctHudFlags::HideHud | EScctHudFlags::Spectating | EScctHudFlags::GameEnded)) == 0
		&& !(State.Side == 1 && Visor.bLatch && !Visor.bRebooting);
	const bool bShown = bGauge && (State.Flags & EScctHudFlags::Poisoned) != 0;
	if (bShown == Visor.bPoisonShown)
	{
		return;
	}
	Visor.bPoisonShown = bShown;
	if (bShown)
	{
		// The area is shown and sought to frame 1: the loop's clock runs from here.
		Visor.PoisonShownAt = State.TimeSeconds;
		const FBox2f Wash = ScctPoisonLocal::WashOnView(ScctPoisonLocal::LoopStart, ScctPoisonLocal::HealthOf(State.Side));
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: poison overlay up at t=%.2f (%s page; effet01 starts over %.3f,%.3f to %.3f,%.3f of the view)"), State.TimeSeconds,
			State.Side == 1 ? TEXT("merc") : TEXT("spy"), Wash.Min.X, Wash.Min.Y, Wash.Max.X, Wash.Max.Y);
		if (UScctHudTextures::Find(TEXT("Hud"), TEXT("stickshock")) == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Visor: poison overlay: the content has no Hud.stickshock, so neither layer is drawn"));
		}
	}
	else
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: poison overlay down at t=%.2f, %.2f s after it came up (%s)"), State.TimeSeconds, State.TimeSeconds - Visor.PoisonShownAt,
			(State.Flags & EScctHudFlags::Poisoned) != 0 ? TEXT("the health gauge is hidden") : TEXT("cured"));
	}
}

void ScctPoisonOverlay::Paint(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	using namespace ScctPoisonLocal;
	if (Model.Visor == nullptr || !Model.bHasState || !Model.Visor->bPoisonShown)
	{
		return;
	}
	const UScctHudTextures::FEntry* Texture = UScctHudTextures::Find(TEXT("Hud"), TEXT("stickshock"));
	if (Texture == nullptr)
	{
		return;
	}
	const FScctHudState& State = Model.State;
	const FHealthArea& Health = HealthOf(State.Side);
	const FBox2f Wash = WashOnView(FrameAt(State.TimeSeconds - Model.Visor->PoisonShownAt), Health);
	const FVector2f Size = Painter.Size();
	const FLinearColor Tint(
		ToLinear(Tinted(WashColour[0], Health.Colour[0])),
		ToLinear(Tinted(WashColour[1], Health.Colour[1])),
		ToLinear(Tinted(WashColour[2], Health.Colour[2])),
		WashColour[3] / 255.0f);
	Painter.Image(FBox2f(Wash.Min * Size, Wash.Max * Size), Texture->Brush, FBox2f(FVector2f::ZeroVector, FVector2f::UnitVector), Tint);
}

bool FScctPoisonDodge::Ensure(UWorld* World)
{
	if (Volume.IsValid() && Material.IsValid())
	{
		return true;
	}
	if (bTried || World == nullptr)
	{
		return false;
	}
	bTried = true;

	static const TCHAR* MaterialPath = TEXT("/Game/OpenSCCT/UI/Vision/M_ScctPoisonDodge.M_ScctPoisonDodge");
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
	if (Base == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Visor: poison overlay: %s is missing; run Tools/Materials/Build-PoisonMaterial.ps1. The poison will not brighten the view."), MaterialPath);
		return false;
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	APostProcessVolume* Made = World->SpawnActor<APostProcessVolume>(Parameters);
	if (!IsValid(Made))
	{
		return false;
	}
	Made->bUnbound = true;
	Made->BlendWeight = 1.0f;
	// Over the goggles' volume (1000) and the double vision's (1001); the material's own blendable priority puts it
	// between their materials.
	Made->Priority = 1002.0f;
	// The volume's blendables hold the material, so it lives as long as the volume does.
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, Made);
	Instance->SetScalarParameterValue(TEXT("Amount"), 0.0f);
	Made->Settings.WeightedBlendables.Array.Emplace(0.0f, Instance);
	Volume = Made;
	Material = Instance;
	return true;
}

void FScctPoisonDodge::Present(UWorld* World, const FScctVisorState& Visor, const FScctHudState* State)
{
	using namespace ScctPoisonLocal;
	UTexture* Pattern = State != nullptr && Visor.bPoisonShown ? PatternTexture() : nullptr;
	if (Pattern == nullptr || !Ensure(World))
	{
		if (bOn)
		{
			bOn = false;
			UE_LOG(LogOpenSCCT, Log, TEXT("Visor: poison brightening off"));
		}
		if (Volume.IsValid() && Material.IsValid())
		{
			Material->SetScalarParameterValue(TEXT("Amount"), 0.0f);
			Volume->Settings.WeightedBlendables.Array[0].Weight = 0.0f;
		}
		return;
	}

	if (!bOn || Side != State->Side)
	{
		bOn = true;
		Side = State->Side;
		const FHealthArea& Health = HealthOf(Side);
		const FBox2f Rect = OnView(DodgeAt, Health);
		const FLinearColor Tint(Tinted(DodgeColour[0], Health.Colour[0]), Tinted(DodgeColour[1], Health.Colour[1]), Tinted(DodgeColour[2], Health.Colour[2]), 1.0f);
		Material->SetTextureParameterValue(TEXT("Pattern"), Pattern);
		Material->SetVectorParameterValue(TEXT("Tint"), Tint);
		Material->SetVectorParameterValue(TEXT("Rect"), FLinearColor(Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y));
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: poison brightening on (effet02_dodge over %.3f,%.3f to %.3f,%.3f of the view, colour %.3f %.3f %.3f)"),
			Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y, Tint.R, Tint.G, Tint.B);
	}
	Material->SetScalarParameterValue(TEXT("Amount"), 1.0f);
	Volume->Settings.WeightedBlendables.Array[0].Weight = 1.0f;
}
