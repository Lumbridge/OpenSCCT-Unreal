#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"
#include "Hud/ScctHudTextures.h"

// The HUD's blocks drawn from the content's pictures (UScctHudTextures): the spy's coop icon and the Merc Deathmatch
// respawn weapon pick.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	/** The status block's size (ScctHudElementsPlayer.cpp): the coop icon sits above its right end. */
	constexpr float StatusWidth = 252.0f;
	constexpr float StatusHeight = 72.0f;

	/** HUD units per unit of the 640 x 480 Magma page the coop clips are laid out on (about the original's size at 1080p). */
	constexpr float CoopScale = 2.0f;

	/** The respawn pick's weapon tile: 64 x 64 texels of SHudTextures.Icones_Weapons, drawn this many units square. */
	constexpr float WeaponTexels = 64.0f;
	constexpr float WeaponSize = 96.0f;

	/** A colour of the content (sRGB 0..1 channels, alpha as is) as Slate takes it. */
	FLinearColor ContentColour(float R, float G, float B, float A)
	{
		return FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>(FMath::Clamp(R, 0.0f, 1.0f) * 255.0f + 0.5f),
			static_cast<uint8>(FMath::Clamp(G, 0.0f, 1.0f) * 255.0f + 0.5f),
			static_cast<uint8>(FMath::Clamp(B, 0.0f, 1.0f) * 255.0f + 0.5f))).CopyWithNewOpacity(FMath::Clamp(A, 0.0f, 1.0f));
	}
}

void ScctHudElements::PaintCoopIcon(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (Model.CoopIcon == 0 || Model.CoopLayers.Num() == 0 || !Model.CoopBounds.bIsValid || !ShowsPlayerHud(Model))
	{
		return;
	}

	// The clips are posed figures on the original's page; the box every pose of the shown clip stays in is placed
	// above the right end of the status block, so the figures move inside a panel that keeps still while it plays.
	const FVector2f Size = Painter.Size();
	constexpr float Pad = 10.0f;
	const FVector2f Page = Model.CoopBounds.GetSize() * CoopScale;
	const float Right = Margin + StatusWidth;
	const float Bottom = Size.Y - Margin - StatusHeight - 10.0f;
	const FBox2f Panel = HudBox(Right - Page.X - Pad * 2.0f, Bottom - Page.Y - Pad * 2.0f, Page.X + Pad * 2.0f, Page.Y + Pad * 2.0f);
	Painter.Glass(Panel, Corner, 0.5f);
	Painter.GlowLine(FVector2f(Panel.Max.X - 1.0f, Panel.Min.Y + 12.0f), FVector2f(Panel.Max.X - 1.0f, Panel.Max.Y - 12.0f), Accent(0), 2.0f);

	const FVector2f Origin = Panel.Min + FVector2f(Pad) - Model.CoopBounds.Min * CoopScale;
	for (const FScctHudTextureLayer& Layer : Model.CoopLayers)
	{
		const UScctHudTextures::FEntry* Texture = UScctHudTextures::Find(Layer.Package, Layer.Texture);
		if (Texture == nullptr)
		{
			continue;
		}
		const FBox2f Box(Origin + FVector2f(Layer.L, Layer.T) * CoopScale, Origin + FVector2f(Layer.R, Layer.B) * CoopScale);
		Painter.Image(Box, Texture->Brush, Texture->Region(Layer.U0, Layer.V0, Layer.U1, Layer.V1),
			ContentColour(Layer.Red, Layer.Green, Layer.Blue, Layer.Alpha));
	}
}

void ScctHudElements::PaintRespawnWeapon(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	const FScctHudState& State = Model.State;
	if (!Model.bHasState || State.RespawnWeaponIconU < 0 || State.RespawnWeaponIconV < 0
		|| (State.Flags & (EScctHudFlags::HideHud | EScctHudFlags::GameEnded)) != 0)
	{
		return;
	}
	const UScctHudTextures::FEntry* Atlas = UScctHudTextures::Find(TEXT("SHudTextures"), TEXT("Icones_Weapons"));
	if (Atlas == nullptr)
	{
		return;
	}

	// SHud.DrawRespawnWeapon: the weapon's tile in the bottom right corner with the Use key's name under it, in the
	// merc colour; here in a glass panel of the HUD's own look.
	const FVector2f Size = Painter.Size();
	const FLinearColor Colour = Accent(1);
	const FSlateFontInfo KeyFont = Label(9.0f);
	const FString Key = Model.RespawnUseKey.ToUpper();
	constexpr float Pad = 14.0f, KeyHeight = 22.0f;
	const float Width = FMath::Max(WeaponSize, Painter.Measure(Key, KeyFont).X) + Pad * 2.0f;
	const float Height = WeaponSize + Pad * 2.0f + (Key.IsEmpty() ? 0.0f : KeyHeight);
	const float Right = Size.X - Margin, Bottom = Size.Y - Margin;
	const FBox2f Panel = HudBox(Right - Width, Bottom - Height, Width, Height);
	Painter.Glass(Panel, Corner);
	Painter.GlowLine(FVector2f(Right - 1.0f, Panel.Min.Y + 14.0f), FVector2f(Right - 1.0f, Panel.Max.Y - 14.0f), Colour, 2.0f);

	const float CentreX = Panel.GetCenter().X;
	const float U = State.RespawnWeaponIconU, V = State.RespawnWeaponIconV;
	Painter.Image(HudBox(CentreX - WeaponSize * 0.5f, Panel.Min.Y + Pad, WeaponSize, WeaponSize), Atlas->Brush,
		Atlas->Region(U, V, U + WeaponTexels, V + WeaponTexels), Colour);
	if (!Key.IsEmpty())
	{
		Painter.Text(FVector2f(CentreX, Panel.Min.Y + Pad + WeaponSize + 6.0f), Key, KeyFont, Text(), EAlign::Centre);
	}
}
