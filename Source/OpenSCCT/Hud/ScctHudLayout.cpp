#include "Hud/ScctHudLayout.h"

#include "HAL/PlatformTime.h"
#include "Hud/ScctHudPainter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"

// The HUD layout's values (Godot's HudEditorSettings), kept in the settings registry and cached here, and the scope a
// canvas paints a group in (Godot's HudEditorTransform.Apply, done as a Slate layout transform instead of moving
// every draw item).

#define LOCTEXT_NAMESPACE "ScctHudLayout"

namespace
{
	enum EHudLayoutField : uint8 { HudLayoutX, HudLayoutY, HudLayoutScale, HudLayoutOpacity, HudLayoutVisible, HudLayoutNumFields };

	const TCHAR* const HudLayoutFieldNames[HudLayoutNumFields] = { TEXT("X"), TEXT("Y"), TEXT("Scale"), TEXT("Opacity"), TEXT("Visible") };

	struct FHudLayoutGroupInfo
	{
		const TCHAR* Id;
		const TCHAR* Label;
		FVector2f Anchor;
	};

	const FHudLayoutGroupInfo HudLayoutGroups[ScctHudLayout::NumGroups] =
	{
		{ TEXT("Status"), TEXT("Status block"), FVector2f(0.0f, 1.0f) },
		{ TEXT("Equipment"), TEXT("Equipment"), FVector2f(1.0f, 1.0f) },
		{ TEXT("Radar"), TEXT("Radar"), FVector2f(0.0f, 0.0f) },
		{ TEXT("Objectives"), TEXT("Objectives"), FVector2f(0.0f, 0.0f) },
		{ TEXT("Feed"), TEXT("Kill feed"), FVector2f(1.0f, 0.0f) },
		{ TEXT("Log"), TEXT("Message log"), FVector2f(0.0f, 1.0f) },
		{ TEXT("Round"), TEXT("Round and headlines"), FVector2f(0.5f, 0.0f) },
		{ TEXT("Telemetry"), TEXT("FPS and network"), FVector2f(1.0f, 0.0f) },
		{ TEXT("Prompt"), TEXT("Interaction prompt"), FVector2f(0.5f, 1.0f) },
		{ TEXT("Chat"), TEXT("Chat lines"), FVector2f(0.0f, 1.0f) },
	};

	/** The option keys, [side][group][field], made once. */
	FName HudLayoutKeys[2][ScctHudLayout::NumGroups][HudLayoutNumFields];
	bool bHudLayoutKeysMade = false;

	FScctHudGroupLayout HudLayoutCache[2][ScctHudLayout::NumGroups];
	bool bHudLayoutCacheDirty = true;
	double HudLayoutCacheReadAt = 0.0;
	FDelegateHandle HudLayoutChangedHandle;

	FScctHudGroupLive HudLayoutLive[ScctHudLayout::NumGroups];
	bool bHudLayoutEditing = false;
	bool bHudLayoutSwitchDone = false;

	void MakeHudLayoutKeys()
	{
		if (bHudLayoutKeysMade)
		{
			return;
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (int32 Group = 0; Group < ScctHudLayout::NumGroups; ++Group)
			{
				for (int32 Field = 0; Field < HudLayoutNumFields; ++Field)
				{
					HudLayoutKeys[Side][Group][Field] = FName(*FString::Printf(TEXT("Hud.Layout.%s.%s.%s"), Side == 0 ? TEXT("Spy") : TEXT("Merc"),
						HudLayoutGroups[Group].Id, HudLayoutFieldNames[Field]));
				}
			}
		}
		bHudLayoutKeysMade = true;
	}

	void RefreshHudLayoutCache()
	{
		MakeHudLayoutKeys();
		const double Now = FPlatformTime::Seconds();
		// The registry raises OnChanged for each value it stores; the second's re-read also catches a reset of the page.
		if (!bHudLayoutCacheDirty && Now - HudLayoutCacheReadAt < 1.0)
		{
			return;
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (int32 Group = 0; Group < ScctHudLayout::NumGroups; ++Group)
			{
				const FName* Keys = HudLayoutKeys[Side][Group];
				FScctHudGroupLayout& L = HudLayoutCache[Side][Group];
				L.X = FScctSettings::GetFloat(Keys[HudLayoutX]);
				L.Y = FScctSettings::GetFloat(Keys[HudLayoutY]);
				L.Scale = FScctSettings::GetFloat(Keys[HudLayoutScale]);
				L.Opacity = FScctSettings::GetFloat(Keys[HudLayoutOpacity]);
				L.bVisible = FScctSettings::GetBool(Keys[HudLayoutVisible]);
				L.Clamp();
			}
		}
		bHudLayoutCacheDirty = false;
		HudLayoutCacheReadAt = Now;
	}

	float HudLayoutSafe(float Value, float Min, float Max, float Fallback)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Fallback;
	}

	/** -ScctHudLayout=merc.radar:0.2,0.1,1.3,0.7+spy.feed:0,0.05,1,1 or reset. */
	void ApplyHudLayoutSwitch()
	{
		FString Value;
		if (bHudLayoutSwitchDone || !FParse::Value(FCommandLine::Get(), TEXT("ScctHudLayout="), Value, /*bShouldStopOnSeparator*/ false))
		{
			return;
		}
		bHudLayoutSwitchDone = true;
		TArray<FString> Entries;
		Value.ParseIntoArray(Entries, TEXT("+"));
		for (const FString& Entry : Entries)
		{
			if (Entry.Equals(TEXT("reset"), ESearchCase::IgnoreCase))
			{
				ScctHudLayout::ResetSide(0);
				ScctHudLayout::ResetSide(1);
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudLayout: -ScctHudLayout put every group of both sides back to the stock layout"));
				continue;
			}
			FString Target, Numbers, SideText, GroupText;
			EScctHudGroup Group = EScctHudGroup::Status;
			if (!Entry.Split(TEXT(":"), &Target, &Numbers) || !Target.Split(TEXT("."), &SideText, &GroupText) || !ScctHudLayout::ParseGroup(GroupText, Group)
				|| !(SideText.Equals(TEXT("spy"), ESearchCase::IgnoreCase) || SideText.Equals(TEXT("merc"), ESearchCase::IgnoreCase)))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHudLayout: -ScctHudLayout entry '%s' is not <spy|merc>.<group>:X,Y,Scale,Opacity[,Visible]"), *Entry);
				continue;
			}
			const int32 Side = SideText.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 1 : 0;
			TArray<FString> Parts;
			Numbers.ParseIntoArray(Parts, TEXT(","));
			FScctHudGroupLayout Layout = ScctHudLayout::Get(Side, Group);
			float* const Fields[] = { &Layout.X, &Layout.Y, &Layout.Scale, &Layout.Opacity };
			for (int32 Index = 0; Index < Parts.Num() && Index < UE_ARRAY_COUNT(Fields); ++Index)
			{
				*Fields[Index] = FCString::Atof(*Parts[Index]);
			}
			if (Parts.Num() > 4)
			{
				Layout.bVisible = FCString::Atoi(*Parts[4]) != 0 || Parts[4].Equals(TEXT("true"), ESearchCase::IgnoreCase);
			}
			ScctHudLayout::Set(Side, Group, Layout);
		}
		FScctSettings::Save();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudLayout: -ScctHudLayout stored and saved: %s"), *ScctHudLayout::Describe());
	}
}

bool FScctHudGroupLayout::IsStock() const
{
	return FMath::IsNearlyZero(X, 1e-4f) && FMath::IsNearlyZero(Y, 1e-4f) && FMath::IsNearlyEqual(Scale, 1.0f, 1e-4f) && FMath::IsNearlyEqual(Opacity, 1.0f, 1e-4f) && bVisible;
}

void FScctHudGroupLayout::Clamp()
{
	X = HudLayoutSafe(X, -1.0f, 1.0f, 0.0f);
	Y = HudLayoutSafe(Y, -1.0f, 1.0f, 0.0f);
	Scale = HudLayoutSafe(Scale, 0.25f, 3.0f, 1.0f);
	Opacity = HudLayoutSafe(Opacity, 0.1f, 1.0f, 1.0f);
}

const TCHAR* ScctHudLayout::GroupId(EScctHudGroup Group)
{
	const int32 Index = static_cast<int32>(Group);
	return Index >= 0 && Index < NumGroups ? HudLayoutGroups[Index].Id : TEXT("?");
}

FText ScctHudLayout::GroupLabel(EScctHudGroup Group)
{
	const int32 Index = static_cast<int32>(Group);
	return FText::FromString(Index >= 0 && Index < NumGroups ? HudLayoutGroups[Index].Label : TEXT("?"));
}

bool ScctHudLayout::ParseGroup(const FString& Text, EScctHudGroup& OutGroup)
{
	for (int32 Index = 0; Index < NumGroups; ++Index)
	{
		if (Text.Equals(HudLayoutGroups[Index].Id, ESearchCase::IgnoreCase))
		{
			OutGroup = static_cast<EScctHudGroup>(Index);
			return true;
		}
	}
	return false;
}

const TCHAR* ScctHudLayout::SideId(int32 Side)
{
	return Side == 1 ? TEXT("merc") : TEXT("spy");
}

FVector2f ScctHudLayout::Anchor(EScctHudGroup Group)
{
	const int32 Index = static_cast<int32>(Group);
	return Index >= 0 && Index < NumGroups ? HudLayoutGroups[Index].Anchor : FVector2f::ZeroVector;
}

void ScctHudLayout::RegisterOptions()
{
	MakeHudLayoutKeys();
	const FText SideGroups[2] = { LOCTEXT("SpyGroup", "Layout: Spy"), LOCTEXT("MercGroup", "Layout: Mercenary") };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 Group = 0; Group < NumGroups; ++Group)
		{
			const FText Label = FText::FromString(HudLayoutGroups[Group].Label);
			const int32 Order = 200 + Side * 100 + Group * 5;
			for (int32 Field = 0; Field < HudLayoutNumFields; ++Field)
			{
				FScctSettingDesc Desc;
				Desc.Key = HudLayoutKeys[Side][Group][Field];
				Desc.Page = TEXT("Hud");
				Desc.Group = SideGroups[Side];
				Desc.Order = Order + Field;
				Desc.Type = EScctSettingType::Float;
				switch (Field)
				{
				case HudLayoutX:
				case HudLayoutY:
					// Moved with the mouse in the edit mode, not from a slider.
					Desc.Label = FText::Format(LOCTEXT("OffsetLabel", "{0} offset {1}"), Label, FText::FromString(HudLayoutFieldNames[Field]));
					Desc.Default = TEXT("0");
					Desc.Min = -1.0f;
					Desc.Max = 1.0f;
					Desc.bVisible = false;
					break;
				case HudLayoutScale:
					Desc.Label = FText::Format(LOCTEXT("ScaleLabel", "{0} size"), Label);
					Desc.Tooltip = LOCTEXT("ScaleTip", "How big this part of the HUD is drawn. Edit HUD layout in the match menu (or the console's scct.HudEdit) moves it and sizes it with the mouse.");
					Desc.Default = TEXT("1");
					Desc.Min = 0.25f;
					Desc.Max = 3.0f;
					Desc.Step = 0.05f;
					Desc.FormatValue = [](float Value) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt32(Value * 100.0f)); };
					break;
				case HudLayoutOpacity:
					Desc.Label = FText::Format(LOCTEXT("OpacityLabel", "{0} opacity"), Label);
					Desc.Tooltip = LOCTEXT("OpacityTip", "How solid this part of the HUD is drawn, on top of its own fades.");
					Desc.Default = TEXT("1");
					Desc.Min = 0.1f;
					Desc.Max = 1.0f;
					Desc.Step = 0.05f;
					Desc.FormatValue = [](float Value) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt32(Value * 100.0f)); };
					break;
				default:
					Desc.Label = FText::Format(LOCTEXT("VisibleLabel", "Show {0}"), Label);
					Desc.Type = EScctSettingType::Bool;
					Desc.Default = TEXT("true");
					Desc.bVisible = false;
					break;
				}
				FScctSettings::Register(Desc);
			}
		}
	}

	if (!HudLayoutChangedHandle.IsValid())
	{
		HudLayoutChangedHandle = FScctSettings::OnChanged().AddLambda([](FName Key)
		{
			if (Key.ToString().StartsWith(TEXT("Hud.Layout.")))
			{
				bHudLayoutCacheDirty = true;
			}
		});
	}
	bHudLayoutCacheDirty = true;

	ApplyHudLayoutSwitch();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudLayout: loaded layout %s"), *Describe());
}

FScctHudGroupLayout ScctHudLayout::Get(int32 Side, EScctHudGroup Group)
{
	const int32 Index = static_cast<int32>(Group);
	if (Index < 0 || Index >= NumGroups)
	{
		return FScctHudGroupLayout();
	}
	RefreshHudLayoutCache();
	return HudLayoutCache[Side == 1 ? 1 : 0][Index];
}

void ScctHudLayout::Set(int32 Side, EScctHudGroup Group, const FScctHudGroupLayout& InLayout)
{
	const int32 Index = static_cast<int32>(Group);
	if (Index < 0 || Index >= NumGroups)
	{
		return;
	}
	MakeHudLayoutKeys();
	FScctHudGroupLayout Layout = InLayout;
	Layout.Clamp();
	const FName* Keys = HudLayoutKeys[Side == 1 ? 1 : 0][Index];
	FScctSettings::SetFloat(Keys[HudLayoutX], Layout.X);
	FScctSettings::SetFloat(Keys[HudLayoutY], Layout.Y);
	FScctSettings::SetFloat(Keys[HudLayoutScale], Layout.Scale);
	FScctSettings::SetFloat(Keys[HudLayoutOpacity], Layout.Opacity);
	FScctSettings::SetBool(Keys[HudLayoutVisible], Layout.bVisible);
	bHudLayoutCacheDirty = true;
}

void ScctHudLayout::ResetGroup(int32 Side, EScctHudGroup Group)
{
	const int32 Index = static_cast<int32>(Group);
	if (Index < 0 || Index >= NumGroups)
	{
		return;
	}
	MakeHudLayoutKeys();
	for (int32 Field = 0; Field < HudLayoutNumFields; ++Field)
	{
		FScctSettings::Reset(HudLayoutKeys[Side == 1 ? 1 : 0][Index][Field]);
	}
	bHudLayoutCacheDirty = true;
}

void ScctHudLayout::ResetSide(int32 Side)
{
	for (int32 Group = 0; Group < NumGroups; ++Group)
	{
		ResetGroup(Side, static_cast<EScctHudGroup>(Group));
	}
}

FString ScctHudLayout::Describe()
{
	TArray<FString> Parts;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 Group = 0; Group < NumGroups; ++Group)
		{
			const FScctHudGroupLayout L = Get(Side, static_cast<EScctHudGroup>(Group));
			if (!L.IsStock())
			{
				Parts.Add(FString::Printf(TEXT("%s.%s x=%.2f y=%.2f scale=%.2f opacity=%.2f%s"), SideId(Side), HudLayoutGroups[Group].Id, L.X, L.Y, L.Scale, L.Opacity,
					L.bVisible ? TEXT("") : TEXT(" hidden")));
			}
		}
	}
	return Parts.Num() > 0 ? FString::Join(Parts, TEXT("; ")) : FString(TEXT("stock (every group in place)"));
}

void ScctHudLayout::Placement(EScctHudGroup Group, const FScctHudGroupLayout& Layout, const FVector2f& CanvasSize, const FBox2f& LastRaw, FVector2f& OutOffset, float& OutScale)
{
	OutScale = Layout.Scale;
	const FVector2f AnchorPoint = Anchor(Group) * CanvasSize;
	OutOffset = AnchorPoint * (1.0f - Layout.Scale) + FVector2f(Layout.X, Layout.Y) * CanvasSize;
	if (Layout.IsStock() || !LastRaw.bIsValid)
	{
		return;
	}
	// HudEditorTransform.Transform: an edited group stays where it can be seen (and found again) on a smaller screen.
	const FVector2f Min = OutOffset + LastRaw.Min * OutScale;
	const FVector2f Size = LastRaw.GetSize() * OutScale;
	const FVector2f Room = (CanvasSize - Size).ComponentMax(FVector2f::ZeroVector);
	const FVector2f Clamped(FMath::Clamp(Min.X, 0.0f, Room.X), FMath::Clamp(Min.Y, 0.0f, Room.Y));
	OutOffset += Clamped - Min;
}

bool ScctHudLayout::IsEditing()
{
	return bHudLayoutEditing;
}

void ScctHudLayout::SetEditing(bool bEditing)
{
	bHudLayoutEditing = bEditing;
}

const FScctHudGroupLive& ScctHudLayout::Live(EScctHudGroup Group)
{
	static const FScctHudGroupLive None;
	const int32 Index = static_cast<int32>(Group);
	return Index >= 0 && Index < NumGroups ? HudLayoutLive[Index] : None;
}

ScctHudLayout::FScope::FScope(FScctHudPainter& InPainter, EScctHudGroup InGroup, int32 InSide)
	: Painter(InPainter)
	, Group(InGroup)
	, Side(InSide == 1 ? 1 : 0)
{
	const FScctHudGroupLayout Layout = Get(Side, Group);
	if (!Layout.bVisible && !bHudLayoutEditing)
	{
		HudLayoutLive[static_cast<int32>(Group)].Raw = FBox2f(ForceInit);
		HudLayoutLive[static_cast<int32>(Group)].Placed = FBox2f(ForceInit);
		return;
	}
	const FScctHudGroupLive& Last = HudLayoutLive[static_cast<int32>(Group)];
	Placement(Group, Layout, Painter.Size(), Last.Side == Side ? Last.Raw : FBox2f(ForceInit), Offset, Scale);
	// A hidden group shows faint while the layout is edited, so it can be found and shown again.
	Painter.BeginGroup(Offset, Scale, Layout.bVisible ? Layout.Opacity : 0.25f * Layout.Opacity);
	bDraws = true;
}

ScctHudLayout::FScope::~FScope()
{
	if (!bDraws)
	{
		return;
	}
	const FBox2f Raw = Painter.EndGroup();
	FScctHudGroupLive& Live = HudLayoutLive[static_cast<int32>(Group)];
	const FGeometry& Canvas = Painter.GetCanvasGeometry();
	Live.CanvasSize = Painter.Size();
	Live.AbsoluteOrigin = FVector2f(Canvas.LocalToAbsolute(FVector2f::ZeroVector));
	Live.AbsoluteScale = Canvas.GetAccumulatedLayoutTransform().GetScale();
	// A frame the group had nothing to show keeps the box it drew last: a blinking line does not lose its outline.
	if (Raw.bIsValid)
	{
		Live.Raw = Raw;
		Live.Placed = FBox2f(Offset + Raw.Min * Scale, Offset + Raw.Max * Scale);
		Live.Side = Side;
		Live.PaintedAt = FPlatformTime::Seconds();
		Live.PaintedFrame = GFrameCounter;
	}
}

#undef LOCTEXT_NAMESPACE
