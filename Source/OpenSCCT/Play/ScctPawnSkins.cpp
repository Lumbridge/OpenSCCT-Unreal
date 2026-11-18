#include "Play/ScctPawnSkins.h"

#include "CollisionQueryParams.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "Hud/ScctHudSubsystem.h"
#include "Hud/ScctVisionSubsystem.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctExposure.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Play/ScctViewPawn.h"

namespace
{
	const TCHAR* const StickyPath = TEXT("/Game/OpenSCCT/Materials/PawnSkins/M_ScctStickySkin.M_ScctStickySkin");
	const TCHAR* const EdgePath = TEXT("/Game/OpenSCCT/Materials/PawnSkins/M_ScctShockImmunityEdge.M_ScctShockImmunityEdge");
	const TCHAR* const WetPaths[] = {
		TEXT("/Game/OpenSCCT/Materials/PawnSkins/MI_ScctWetSkin_ATT.MI_ScctWetSkin_ATT"),
		TEXT("/Game/OpenSCCT/Materials/PawnSkins/MI_ScctWetSkin_ATT2.MI_ScctWetSkin_ATT2"),
		TEXT("/Game/OpenSCCT/Materials/PawnSkins/MI_ScctWetSkin_DEF.MI_ScctWetSkin_DEF"),
	};

	/**
	 * The slots the skins go on, by the end of the slot material's name (the imported surface, MI_SPersoTextures_...):
	 * the Stickess shaders over the spy's Skins 0 and 1 and the merc's 0 and 2 (BeginStickySkin), the wet skins over
	 * the same surfaces (BeginWetSkin; the merc's face stays dry). The head is tested before the body it begins with.
	 */
	struct FSlotRule
	{
		const TCHAR* Suffix;
		bool bSticky;
		int32 Wet; // into WetPaths
	};
	const FSlotRule SlotRules[] = {
		{ TEXT("Shadw_agent_2"), true, 1 },
		{ TEXT("Shadw_agent"), true, 0 },
		{ TEXT("DEF_01_A_Body"), true, 2 },
	};

	/** The first shock spark's light (SBasePawn.StickyShockSfx): LT_Blink, hue 145, saturation 174, brightness 150, radius 10. */
	constexpr float ShockLightHue = 145.0f;
	constexpr float ShockLightSaturation = 174.0f;
	constexpr float ShockLightBrightness = 150.0f;
	constexpr float ShockLightRadius = 10.0f;

	/** As the effect lights are: candelas per step of UE2 LightBrightness, and 25 cm per step of LightRadius plus one. */
	constexpr float SkinCandelasPerBrightness = 2.0f;
	constexpr float LightRadiusUnit = 25.0f;

	/** LT_Blink on and off; the original's period is not decoded, the Godot client blinks it every 0.2 s. */
	constexpr double BlinkPeriod = 0.2;

	/** The bone the first spark hangs from. */
	const TCHAR* const ShockLightBone = TEXT("B R ForeArm");

	/** The protection edge: its pulse period, and the least of the body lit while any protection is left. */
	constexpr float PulseSeconds = 1.5f;
	constexpr float MinimumCoverage = 0.035f;

	const FName CoverageName(TEXT("Coverage"));
	const FName FeetZName(TEXT("FeetZ"));
	const FName HeadZName(TEXT("HeadZ"));
	const FName PulseName(TEXT("Pulse"));
	const FName OpacityName(TEXT("Opacity"));
	const FName PictureScaleName(TEXT("PictureScale"));
	const FName FillName(TEXT("Fill"));
	const FName RimName(TEXT("Rim"));
	const FName StrengthName(TEXT("Strength"));

	/** The skins' textures in the converted content, under shared/chars/SPerso/textures (SPersoTextures.*). */
	const TCHAR* const TextureFolder = TEXT("shared/chars/SPerso/textures");
	const TCHAR* const StickyFile = TEXT("SPersoTextures_shadwagent_sticky_1.png");
	const TCHAR* const WetEnvFile = TEXT("SPersoTextures_envmap_wet.png");
	const TCHAR* const WetMaskFiles[] = { // as WetPaths
		TEXT("SPersoTextures_ATT_spec_mask.png"),
		TEXT("SPersoTextures_ATT_2_wet.png"),
		TEXT("SPersoTextures_DEF_01_A_Body_envmap.png"),
	};
	const FName StickyTextureName(TEXT("Sticky"));
	const FName EnvMapName(TEXT("EnvMap"));
	const FName SpecMaskName(TEXT("SpecMask"));

	/**
	 * A converted PNG as a transient texture with its box-filtered mip chain (as the pawn looks make theirs), or null
	 * with a warning. The masks are read from their alpha, which no colour space changes, so all are sRGB.
	 */
	UTexture2D* MakeSkinTexture(const FString& Path)
	{
		FImage Image;
		if (!FImageUtils::LoadImage(*Path, Image) || Image.SizeX <= 0 || Image.SizeY <= 0)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("PawnSkins: could not read %s"), *Path);
			return nullptr;
		}
		Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		UTexture2D* Texture = UTexture2D::CreateTransient(Image.SizeX, Image.SizeY, PF_B8G8R8A8, *FString::Printf(TEXT("PawnSkin_%s"), *FPaths::GetBaseFilename(Path)));
		if (Texture == nullptr)
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->NeverStream = true;
		Texture->Filter = TF_Trilinear;
		FTexturePlatformData* Data = Texture->GetPlatformData();
		TArray64<uint8> Level(Image.RawData);
		int32 Width = Image.SizeX;
		int32 Height = Image.SizeY;
		for (int32 MipIndex = 0;; ++MipIndex)
		{
			FTexture2DMipMap* Mip = MipIndex < Data->Mips.Num() ? &Data->Mips[MipIndex] : new FTexture2DMipMap(Width, Height, 1);
			if (MipIndex >= Data->Mips.Num())
			{
				Data->Mips.Add(Mip);
			}
			Mip->SizeX = Width;
			Mip->SizeY = Height;
			Mip->BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Mip->BulkData.Realloc(Level.Num()), Level.GetData(), Level.Num());
			Mip->BulkData.Unlock();
			if (Width == 1 && Height == 1)
			{
				break;
			}
			const int32 NextWidth = FMath::Max(1, Width / 2);
			const int32 NextHeight = FMath::Max(1, Height / 2);
			TArray64<uint8> Next;
			Next.SetNumUninitialized(static_cast<int64>(NextWidth) * NextHeight * 4);
			for (int32 Y = 0; Y < NextHeight; ++Y)
			{
				for (int32 X = 0; X < NextWidth; ++X)
				{
					const int32 X0 = FMath::Min(X * 2, Width - 1), X1 = FMath::Min(X * 2 + 1, Width - 1);
					const int32 Y0 = FMath::Min(Y * 2, Height - 1), Y1 = FMath::Min(Y * 2 + 1, Height - 1);
					for (int32 Channel = 0; Channel < 4; ++Channel)
					{
						const int32 Sum = Level[(static_cast<int64>(Y0) * Width + X0) * 4 + Channel] + Level[(static_cast<int64>(Y0) * Width + X1) * 4 + Channel] +
							Level[(static_cast<int64>(Y1) * Width + X0) * 4 + Channel] + Level[(static_cast<int64>(Y1) * Width + X1) * 4 + Channel];
						Next[(static_cast<int64>(Y) * NextWidth + X) * 4 + Channel] = static_cast<uint8>((Sum + 2) / 4);
					}
				}
			}
			Level = MoveTemp(Next);
			Width = NextWidth;
			Height = NextHeight;
		}
		Texture->UpdateResource();
		return Texture;
	}

	FLinearColor ShockLightColor()
	{
		// UE2 light saturation 255 is white: the colour is hue 145 at a third of full saturation, a pale blue.
		return FLinearColor(ShockLightHue / 255.0f * 360.0f, 1.0f - ShockLightSaturation / 255.0f, 1.0f).HSVToLinearRGB();
	}
}

bool UScctPawnSkinsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctPawnSkinsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UScctSessionSubsystem* Match = Collection.InitializeDependency<UScctSessionSubsystem>();
	// The HUD refreshes the vision rows on its OnFrame. A multicast delegate calls its last binding first, so the
	// skins, bound after the HUD, read the rows of the HUD's previous refresh: a skin goes on or off one frame late.
	Collection.InitializeDependency<UScctHudSubsystem>();
	if (Match == nullptr)
	{
		return;
	}
	Session = Match;
	FScctExposure::Start();
	FrameHandle = Match->OnFrame.AddUObject(this, &UScctPawnSkinsSubsystem::OnFrame);
	StoppedHandle = Match->OnMatchStopped.AddUObject(this, &UScctPawnSkinsSubsystem::OnMatchStopped);

	FString Forced;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctForceSkin="), Forced, false))
	{
		TArray<FString> Parts;
		Forced.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			bForceSticky |= Part.TrimStartAndEnd().Equals(TEXT("sticky"), ESearchCase::IgnoreCase);
			bForceImmune |= Part.TrimStartAndEnd().Equals(TEXT("immune"), ESearchCase::IgnoreCase);
			bForceWet |= Part.TrimStartAndEnd().Equals(TEXT("wet"), ESearchCase::IgnoreCase);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: -ScctForceSkin=%s puts%s%s%s on every pawn"), *Forced,
			bForceSticky ? TEXT(" sticky") : TEXT(""), bForceImmune ? TEXT(" immune") : TEXT(""), bForceWet ? TEXT(" wet") : TEXT(""));
	}

	// -ScctSkinEdge=fill,rim,strength and -ScctSkinWet=strength: the looks' levels for tuning without rebuilding them.
	FString Edge;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctSkinEdge="), Edge, false))
	{
		TArray<FString> Parts;
		Edge.ParseIntoArray(Parts, TEXT(","));
		for (int32 Index = 0; Index < Parts.Num() && Index < UE_ARRAY_COUNT(EdgeTune); ++Index)
		{
			EdgeTune[Index] = FCString::Atof(*Parts[Index]);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: protection edge fill %.3f, rim %.3f, strength %.3f by -ScctSkinEdge"), EdgeTune[0], EdgeTune[1], EdgeTune[2]);
	}
	FParse::Value(FCommandLine::Get(), TEXT("ScctSkinWet="), WetTune);

	FString Camera;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctSkinCam="), Camera, false))
	{
		SkinCamera = Camera.Equals(TEXT("spy"), ESearchCase::IgnoreCase) ? 2 : Camera.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 3 : 1;
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("ScctSkinCam")))
	{
		SkinCamera = 1;
	}
}

void UScctPawnSkinsSubsystem::Deinitialize()
{
	if (UScctSessionSubsystem* Match = Session.Get())
	{
		Match->OnFrame.Remove(FrameHandle);
		Match->OnMatchStopped.Remove(StoppedHandle);
	}
	RemoveAll();
	Super::Deinitialize();
}

bool UScctPawnSkinsSubsystem::EnsureMaterials()
{
	if (bTriedMaterials)
	{
		return StickyMaterial != nullptr;
	}
	bTriedMaterials = true;

	// The textures are the original game's: made from the player's converted content, never shipped in the materials.
	const FString Folder = FPaths::Combine(FOpenSCCTModule::FindContentRoot(), TextureFolder);
	SkinTextures.Reset();
	const auto Make = [this, &Folder](const TCHAR* File)
	{
		UTexture2D* Texture = MakeSkinTexture(FPaths::Combine(Folder, File));
		SkinTextures.Add(Texture);
		return Texture;
	};
	UTexture2D* StickyTexture = Make(StickyFile);
	UTexture2D* EnvTexture = Make(WetEnvFile);
	bool bAll = StickyTexture != nullptr && EnvTexture != nullptr;

	UMaterialInterface* Sticky = LoadObject<UMaterialInterface>(nullptr, StickyPath, nullptr, LOAD_NoWarn);
	EdgeMaterial = LoadObject<UMaterialInterface>(nullptr, EdgePath, nullptr, LOAD_NoWarn);
	bAll &= Sticky != nullptr && EdgeMaterial != nullptr;
	if (bAll)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Sticky, this);
		Instance->SetTextureParameterValue(StickyTextureName, StickyTexture);
		StickyMaterial = Instance;
	}
	WetMaterials.Reset();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(WetPaths); ++Index)
	{
		// One dynamic instance per wet skin, shared by every pawn: the picture scale is the same for all of them.
		UMaterialInterface* Wet = LoadObject<UMaterialInterface>(nullptr, WetPaths[Index], nullptr, LOAD_NoWarn);
		UTexture2D* Mask = Make(WetMaskFiles[Index]);
		UMaterialInstanceDynamic* Instance = Wet != nullptr && Mask != nullptr && EnvTexture != nullptr ? UMaterialInstanceDynamic::Create(Wet, this) : nullptr;
		if (Instance != nullptr)
		{
			Instance->SetTextureParameterValue(EnvMapName, EnvTexture);
			Instance->SetTextureParameterValue(SpecMaskName, Mask);
		}
		WetMaterials.Add(Instance);
		bAll &= Instance != nullptr;
	}
	if (!bAll)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("PawnSkins: the skin materials or their textures (%s) are missing; the materials come from Tools/Materials/Build-PawnSkinMaterials.ps1. No pawn shows a state skin."), *Folder);
		StickyMaterial = nullptr;
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: sticky, protection edge and %d wet skins ready, textures from %s"), WetMaterials.Num(), *Folder);
	return true;
}

const TArray<UScctPawnSkinsSubsystem::FSlotSkins>& UScctPawnSkinsSubsystem::SlotsOf(const UPoseableMeshComponent& Body)
{
	const UObject* Model = Body.GetSkinnedAsset();
	if (const TArray<FSlotSkins>* Found = SlotsByModel.Find(Model))
	{
		return *Found;
	}

	TArray<FSlotSkins>& Slots = SlotsByModel.Add(Model);
	FString Names;
	for (int32 Slot = 0; Slot < Body.GetNumMaterials(); ++Slot)
	{
		// The model's own slot material: the camo suit may have swapped the component's for its own.
		const UMaterialInterface* Material = Body.GetSkinnedAsset() != nullptr && Body.GetSkinnedAsset()->GetMaterials().IsValidIndex(Slot)
			? Body.GetSkinnedAsset()->GetMaterials()[Slot].MaterialInterface.Get() : nullptr;
		const FString Name = Material != nullptr ? Material->GetName() : FString();
		FSlotSkins& Taken = Slots.AddDefaulted_GetRef();
		for (const FSlotRule& Rule : SlotRules)
		{
			if (Name.EndsWith(Rule.Suffix))
			{
				Taken.bSticky = Rule.bSticky;
				Taken.Wet = Rule.Wet;
				break;
			}
		}
		Names += FString::Printf(TEXT(" %d=%s%s%s"), Slot, *Name, Taken.bSticky ? TEXT("(sticky") : TEXT("(-"),
			Taken.Wet != INDEX_NONE ? *FString::Printf(TEXT(", wet %d)"), Taken.Wet) : TEXT(")"));
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: model %s slots%s"), Model != nullptr ? *Model->GetName() : TEXT("none"), *Names);
	return Slots;
}

void UScctPawnSkinsSubsystem::OnFrame(float DeltaSeconds)
{
	const UScctSessionSubsystem* Match = Session.Get();
	if (Match == nullptr || !EnsureMaterials())
	{
		return;
	}
	const UScctVisionSubsystem* Vision = UScctVisionSubsystem::Find(GetWorld());
	const bool bHeatOrEmf = Vision != nullptr && Vision->IsHeatOrEmf();

	// The edge and the sheen are brightnesses in the finished picture: divided by the exposure of the last frame.
	PictureScale = FMath::Clamp(1.0f / FMath::Max(FScctExposure::Get(), 0.001f), 0.002f, 16.0f);
	for (UMaterialInterface* Wet : WetMaterials)
	{
		if (UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Wet))
		{
			Instance->SetScalarParameterValue(PictureScaleName, PictureScale);
			if (WetTune >= 0.0f)
			{
				Instance->SetScalarParameterValue(StrengthName, WetTune);
			}
		}
	}

	int32 Sticky = 0;
	int32 Immune = 0;
	int32 Wet = 0;
	for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Match->GetProxies())
	{
		AScctPawnProxy* Proxy = Pair.Value;
		if (!IsValid(Proxy) || !Proxy->HasModel())
		{
			continue;
		}
		const FScctVisionPawn* Row = Vision != nullptr ? Vision->GetData().FindPawn(Proxy->GetActorId()) : nullptr;
		const int32 Flags = Row != nullptr ? Row->Flags : 0;
		const bool bStickyFlag = (Flags & EScctVisionPawnFlags::StickySkinned) != 0;
		const bool bImmuneFlag = (Flags & EScctVisionPawnFlags::ShockImmune) != 0;
		const bool bWetFlag = (Flags & EScctVisionPawnFlags::Wet) != 0;
		const bool bSticky = bForceSticky || bStickyFlag;
		const bool bImmune = bForceImmune || bImmuneFlag;
		// Wet only matters to the eye in plain sight and night vision (see the header).
		const bool bWet = (bForceWet || bWetFlag) && !bHeatOrEmf;

		// All of him while a state protects him or the switch holds it; else the share of the protection time left.
		float Coverage = 1.0f;
		if (bImmuneFlag && !bForceImmune && (Flags & EScctVisionPawnFlags::ShockStateProtected) == 0)
		{
			Coverage = FMath::Clamp(Row->ShockFraction, MinimumCoverage, 1.0f);
		}

		FPawnSkin& Skin = Skins.FindOrAdd(Proxy->GetActorId());
		if (Skin.Proxy.Get() != Proxy)
		{
			Skin = FPawnSkin();
			Skin.Proxy = Proxy;
			Skin.Body = Proxy->GetBodyMesh();
		}

		const auto Says = [](bool bFlag, bool bForced) { return bFlag ? TEXT("the simulation") : bForced ? TEXT("-ScctForceSkin") : TEXT(""); };
		if (bSticky != Skin.bSticky)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: pawn %d (%s) Stickess skin %s%s%s"), Proxy->GetActorId(), Proxy->GetKind() == 1 ? TEXT("merc") : TEXT("spy"),
				bSticky ? TEXT("on, blinking blue light at ") : TEXT("off"), bSticky ? ShockLightBone : TEXT(""),
				bSticky ? *FString::Printf(TEXT(" (%s)"), Says(bStickyFlag, bForceSticky)) : TEXT(""));
		}
		if (bImmune != Skin.bImmune)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: pawn %d (%s) shock protection edge %s%s"), Proxy->GetActorId(), Proxy->GetKind() == 1 ? TEXT("merc") : TEXT("spy"),
				bImmune ? TEXT("on") : TEXT("off"),
				bImmune ? *FString::Printf(TEXT(" (%s, %.0f%% of the body, %.1f s left)"), Says(bImmuneFlag, bForceImmune), Coverage * 100.0f, Row != nullptr ? Row->ShockSeconds : 0.0f) : TEXT(""));
		}
		if ((bForceWet || bWetFlag) != Skin.bWet)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: pawn %d (%s) wet %s%s"), Proxy->GetActorId(), Proxy->GetKind() == 1 ? TEXT("merc") : TEXT("spy"),
				(bForceWet || bWetFlag) ? TEXT("on") : TEXT("off"),
				(bForceWet || bWetFlag) ? *FString::Printf(TEXT(" (%s, heat %.2f)"), Says(bWetFlag, bForceWet), Row != nullptr ? Row->HeatIntensity : 1.0f) : TEXT(""));
		}

		// The simulation's own shock spark carries the blinking light (the effects draw it, blinked by the bridge);
		// only a skin forced by the switch, with no spark, gets one here.
		bOwnShockLight = bSticky && !bStickyFlag;
		Apply(*Proxy, Skin, bSticky, bImmune, bWet, Coverage, Row != nullptr ? Row->Cloak : 0.0f, DeltaSeconds);
		Skin.bWet = bForceWet || bWetFlag;
		Sticky += bSticky ? 1 : 0;
		Immune += bImmune ? 1 : 0;
		Wet += Skin.bWet ? 1 : 0;
	}

	// Pawns that are gone took their components with them.
	for (auto It = Skins.CreateIterator(); It; ++It)
	{
		if (!It.Value().Proxy.IsValid())
		{
			It.RemoveCurrent();
		}
	}

	const int32 Summary = Sticky | (Immune << 8) | (Wet << 16);
	const double Now = FPlatformTime::Seconds();
	if (Summary != LastSummary || (Summary != 0 && Now - LastSummaryLog > 5.0))
	{
		LastSummary = Summary;
		LastSummaryLog = Now;
		UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: sticky %d, immune %d, wet %d (exposure %.2f, picture scale %.3f)"), Sticky, Immune, Wet, FScctExposure::Get(), PictureScale);
	}

	PlaceSkinCamera();
}

void UScctPawnSkinsSubsystem::Apply(AScctPawnProxy& Proxy, FPawnSkin& Skin, bool bSticky, bool bImmune, bool bWet, float Coverage, float Cloak, float DeltaSeconds)
{
	UPoseableMeshComponent* Body = Skin.Body.Get();
	if (Body == nullptr)
	{
		return;
	}

	// The body as the frame draws it: a body the goggles hide or lay over the scene from the custom depth only is
	// left alone, so nothing of it shows where the mode says nothing is.
	const bool bDrawn = Proxy.IsShown() && Body->bRenderInMainPass;

	if (bSticky && !Skin.bSticky)
	{
		Skin.StickySince = GetWorld()->GetTimeSeconds();
	}
	Skin.bSticky = bSticky;
	Skin.bImmune = bImmune;

	// Per slot: the Stickess skin while shocked, else the wet one (not over the camo suit, and not over the
	// protection edge, the cue that matters more).
	const bool bWetShown = bWet && !bSticky && !bImmune && Cloak < 0.01f;
	const TArray<FSlotSkins>& Slots = SlotsOf(*Body);
	Skin.SlotOverlays.SetNum(Slots.Num());
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		UMaterialInterface* Wanted = nullptr;
		if (bDrawn && bSticky && Slots[Slot].bSticky)
		{
			Wanted = StickyMaterial;
		}
		else if (bDrawn && bWetShown && WetMaterials.IsValidIndex(Slots[Slot].Wet))
		{
			Wanted = WetMaterials[Slots[Slot].Wet];
		}
		if (Skin.SlotOverlays[Slot].Get() != Wanted)
		{
			Body->SetOverlayMaterial(Wanted, true, Slot);
			Skin.SlotOverlays[Slot] = Wanted;
		}
	}

	UpdateEdge(Proxy, *Body, Skin, bDrawn && bImmune, Coverage, DeltaSeconds);
	UpdateLight(Proxy, Skin, bSticky && bOwnShockLight && Proxy.IsShown());
}

void UScctPawnSkinsSubsystem::UpdateEdge(AScctPawnProxy& Proxy, UPoseableMeshComponent& Body, FPawnSkin& Skin, bool bShown, float Coverage, float DeltaSeconds)
{
	if (!bShown)
	{
		if (Skin.bEdgeShown)
		{
			Body.SetOverlayMaterial(nullptr);
			Skin.bEdgeShown = false;
		}
		return;
	}

	UMaterialInstanceDynamic* Edge = Skin.Edge.Get();
	if (Edge == nullptr)
	{
		Edge = UMaterialInstanceDynamic::Create(EdgeMaterial, &Body);
		Skin.Edge = Edge;
	}
	if (!Skin.bEdgeShown || Body.GetOverlayMaterial() != Edge)
	{
		Body.SetOverlayMaterial(Edge);
		Skin.bEdgeShown = true;
	}

	// Head to feet as he is posed now, so standing and crouching both light the right share.
	const auto Height = [&Proxy, &Body](const TCHAR* Bone, float Fallback)
	{
		const FName Name = Proxy.FindMeshBoneName(Bone);
		return Name.IsNone() ? Fallback : static_cast<float>(Body.GetSocketLocation(Name).Z);
	};
	const float Base = static_cast<float>(Proxy.GetActorLocation().Z);
	const float Feet = FMath::Min(FMath::Min(Height(TEXT("B L Foot"), Base), Height(TEXT("B R Foot"), Base)),
		FMath::Min(Height(TEXT("B L Toe0"), Base), Height(TEXT("B R Toe0"), Base))) - 3.5f;
	const float Head = FMath::Max(Feet + 10.0f, Height(TEXT("B Head"), Base + 160.0f) + 14.0f);

	Skin.PulsePhase = FMath::Fmod(Skin.PulsePhase + FMath::Max(DeltaSeconds, 0.0f), PulseSeconds);
	Edge->SetScalarParameterValue(CoverageName, Coverage);
	Edge->SetScalarParameterValue(FeetZName, Feet);
	Edge->SetScalarParameterValue(HeadZName, Head);
	Edge->SetScalarParameterValue(PulseName, 0.72f + 0.28f * FMath::Cos(Skin.PulsePhase * UE_TWO_PI / PulseSeconds));
	Edge->SetScalarParameterValue(OpacityName, 1.0f);
	Edge->SetScalarParameterValue(PictureScaleName, PictureScale);
	const FName* const TuneNames[] = { &FillName, &RimName, &StrengthName };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(EdgeTune); ++Index)
	{
		if (EdgeTune[Index] >= 0.0f)
		{
			Edge->SetScalarParameterValue(*TuneNames[Index], EdgeTune[Index]);
		}
	}
}

void UScctPawnSkinsSubsystem::UpdateLight(AScctPawnProxy& Proxy, FPawnSkin& Skin, bool bOn)
{
	UPointLightComponent* Light = Skin.Light.Get();
	if (!bOn)
	{
		if (Light != nullptr)
		{
			Light->DestroyComponent();
			Skin.Light = nullptr;
		}
		return;
	}

	if (Light == nullptr)
	{
		Light = NewObject<UPointLightComponent>(&Proxy, NAME_None, RF_Transient);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(ShockLightBrightness * SkinCandelasPerBrightness);
		Light->SetLightColor(ShockLightColor());
		Light->SetAttenuationRadius(LightRadiusUnit * (ShockLightRadius + 1.0f));
		Light->SetCastShadows(false);
		Light->RegisterComponent();
		if (!Proxy.AttachToBone(Light, ShockLightBone))
		{
			Light->AttachToComponent(Proxy.GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			Light->SetRelativeLocation(FVector(0.0, 0.0, 20.0));
		}
		Skin.Light = Light;
	}

	// LT_Blink: on for one period, off for the next, from the moment of the shock.
	const double Since = GetWorld()->GetTimeSeconds() - Skin.StickySince;
	const bool bLit = FMath::FloorToInt(Since / BlinkPeriod) % 2 == 0;
	if (Light->IsVisible() != bLit)
	{
		Light->SetVisibility(bLit);
	}

	const double Now = FPlatformTime::Seconds();
	if (Now - LastBlinkLog > 2.0)
	{
		LastBlinkLog = Now;
		UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: pawn %d blue light %s at %s, %.1f s into the shock"), Proxy.GetActorId(), bLit ? TEXT("on") : TEXT("off"),
			*Light->GetComponentLocation().ToCompactString(), Since);
	}
}

void UScctPawnSkinsSubsystem::Remove(FPawnSkin& Skin)
{
	if (UPoseableMeshComponent* Body = Skin.Body.Get())
	{
		for (int32 Slot = 0; Slot < Skin.SlotOverlays.Num(); ++Slot)
		{
			if (Skin.SlotOverlays[Slot].IsValid())
			{
				Body->SetOverlayMaterial(nullptr, true, Slot);
			}
		}
		if (Skin.bEdgeShown)
		{
			Body->SetOverlayMaterial(nullptr);
		}
	}
	if (UPointLightComponent* Light = Skin.Light.Get())
	{
		Light->DestroyComponent();
	}
	Skin = FPawnSkin();
}

void UScctPawnSkinsSubsystem::RemoveAll()
{
	for (TPair<int32, FPawnSkin>& Pair : Skins)
	{
		Remove(Pair.Value);
	}
	Skins.Reset();
	LastSummary = -1;
}

void UScctPawnSkinsSubsystem::OnMatchStopped()
{
	RemoveAll();
	SkinCameraTarget = 0;
}

void UScctPawnSkinsSubsystem::PlaceSkinCamera()
{
	const UScctSessionSubsystem* Match = Session.Get();
	if (SkinCamera == 0 || Match == nullptr)
	{
		return;
	}
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	AScctViewPawn* ViewPawn = Player != nullptr ? Cast<AScctViewPawn>(Player->GetPawn()) : nullptr;
	if (ViewPawn == nullptr)
	{
		return;
	}

	// The pawn it follows, while he lives; else the nearest other pawn of the side asked for.
	const AScctPawnProxy* Target = SkinCameraTarget != 0 ? Match->FindProxy(SkinCameraTarget) : nullptr;
	if (Target == nullptr || !Target->IsShown())
	{
		Target = nullptr;
		const AScctPawnProxy* Local = Match->FindLocalProxy();
		const FVector From = Local != nullptr ? Local->GetActorLocation() : ViewPawn->GetActorLocation();
		double Best = TNumericLimits<double>::Max();
		for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Match->GetProxies())
		{
			const AScctPawnProxy* Proxy = Pair.Value;
			if (!IsValid(Proxy) || Proxy->IsLocal() || !Proxy->IsShown() || (SkinCamera == 2 && Proxy->GetKind() != 0) || (SkinCamera == 3 && Proxy->GetKind() != 1))
			{
				continue;
			}
			const double Distance = FVector::DistSquared(Proxy->GetActorLocation(), From);
			if (Distance < Best)
			{
				Best = Distance;
				Target = Proxy;
			}
		}
		SkinCameraTarget = Target != nullptr ? Target->GetActorId() : 0;
		if (Target != nullptr)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: -ScctSkinCam looks at pawn %d (%s)"), Target->GetActorId(), Target->GetKind() == 1 ? TEXT("merc") : TEXT("spy"));
		}
	}
	if (Target == nullptr)
	{
		return;
	}

	// 2 m from his chest (the spine bone as posed, so a crouching pawn is framed too) and a little above it, in front
	// of him where the room allows, else from the side with the most room; looking at him.
	const FScctBridgePawn& Pawn = Target->GetPawn();
	const FName Spine = Target->FindMeshBoneName(TEXT("B Spine2"));
	const FVector Chest = !Spine.IsNone() && Target->GetBodyMesh() != nullptr
		? Target->GetBodyMesh()->GetSocketLocation(Spine) : Target->GetActorLocation() + FVector(0.0, 0.0, 20.0);
	const double Now = FPlatformTime::Seconds();
	FCollisionQueryParams Query(TEXT("ScctSkinCam"), false, Target);
	Query.AddIgnoredActor(ViewPawn);
	TArray<AActor*> Carried;
	Target->GetAttachedActors(Carried, true, true);
	Query.AddIgnoredActors(Carried);
	FVector Eye = Chest;
	double Room = -1.0;
	for (const float Turn : { 0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 110.0f, -110.0f, 180.0f })
	{
		const FVector Want = Chest + FRotator(0.0, Pawn.Yaw + Turn, 0.0).Vector() * 200.0 + FVector(0.0, 0.0, 40.0);
		// Only the level's own geometry stands in the way: volumes and effect actors are not walls, and neither is a
		// mesh the pawn stands inside (the trace starts in it).
		FHitResult Hit;
		bool bBlocked = false;
		for (int32 Try = 0; Try < 4; ++Try)
		{
			bBlocked = GetWorld()->LineTraceSingleByObjectType(Hit, Chest, Want, FCollisionObjectQueryParams(ECC_WorldStatic), Query);
			if (!bBlocked || !Hit.bStartPenetrating || Hit.GetActor() == nullptr)
			{
				break;
			}
			Query.AddIgnoredActor(Hit.GetActor());
			bBlocked = false;
		}
		const double Reach = bBlocked ? FMath::Max(Hit.Distance - 20.0, 40.0) : 200.0;
		if (bBlocked && Turn == 0.0f && Now - LastCameraLog > 3.0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: -ScctSkinCam in front of pawn %d blocked by %s at %.0f cm"), Target->GetActorId(), *GetNameSafe(Hit.GetActor()), Hit.Distance);
		}
		if (Reach > Room + 1.0)
		{
			Room = Reach;
			Eye = Chest + (Want - Chest).GetSafeNormal() * Reach;
		}
		if (Reach >= 199.0)
		{
			break;
		}
	}

	if (Now - LastCameraLog > 3.0)
	{
		LastCameraLog = Now;
		UE_LOG(LogOpenSCCT, Log, TEXT("PawnSkins: -ScctSkinCam at %s, %.0f cm from pawn %d's chest %s (pawn at %s)"), *Eye.ToCompactString(), Room,
			Target->GetActorId(), *Chest.ToCompactString(), *Target->GetActorLocation().ToCompactString());
	}

	FScctBridgeView View = {};
	View.X = Eye.X;
	View.Y = Eye.Y;
	View.Z = Eye.Z;
	const FRotator Look = (Chest - Eye).Rotation();
	View.Pitch = Look.Pitch;
	View.Yaw = Look.Yaw;
	View.Roll = 0.0f;
	View.Fov = 70.0f;
	View.Valid = 1;
	ViewPawn->ApplyView(View);
}
