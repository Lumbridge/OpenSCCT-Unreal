#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"
#include "Hud/ScctHudTextures.h"

// The merc backpack panels (the original's P_HudDEF Backpack area: BackPackSelf, BackPackFriend, BackPackFriend1),
// drawn from the bridge's backpack table (Runtime/ScctHudBackpackApi.h) in the HUD's own glass look. Each panel holds
// up to four gadgets in the original's diamond (top, left, right, bottom), the gadget's icon from the content with
// the charges the backpack holds for it on its lower edge: white, red when none are left, grey for a slot the
// backpack does not fill.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	/** The equipment block's width (ScctHudElementsPlayer.cpp): the panels sit to its left. */
	constexpr float EquipmentWidth = 252.0f;

	constexpr float BackpackPanelWidth = 118.0f;
	constexpr float BackpackPanelHeight = 150.0f;
	constexpr float PanelGap = 8.0f;
	constexpr float BackpackTitleHeight = 36.0f;

	/** The icons: this many units square, their centres this far from the diamond's centre. */
	constexpr float IconSize = 34.0f;
	constexpr float BackpackRowSpread = 30.0f;

	/** Ico1..Ico4 of the original: top, left, right, bottom. */
	const FVector2f RowOffsets[4] = { FVector2f(0.0f, -1.0f), FVector2f(-1.0f, 0.0f), FVector2f(1.0f, 0.0f), FVector2f(0.0f, 1.0f) };

	void PaintPanel(FScctHudPainter& Painter, const FScctHudModel& Model, const FBox2f& Box, int32 Panel, const FString& Title, const FString& Name)
	{
		const FLinearColor Accent = ScctHudStyle::Accent(1);
		Painter.Glass(Box, Corner, 0.66f);
		Painter.GlowLine(FVector2f(Box.Min.X + 12.0f, Box.Min.Y + 1.0f), FVector2f(Box.Max.X - 12.0f, Box.Min.Y + 1.0f), Accent, 2.0f);

		// The title the content gives the panel ("You", "Teammate") and the mate's name under it.
		const float CentreX = Box.GetCenter().X;
		Painter.Text(FVector2f(CentreX, Box.Min.Y + 8.0f), Title.ToUpper(), Label(8.0f), TextDim(), EAlign::Centre);
		if (!Name.IsEmpty())
		{
			FString Shown = Name;
			const FSlateFontInfo NameFont = Font(EWeight::Regular, 9.5f);
			while (Shown.Len() > 3 && Painter.Measure(Shown, NameFont).X > BackpackPanelWidth - 16.0f)
			{
				Shown = Shown.LeftChop(1);
			}
			Painter.Text(FVector2f(CentreX, Box.Min.Y + 20.0f), Shown, NameFont, Fade(Accent, 0.95f), EAlign::Centre);
		}

		const FVector2f Centre(CentreX, Box.Min.Y + BackpackTitleHeight + (BackpackPanelHeight - BackpackTitleHeight) * 0.5f);
		const FSlateFontInfo CountFont = Font(EWeight::Bold, 12.0f);
		for (const FScctHudBackpackRow& Row : Model.BackpackRows)
		{
			if (Row.Panel != Panel || Row.Row < 0 || Row.Row >= FMath::Min(Model.Backpack.Rows, 4))
			{
				continue;
			}
			const FVector2f At = Centre + RowOffsets[Row.Row] * BackpackRowSpread;
			const FBox2f Icon(At - FVector2f(IconSize * 0.5f), At + FVector2f(IconSize * 0.5f));
			if (const UScctHudTextures::FEntry* Texture = Row.Texture[0] != 0 ? UScctHudTextures::Find(Row.Package, Row.Texture) : nullptr)
			{
				// The original draws the icons at half opacity; a little more here, over the glass.
				Painter.Image(Icon, Texture->Brush, Texture->Region(Row.U0, Row.V0, Row.U1, Row.V1), FLinearColor(1.0f, 1.0f, 1.0f, Row.Charges < 0 ? 0.35f : 0.8f));
			}
			else
			{
				Painter.Fill(Icon.ExpandBy(-4.0f), Fade(Text(), 0.06f), 3.0f, Fade(Text(), 0.18f), 1.0f);
			}

			// The original's grey for a slot the backpack does not fill (an unlimited gadget's share is negative too).
			const FLinearColor CountColour = Row.Charges < 0 ? Fade(TextDim(), 0.8f) : Row.Charges == 0 ? Danger() : Text();
			const float CountHeight = Painter.Measure(TEXT("0"), CountFont).Y;
			Painter.Text(FVector2f(At.X, Icon.Max.Y - CountHeight + 2.0f), FString::FromInt(FMath::Max(Row.Charges, 0)), CountFont, CountColour, EAlign::Centre);
		}
	}
}

void ScctHudElements::PaintBackpack(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	const FScctHudBackpackState& Backpack = Model.Backpack;
	if (Backpack.Mode == 0 || Backpack.Panels == 0 || !ShowsPlayerHud(Model))
	{
		return;
	}

	// Left of the equipment column, on the bottom margin: the own panel first, then mate A and mate B, as the
	// original lays BackPackSelf, BackPackFriend and BackPackFriend1 out left to right.
	struct FShown
	{
		int32 Panel;
		FString Title;
		FString Name;
	};
	TArray<FShown, TInlineAllocator<3>> Shown;
	if ((Backpack.Panels & 1) != 0)
	{
		// The refill share is the own panel filled from the mate's backpack: it names the mate it comes from.
		Shown.Add({ 0, Model.Text(Backpack.SelfLabelId), Backpack.Mode == 2 ? Model.Text(Backpack.MateANameId) : FString() });
	}
	if ((Backpack.Panels & 2) != 0)
	{
		Shown.Add({ 1, Model.Text(Backpack.MateLabelId), Model.Text(Backpack.MateANameId) });
	}
	if ((Backpack.Panels & 4) != 0)
	{
		Shown.Add({ 2, Model.Text(Backpack.MateLabelId), Model.Text(Backpack.MateBNameId) });
	}

	const FVector2f Size = Painter.Size();
	const float Right = Size.X - Margin - EquipmentWidth - 14.0f;
	const float Top = Size.Y - Margin - BackpackPanelHeight;
	float X = Right - Shown.Num() * BackpackPanelWidth - (Shown.Num() - 1) * PanelGap;
	for (const FShown& Panel : Shown)
	{
		PaintPanel(Painter, Model, HudBox(X, Top, BackpackPanelWidth, BackpackPanelHeight), Panel.Panel, Panel.Title, Panel.Name);
		X += BackpackPanelWidth + PanelGap;
	}
}
