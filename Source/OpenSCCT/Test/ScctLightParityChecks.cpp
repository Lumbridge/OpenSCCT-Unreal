// The light parity measurement's switch test and tone ramp (Test/ScctLightParitySubsystem.h says what each measures).

#include "Test/ScctLightParitySubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "OpenSCCT.h"
#include "Play/ScctExposure.h"
#include "Play/ScctLevelStateDebug.h"
#include "Play/ScctLightPlan.h"
#include "Play/ScctSessionSubsystem.h"

namespace
{
	constexpr int32 ParityMaxSwitches = 16;

	// The tone ramp's board: patches of ToneSize cm, ToneStep apart, ToneColumns to a row, ToneDistance in front of the camera.
	constexpr float ToneDistance = 100.0f;
	constexpr float ToneSize = 12.0f;
	constexpr float ToneStep = 17.0f;
	constexpr int32 ToneColumns = 6;

	// Display values of the original, of 255. Up to 255 a texture under a light term of up to 1; above, the overbright
	// range the term reaches (it caps at 2 = 510) before the original's frame buffer clips it.
	const float ToneCodes[] = {
		0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f,
		8.0f, 12.0f, 16.0f, 24.0f, 32.0f, 48.0f,
		64.0f, 80.0f, 96.0f, 128.0f, 160.0f, 200.0f,
		224.0f, 255.0f, 319.0f, 383.0f, 446.0f, 510.0f,
	};

	const FName ToneLevelParameter(TEXT("Level"));

	// The baked cards (FToneCard): the texture's display value and the lightmap's stored value, of 255. The original
	// shows texture x min(2, 2 x stored / 255): 24, 48, 80, 128 on the mid-grey texture, 80 on white, and 128 on a
	// dark texture under the term's cap of 2.
	const float ToneCardValues[][2] = {
		{ 128.0f, 24.0f }, { 128.0f, 48.0f }, { 128.0f, 80.0f }, { 128.0f, 127.0f }, { 255.0f, 40.0f }, { 64.0f, 255.0f },
	};
	const TCHAR* const ToneSurfaceMaster = TEXT("/Game/OpenSCCT/Materials/M_ScctSurface.M_ScctSurface");

	/** A texture of one sRGB grey. */
	UTexture2D* ToneGreyTexture(UObject* Outer, float Value)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
		if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}
		const uint8 Byte = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Value), 0, 255));
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		uint8* Pixels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Pixel = 0; Pixel < 16; ++Pixel)
		{
			Pixels[Pixel * 4 + 0] = Pixels[Pixel * 4 + 1] = Pixels[Pixel * 4 + 2] = Byte;
			Pixels[Pixel * 4 + 3] = 255;
		}
		Mip.BulkData.Unlock();
		Texture->SRGB = true;
		Texture->NeverStream = true;
		Texture->Filter = TF_Nearest;
		Texture->UpdateResource();
		return Texture;
	}

	float ToneLuma(const FColor& Pixel)
	{
		return 0.3f * Pixel.R + 0.59f * Pixel.G + 0.11f * Pixel.B;
	}

	FString SwitchState(const TCHAR* State, const FScctLightReading& Reading, float Predicted, float Picture, float Around, float Exposure)
	{
		return FString::Printf(TEXT(" %s sim=%.3f facing=%.3f pred=%.1f sphere=%.1f ground=%.1f exposure=%.2f"), State, Reading.Level, Reading.LevelFacing,
			Predicted, Picture, Around, Exposure);
	}
}

void UScctLightParitySubsystem::AddSwitchSpots()
{
	Switches.SetNumZeroed(ParityMaxSwitches);
	Switches.SetNum(FMath::Max(Api->FindSwitchSpots(Switches.GetData(), Switches.Num()), 0));
	for (int32 Light = 0; Light < Switches.Num(); ++Light)
	{
		const FScctLightSwitch& Switch = Switches[Light];
		for (int32 Far = 0; Far < (Switch.HasFar != 0 ? 2 : 1); ++Far)
		{
			FSpot& Entry = Spots.AddDefaulted_GetRef();
			Entry.Spot.X = Far != 0 ? Switch.FarX : Switch.NearX;
			Entry.Spot.Y = Far != 0 ? Switch.FarY : Switch.NearY;
			Entry.Spot.Z = Far != 0 ? Switch.FarZ : Switch.NearZ;
			Entry.Spot.Level = Far != 0 ? Switch.FarLevel : Switch.NearLevel;
			Entry.Spot.Kind = -1;
			Entry.Switch = Light;
			Entry.bFar = Far != 0;
			PrepareSpot(Entry);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("LightParity: switch test on %d switched lights (%d spots)."), Switches.Num(), Spots.Num() - OrdinarySpots);
}

bool UScctLightParitySubsystem::FireSwitches()
{
	FString Error;
	const FScctWorldDebugApi* Debug = static_cast<const FScctWorldDebugApi*>(FOpenSCCTModule::Get().FindFeatureTable(
		TEXT("OpenScct.UnrealBridge.WorldDebugExports"), sizeof(FScctWorldDebugApi), ScctWorldDebugApiVersion, Error));
	const bool bQueued = Debug != nullptr && Debug->FireLights != nullptr && Debug->FireLights() != 0;
	if (!bQueued)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("LightParity: the switched lights could not be fired (%s); the switch test's later states are the first again."),
			Debug == nullptr ? *Error : TEXT("no local server"));
	}
	return bQueued;
}

void UScctLightParitySubsystem::WriteSwitches() const
{
	if (Switches.Num() == 0)
	{
		return;
	}
	FString Table = TEXT("name,light,index,role,on,effect,radius,brightness,distance,x,y,z,")
		TEXT("sim_first,facing_first,pred_first,sphere_first,ground_first,exposure_first,")
		TEXT("sim_fired,facing_fired,pred_fired,sphere_fired,ground_fired,exposure_fired,")
		TEXT("sim_back,facing_back,pred_back,sphere_back,ground_back,exposure_back,plan,seen\n");
	const int32 Plan = FScctLightPlan::Active();
	for (int32 Spot = OrdinarySpots; Spot < Spots.Num(); ++Spot)
	{
		const FSpot& At = Spots[Spot];
		const FScctLightSwitch& Switch = Switches[At.Switch];
		const FString LightName = ANSI_TO_TCHAR(Switch.Name);
		const double Distance = FVector::Dist(At.Centre, FVector(Switch.LightX, Switch.LightY, Switch.LightZ));
		UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_SWITCH %s %02d light=%s %s was=%s radius=%.0f distance=%.0f seen=%d%s%s%s"),
			*Name, Spot, *LightName, At.bFar ? TEXT("far") : TEXT("near"), Switch.On != 0 ? TEXT("on") : TEXT("off"), Switch.Radius, Distance,
			At.Sphere.bSeen && At.Switched.bSeen && At.Back.bSeen ? 1 : 0,
			*SwitchState(TEXT("first:"), At.Reading, At.Predicted, At.Sphere.Picture, At.Sphere.PictureAround, At.Sphere.Exposure),
			*SwitchState(TEXT("fired:"), At.ReadingSwitched, At.PredictedSwitched, At.Switched.Picture, At.Switched.PictureAround, At.Switched.Exposure),
			*SwitchState(TEXT("back:"), At.ReadingBack, At.PredictedBack, At.Back.Picture, At.Back.PictureAround, At.Back.Exposure));
		Table += FString::Printf(TEXT("%s,%s,%d,%s,%d,%d,%.0f,%.0f,%.0f,%.1f,%.1f,%.1f,"), *Name, *LightName, Spot, At.bFar ? TEXT("far") : TEXT("near"), Switch.On,
			Switch.Effect, Switch.Radius, Switch.Brightness, Distance, At.Spot.X, At.Spot.Y, At.Spot.Z);
		Table += FString::Printf(TEXT("%.4f,%.4f,%.2f,%.2f,%.2f,%.4f,"), At.Reading.Level, At.Reading.LevelFacing, At.Predicted, At.Sphere.Picture,
			At.Sphere.PictureAround, At.Sphere.Exposure);
		Table += FString::Printf(TEXT("%.4f,%.4f,%.2f,%.2f,%.2f,%.4f,"), At.ReadingSwitched.Level, At.ReadingSwitched.LevelFacing, At.PredictedSwitched,
			At.Switched.Picture, At.Switched.PictureAround, At.Switched.Exposure);
		Table += FString::Printf(TEXT("%.4f,%.4f,%.2f,%.2f,%.2f,%.4f,%d,%d\n"), At.ReadingBack.Level, At.ReadingBack.LevelFacing, At.PredictedBack,
			At.Back.Picture, At.Back.PictureAround, At.Back.Exposure, Plan, At.Sphere.bSeen && At.Switched.bSeen && At.Back.bSeen ? 1 : 0);
	}
	FFileHelper::SaveStringToFile(Table, *(Directory / (Name + TEXT("_switch.csv"))));
}

bool UScctLightParitySubsystem::BeginTone()
{
#if WITH_EDITOR
	if (Spots.Num() == 0 || Rig == nullptr)
	{
		return false;
	}

	// One unlit material whose glow is a parameter: a patch is scene light of a known amount, whatever lights the level.
	UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterialExpressionScalarParameter* Level = NewObject<UMaterialExpressionScalarParameter>(Material);
	Level->ParameterName = ToneLevelParameter;
	Level->DefaultValue = 0.0f;
	Material->GetExpressionCollection().AddExpression(Level);
	Material->GetEditorOnlyData()->EmissiveColor.Expression = Level;
	Material->SetShadingModel(MSM_Unlit);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->EnsureIsComplete();
	ToneMaterials.Add(Material);

	// The board needs open room in front of a camera: the first spot whose camera sees its sphere and has a clear
	// line (through the simulation's collision) to the board's middle and to a little beyond each of its corners.
	const int32 Count = UE_ARRAY_COUNT(ToneCodes);
	UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, ToneSurfaceMaster);
	const int32 CardCount = Surface != nullptr ? static_cast<int32>(UE_ARRAY_COUNT(ToneCardValues)) : 0;
	const int32 RampRows = (Count + ToneColumns - 1) / ToneColumns;
	const int32 Rows = RampRows + (CardCount + ToneColumns - 1) / ToneColumns;
	const FVector2D Board(ToneColumns * ToneStep + 6.0f, Rows * ToneStep + 6.0f);
	int32 Chosen = INDEX_NONE;
	for (int32 Spot = 0; Spot < OrdinarySpots && Chosen == INDEX_NONE; ++Spot)
	{
		const FTransform& Camera = Spots[Spot].Camera;
		bool bOpen = Spots[Spot].Sphere.bSeen;
		for (int32 Corner = 0; Corner < 5 && bOpen; ++Corner)
		{
			const FVector To = Camera.GetLocation() + Camera.GetUnitAxis(EAxis::X) * (ToneDistance + 20.0f) +
				(Corner == 4 ? FVector::ZeroVector : Camera.GetUnitAxis(EAxis::Y) * ((Corner & 1 ? 0.5 : -0.5) * (Board.X + 20.0)) +
					Camera.GetUnitAxis(EAxis::Z) * ((Corner & 2 ? 0.5 : -0.5) * (Board.Y + 20.0)));
			float Met = 0.0f;
			bOpen = !FScctExposure::TraceLevel(Camera.GetLocation(), To, Met);
		}
		if (bOpen)
		{
			Chosen = Spot;
		}
	}
	if (Chosen == INDEX_NONE)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("LightParity: no spot has open room for the tone ramp's board; it stands at the first spot and may be cut by a wall."));
		Chosen = 0;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("LightParity: the tone ramp stands at spot %02d."), Chosen);
	ToneCamera = Spots[Chosen].Camera;
	const FVector Eye = ToneCamera.GetLocation();
	const FVector Forward = ToneCamera.GetUnitAxis(EAxis::X);
	const FVector Right = ToneCamera.GetUnitAxis(EAxis::Y);
	const FVector Up = ToneCamera.GetUnitAxis(EAxis::Z);
	// The engine's plane lies in its own XY and faces +Z: turned to face the camera.
	const FQuat Facing = FRotationMatrix::MakeFromZY(-Forward, Right).ToQuat();
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));

	const auto AddPlane = [this, Plane, Material, &Facing](const FVector& Centre, const FVector2D& Size, float Emissive)
	{
		UStaticMeshComponent* Patch = NewObject<UStaticMeshComponent>(Rig);
		Patch->SetMobility(EComponentMobility::Movable);
		Patch->SetStaticMesh(Plane);
		UMaterialInstanceDynamic* Glow = UMaterialInstanceDynamic::Create(Material, this);
		Glow->SetScalarParameterValue(ToneLevelParameter, Emissive);
		ToneMaterials.Add(Glow);
		Patch->SetMaterial(0, Glow);
		Patch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Patch->SetCastShadow(false);
		Patch->bAffectDynamicIndirectLighting = false;
		Patch->bAffectDistanceFieldLighting = false;
		Patch->SetWorldTransform(FTransform(Facing, Centre, FVector(Size.X / 100.0, Size.Y / 100.0, 1.0)));
		Patch->RegisterComponent();
		ToneBoard.Add(Patch);
	};

	// A black board behind the patches, so what is behind the board is not what a patch is read against.
	// (The plane's own X is the camera's up and down after the turn, its Y the camera's right.)
	AddPlane(Eye + Forward * (ToneDistance + 1.0f), FVector2D(Board.Y, Board.X), 0.0f);
	TonePatches.Reset();
	for (int32 Patch = 0; Patch < Count; ++Patch)
	{
		if (ToneCodes[Patch] > ToneMax)
		{
			continue;
		}
		const int32 Column = Patch % ToneColumns, Row = Patch / ToneColumns;
		FTonePatch& Entry = TonePatches.AddDefaulted_GetRef();
		Entry.Code = ToneCodes[Patch];
		Entry.Emissive = FMath::Pow(Entry.Code / 255.0f, 2.2f);
		// Darkest at the top left, brightest at the bottom right.
		Entry.Centre = Eye + Forward * ToneDistance + Right * ((Column - 0.5f * (ToneColumns - 1)) * ToneStep) + Up * ((0.5f * (Rows - 1) - Row) * ToneStep);
		AddPlane(Entry.Centre, FVector2D(ToneSize, ToneSize), Entry.Emissive);
	}

	// The baked cards, a row under the ramp: the surface material as a level surface has it, on no lighting channel
	// (so no lamp lights it), with the released picture's fill and floor (which the lighting plan's formula ignores).
	ToneCards.Reset();
	for (int32 Card = 0; Card < CardCount; ++Card)
	{
		const int32 Column = Card % ToneColumns, Row = RampRows + Card / ToneColumns;
		FToneCard& Entry = ToneCards.AddDefaulted_GetRef();
		Entry.Emissive = ToneCardValues[Card][0];
		Entry.Stored = ToneCardValues[Card][1];
		Entry.Code = Entry.Emissive * FMath::Min(2.0f, 2.0f * Entry.Stored / 255.0f);
		Entry.Centre = Eye + Forward * ToneDistance + Right * ((Column - 0.5f * (ToneColumns - 1)) * ToneStep) + Up * ((0.5f * (Rows - 1) - Row) * ToneStep);
		UTexture2D* Texture = ToneGreyTexture(this, Entry.Emissive);
		UTexture2D* Page = ToneGreyTexture(this, Entry.Stored);
		UMaterialInstanceDynamic* Lit = UMaterialInstanceDynamic::Create(Surface, this);
		if (Texture == nullptr || Page == nullptr || Lit == nullptr)
		{
			ToneCards.Pop();
			continue;
		}
		ToneTextures.Add(Texture);
		ToneTextures.Add(Page);
		Lit->SetTextureParameterValue(TEXT("BaseColor"), Texture);
		Lit->SetTextureParameterValue(TEXT("Lightmap"), Page);
		Lit->SetScalarParameterValue(TEXT("BakedFill"), 0.5f);
		Lit->SetScalarParameterValue(TEXT("AmbientFloor"), 0.012f);
		Lit->SetScalarParameterValue(TEXT("Specular"), 0.0f);
		ToneMaterials.Add(Lit);
		UStaticMeshComponent* Patch = NewObject<UStaticMeshComponent>(Rig);
		Patch->SetMobility(EComponentMobility::Movable);
		Patch->SetStaticMesh(Plane);
		Patch->SetMaterial(0, Lit);
		Patch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Patch->SetCastShadow(false);
		Patch->SetLightingChannels(false, false, false);
		Patch->bAffectDynamicIndirectLighting = false;
		Patch->bAffectDistanceFieldLighting = false;
		Patch->SetWorldTransform(FTransform(Facing, Entry.Centre, FVector(ToneSize / 100.0, ToneSize / 100.0, 1.0)));
		Patch->RegisterComponent();
		ToneBoard.Add(Patch);
	}

	Sphere->SetVisibility(false);
	if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->SetCheckCamera(ToneCamera);
	}
	return TonePatches.Num() > 0;
#else
	UE_LOG(LogOpenSCCT, Warning, TEXT("LightParity: this build cannot make the tone ramp's material; no ramp."));
	return false;
#endif
}

void UScctLightParitySubsystem::MeasureTone(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	int32 ViewWidth = 0, ViewHeight = 0;
	if (Player != nullptr)
	{
		Player->GetViewportSize(ViewWidth, ViewHeight);
	}
	if (Player == nullptr || ViewWidth <= 0)
	{
		return;
	}
	const double Scale = static_cast<double>(Width) / ViewWidth;
	const int32 Plan = FScctLightPlan::Active();
	FString Table = TEXT("name,code,emissive,picture,r,g,b,exposure,plan\n");
	FString CardTable = TEXT("name,expected,texture,stored,picture,r,g,b,exposure,plan\n");
	TArray<const FTonePatch*> Read;
	for (const FTonePatch& Patch : TonePatches)
	{
		Read.Add(&Patch);
	}
	for (const FToneCard& Card : ToneCards)
	{
		Read.Add(&Card);
	}
	for (int32 Entry = 0; Entry < Read.Num(); ++Entry)
	{
		const FTonePatch& Patch = *Read[Entry];
		const FToneCard* Card = Entry >= TonePatches.Num() ? &ToneCards[Entry - TonePatches.Num()] : nullptr;
		FVector2D Centre, Beside;
		if (!Player->ProjectWorldLocationToScreen(Patch.Centre, Centre, false) ||
			!Player->ProjectWorldLocationToScreen(Patch.Centre + ToneCamera.GetUnitAxis(EAxis::Y) * 10.0, Beside, false))
		{
			continue;
		}
		// The middle of the patch: 0.3 of its size each way, clear of its edge and of the glow round it.
		const double Reach = 0.3 * ToneSize * FVector2D::Distance(Centre, Beside) * Scale / 10.0;
		const int32 X0 = FMath::Max(0, FMath::FloorToInt32(Centre.X * Scale - Reach)), X1 = FMath::Min(Width - 1, FMath::CeilToInt32(Centre.X * Scale + Reach));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt32(Centre.Y * Scale - Reach)), Y1 = FMath::Min(Height - 1, FMath::CeilToInt32(Centre.Y * Scale + Reach));
		double Sum = 0.0, R = 0.0, G = 0.0, B = 0.0;
		int32 Count = 0;
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FColor& Pixel = Bitmap[Y * Width + X];
				Sum += ToneLuma(Pixel);
				R += Pixel.R;
				G += Pixel.G;
				B += Pixel.B;
				++Count;
			}
		}
		if (Count == 0)
		{
			continue;
		}
		if (Card != nullptr)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_CARD %s texture=%.0f stored=%.0f original=%.1f picture=%.1f rgb=%.1f,%.1f,%.1f exposure=%.3f"), *Name, Card->Emissive, Card->Stored,
				Card->Code, Sum / Count, R / Count, G / Count, B / Count, ToneExposure);
			CardTable += FString::Printf(TEXT("%s,%.2f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.4f,%d\n"), *Name, Card->Code, Card->Emissive, Card->Stored, Sum / Count, R / Count, G / Count,
				B / Count, ToneExposure, Plan);
			continue;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_TONE %s code=%.0f emissive=%.5f picture=%.1f rgb=%.1f,%.1f,%.1f exposure=%.3f"), *Name, Patch.Code, Patch.Emissive,
			Sum / Count, R / Count, G / Count, B / Count, ToneExposure);
		Table += FString::Printf(TEXT("%s,%.0f,%.6f,%.2f,%.2f,%.2f,%.2f,%.4f,%d\n"), *Name, Patch.Code, Patch.Emissive, Sum / Count, R / Count, G / Count, B / Count,
			ToneExposure, Plan);
	}
	FFileHelper::SaveStringToFile(Table, *(Directory / (Name + TEXT("_tone.csv"))));
	if (ToneCards.Num() > 0)
	{
		FFileHelper::SaveStringToFile(CardTable, *(Directory / (Name + TEXT("_cards.csv"))));
	}

	TArray<FColor> Opaque = Bitmap;
	for (FColor& Pixel : Opaque)
	{
		Pixel.A = 255;
	}
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	FFileHelper::SaveArrayToFile(Png, *(Directory / (Name + TEXT("_tone.png"))));
}
