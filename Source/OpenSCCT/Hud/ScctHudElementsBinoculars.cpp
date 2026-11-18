#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"
#include "Hud/ScctHudTextures.h"

// The spy's binocular page: the content's P_BinocularsATT as the bridge evaluates it for this moment, and the
// objective targets over it as SHud.DrawObjectivesBinocularsAtt draws them (spec/hud/binoculars.md rules 12 to 17).
// Both are laid out on the original's 640 x 480 canvas stretched over the whole screen, texts included.
//
// The merc's rifle scope: the content's P_BinocularsDEF the same way, but kept round: the page is as high as the
// screen and sits in its middle, and the pictures that reach the page's sides (the tint beside the rings, the
// horizontal axis) are drawn out to the screen's sides, as the Godot client's renderer lays it out.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;

namespace
{
	constexpr float PageWidth = 640.0f;
	constexpr float PageHeight = 480.0f;

	/** The original's target point of the lock (s_H_Binoculars_Att.PostRender: 0.46 ClipX, 0.45 ClipY), on the canvas. */
	const FVector2f LockTarget(PageWidth * 0.46f, PageHeight * 0.45f);

	/** Margins inside which a target counts as on screen: half the 32 x 32 arrow plus 52 and 51 (0x10a90f70). */
	const FVector2f EdgeMargin(16.0f + 52.0f, 16.0f + 51.0f);

	/** The lock box: a target whose point is less than this right of and below the target point is the lock candidate. */
	constexpr float LockBox = 40.0f;

	/** Canvas text height of C.TinyFont (not decoded; the Godot client uses the Enhanced Reality labels' 13). */
	constexpr float MarkerTextHeight = 13.0f;

	/** Slate font size for a line of text this many screen units high (Magma heights are the whole line). */
	constexpr float FontPerHeight = 0.62f;

	const TCHAR* const MarkerPackage = TEXT("SHudTextures");

	/** A colour of the content (sRGB 0..1 channels, alpha as is) as Slate takes it. */
	FLinearColor BinocularContentColour(float R, float G, float B, float A)
	{
		return FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>(FMath::Clamp(R, 0.0f, 1.0f) * 255.0f + 0.5f),
			static_cast<uint8>(FMath::Clamp(G, 0.0f, 1.0f) * 255.0f + 0.5f),
			static_cast<uint8>(FMath::Clamp(B, 0.0f, 1.0f) * 255.0f + 0.5f))).CopyWithNewOpacity(FMath::Clamp(A, 0.0f, 1.0f));
	}

	/** The canvas colour of the original's markers: the bytes given are doubled by the canvas (spec/hud/screen-effects.md rule 0). */
	FLinearColor CanvasColour(int32 R, int32 G, int32 B)
	{
		return BinocularContentColour(FMath::Min(255, 2 * R) / 255.0f, FMath::Min(255, 2 * G) / 255.0f, FMath::Min(255, 2 * B) / 255.0f, 1.0f);
	}

	/** The marker atlas tile of a target kind (FScctHudObjective::Kind), as HudMarkers.IconTile; false for one without. */
	bool IconTile(int32 Kind, FVector2f& Tile)
	{
		switch (Kind)
		{
		case 0: Tile = FVector2f(64.0f, 96.0f); return true;  // terminal
		case 1: Tile = FVector2f(0.0f, 96.0f); return true;   // disk
		case 2: Tile = FVector2f(128.0f, 96.0f); return true; // drop zone
		case 3: Tile = FVector2f(32.0f, 96.0f); return true;  // bomb spot
		case 4: Tile = FVector2f(224.0f, 96.0f); return true; // Disk Hunt disk
		default: return false;
		}
	}

	/** The canvas point of a marker placed on the screen (widget units). */
	FVector2f CanvasPoint(const FScctHudMarker& Marker, const FVector2f& Size)
	{
		return FVector2f(Marker.Screen.X * PageWidth / FMath::Max(Size.X, 1.0f), Marker.Screen.Y * PageHeight / FMath::Max(Size.Y, 1.0f));
	}

	bool IsOnScreen(const FScctHudMarker& Marker, const FVector2f& P)
	{
		return Marker.bInFront && P.X > EdgeMargin.X && P.X < PageWidth - EdgeMargin.X && P.Y > EdgeMargin.Y && P.Y < PageHeight - EdgeMargin.Y;
	}

	/** Draws one layer of the page; K is screen units per page unit and Origin where the page's corner is on the screen. */
	void PaintLayer(FScctHudPainter& Painter, const FScctHudPageLayer& Layer, const FVector2f& K, const FVector2f& Origin = FVector2f::ZeroVector)
	{
		const FLinearColor Colour = BinocularContentColour(Layer.Red, Layer.Green, Layer.Blue, Layer.Alpha);
		if (Layer.Kind == 1)
		{
			const FString Words(Layer.Text);
			const float Height = Layer.Height * K.Y;
			const FSlateFontInfo LayerFont = Font(EWeight::Regular, Height * FontPerHeight, FMath::RoundToInt32(Layer.Tracking));
			const EAlign Align = Layer.Align == 1 ? EAlign::Centre : (Layer.Align == 2 ? EAlign::Right : EAlign::Left);
			const float X = Layer.Align == 1 ? (Layer.L + Layer.R) * 0.5f * K.X : (Layer.Align == 2 ? Layer.R * K.X : Layer.L * K.X);
			const FVector2f At = Origin + FVector2f(X, Layer.T * K.Y);
			if (Layer.ShadowAlpha > 0.0f)
			{
				Painter.Text(At + FVector2f(Layer.ShadowX * K.X, Layer.ShadowY * K.Y), Words, LayerFont,
					BinocularContentColour(Layer.ShadowRed, Layer.ShadowGreen, Layer.ShadowBlue, Layer.ShadowAlpha), Align, false);
			}
			Painter.Text(At, Words, LayerFont, Colour, Align, false);
			return;
		}

		const UScctHudTextures::FEntry* Texture = UScctHudTextures::Find(Layer.Package, Layer.Texture);
		if (Texture == nullptr)
		{
			return;
		}
		const FBox2f UV = Texture->Region(Layer.U0, Layer.V0, Layer.U1, Layer.V1);
		const FBox2f Box(Origin + FVector2f(Layer.L, Layer.T) * K, Origin + FVector2f(Layer.R, Layer.B) * K);
		const float Turn = FMath::Fmod(Layer.Rotation, 360.0f);
		if (Layer.Kind == 4)
		{
			// A Dodge picture (the glint in the middle of the merc's scope) brightens what is under it and covers
			// nothing: the original multiplies the screen by 1 + 2 x texture x colour. Slate cannot read the screen, so
			// the picture is added at the strength that product has over a mid-grey view (texture x colour).
			Painter.ImageAdded(Box, Texture->Brush, UV, Colour);
			return;
		}
		if (FMath::IsNearlyZero(Turn))
		{
			Painter.Image(Box, Texture->Brush, UV, Colour);
			return;
		}

		// The page turns a picture about its box's centre in page units and then stretches the page; a quarter turn
		// stays a box, which on screen is the turned box's size, so the box given to the turn is that size transposed.
		const FVector2f PageSize(FMath::Abs(Layer.R - Layer.L), FMath::Abs(Layer.B - Layer.T));
		const FVector2f Centre = Box.GetCenter();
		const bool bQuarter = FMath::IsNearlyEqual(FMath::Abs(FMath::Fmod(Turn, 180.0f)), 90.0f, 0.5f);
		const FVector2f Drawn = bQuarter ? FVector2f(PageSize.X * K.Y, PageSize.Y * K.X) : PageSize * K;
		Painter.RotatedImage(FBox2f(Centre - Drawn * 0.5f, Centre + Drawn * 0.5f), Texture->Brush, UV, Colour, Turn);
	}
}

void ScctHudElements::PaintScopePage(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	// MagmaRenderer's centre anchor: s = height / 480, the page's middle on the screen's.
	const FVector2f Size = Painter.Size();
	const float S = Size.Y / PageHeight;
	const FVector2f K(S, S);
	const FVector2f Origin((Size.X - PageWidth * S) * 0.5f, 0.0f);

	// Page units from the page's side within which a picture's edge still reaches the screen's (PageEdgeSlack).
	constexpr float EdgeSlack = 2.0f;

	// The mask the last mask layer set (the breath gauge's sweeping box), in screen units; a plain layer ends it.
	bool bMask = false;
	FVector2f MaskCentre = FVector2f::ZeroVector, MaskHalf = FVector2f::ZeroVector;
	float MaskTurn = 0.0f;

	for (const FScctHudPageLayer& Layer : Model.ScopeLayers)
	{
		const FBox2f Box(Origin + FVector2f(Layer.L, Layer.T) * K, Origin + FVector2f(Layer.R, Layer.B) * K);
		const float Turn = FMath::Fmod(Layer.Rotation, 360.0f);
		if (Layer.Kind == 2)
		{
			bMask = true;
			MaskCentre = Box.GetCenter();
			MaskHalf = Box.GetSize() * 0.5f;
			MaskTurn = Turn;
			continue;
		}
		if (Layer.Kind == 3 && bMask)
		{
			if (const UScctHudTextures::FEntry* Texture = UScctHudTextures::Find(Layer.Package, Layer.Texture))
			{
				Painter.ImageOutside(Box, Texture->Brush, Texture->Region(Layer.U0, Layer.V0, Layer.U1, Layer.V1),
					BinocularContentColour(Layer.Red, Layer.Green, Layer.Blue, Layer.Alpha), MaskCentre, MaskHalf, MaskTurn);
			}
			continue;
		}
		if (Layer.Kind != 3)
		{
			bMask = false;
		}

		// An upright picture's edge at or past a side of the page is drawn at or past that side of the screen.
		const auto Reaches = [](float PageX) { return PageX <= EdgeSlack || PageX >= PageWidth - EdgeSlack; };
		if (Layer.Kind != 1 && FMath::IsNearlyZero(Turn) && (Reaches(Layer.L) || Reaches(Layer.R)) && Origin.X > 0.0f)
		{
			const auto Reach = [&Size](float PageX, float ScreenX)
			{
				return PageX <= EdgeSlack ? FMath::Min(ScreenX, 0.0f) : (PageX >= PageWidth - EdgeSlack ? FMath::Max(ScreenX, Size.X) : ScreenX);
			};
			FBox2f Wide = Box;
			Wide.Min.X = Reach(Layer.L, Box.Min.X);
			Wide.Max.X = Reach(Layer.R, Box.Max.X);
			const UScctHudTextures::FEntry* Texture = UScctHudTextures::Find(Layer.Package, Layer.Texture);
			if (Texture != nullptr && Wide.Max.X > Wide.Min.X)
			{
				Painter.Image(Wide, Texture->Brush, Texture->Region(Layer.U0, Layer.V0, Layer.U1, Layer.V1),
					BinocularContentColour(Layer.Red, Layer.Green, Layer.Blue, Layer.Alpha));
			}
			continue;
		}
		PaintLayer(Painter, Layer, K, Origin);
	}
}

int32 ScctHudElements::BinocularLockCandidate(const FScctHudModel& Model, const FVector2f& Size)
{
	for (int32 Index = 0; Index < Model.BinocularMarkers.Num(); ++Index)
	{
		const FScctHudMarker& Marker = Model.BinocularMarkers[Index];
		const FVector2f P = CanvasPoint(Marker, Size);
		if (IsOnScreen(Marker, P) && P.X - LockBox < LockTarget.X && LockTarget.X < P.X && P.Y - LockBox < LockTarget.Y && LockTarget.Y < P.Y)
		{
			return Index;
		}
	}
	return -1;
}

void ScctHudElements::PaintSpyBinoculars(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	const FScctHudState& State = Model.State;
	if (!Model.bHasState || State.BinocularMode != 1 || State.GadgetView != 0 || (State.Flags & EScctHudFlags::HasPawn) == 0
		|| (State.Flags & (EScctHudFlags::HideHud | EScctHudFlags::Spectating | EScctHudFlags::GameEnded)) != 0)
	{
		return;
	}

	const FVector2f Size = Painter.Size();
	const FVector2f K(Size.X / PageWidth, Size.Y / PageHeight);
	for (const FScctHudPageLayer& Layer : Model.BinocularLayers)
	{
		PaintLayer(Painter, Layer, K);
	}

	// The objective targets, while Enhanced Reality is on (rule 12).
	if ((State.Flags & EScctHudFlags::EnhancedReality) == 0)
	{
		return;
	}
	const UScctHudTextures::FEntry* Icons = UScctHudTextures::Find(MarkerPackage, TEXT("MN_boutons"));
	const UScctHudTextures::FEntry* Panel = UScctHudTextures::Find(MarkerPackage, TEXT("BinocularA_all"));
	const UScctHudTextures::FEntry* Arrow = UScctHudTextures::Find(MarkerPackage, TEXT("Att_Arrow"));
	const FLinearColor Triggering = CanvasColour(96, 0, 0), Locked = CanvasColour(96, 96, 0), Idle = CanvasColour(100, 123, 92);
	const FLinearColor Grey = CanvasColour(128, 128, 128);
	const FSlateFontInfo MarkerFont = Font(EWeight::Regular, MarkerTextHeight * K.Y * FontPerHeight);
	const auto Canvas = [&K](float X, float Y, float W, float H) { return FBox2f(FVector2f(X, Y) * K, FVector2f(X + W, Y + H) * K); };
	const float Unit = State.DistanceScale > 0.0f ? State.DistanceScale : 0.01f;
	const FString& UnitName = Model.Text(State.DistanceUnitId);
	const bool bPulse = State.TimeSeconds - FMath::FloorToFloat(State.TimeSeconds) > 0.5f;
	const int32 Candidate = BinocularLockCandidate(Model, Size);

	for (int32 Index = 0; Index < Model.BinocularMarkers.Num(); ++Index)
	{
		const FScctHudMarker& Marker = Model.BinocularMarkers[Index];
		const FScctHudObjective& Target = Marker.Objective;
		const FLinearColor Colour = (Target.Flags & EScctHudObjectiveFlags::Triggering) != 0 ? Triggering
			: ((Target.Flags & EScctHudObjectiveFlags::Locked) != 0 ? Locked : Idle);
		const FVector2f P = CanvasPoint(Marker, Size);

		if (!IsOnScreen(Marker, P))
		{
			// Off the screen: the arrow (it points down in its texture) at the clamped point, turned towards the target.
			if (Arrow != nullptr)
			{
				const FVector2f Edge(FMath::Clamp(P.X, EdgeMargin.X, PageWidth - EdgeMargin.X - 16.0f), FMath::Clamp(P.Y, EdgeMargin.Y, PageHeight - EdgeMargin.Y - 16.0f));
				const float Degrees = FMath::RadiansToDegrees(FMath::Atan2(P.Y - PageHeight * 0.5f, P.X - PageWidth * 0.5f)) - 90.0f;
				Painter.RotatedImage(Canvas(Edge.X - 16.0f, Edge.Y - 16.0f, 32.0f, 32.0f), Arrow->Brush, FBox2f(FVector2f(0.0f), FVector2f(1.0f)), Colour, Degrees);
			}
			continue;
		}

		if (Index == Candidate)
		{
			// The lock panel: its name (Drop zone for a drop zone), the distance with the capture percent, and the hint
			// to lock or unlock with the Use key's name in place of \USE (rule 15).
			const bool bDropZone = Target.Kind == 2;
			const FString Name = bDropZone ? Model.Text(State.DropZoneTextId) : Model.Text(Target.NameId);
			FString Distance = FString::Printf(TEXT("%.0f %s"), Marker.Distance * Unit, *UnitName);
			if (Target.Percent >= 0 && Target.Kind != 1 && !bDropZone)
			{
				Distance += FString::Printf(TEXT(" / %d %%"), Target.Percent);
			}
			const bool bMarked = (Target.Flags & EScctHudObjectiveFlags::Marked) != 0;
			const FString Hint = Model.Text(bMarked ? State.LockedTextId : State.LockTextId).Replace(TEXT("\\USE"), *Model.BinocularUseKey);
			if (Panel != nullptr)
			{
				Painter.Image(Canvas(P.X - 73.0f, P.Y - 43.0f, 300.0f, 60.0f), Panel->Brush, Panel->Region(0.0f, 64.0f, 128.0f, 128.0f), Grey);
			}
			Painter.Text(FVector2f(P.X - 70.0f, P.Y - 40.0f) * K, Name, MarkerFont, Grey, EAlign::Left, false);
			Painter.Text(FVector2f(P.X - 69.0f, P.Y - 20.0f) * K, Distance, MarkerFont, Grey, EAlign::Left, false);
			Painter.Text(FVector2f(P.X - 69.0f, P.Y - 5.0f) * K, Hint, MarkerFont, Grey, EAlign::Left, false);
			continue;
		}

		// Another target: its icon in its colour and two grey brackets pulsing at 1 Hz (rule 16).
		FVector2f Tile;
		if (Icons == nullptr || !IconTile(Target.Kind, Tile))
		{
			continue;
		}
		Painter.Image(Canvas(P.X - 15.0f, P.Y - 15.0f, 30.0f, 30.0f), Icons->Brush, Icons->Region(Tile.X + 1.0f, Tile.Y + 1.0f, Tile.X + 31.0f, Tile.Y + 31.0f), Colour);
		const float Gap = bPulse ? 6.0f : 13.0f;
		Painter.Image(Canvas(P.X + Gap, P.Y - Gap - 7.0f, 7.0f, 7.0f), Icons->Brush, Icons->Region(121.0f, 96.0f, 128.0f, 103.0f), Grey);
		Painter.Image(Canvas(P.X - Gap - 7.0f, P.Y + Gap, 7.0f, 7.0f), Icons->Brush, Icons->Region(96.0f, 121.0f, 103.0f, 128.0f), Grey);
	}
}
