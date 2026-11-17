#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// The look of the HUD in one place: type, colours and the few measures every element shares. The direction is a
// quiet tactical display: thin type, small spaced capitals for labels, one accent colour per side (spies cool,
// mercenaries warm) and glass panels that blur what is behind them.
//
// All measures are in Slate units: the viewport's DPI scaling makes one unit one pixel at 1080p and two at 4K.
namespace ScctHudStyle
{
	enum class EWeight : uint8
	{
		Light,
		Regular,
		Bold,
	};

	/** The engine's Roboto. LetterSpacing is in thousandths of an em. */
	FSlateFontInfo Font(EWeight Weight, float Size, int32 LetterSpacing = 0);

	/** A small label in spaced capitals (the caller upper-cases the text). */
	FSlateFontInfo Label(float Size = 8.5f);

	/** A colour given as it looks on screen (sRGB bytes), which is how a palette is chosen. */
	FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float Alpha = 1.0f);

	/** The accent of a side or team: 0 spies (cyan), 1 mercenaries (amber); anything else is neutral. */
	FLinearColor Accent(int32 Side);

	FLinearColor Text();
	FLinearColor TextDim();
	FLinearColor TextFaint();
	FLinearColor Danger();
	FLinearColor Good();

	/** The same colour with another alpha. */
	inline FLinearColor Fade(const FLinearColor& Colour, float Alpha)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Colour.A * Alpha);
	}

	/** Distance of the HUD's blocks from the edge of the safe area. */
	constexpr float Margin = 40.0f;

	/** Corner radius of the glass panels. */
	constexpr float Corner = 6.0f;

	/** Moves Value towards Target so that the gap shrinks by the same share every Rate-th of a second. */
	inline float Ease(float Value, float Target, float DeltaSeconds, float Rate)
	{
		return Target + (Value - Target) * FMath::Exp(-DeltaSeconds * Rate);
	}

	/** 0..1 that starts fast and settles (for things that slide in). */
	inline float EaseOut(float Alpha)
	{
		const float Inverse = 1.0f - FMath::Clamp(Alpha, 0.0f, 1.0f);
		return 1.0f - Inverse * Inverse * Inverse;
	}
}
