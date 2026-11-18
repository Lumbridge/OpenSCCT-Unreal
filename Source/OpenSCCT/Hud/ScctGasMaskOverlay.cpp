#include "Hud/ScctGasMaskOverlay.h"

#include "Hud/ScctHudPainter.h"
#include "Hud/ScctHudTextures.h"
#include "Hud/ScctVisorEffects.h"
#include "OpenSCCT.h"
#include "Runtime/ScctHudApi.h"

namespace ScctGasMaskLocal
{
	/** The page the layers are placed on, and its frame rate. */
	constexpr float PageWidth = 640.0f;
	constexpr float PageHeight = 480.0f;
	constexpr float FramesPerSecond = 10.0f;

	/** An edge this close to the page's side counts as on it (MagmaRenderer.PageEdgeSlack). */
	constexpr float PageEdgeSlack = 2.0f;

	/** One key of a layer: the frame, the rectangle in page units and the alpha (0 to 255). */
	struct FMaskKey
	{
		float Frame;
		float L, T, R, B;
		float Alpha;
	};

	/** One image of the area: a whole texture of the Hud package, mirrored as its tiling says, moving between its keys. */
	struct FMaskLayer
	{
		const TCHAR* Texture;
		bool bFlipX;
		bool bFlipY;
		int32 NumKeys;
		FMaskKey Keys[3];
	};

	/** A_Frame_maskgaz, in its drawing order. Its clock runs to frame 23 and goes back to 5. */
	const FMaskLayer FrameLayers[] = {
		// The lower rims.
		{ TEXT("sniper_secondborder"), true, false, 1, { { 0, 267, 348, 714, 765, 128 } } },
		{ TEXT("sniper_secondborder"), true, false, 1, { { 0, -180, 349, 267, 766, 128 } } },
		// A faint ring in each eyepiece.
		{ TEXT("zoom_rond01"), false, false, 1, { { 0, 79, 93, 318, 341, 17 } } },
		{ TEXT("zoom_rond01"), false, false, 1, { { 0, 326, 93, 565, 341, 17 } } },
		// The grain of the glass.
		{ TEXT("maskgaz02"), false, false, 1, { { 0, 0, 0, 640, 480, 64 } } },
		// The eyepiece rims: four pieces round each.
		{ TEXT("sniper_secondborder"), false, true, 1, { { 0, 322, 220, 468, 349, 128 } } },
		{ TEXT("sniper_secondborder"), false, false, 1, { { 0, 322, 70, 467, 220, 128 } } },
		{ TEXT("sniper_secondborder"), true, true, 1, { { 0, 468, 220, 595, 349, 128 } } },
		{ TEXT("sniper_secondborder"), true, false, 1, { { 0, 467, 70, 595, 220, 128 } } },
		{ TEXT("sniper_secondborder"), true, false, 1, { { 0, 176, 70, 322, 220, 128 } } },
		{ TEXT("sniper_secondborder"), false, false, 1, { { 0, 48, 70, 176, 220, 128 } } },
		{ TEXT("sniper_secondborder"), true, true, 1, { { 0, 175, 220, 322, 349, 128 } } },
		{ TEXT("sniper_secondborder"), false, true, 1, { { 0, 48, 220, 175, 349, 128 } } },
		// The haze that comes and goes over the whole glass.
		{ TEXT("stickshock"), true, false, 3, { { 4, 0, 0, 689, 517, 0 }, { 7, 0, 0, 689, 517, 33 }, { 23, 0, 0, 689, 517, 0 } } },
		// The black surround, one texture in four quarters.
		{ TEXT("maskgaz"), false, true, 1, { { 0, 321, -7, 666, 160, 255 } } },
		{ TEXT("maskgaz"), true, true, 1, { { 0, -24, -7, 321, 160, 255 } } },
		{ TEXT("maskgaz"), false, false, 1, { { 0, 321, 160, 666, 590, 255 } } },
		{ TEXT("maskgaz"), true, false, 1, { { 0, -24, 160, 321, 590, 255 } } },
	};
	constexpr float FrameLoopEnd = 23.0f;
	constexpr float FrameLoopStart = 5.0f;

	/** A_RespirationMask. Its clock runs to frame 23 and goes back to 4. */
	const FMaskLayer BreathLayers[] = {
		{ TEXT("maskgaz02"), false, false, 3, { { 4, 15, -78, 354, 425, 0 }, { 7, 15, -78, 354, 491, 64 }, { 23, 15, -46, 354, 637, 32 } } },
		{ TEXT("maskgaz02"), false, false, 3, { { 4, 358, -96, 697, 407, 0 }, { 7, 360, -96, 699, 473, 64 }, { 23, 358, -64, 697, 619, 32 } } },
		{ TEXT("stickshock"), false, false, 3, { { 4, 34, 13, 608, 486, 0 }, { 7, 110, 8, 548, 506, 33 }, { 23, 27, 6, 610, 511, 0 } } },
		{ TEXT("stickshock"), false, false, 3, { { 4, 608, 41, 965, 464, 0 }, { 7, 548, 41, 962, 464, 33 }, { 23, 610, 41, 965, 464, 0 } } },
		{ TEXT("stickshock"), false, false, 3, { { 4, -307, 45, 34, 468, 0 }, { 7, -307, 45, 110, 468, 33 }, { 23, -307, 45, 28, 468, 0 } } },
		{ TEXT("stickshock02"), false, false, 3, { { 1, 101, 119, 501, 519, 0 }, { 4, 105, 161, 505, 561, 64 }, { 21, 126, 140, 526, 540, 11 } } },
	};
	constexpr float BreathLoopEnd = 23.0f;
	constexpr float BreathLoopStart = 4.0f;

	/** The frame fades in from frame 1 to frame 5 of the mask's clock (the Frame instance's keys on A_MaskGas). */
	constexpr float FadeFrom = 1.0f;
	constexpr float FadeTo = 5.0f;

	/** The frame of a clock that goes back to LoopStart on reaching LoopEnd. */
	float LoopedFrame(float Seconds, float LoopStart, float LoopEnd)
	{
		const float Frame = FMath::Max(Seconds, 0.0f) * FramesPerSecond;
		return Frame < LoopEnd ? Frame : LoopStart + FMath::Fmod(Frame - LoopStart, LoopEnd - LoopStart);
	}

	void PaintLayer(FScctHudPainter& Painter, const FMaskLayer& Layer, float Frame)
	{
		// The key the frame is in: held before the first and after the last, a straight line between two.
		FMaskKey Key = Layer.Keys[0];
		for (int32 Index = 0; Index < Layer.NumKeys; ++Index)
		{
			const FMaskKey& From = Layer.Keys[Index];
			if (Frame < From.Frame)
			{
				break;
			}
			Key = From;
			if (Index + 1 < Layer.NumKeys && Frame < Layer.Keys[Index + 1].Frame)
			{
				const FMaskKey& To = Layer.Keys[Index + 1];
				const float Share = (Frame - From.Frame) / (To.Frame - From.Frame);
				Key.L = FMath::Lerp(From.L, To.L, Share);
				Key.T = FMath::Lerp(From.T, To.T, Share);
				Key.R = FMath::Lerp(From.R, To.R, Share);
				Key.B = FMath::Lerp(From.B, To.B, Share);
				Key.Alpha = FMath::Lerp(From.Alpha, To.Alpha, Share);
				break;
			}
		}
		if (Key.Alpha < 0.5f)
		{
			return;
		}
		const UScctHudTextures::FEntry* Texture = UScctHudTextures::Find(TEXT("Hud"), Layer.Texture);
		if (Texture == nullptr)
		{
			return;
		}

		// The page is as high as the screen and centred on it; an edge on the page's side goes to the screen's side.
		const FVector2f Size = Painter.Size();
		const float S = Size.Y / PageHeight;
		const auto ScreenX = [&Size, S](float X)
		{
			const float At = Size.X * 0.5f + (X - PageWidth * 0.5f) * S;
			return X <= PageEdgeSlack ? FMath::Min(At, 0.0f) : X >= PageWidth - PageEdgeSlack ? FMath::Max(At, Size.X) : At;
		};
		// The edges as the page gives them (the original's image draw scales them directly): pieces that share an edge,
		// as the surround's quarters and the rims' pieces do, then meet without lying over each other, which showed as a
		// line down the middle of the view.
		FBox2f Box(FVector2f(ScreenX(Key.L), Key.T * S), FVector2f(ScreenX(Key.R), Key.B * S));
		if (Layer.bFlipX)
		{
			Swap(Box.Min.X, Box.Max.X);
		}
		if (Layer.bFlipY)
		{
			Swap(Box.Min.Y, Box.Max.Y);
		}
		// The surround is black art shaped by its alpha; its stored colour is a blocky near-black (HudAssets.BlackenNearBlack).
		const bool bSurround = FCString::Stricmp(Layer.Texture, TEXT("maskgaz")) == 0;
		const float Grey = bSurround ? 0.0f : 1.0f;
		// The quarters meet at the texture's clear edge, which would take in the opaque far edge when sampled right on
		// it (a dark line down and across the middle of the view): the page nudges them 0.01 in, and so does this.
		const FBox2f Region = bSurround ? FBox2f(FVector2f(0.01f), FVector2f(0.995f)) : FBox2f(FVector2f::ZeroVector, FVector2f::UnitVector);
		Painter.Image(Box, Texture->Brush, Region, FLinearColor(Grey, Grey, Grey, Key.Alpha / 255.0f));
	}
}

void ScctGasMaskOverlay::Update(FScctVisorState& Visor, const FScctHudState& State)
{
	const float Now = State.TimeSeconds;
	const bool bShown = State.Side == 1 && State.GasMaskAirSeconds >= 0;
	const bool bWorn = bShown && (State.EffectFlags & EScctHudEffectFlags::GasMaskWorn) != 0;
	if (bShown != Visor.bMaskShown)
	{
		Visor.bMaskShown = bShown;
		Visor.MaskShownAt = Now;
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: gas mask frame %s at t=%.2f (air %d s)"), bShown ? TEXT("up") : TEXT("down"), Now, State.GasMaskAirSeconds);
		if (bShown && UScctHudTextures::Find(TEXT("Hud"), TEXT("maskgaz")) == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Visor: gas mask: the content has no Hud.maskgaz, so the mask's surround is not drawn"));
		}
	}
	if (bWorn != Visor.bMaskWorn)
	{
		Visor.bMaskWorn = bWorn;
		Visor.MaskWornAt = Now;
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: gas mask %s at t=%.2f (air %d s)"), bWorn ? TEXT("on, breathing") : TEXT("off"), Now, State.GasMaskAirSeconds);
	}
}

void ScctGasMaskOverlay::Paint(FScctHudPainter& Painter, const FScctVisorState& Visor, const FScctHudState& State)
{
	using namespace ScctGasMaskLocal;
	if (!Visor.bMaskShown)
	{
		return;
	}
	const float Now = State.TimeSeconds;
	const float Shown = Now - Visor.MaskShownAt;
	const float Fade = FMath::Clamp((Shown * FramesPerSecond - FadeFrom) / (FadeTo - FadeFrom), 0.0f, 1.0f);
	const float Opacity = Painter.GetOpacity();

	// The breathing lies under the frame, as RespirationMask is drawn before Frame.
	if (Visor.bMaskWorn)
	{
		const float Frame = LoopedFrame(Now - Visor.MaskWornAt, BreathLoopStart, BreathLoopEnd);
		for (const FMaskLayer& Layer : BreathLayers)
		{
			PaintLayer(Painter, Layer, Frame);
		}
	}

	if (Fade > 0.0f)
	{
		Painter.SetOpacity(Opacity * Fade);
		const float Frame = LoopedFrame(Shown, FrameLoopStart, FrameLoopEnd);
		for (const FMaskLayer& Layer : FrameLayers)
		{
			PaintLayer(Painter, Layer, Frame);
		}
		Painter.SetOpacity(Opacity);
	}
}
