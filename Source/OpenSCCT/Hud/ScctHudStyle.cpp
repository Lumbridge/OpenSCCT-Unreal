#include "Hud/ScctHudStyle.h"

#include "Styling/CoreStyle.h"

namespace ScctHudStyle
{
	FSlateFontInfo Font(EWeight Weight, float Size, int32 LetterSpacing)
	{
		static const FName Names[] = { FName(TEXT("Light")), FName(TEXT("Regular")), FName(TEXT("Bold")) };
		FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(Names[static_cast<int32>(Weight)], Size);
		Info.LetterSpacing = LetterSpacing;
		return Info;
	}

	FSlateFontInfo Label(float Size)
	{
		return Font(EWeight::Regular, Size, 160);
	}

	FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float Alpha)
	{
		FLinearColor Colour = FLinearColor::FromSRGBColor(FColor(R, G, B));
		Colour.A = Alpha;
		return Colour;
	}

	FLinearColor Accent(int32 Side)
	{
		switch (Side)
		{
		case 0: return Srgb(92, 214, 255);
		case 1: return Srgb(255, 168, 64);
		default: return Srgb(200, 208, 216);
		}
	}

	FLinearColor Text() { return Srgb(236, 242, 246); }
	FLinearColor TextDim() { return Srgb(236, 242, 246, 0.62f); }
	FLinearColor TextFaint() { return Srgb(236, 242, 246, 0.36f); }
	FLinearColor Danger() { return Srgb(255, 84, 72); }
	FLinearColor Good() { return Srgb(120, 232, 160); }
}
