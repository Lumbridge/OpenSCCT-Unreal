#include "Hud/ScctCrosshairShape.h"

#include "Hud/ScctHudPainter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"
#include "Settings/ScctSettingsForced.h"

#define LOCTEXT_NAMESPACE "ScctCrosshair"

namespace
{
	// The registry's keys (Mercenary page, group Crosshair).
	const FName CrosshairCustomKey(TEXT("Hud.Crosshair.Custom"));
	const FName CrosshairStyleKey(TEXT("Hud.Crosshair.Style"));
	const FName CrosshairSizeKey(TEXT("Hud.Crosshair.Size"));
	const FName CrosshairGapKey(TEXT("Hud.Crosshair.Gap"));
	const FName CrosshairThicknessKey(TEXT("Hud.Crosshair.Thickness"));
	const FName CrosshairOutlineKey(TEXT("Hud.Crosshair.Outline"));
	const FName CrosshairDotKey(TEXT("Hud.Crosshair.Dot"));
	const FName CrosshairTStyleKey(TEXT("Hud.Crosshair.TStyle"));
	const FName CrosshairRedKey(TEXT("Hud.Crosshair.ColourR"));
	const FName CrosshairGreenKey(TEXT("Hud.Crosshair.ColourG"));
	const FName CrosshairBlueKey(TEXT("Hud.Crosshair.ColourB"));
	const FName CrosshairAlphaKey(TEXT("Hud.Crosshair.Alpha"));
	const FName CrosshairOutlineRedKey(TEXT("Hud.Crosshair.OutlineR"));
	const FName CrosshairOutlineGreenKey(TEXT("Hud.Crosshair.OutlineG"));
	const FName CrosshairOutlineBlueKey(TEXT("Hud.Crosshair.OutlineB"));
	const FName CrosshairOutlineAlphaKey(TEXT("Hud.Crosshair.OutlineAlpha"));
	const FName CrosshairFollowRecoilKey(TEXT("Hud.Crosshair.FollowRecoil"));
	const FName CrosshairHitFlashKey(TEXT("Hud.Crosshair.HitFlash"));
	const FName CrosshairSpreadLimitKey(TEXT("Hud.Crosshair.SpreadLimit"));
	const FName CrosshairSplitDistanceKey(TEXT("Hud.Crosshair.SplitDistance"));
	const FName CrosshairInnerSplitAlphaKey(TEXT("Hud.Crosshair.InnerSplitAlpha"));
	const FName CrosshairOuterSplitAlphaKey(TEXT("Hud.Crosshair.OuterSplitAlpha"));
	const FName CrosshairSplitRatioKey(TEXT("Hud.Crosshair.SplitSizeRatio"));
	const FName CrosshairScreenHeightKey(TEXT("Hud.Crosshair.ScreenHeight"));
	const FName CrosshairCodeKey(TEXT("Hud.Crosshair.Code"));
	const FName CrosshairClassicKey(TEXT("Hud.ClassicCrosshair"));

	const TCHAR* const CrosshairStyleNames[] = {
		TEXT("Dynamic cross"), TEXT("Dynamic circle"), TEXT("Dynamic cross (split)"), TEXT("Static circle"), TEXT("Static cross"),
		TEXT("Static cross (shot feedback)"), TEXT("Dot only"), TEXT("Dynamic quad"), TEXT("Static square"), TEXT("Static quad"),
	};
	const TCHAR* const CrosshairOutlineNames[] = { TEXT("None"), TEXT("Full"), TEXT("Half") };

	/** The hit colour of the original reticle. */
	const FLinearColor CrosshairHitColour(1.0f, 0.0f, 0.0f, 1.0f);

	/** True while the share code and the values are being brought in line (their change events are ours). */
	bool bCrosshairSyncing = false;

	int32 CrosshairPx(float Value) { return FMath::RoundToInt32(Value); }

	FLinearColor CrosshairRgba(int32 R, int32 G, int32 B, int32 A)
	{
		return FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
	}

	float CrosshairFraction(float Value, float Min, float Max)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Min;
	}

	int32 CrosshairSteps(float Value) { return FMath::RoundToInt32(Value * 100.0f); }

	// ---- CS2 share codes (cs2-v1), as the Godot client reads and writes them ----------------------------------------
	// "CS" then 44 base-57 characters, least significant first, holding 32 bytes (big-endian); byte 0 is the sum of the others.
	const TCHAR* const CrosshairCodeDictionary = TEXT("ABCDEFGHJKLMNOPQRSTUVWXYZabcdefhijkmnopqrstuvwxyz23456789");
	constexpr int32 CrosshairCodeBase = 57;
	constexpr int32 CrosshairCodeChars = 44;
	constexpr int32 CrosshairCodeBytes = 32;

	void RegisterCrosshairBool(FName Key, const FText& Label, const FText& Tip, bool bDefault, int32 Order)
	{
		FScctSettingDesc Desc;
		Desc.Key = Key;
		Desc.Page = TEXT("Mercenary");
		Desc.Group = LOCTEXT("Group", "Crosshair");
		Desc.Label = Label;
		Desc.Tooltip = Tip;
		Desc.Type = EScctSettingType::Bool;
		Desc.Default = bDefault ? TEXT("true") : TEXT("false");
		Desc.Order = Order;
		FScctSettings::Register(Desc);
	}

	void RegisterCrosshairNumber(FName Key, const FText& Label, const FText& Tip, bool bInt, float Default, float Min, float Max, float Step, int32 Order)
	{
		FScctSettingDesc Desc;
		Desc.Key = Key;
		Desc.Page = TEXT("Mercenary");
		Desc.Group = LOCTEXT("Group", "Crosshair");
		Desc.Label = Label;
		Desc.Tooltip = Tip;
		Desc.Type = bInt ? EScctSettingType::Int : EScctSettingType::Float;
		Desc.Default = bInt ? FString::FromInt(FMath::RoundToInt32(Default)) : FString::SanitizeFloat(Default);
		Desc.Min = Min;
		Desc.Max = Max;
		Desc.Step = Step;
		Desc.Order = Order;
		FScctSettings::Register(Desc);
	}

	void RegisterCrosshairChoice(FName Key, const FText& Label, const FText& Tip, const TCHAR* const* Names, int32 Count, int32 Default, int32 Order)
	{
		FScctSettingDesc Desc;
		Desc.Key = Key;
		Desc.Page = TEXT("Mercenary");
		Desc.Group = LOCTEXT("Group", "Crosshair");
		Desc.Label = Label;
		Desc.Tooltip = Tip;
		Desc.Type = EScctSettingType::Choice;
		Desc.Default = FString::FromInt(Default);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Desc.Choices.Add(FText::FromString(Names[Index]));
		}
		Desc.Order = Order;
		FScctSettings::Register(Desc);
	}

	/** Writes every value but the code itself into the registry (as the player's settings). */
	void WriteCrosshairSettings(const ScctCrosshair::FSettings& S)
	{
		FScctSettings::SetBool(CrosshairCustomKey, S.bCustom);
		FScctSettings::SetInt(CrosshairStyleKey, S.Style);
		FScctSettings::SetInt(CrosshairSizeKey, S.Length);
		FScctSettings::SetInt(CrosshairGapKey, S.Gap);
		FScctSettings::SetInt(CrosshairThicknessKey, S.Thickness);
		FScctSettings::SetInt(CrosshairOutlineKey, S.OutlineMode);
		FScctSettings::SetBool(CrosshairDotKey, S.bCenterDot);
		FScctSettings::SetBool(CrosshairTStyleKey, S.bTStyle);
		FScctSettings::SetInt(CrosshairRedKey, S.Red);
		FScctSettings::SetInt(CrosshairGreenKey, S.Green);
		FScctSettings::SetInt(CrosshairBlueKey, S.Blue);
		FScctSettings::SetInt(CrosshairAlphaKey, S.Alpha);
		FScctSettings::SetInt(CrosshairOutlineRedKey, S.OutlineRed);
		FScctSettings::SetInt(CrosshairOutlineGreenKey, S.OutlineGreen);
		FScctSettings::SetInt(CrosshairOutlineBlueKey, S.OutlineBlue);
		FScctSettings::SetInt(CrosshairOutlineAlphaKey, S.OutlineAlpha);
		FScctSettings::SetBool(CrosshairFollowRecoilKey, S.bFollowRecoil);
		FScctSettings::SetInt(CrosshairSpreadLimitKey, S.DynamicSpreadLimit);
		FScctSettings::SetInt(CrosshairSplitDistanceKey, S.SplitDistance);
		FScctSettings::SetFloat(CrosshairInnerSplitAlphaKey, S.InnerSplitAlpha);
		FScctSettings::SetFloat(CrosshairOuterSplitAlphaKey, S.OuterSplitAlpha);
		FScctSettings::SetFloat(CrosshairSplitRatioKey, S.SplitSizeRatio);
		FScctSettings::SetInt(CrosshairScreenHeightKey, S.ScreenHeight);
	}

	/** The keys whose values the share code holds. */
	bool IsCrosshairCodeValue(FName Key)
	{
		return Key == CrosshairStyleKey || Key == CrosshairSizeKey || Key == CrosshairGapKey || Key == CrosshairThicknessKey
			|| Key == CrosshairOutlineKey || Key == CrosshairDotKey || Key == CrosshairTStyleKey || Key == CrosshairRedKey
			|| Key == CrosshairGreenKey || Key == CrosshairBlueKey || Key == CrosshairAlphaKey || Key == CrosshairOutlineRedKey
			|| Key == CrosshairOutlineGreenKey || Key == CrosshairOutlineBlueKey || Key == CrosshairOutlineAlphaKey
			|| Key == CrosshairFollowRecoilKey || Key == CrosshairSpreadLimitKey || Key == CrosshairSplitDistanceKey
			|| Key == CrosshairInnerSplitAlphaKey || Key == CrosshairOuterSplitAlphaKey || Key == CrosshairSplitRatioKey
			|| Key == CrosshairScreenHeightKey;
	}

	/** A code typed on the settings page sets the values; a value changed there rewrites the code. */
	void HandleCrosshairSettingChanged(FName Key)
	{
		if (bCrosshairSyncing)
		{
			return;
		}
		if (Key == CrosshairCodeKey)
		{
			const FString Code = FScctSettings::GetString(CrosshairCodeKey).TrimStartAndEnd();
			ScctCrosshair::FSettings Read = ScctCrosshair::Read();
			if (ScctCrosshair::FromShareCode(Code, Read))
			{
				TGuardValue<bool> Guard(bCrosshairSyncing, true);
				WriteCrosshairSettings(Read);
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctCrosshair: share code %s read: %s"), *Code, *ScctCrosshair::Describe(Read));
			}
			else if (Code.Len() >= 2 + CrosshairCodeChars)
			{
				// Shorter text is a code still being typed.
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctCrosshair: '%s' is not a CS2 crosshair share code (CS and 44 characters)"), *Code);
			}
			return;
		}
		if (IsCrosshairCodeValue(Key))
		{
			TGuardValue<bool> Guard(bCrosshairSyncing, true);
			FScctSettings::SetString(CrosshairCodeKey, ScctCrosshair::ToShareCode(ScctCrosshair::Read()));
		}
	}

	/** Forces one option for the run (not saved). */
	void ForceCrosshairOption(FName Key, const FString& Value)
	{
		FScctSettingsForced::Force(Key, Value);
	}

	void ForceCrosshairSettings(const ScctCrosshair::FSettings& S)
	{
		ForceCrosshairOption(CrosshairCustomKey, S.bCustom ? TEXT("true") : TEXT("false"));
		ForceCrosshairOption(CrosshairStyleKey, FString::FromInt(S.Style));
		ForceCrosshairOption(CrosshairSizeKey, FString::FromInt(S.Length));
		ForceCrosshairOption(CrosshairGapKey, FString::FromInt(S.Gap));
		ForceCrosshairOption(CrosshairThicknessKey, FString::FromInt(S.Thickness));
		ForceCrosshairOption(CrosshairOutlineKey, FString::FromInt(S.OutlineMode));
		ForceCrosshairOption(CrosshairDotKey, S.bCenterDot ? TEXT("true") : TEXT("false"));
		ForceCrosshairOption(CrosshairTStyleKey, S.bTStyle ? TEXT("true") : TEXT("false"));
		ForceCrosshairOption(CrosshairRedKey, FString::FromInt(S.Red));
		ForceCrosshairOption(CrosshairGreenKey, FString::FromInt(S.Green));
		ForceCrosshairOption(CrosshairBlueKey, FString::FromInt(S.Blue));
		ForceCrosshairOption(CrosshairAlphaKey, FString::FromInt(S.Alpha));
		ForceCrosshairOption(CrosshairOutlineRedKey, FString::FromInt(S.OutlineRed));
		ForceCrosshairOption(CrosshairOutlineGreenKey, FString::FromInt(S.OutlineGreen));
		ForceCrosshairOption(CrosshairOutlineBlueKey, FString::FromInt(S.OutlineBlue));
		ForceCrosshairOption(CrosshairOutlineAlphaKey, FString::FromInt(S.OutlineAlpha));
		ForceCrosshairOption(CrosshairFollowRecoilKey, S.bFollowRecoil ? TEXT("true") : TEXT("false"));
		ForceCrosshairOption(CrosshairSpreadLimitKey, FString::FromInt(S.DynamicSpreadLimit));
		ForceCrosshairOption(CrosshairSplitDistanceKey, FString::FromInt(S.SplitDistance));
		ForceCrosshairOption(CrosshairInnerSplitAlphaKey, FString::SanitizeFloat(S.InnerSplitAlpha));
		ForceCrosshairOption(CrosshairOuterSplitAlphaKey, FString::SanitizeFloat(S.OuterSplitAlpha));
		ForceCrosshairOption(CrosshairSplitRatioKey, FString::SanitizeFloat(S.SplitSizeRatio));
		ForceCrosshairOption(CrosshairScreenHeightKey, FString::FromInt(S.ScreenHeight));
	}

	/** -ScctCrosshair: the values for this run. */
	void ApplyCrosshairCommandLine()
	{
		FString Value;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ScctCrosshair="), Value, false))
		{
			return;
		}
		Value.TrimStartAndEndInline();
		ScctCrosshair::FSettings S = ScctCrosshair::Read();
		if (Value.Equals(TEXT("original"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			S.bCustom = false;
		}
		else if (Value.StartsWith(TEXT("CS"), ESearchCase::CaseSensitive))
		{
			if (!ScctCrosshair::FromShareCode(Value, S))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctCrosshair: '%s' is not a CS2 share code"), *Value);
				return;
			}
		}
		else
		{
			TArray<FString> Parts;
			Value.ParseIntoArray(Parts, TEXT(","), false);
			auto Part = [&Parts](int32 Index, int32 Fallback) { return Parts.IsValidIndex(Index) && !Parts[Index].TrimStartAndEnd().IsEmpty() ? FCString::Atoi(*Parts[Index]) : Fallback; };
			S.bCustom = true;
			S.Style = Part(0, S.Style);
			S.Length = Part(1, S.Length);
			S.Gap = Part(2, S.Gap);
			S.Thickness = Part(3, S.Thickness);
			S.OutlineMode = Part(4, S.OutlineMode);
			S.bCenterDot = Part(5, S.bCenterDot ? 1 : 0) != 0;
			S.Red = Part(6, S.Red);
			S.Green = Part(7, S.Green);
			S.Blue = Part(8, S.Blue);
			S.Alpha = Part(9, S.Alpha);
		}
		S.Clamp();
		ForceCrosshairSettings(S);
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctCrosshair: for this run (not saved): %s"), *ScctCrosshair::Describe(S));
	}
}

void ScctCrosshair::FSettings::Clamp()
{
	Style = FMath::Clamp(Style, 0, 9);
	OutlineMode = FMath::Clamp(OutlineMode, 0, 2);
	Red = FMath::Clamp(Red, 0, 255);
	Green = FMath::Clamp(Green, 0, 255);
	Blue = FMath::Clamp(Blue, 0, 255);
	Alpha = FMath::Clamp(Alpha, 0, 255);
	OutlineRed = FMath::Clamp(OutlineRed, 0, 255);
	OutlineGreen = FMath::Clamp(OutlineGreen, 0, 255);
	OutlineBlue = FMath::Clamp(OutlineBlue, 0, 255);
	OutlineAlpha = FMath::Clamp(OutlineAlpha, 0, 255);
	Gap = FMath::Clamp(Gap, -3840, 3840);
	Length = FMath::Clamp(Length, 0, 255);
	Thickness = FMath::Clamp(Thickness, 0, 32);
	DynamicSpreadLimit = FMath::Clamp(DynamicSpreadLimit, 0, 255);
	SplitDistance = FMath::Clamp(SplitDistance, 0, 127);
	InnerSplitAlpha = CrosshairFraction(InnerSplitAlpha, 0.0f, 1.0f);
	OuterSplitAlpha = CrosshairFraction(OuterSplitAlpha, 0.3f, 1.0f);
	SplitSizeRatio = CrosshairFraction(SplitSizeRatio, 0.0f, 1.0f);
	ScreenHeight = FMath::Clamp(ScreenHeight, 240, 65535);
}

const TCHAR* ScctCrosshair::StyleName(int32 Style)
{
	return CrosshairStyleNames[FMath::Clamp(Style, 0, 9)];
}

TArray<ScctCrosshair::FPiece> ScctCrosshair::Build(const FSettings& C, const FVector2f& Centre, float ScreenHeight, float SpreadPx, bool bHit)
{
	struct FFill
	{
		FBox2f Box;
		float Alpha;
	};
	struct FRingSpec
	{
		float Inner, Outer, Alpha;
	};
	TArray<FFill, TInlineAllocator<24>> Fills;
	TArray<FRingSpec, TInlineAllocator<2>> Rings;

	const float K = ScreenHeight / FMath::Max(240, C.ScreenHeight);
	const int32 T = FMath::Max(1, CrosshairPx(C.Thickness * K));
	const int32 Len = FMath::Max(0, CrosshairPx(C.Length * K));
	const int32 GapPx = CrosshairPx(C.Gap * K);
	float Spread = 0.0f;
	switch (C.Style)
	{
	case 0: case 1: case 2: case 7:
		Spread = FMath::Clamp(SpreadPx, 0.0f, C.DynamicSpreadLimit * K);
		break;
	case 5:
		// Shot feedback: a short kick of the static cross on each shot, not the whole spread.
		Spread = FMath::Clamp(SpreadPx, 0.0f, 4.0f * K);
		break;
	default:
		break;
	}
	const int32 S = CrosshairPx(Spread);
	const int32 Half = T / 2;
	const FVector2f O(FMath::RoundToFloat(Centre.X), FMath::RoundToFloat(Centre.Y));
	const auto Rect = [](float X, float Y, float W, float H) { return FBox2f(FVector2f(X, Y), FVector2f(X + W, Y + H)); };

	const auto Arms = [&](int32 Offset, int32 Length, float Alpha)
	{
		if (Length <= 0)
		{
			return;
		}
		const int32 Near = T - Half + Offset;
		Fills.Add({ Rect(O.X + Near, O.Y - Half, Length, T), Alpha });
		Fills.Add({ Rect(O.X - Half - Offset - Length, O.Y - Half, Length, T), Alpha });
		Fills.Add({ Rect(O.X - Half, O.Y + Near, T, Length), Alpha });
		if (!C.bTStyle)
		{
			Fills.Add({ Rect(O.X - Half, O.Y - Half - Offset - Length, T, Length), Alpha });
		}
	};

	switch (C.Style)
	{
	case 0: case 4: case 5:
		Arms(GapPx + S, Len, 1.0f);
		break;
	case 2:
	{
		// CS:GO's split cross: the inner pieces follow the spread, the outer ones stop at the split distance.
		const int32 OuterLen = CrosshairPx(Len * C.SplitSizeRatio);
		const int32 InnerLen = Len - OuterLen;
		Arms(GapPx + S, InnerLen, C.InnerSplitAlpha);
		Arms(GapPx + FMath::Min(S, CrosshairPx(C.SplitDistance * K)) + InnerLen, OuterLen, C.OuterSplitAlpha);
		break;
	}
	case 1: case 3:
	{
		const float R = static_cast<float>(FMath::Max(T, GapPx + S + Len));
		Rings.Add({ R - T / 2.0f, R + T / 2.0f, 1.0f });
		break;
	}
	case 8:
	{
		const int32 H = FMath::Max(T, GapPx + Len);
		Fills.Add({ Rect(O.X - H, O.Y - H, 2 * H, T), 1.0f });
		Fills.Add({ Rect(O.X - H, O.Y + H - T, 2 * H, T), 1.0f });
		Fills.Add({ Rect(O.X - H, O.Y - H + T, T, 2 * H - 2 * T), 1.0f });
		Fills.Add({ Rect(O.X + H - T, O.Y - H + T, T, 2 * H - 2 * T), 1.0f });
		break;
	}
	case 7: case 9:
	{
		// Four corner brackets.
		const int32 H = FMath::Max(T, GapPx + S + Len);
		const int32 Q = FMath::Max(T, CrosshairPx(Len * 0.6f));
		const int32 Corners[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
		for (const auto& Corner : Corners)
		{
			const float X = Corner[0] < 0 ? O.X - H : O.X + H - Q;
			const float Y = Corner[1] < 0 ? O.Y - H : O.Y + H - T;
			Fills.Add({ Rect(X, Y, Q, T), 1.0f });
			const float VX = Corner[0] < 0 ? O.X - H : O.X + H - T;
			const float VY = Corner[1] < 0 ? O.Y - H + T : O.Y + H - Q;
			Fills.Add({ Rect(VX, VY, T, Q - T), 1.0f });
		}
		break;
	}
	default:
		break;
	}

	if (C.bCenterDot || C.Style == 6)
	{
		const int32 D = C.Style == 6 ? FMath::Max(2, T) : T;
		const int32 DH = D / 2;
		Fills.Add({ Rect(O.X - DH, O.Y - DH, D, D), 1.0f });
	}

	FLinearColor Fill = CrosshairRgba(C.Red, C.Green, C.Blue, C.Alpha);
	if (bHit && C.bHitFlash)
	{
		Fill = FLinearColor(CrosshairHitColour.R, CrosshairHitColour.G, CrosshairHitColour.B, C.Alpha / 255.0f);
	}
	const FLinearColor Line = CrosshairRgba(C.OutlineRed, C.OutlineGreen, C.OutlineBlue, C.OutlineAlpha);
	const float W = static_cast<float>(FMath::Max(1, CrosshairPx(K)));
	const auto Faded = [](FLinearColor Colour, float Alpha) { Colour.A *= Alpha; return Colour; };

	TArray<FPiece> Pieces;
	Pieces.Reserve(Fills.Num() * 2 + Rings.Num() * 2);
	const auto AddBox = [&Pieces](const FBox2f& Box, const FLinearColor& Colour)
	{
		FPiece& Piece = Pieces.AddDefaulted_GetRef();
		Piece.Box = Box;
		Piece.Colour = Colour;
	};
	const auto AddRing = [&Pieces](const FVector2f& At, float Inner, float Outer, const FLinearColor& Colour)
	{
		FPiece& Piece = Pieces.AddDefaulted_GetRef();
		Piece.bRing = true;
		Piece.Centre = At;
		Piece.Inner = Inner;
		Piece.Outer = Outer;
		Piece.Colour = Colour;
	};

	if (C.OutlineMode != 0)
	{
		for (const FFill& F : Fills)
		{
			const FBox2f Back = C.OutlineMode == 1 ? F.Box.ExpandBy(W) : F.Box.ShiftBy(FVector2f(W, W));
			AddBox(Back, Faded(Line, F.Alpha));
		}
		for (const FRingSpec& R : Rings)
		{
			const FVector2f Shift = C.OutlineMode == 1 ? FVector2f::ZeroVector : FVector2f(W, W);
			const float Grow = C.OutlineMode == 1 ? W : 0.0f;
			AddRing(O + Shift, FMath::Max(0.0f, R.Inner - Grow), R.Outer + Grow, Faded(Line, R.Alpha));
		}
	}
	for (const FFill& F : Fills)
	{
		AddBox(F.Box, Faded(Fill, F.Alpha));
	}
	for (const FRingSpec& R : Rings)
	{
		AddRing(O, R.Inner, R.Outer, Faded(Fill, R.Alpha));
	}
	return Pieces;
}

void ScctCrosshair::Paint(FScctHudPainter& Painter, const TArray<FPiece>& Pieces)
{
	for (const FPiece& Piece : Pieces)
	{
		if (Piece.Colour.A <= 0.0f)
		{
			continue;
		}
		if (Piece.bRing)
		{
			const float Thickness = Piece.Outer - Piece.Inner;
			// A little past a full turn, so the line's two ends do not leave a notch where they meet.
			Painter.Arc(Piece.Centre, (Piece.Inner + Piece.Outer) * 0.5f, -3.0f, 363.0f, Piece.Colour, FMath::Max(Thickness, 1.0f));
		}
		else
		{
			Painter.Fill(Piece.Box, Piece.Colour);
		}
	}
}

ScctCrosshair::FSettings ScctCrosshair::Read()
{
	FSettings S;
	if (FScctSettings::Find(CrosshairCustomKey) == nullptr)
	{
		// Not registered (no HUD has started): the defaults.
		return S;
	}
	S.bCustom = FScctSettings::GetBool(CrosshairCustomKey);
	S.Style = FScctSettings::GetInt(CrosshairStyleKey);
	S.Length = FScctSettings::GetInt(CrosshairSizeKey);
	S.Gap = FScctSettings::GetInt(CrosshairGapKey);
	S.Thickness = FScctSettings::GetInt(CrosshairThicknessKey);
	S.OutlineMode = FScctSettings::GetInt(CrosshairOutlineKey);
	S.bCenterDot = FScctSettings::GetBool(CrosshairDotKey);
	S.bTStyle = FScctSettings::GetBool(CrosshairTStyleKey);
	S.Red = FScctSettings::GetInt(CrosshairRedKey);
	S.Green = FScctSettings::GetInt(CrosshairGreenKey);
	S.Blue = FScctSettings::GetInt(CrosshairBlueKey);
	S.Alpha = FScctSettings::GetInt(CrosshairAlphaKey);
	S.OutlineRed = FScctSettings::GetInt(CrosshairOutlineRedKey);
	S.OutlineGreen = FScctSettings::GetInt(CrosshairOutlineGreenKey);
	S.OutlineBlue = FScctSettings::GetInt(CrosshairOutlineBlueKey);
	S.OutlineAlpha = FScctSettings::GetInt(CrosshairOutlineAlphaKey);
	S.bFollowRecoil = FScctSettings::GetBool(CrosshairFollowRecoilKey);
	S.bHitFlash = FScctSettings::GetBool(CrosshairHitFlashKey);
	S.DynamicSpreadLimit = FScctSettings::GetInt(CrosshairSpreadLimitKey);
	S.SplitDistance = FScctSettings::GetInt(CrosshairSplitDistanceKey);
	S.InnerSplitAlpha = FScctSettings::GetFloat(CrosshairInnerSplitAlphaKey);
	S.OuterSplitAlpha = FScctSettings::GetFloat(CrosshairOuterSplitAlphaKey);
	S.SplitSizeRatio = FScctSettings::GetFloat(CrosshairSplitRatioKey);
	S.ScreenHeight = FScctSettings::GetInt(CrosshairScreenHeightKey);
	S.bClassicPosition = FScctSettings::GetBool(CrosshairClassicKey);
	S.Clamp();
	return S;
}

void ScctCrosshair::RegisterOptions()
{
	static bool bRegistered = false;
	if (bRegistered)
	{
		return;
	}
	bRegistered = true;

	const FSettings D;
	int32 Order = 40;
	RegisterCrosshairBool(CrosshairClassicKey, LOCTEXT("ClassicLabel", "Classic crosshair position"),
		LOCTEXT("ClassicTip", "Reloaded's classic_crosshair: the merc reticle slightly off-centre (3 units left and 3 down on the original's 640 x 480 screen), where the original game drew it. Applies to the original reticle and the custom crosshair."),
		false, Order++);
	RegisterCrosshairBool(CrosshairCustomKey, LOCTEXT("CustomLabel", "Custom crosshair"),
		LOCTEXT("CustomTip", "Off: the original reticle. On: the crosshair below, modelled on CS2's crosshair settings. The sound arrows, the reticle message and the red hit flash stay either way. The HUD in a match is the preview: changes show at once."),
		D.bCustom, Order++);
	RegisterCrosshairChoice(CrosshairStyleKey, LOCTEXT("StyleLabel", "Style"),
		LOCTEXT("StyleTip", "CS2's ten styles. Dynamic styles open with the weapon's spread as the original reticle does; shot feedback kicks a little on each shot."),
		CrosshairStyleNames, UE_ARRAY_COUNT(CrosshairStyleNames), D.Style, Order++);
	RegisterCrosshairNumber(CrosshairSizeKey, LOCTEXT("SizeLabel", "Length"), LOCTEXT("SizeTip", "Arm length in pixels at the reference screen height."), true, D.Length, 0.0f, 100.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairGapKey, LOCTEXT("GapLabel", "Gap"), LOCTEXT("GapTip", "Pixels between the centre and the arms at the reference screen height; below 0 the arms overlap."), true, D.Gap, -20.0f, 60.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairThicknessKey, LOCTEXT("ThicknessLabel", "Thickness"), LOCTEXT("ThicknessTip", "Line thickness in pixels at the reference screen height (at least one pixel is drawn)."), true, D.Thickness, 0.0f, 32.0f, 1.0f, Order++);
	RegisterCrosshairChoice(CrosshairOutlineKey, LOCTEXT("OutlineLabel", "Outline"), LOCTEXT("OutlineTip", "None, a full outline around each piece, or half: a shadow to the bottom right."),
		CrosshairOutlineNames, UE_ARRAY_COUNT(CrosshairOutlineNames), D.OutlineMode, Order++);
	RegisterCrosshairBool(CrosshairDotKey, LOCTEXT("DotLabel", "Centre dot"), LOCTEXT("DotTip", "A dot at the centre."), D.bCenterDot, Order++);
	RegisterCrosshairBool(CrosshairTStyleKey, LOCTEXT("TStyleLabel", "T style"), LOCTEXT("TStyleTip", "No top arm."), D.bTStyle, Order++);
	RegisterCrosshairNumber(CrosshairRedKey, LOCTEXT("RedLabel", "Colour: red"), LOCTEXT("RedTip", "The crosshair's colour, red part (0 to 255)."), true, D.Red, 0.0f, 255.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairGreenKey, LOCTEXT("GreenLabel", "Colour: green"), LOCTEXT("GreenTip", "The crosshair's colour, green part (0 to 255)."), true, D.Green, 0.0f, 255.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairBlueKey, LOCTEXT("BlueLabel", "Colour: blue"), LOCTEXT("BlueTip", "The crosshair's colour, blue part (0 to 255)."), true, D.Blue, 0.0f, 255.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairAlphaKey, LOCTEXT("AlphaLabel", "Opacity"), LOCTEXT("AlphaTip", "The crosshair's opacity (0 to 255)."), true, D.Alpha, 0.0f, 255.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairOutlineRedKey, LOCTEXT("OutlineRedLabel", "Outline colour: red"), LOCTEXT("OutlineRedTip", "The outline's colour, red part."), true, D.OutlineRed, 0.0f, 255.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairOutlineGreenKey, LOCTEXT("OutlineGreenLabel", "Outline colour: green"), LOCTEXT("OutlineGreenTip", "The outline's colour, green part."), true, D.OutlineGreen, 0.0f, 255.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairOutlineBlueKey, LOCTEXT("OutlineBlueLabel", "Outline colour: blue"), LOCTEXT("OutlineBlueTip", "The outline's colour, blue part."), true, D.OutlineBlue, 0.0f, 255.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairOutlineAlphaKey, LOCTEXT("OutlineAlphaLabel", "Outline opacity"), LOCTEXT("OutlineAlphaTip", "The outline's opacity (0 to 255)."), true, D.OutlineAlpha, 0.0f, 255.0f, 1.0f, Order++);
	RegisterCrosshairBool(CrosshairFollowRecoilKey, LOCTEXT("FollowLabel", "Follow recoil"), LOCTEXT("FollowTip", "The crosshair moves with the aim kick instead of staying at its fixed spot."), D.bFollowRecoil, Order++);
	RegisterCrosshairBool(CrosshairHitFlashKey, LOCTEXT("HitLabel", "Flash red on a hit"), LOCTEXT("HitTip", "The crosshair turns red for a moment when a shot lands, as the original reticle does."), D.bHitFlash, Order++);
	RegisterCrosshairNumber(CrosshairSpreadLimitKey, LOCTEXT("SpreadLabel", "Dynamic spread limit"), LOCTEXT("SpreadTip", "The most a dynamic style opens, in pixels at the reference screen height."), true, D.DynamicSpreadLimit, 0.0f, 255.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairSplitDistanceKey, LOCTEXT("SplitDistanceLabel", "Split distance"), LOCTEXT("SplitDistanceTip", "Split style: how far the inner pieces travel before they split from the outer ones."), true, D.SplitDistance, 0.0f, 127.0f, 1.0f, Order++);
	RegisterCrosshairNumber(CrosshairInnerSplitAlphaKey, LOCTEXT("InnerAlphaLabel", "Inner split opacity"), LOCTEXT("InnerAlphaTip", "Split style: the inner pieces' opacity."), false, D.InnerSplitAlpha, 0.0f, 1.0f, 0.01f, Order++);
	RegisterCrosshairNumber(CrosshairOuterSplitAlphaKey, LOCTEXT("OuterAlphaLabel", "Outer split opacity"), LOCTEXT("OuterAlphaTip", "Split style: the outer pieces' opacity."), false, D.OuterSplitAlpha, 0.3f, 1.0f, 0.01f, Order++);
	RegisterCrosshairNumber(CrosshairSplitRatioKey, LOCTEXT("SplitRatioLabel", "Split size ratio"), LOCTEXT("SplitRatioTip", "Split style: the share of each arm that stays behind as the outer piece."), false, D.SplitSizeRatio, 0.0f, 1.0f, 0.01f, Order++);
	RegisterCrosshairNumber(CrosshairScreenHeightKey, LOCTEXT("HeightLabel", "Reference screen height"), LOCTEXT("HeightTip", "The screen height the pixel sizes are for; on other screens they scale (CS2's cl_crosshair_screen_height)."), true, D.ScreenHeight, 240.0f, 4320.0f, 1.0f, Order++);

	FScctSettingDesc Code;
	Code.Key = CrosshairCodeKey;
	Code.Page = TEXT("Mercenary");
	Code.Group = LOCTEXT("Group", "Crosshair");
	Code.Label = LOCTEXT("CodeLabel", "CS2 share code");
	Code.Tooltip = LOCTEXT("CodeTip", "Paste a CS2 crosshair share code (CS...) to take its crosshair; it turns the custom crosshair on. The current crosshair's code is kept here to copy (reopen the page to see it after other changes). Older CSGO- codes are not read: CS2 no longer imports them either.");
	Code.Type = EScctSettingType::Text;
	Code.Default = ToShareCode(D);
	Code.Order = Order++;
	FScctSettings::Register(Code);

	FScctSettings::OnChanged().AddStatic(&HandleCrosshairSettingChanged);

	// The code shown is the stored crosshair's (before anything is forced for the run).
	const FString StoredCode = ToShareCode(Read());
	if (FScctSettings::GetString(CrosshairCodeKey) != StoredCode)
	{
		TGuardValue<bool> Guard(bCrosshairSyncing, true);
		FScctSettings::SetString(CrosshairCodeKey, StoredCode);
	}
	ApplyCrosshairCommandLine();
}

bool ScctCrosshair::RunCommand(const FString& Name, int32 Argument)
{
	FName Key;
	FString Value = FString::FromInt(Argument);
	if (Name.Equals(TEXT("Crosshair"), ESearchCase::IgnoreCase))
	{
		Key = CrosshairCustomKey;
		Value = Argument != 0 ? TEXT("true") : TEXT("false");
	}
	else if (Name.Equals(TEXT("ClassicCrosshair"), ESearchCase::IgnoreCase))
	{
		Key = CrosshairClassicKey;
		Value = Argument != 0 ? TEXT("true") : TEXT("false");
	}
	else if (Name.Equals(TEXT("CrosshairStyle"), ESearchCase::IgnoreCase))
	{
		Key = CrosshairStyleKey;
	}
	else if (Name.Equals(TEXT("CrosshairGap"), ESearchCase::IgnoreCase))
	{
		Key = CrosshairGapKey;
	}
	else
	{
		return false;
	}
	// For this run only, like -ScctCrosshair.
	ForceCrosshairOption(Key, Value);
	UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: %s %d (%s for this run)"), *Name, Argument, *Key.ToString());
	return true;
}

FString ScctCrosshair::ToShareCode(const FSettings& Settings)
{
	FSettings C = Settings;
	C.Clamp();
	const int32 Bits = C.SplitDistance | (CrosshairSteps(C.InnerSplitAlpha) << 7) | ((CrosshairSteps(C.OuterSplitAlpha) - 30) << 14)
		| (CrosshairSteps(C.SplitSizeRatio) << 21);
	uint8 B[CrosshairCodeBytes] = {};
	B[1] = 1;
	B[2] = static_cast<uint8>(C.ScreenHeight & 0xff);
	B[3] = static_cast<uint8>(C.ScreenHeight >> 8);
	B[4] = static_cast<uint8>(C.Style | (C.bFollowRecoil ? 0x20 : 0) | (C.bCenterDot ? 0x40 : 0) | (C.bTStyle ? 0x80 : 0));
	B[5] = static_cast<uint8>(C.Red);
	B[6] = static_cast<uint8>(C.Green);
	B[7] = static_cast<uint8>(C.Blue);
	B[8] = static_cast<uint8>(C.Alpha);
	B[9] = static_cast<uint8>(C.OutlineRed);
	B[10] = static_cast<uint8>(C.OutlineGreen);
	B[11] = static_cast<uint8>(C.OutlineBlue);
	B[12] = static_cast<uint8>(C.OutlineAlpha);
	B[13] = static_cast<uint8>(C.Thickness | (C.OutlineMode << 6));
	B[14] = static_cast<uint8>(C.Gap & 0xff);
	B[15] = static_cast<uint8>((C.Gap >> 8) & 0xff);
	B[16] = static_cast<uint8>(C.Length);
	B[17] = static_cast<uint8>(C.DynamicSpreadLimit);
	B[18] = static_cast<uint8>(Bits & 0xff);
	B[19] = static_cast<uint8>((Bits >> 8) & 0xff);
	B[20] = static_cast<uint8>((Bits >> 16) & 0xff);
	B[21] = static_cast<uint8>((Bits >> 24) & 0xff);
	B[22] = static_cast<uint8>(100 - 10); // the scope dot's scale (1.0): the merc has no iron-sight dot
	int32 Sum = 0;
	for (int32 Index = 1; Index < CrosshairCodeBytes; ++Index)
	{
		Sum += B[Index];
	}
	B[0] = static_cast<uint8>(Sum & 0xff);

	// The 32 bytes as one big-endian number, written in base 57, least significant digit first.
	FString Code(TEXT("CS"));
	for (int32 Digit = 0; Digit < CrosshairCodeChars; ++Digit)
	{
		int32 Remainder = 0;
		for (int32 Index = 0; Index < CrosshairCodeBytes; ++Index)
		{
			const int32 Current = Remainder * 256 + B[Index];
			B[Index] = static_cast<uint8>(Current / CrosshairCodeBase);
			Remainder = Current % CrosshairCodeBase;
		}
		Code.AppendChar(CrosshairCodeDictionary[Remainder]);
	}
	return Code;
}

bool ScctCrosshair::FromShareCode(const FString& InCode, FSettings& Out)
{
	const FString Code = InCode.TrimStartAndEnd();
	if (Code.Len() != 2 + CrosshairCodeChars || !Code.StartsWith(TEXT("CS"), ESearchCase::CaseSensitive))
	{
		return false;
	}

	uint8 B[CrosshairCodeBytes] = {};
	for (int32 Char = Code.Len() - 1; Char >= 2; --Char)
	{
		const TCHAR* Found = FCString::Strchr(CrosshairCodeDictionary, Code[Char]);
		if (Found == nullptr || Code[Char] == 0)
		{
			return false;
		}
		int32 Carry = static_cast<int32>(Found - CrosshairCodeDictionary);
		for (int32 Index = CrosshairCodeBytes - 1; Index >= 0; --Index)
		{
			const int32 Current = B[Index] * CrosshairCodeBase + Carry;
			B[Index] = static_cast<uint8>(Current & 0xff);
			Carry = Current >> 8;
		}
		if (Carry != 0)
		{
			return false; // more than 32 bytes
		}
	}

	int32 Sum = 0;
	for (int32 Index = 1; Index < CrosshairCodeBytes; ++Index)
	{
		Sum += B[Index];
	}
	const int32 Height = B[2] | (B[3] << 8);
	if (B[0] != (Sum & 0xff) || B[1] != 1 || Height == 0)
	{
		return false;
	}

	const int32 Bits = B[18] | (B[19] << 8) | (B[20] << 16) | (B[21] << 24);
	FSettings C;
	C.bCustom = true;
	C.bHitFlash = Out.bHitFlash;
	C.bClassicPosition = Out.bClassicPosition;
	C.Style = B[4] & 0x1f;
	C.bFollowRecoil = (B[4] & 0x20) != 0;
	C.bCenterDot = (B[4] & 0x40) != 0;
	C.bTStyle = (B[4] & 0x80) != 0;
	C.OutlineMode = B[13] >> 6;
	C.Red = B[5];
	C.Green = B[6];
	C.Blue = B[7];
	C.Alpha = B[8];
	C.OutlineRed = B[9];
	C.OutlineGreen = B[10];
	C.OutlineBlue = B[11];
	C.OutlineAlpha = B[12];
	C.Gap = static_cast<int16>(B[14] | (B[15] << 8));
	C.Length = B[16];
	C.Thickness = B[13] & 0x3f;
	C.DynamicSpreadLimit = B[17];
	C.SplitDistance = Bits & 0x7f;
	C.InnerSplitAlpha = FMath::Min((Bits >> 7) & 0x7f, 100) / 100.0f;
	C.OuterSplitAlpha = (FMath::Min((Bits >> 14) & 0x7f, 70) + 30) / 100.0f;
	C.SplitSizeRatio = FMath::Min((Bits >> 21) & 0x7f, 100) / 100.0f;
	C.ScreenHeight = Height;
	C.Clamp();
	Out = C;
	return true;
}

FString ScctCrosshair::Describe(const FSettings& S)
{
	const FString Position = S.bClassicPosition ? TEXT("classic spot") : TEXT("centred");
	if (!S.bCustom)
	{
		return FString::Printf(TEXT("original reticle, %s"), *Position);
	}
	return FString::Printf(TEXT("custom %d (%s) length %d gap %d thickness %d outline %s dot %s T %s colour %d,%d,%d,%d outline colour %d,%d,%d,%d follow recoil %s hit flash %s spread limit %d at %d px, %s, code %s"),
		S.Style, StyleName(S.Style), S.Length, S.Gap, S.Thickness, CrosshairOutlineNames[FMath::Clamp(S.OutlineMode, 0, 2)],
		S.bCenterDot ? TEXT("on") : TEXT("off"), S.bTStyle ? TEXT("on") : TEXT("off"), S.Red, S.Green, S.Blue, S.Alpha,
		S.OutlineRed, S.OutlineGreen, S.OutlineBlue, S.OutlineAlpha, S.bFollowRecoil ? TEXT("on") : TEXT("off"),
		S.bHitFlash ? TEXT("on") : TEXT("off"), S.DynamicSpreadLimit, S.ScreenHeight, *Position, *ToShareCode(S));
}

#undef LOCTEXT_NAMESPACE
