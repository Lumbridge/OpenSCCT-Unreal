#include "Test/ScctLightParitySubsystem.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Play/ScctExposure.h"
#include "Play/ScctLightAudit.h"
#include "Play/ScctLightPlan.h"
#include "Play/ScctLightRules.h"
#include "Play/ScctPawnLighting.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "TextureResource.h"
#include "UnrealClient.h"

namespace
{
	// A standing pawn (the simulation's AiK): its centre above the floor and its radius, cm.
	constexpr float LightPawnHeight = 87.5f;
	constexpr float LightPawnRadius = 35.0f;

	// The grey sphere: the engine's sphere is 100 cm across.
	constexpr float LightSphereRadius = 15.0f;
	constexpr float LightSphereAlbedo = 0.18f;

	// The camera: as far off the spot as there is room for, within these (cm), on the side with the most room.
	constexpr float LightCameraNearest = 110.0f;
	constexpr float LightCameraFarthest = 380.0f;
	constexpr float LightCameraProbe = 450.0f;
	constexpr int32 LightCameraSides = 16;

	// The HDR capture's size: small, it is read back whole.
	constexpr int32 LightCaptureWidth = 480;
	constexpr int32 LightCaptureHeight = 270;

	constexpr double LightMatchSeconds = 5.0;
	constexpr double LightReadSeconds = 20.0;

	// After the switched lights were fired: a light going out flickers for half a second, and the picture follows.
	constexpr double LightFireSeconds = 3.0;
	constexpr int32 LightMaxSpots = 256;

	// The sphere's albedo as a display value (a texture the original would draw at 0.46 of full white under a light
	// term of 1), and the share of its radius that is measured: what PredictSphere is asked for.
	constexpr float LightSphereDisplay = 0.46f;
	constexpr float LightSphereDisc = 0.6f;

	const TCHAR* LightKindName(int32 Kind)
	{
		switch (Kind)
		{
		case EScctLightSpotKind::Dark: return TEXT("dark");
		case EScctLightSpotKind::Bright: return TEXT("bright");
		case EScctLightSpotKind::Spread: return TEXT("spread");
		case EScctLightSpotKind::EdgeLit: return TEXT("edge-lit");
		case EScctLightSpotKind::EdgeDark: return TEXT("edge-dark");
		default: return TEXT("?");
		}
	}
}

bool UScctLightParitySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Value;
	return (FParse::Param(FCommandLine::Get(), TEXT("ScctLightParity")) || FParse::Value(FCommandLine::Get(), TEXT("ScctLightParity="), Value)) &&
		Super::ShouldCreateSubsystem(Outer);
}

bool UScctLightParitySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UScctLightParitySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctLightParitySubsystem, STATGROUP_Tickables);
}

void UScctLightParitySubsystem::Deinitialize()
{
	if (CaptureHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
		CaptureHandle.Reset();
	}
	Super::Deinitialize();
}

UMaterialInterface* UScctLightParitySubsystem::MakeGrey()
{
	if (FScctLightPlan::IsOn())
	{
		UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Materials/M_ScctSurface.M_ScctSurface"));
		UTexture2D* White = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		if (Surface && White)
		{
			UMaterialInstanceDynamic* GreySurface = UMaterialInstanceDynamic::Create(Surface, this);
			GreySurface->SetTextureParameterValue(TEXT("BaseColor"), White);
			GreySurface->SetVectorParameterValue(TEXT("Tint"), FLinearColor(LightSphereAlbedo, LightSphereAlbedo, LightSphereAlbedo));
			GreySurface->SetScalarParameterValue(TEXT("Specular"), 0.0f);
			GreySurface->SetScalarParameterValue(TEXT("RoughnessDark"), 1.0f);
			GreySurface->SetScalarParameterValue(TEXT("RoughnessBright"), 1.0f);
			return GreySurface;
		}
	}
#if WITH_EDITOR
	// A matte grey of a known albedo, made here so the measurement needs no asset: no highlight, no texture.
	UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterialExpressionConstant3Vector* Colour = NewObject<UMaterialExpressionConstant3Vector>(Material);
	Colour->Constant = FLinearColor(LightSphereAlbedo, LightSphereAlbedo, LightSphereAlbedo);
	UMaterialExpressionConstant* Rough = NewObject<UMaterialExpressionConstant>(Material);
	Rough->R = 1.0f;
	UMaterialExpressionConstant* NoHighlight = NewObject<UMaterialExpressionConstant>(Material);
	NoHighlight->R = 0.0f;
	Material->GetExpressionCollection().AddExpression(Colour);
	Material->GetExpressionCollection().AddExpression(Rough);
	Material->GetExpressionCollection().AddExpression(NoHighlight);
	Material->GetEditorOnlyData()->BaseColor.Expression = Colour;
	Material->GetEditorOnlyData()->Roughness.Expression = Rough;
	Material->GetEditorOnlyData()->Specular.Expression = NoHighlight;
	if (FScctLightPlan::IsOn())
	{
		// Under the lighting plan the sphere is lit as a body is: a body's material takes no bounced or sky light
		// (the surface material's occlusion is 0 on what carries no baked light).
		UMaterialExpressionConstant* NoBounce = NewObject<UMaterialExpressionConstant>(Material);
		NoBounce->R = 0.0f;
		Material->GetExpressionCollection().AddExpression(NoBounce);
		Material->GetEditorOnlyData()->AmbientOcclusion.Expression = NoBounce;
	}
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->EnsureIsComplete();
	return Material;
#else
	UE_LOG(LogOpenSCCT, Warning, TEXT("LightParity: this build cannot make the grey material; the sphere has the engine's basic shape material (not 18 %% grey)."));
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
#endif
}

bool UScctLightParitySubsystem::Begin()
{
	UWorld* World = GetWorld();
	FString Error;
	Api = ScctFeature<FScctLightProbeApi>(&Error);
	if (Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("LightParity: the bridge has no light probe: %s"), *Error);
		return false;
	}

	FString Count;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctLightParity="), Count) && FCString::Atoi(*Count) > 0)
	{
		PerKind = FMath::Clamp(FCString::Atoi(*Count), 1, LightMaxSpots / 4);
	}
	FParse::Value(FCommandLine::Get(), TEXT("ScctLightSettle="), SettleSeconds);
	bMeasureBody = !FParse::Param(FCommandLine::Get(), TEXT("ScctLightNoBody"));
	bAllFloor = FParse::Param(FCommandLine::Get(), TEXT("ScctLightAllFloor"));
	bSwitchTest = !FParse::Param(FCommandLine::Get(), TEXT("ScctLightNoSwitch"));
	bTone = !FParse::Param(FCommandLine::Get(), TEXT("ScctLightNoTone"));
	FParse::Value(FCommandLine::Get(), TEXT("ScctLightToneMax="), ToneMax);
	bSaveCapture = FParse::Param(FCommandLine::Get(), TEXT("ScctLightSaveScene"));
	Name = World->GetMapName();
	FParse::Value(FCommandLine::Get(), TEXT("ScctLightName="), Name);
	FString Tag;
	FParse::Value(FCommandLine::Get(), TEXT("ScctLightTag="), Tag);
	Name += Tag;
	Directory = FPaths::ProjectSavedDir() / TEXT("ScctLight");
	FParse::Value(FCommandLine::Get(), TEXT("ScctLightDir="), Directory);
	IFileManager::Get().MakeDirectory(*Directory, true);

	const int32 Lights = Api->Prepare();
	if (Lights <= 0)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("LightParity: the simulation has no light data for this map (%d)."), Lights);
		return false;
	}

	TArray<FScctLightSpot> Found;
	Found.SetNumZeroed(LightMaxSpots);
	const double Before = FPlatformTime::Seconds();
	Found.SetNum(FMath::Max(Api->FindSpots(PerKind, bAllFloor ? 1 : 0, Found.GetData(), Found.Num(), &Floor), 0));
	UE_LOG(LogOpenSCCT, Log, TEXT("LightParity: %s, scct.Light.Plan %d, %d lights in the simulation's field, %d spots chosen in %.1f s (%d of each kind asked)."),
		*Name, FScctLightPlan::Active(), Lights, Found.Num(), FPlatformTime::Seconds() - Before, PerKind);
	UE_LOG(LogOpenSCCT, Log, TEXT("LightParity: floor sampled: %s, %d of %d walkable nodes (%d on connected floor under a map light), grown from %d starts, objectives and switches."),
		Floor.PlayRule != 0 ? TEXT("the play floor") : TEXT("any connected floor under a map light"), Floor.PlayNodes, Floor.FloorNodes, Floor.LitFloorNodes, Floor.Anchors);
	if (Found.Num() == 0)
	{
		return false;
	}
	CheckPlanExports();

	for (const FScctLightSpot& Spot : Found)
	{
		FSpot& Entry = Spots.AddDefaulted_GetRef();
		Entry.Spot = Spot;
		PrepareSpot(Entry);
	}
	OrdinarySpots = Spots.Num();
	if (bSwitchTest)
	{
		AddSwitchSpots();
	}

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Rig = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	USceneComponent* Root = NewObject<USceneComponent>(Rig);
	Rig->SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
	Root->RegisterComponent();

	Grey = MakeGrey();
	Sphere = NewObject<UStaticMeshComponent>(Rig);
	Sphere->SetMobility(EComponentMobility::Movable);
	Sphere->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	Sphere->SetMaterial(0, Grey);
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sphere->SetWorldScale3D(FVector(LightSphereRadius / 50.0f));
	if (FScctLightRules::BodiesOnLampChannel())
	{
		// And from the lamps a body takes its light from under the plan (Play/ScctLightRules.h).
		Sphere->SetLightingChannels(true, true, false);
	}
	Sphere->RegisterComponent();

	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->InitCustomFormat(LightCaptureWidth, LightCaptureHeight, PF_FloatRGBA, true);
	Target->UpdateResourceImmediate(true);
	Capture = NewObject<USceneCaptureComponent2D>(Rig);
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->TextureTarget = Target;
	Capture->bCaptureEveryFrame = true;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	// The bounce light of the maps is traced in hardware; without this a capture gets none and an unlit sphere reads black.
	Capture->bUseRayTracingIfEnabled = true;
	Capture->RegisterComponent();

	CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UScctLightParitySubsystem::OnCaptured);
	return true;
}

void UScctLightParitySubsystem::PrepareSpot(FSpot& Entry)
{
	Entry.Centre = FVector(Entry.Spot.X, Entry.Spot.Y, Entry.Spot.Z + LightPawnHeight + 2.0f);

	// The side with the most room, looked for at chest height through the simulation's collision.
	const FVector Chest = Entry.Centre + FVector(0.0, 0.0, 30.0);
	float BestRoom = -1.0f;
	FVector BestSide = FVector::ForwardVector;
	for (int32 Side = 0; Side < LightCameraSides; ++Side)
	{
		const FVector Direction = FRotator(0.0, 360.0 * Side / LightCameraSides, 0.0).Vector();
		float Room = LightCameraProbe;
		FScctExposure::TraceLevel(Chest, Chest + Direction * LightCameraProbe, Room);
		if (Room > BestRoom + 1.0f)
		{
			BestRoom = Room;
			BestSide = Direction;
		}
	}
	Entry.CameraDistance = FMath::Clamp(BestRoom - 45.0f, LightCameraNearest, LightCameraFarthest);
	const FVector Eye = Chest + BestSide * Entry.CameraDistance + FVector(0.0, 0.0, 0.1 * Entry.CameraDistance);
	Entry.Camera = FTransform((Entry.Centre - Eye).Rotation(), Eye);
	Api->Read(Entry.Centre.X, Entry.Centre.Y, Entry.Centre.Z, LightPawnRadius, Eye.X, Eye.Y, Eye.Z, &Entry.Reading);
	if (PlanApi != nullptr)
	{
		const int32 AsField = EScctLightRigOptions::NoRuntimeLights | EScctLightRigOptions::PlacedOnly;
		Entry.bRig = PlanApi->GetPointRig(Entry.Centre.X, Entry.Centre.Y, Entry.Centre.Z, LightPawnRadius, Eye.X, Eye.Y, Eye.Z, AsField, &Entry.Rig) != 0 &&
			PlanApi->GetPointRig(Entry.Centre.X, Entry.Centre.Y, Entry.Centre.Z, LightPawnRadius, Eye.X, Eye.Y, Eye.Z, 0, &Entry.RigLive) != 0;
	}
	Entry.Predicted = Predict(Entry);
}

void UScctLightParitySubsystem::CheckPlanExports()
{
	// The lighting plan's exports: how the plan classes the map's lights, the table of them, and the rig against the
	// simulation's field on every sampled floor node (the same rules on the same file: the difference should be 0).
	PlanApi = ScctFeature<FScctLightPlanApi>();
	if (PlanApi == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("LightParity: the bridge has no light plan exports; the rig is not checked."));
		return;
	}
	FScctLightLevel Level = {};
	if (PlanApi->GetLevel(&Level) != 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_LIGHT_CLASSES %s %s"), *Name, *FScctLightAudit::DescribeLevel(Level));
	}

	TArray<FScctLightInfo> Lights;
	Lights.SetNumZeroed(FMath::Max(PlanApi->GetLights(nullptr, 0), 0));
	PlanApi->GetLights(Lights.GetData(), Lights.Num());
	const FScctWorldApi* World = ScctFeature<FScctWorldApi>();
	auto NameOf = [World](int32 Id)
	{
		TCHAR Buffer[128] = {};
		if (World != nullptr && Id != 0)
		{
			World->GetName(Id, Buffer, UE_ARRAY_COUNT(Buffer));
		}
		return FString(Buffer);
	};
	FString Table = TEXT("index,name,class,flags,effect,light_type,brightness,hue,saturation,light_radius,cone,world_radius,cone_degrees,zone,x,y,z,pitch,yaw,roll,")
		TEXT("r,g,b,gain,echelon_range,spot_width,spot_height,near_plane_ratio,normal_influence,apex_distance,attenuation,cookie,period,phase\n");
	for (int32 Light = 0; Light < Lights.Num(); ++Light)
	{
		const FScctLightInfo& L = Lights[Light];
		Table += FString::Printf(TEXT("%d,%s,%d,%d,%d,%d,%.1f,%d,%d,%.1f,%d,%.1f,%.2f,%d,%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.2f,%.1f,%.1f,%.1f,%.4f,%.2f,%.1f,%d,%s,%.1f,%.1f\n"),
			Light, *NameOf(L.NameId), L.Class, L.Flags, L.Effect, L.LightType, L.Brightness, L.Hue, L.Saturation, L.LightRadius, L.Cone, L.WorldRadius,
			L.ConeDegrees, L.Zone, L.X, L.Y, L.Z, L.Pitch, L.Yaw, L.Roll, L.R, L.G, L.B, L.Gain, L.EchelonRange, L.SpotWidth, L.SpotHeight,
			L.NearPlaneRatio, L.NormalInfluence, L.ApexDistance, L.Attenuation, *NameOf(L.CookieNameId), L.Period, L.Phase);
	}
	FFileHelper::SaveStringToFile(Table, *(Directory / (Name + TEXT("_lights.csv"))));

	TArray<FScctLightZone> Zones;
	Zones.SetNumZeroed(FMath::Max(PlanApi->GetZones(nullptr, 0), 0));
	PlanApi->GetZones(Zones.GetData(), Zones.Num());
	int32 Lit = 0;
	float Most = 0.0f;
	for (const FScctLightZone& Zone : Zones)
	{
		const float Ambient = 0.3f * Zone.R + 0.59f * Zone.G + 0.11f * Zone.B;
		Lit += Ambient > 0.0f && (Zone.Flags & 1) == 0 ? 1 : 0;
		Most = (Zone.Flags & 1) == 0 ? FMath::Max(Most, Ambient) : Most;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_LIGHT_ZONES %s zones=%d with_ambient=%d most_ambient=%.3f (the sky zone left out)"), *Name, Zones.Num(), Lit, Most);

	FScctLightRigCheck Check = {};
	const double Before = FPlatformTime::Seconds();
	if (Api->CheckRig != nullptr && Api->CheckRig(&Check) != 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_LIGHT_RIGCHECK %s nodes=%d max_level=%.6f max_facing=%.6f differ=%d live_max_level=%.6f live_differ=%d full=%d occluded=%d (%.1f s)"),
			*Name, Check.Nodes, Check.MaxLevel, Check.MaxFacing, Check.Differ, Check.LiveMaxLevel, Check.LiveDiffer, Check.Full, Check.Occluded,
			FPlatformTime::Seconds() - Before);
	}

	FScctLightSquint Squint = {};
	FScctShadowLighting Shadow = {};
	PlanApi->GetSquint(&Squint);
	PlanApi->GetShadowLighting(&Shadow);
	UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_LIGHT_LOCAL %s squint=%d:%.3f/%.4f shadow=%d:%d dir=%.2f,%.2f,%.2f"), *Name, Squint.Valid, Squint.Factor, Squint.Term,
		Shadow.Valid, Shadow.Darkness, Shadow.DirX, Shadow.DirY, Shadow.DirZ);
}

float UScctLightParitySubsystem::Predict(const FSpot& At) const
{
	const FVector Eye = At.Camera.GetLocation();
	float Rgb[3] = {};
	if (Api->PredictSphere(At.Centre.X, At.Centre.Y, At.Centre.Z, LightPawnRadius, Eye.X, Eye.Y, Eye.Z, LightSphereDisplay, LightSphereDisc, Rgb) == 0)
	{
		return -1.0f;
	}
	return 255.0f * (0.3f * Rgb[0] + 0.59f * Rgb[1] + 0.11f * Rgb[2]);
}

UScctLightParitySubsystem::FMeasure& UScctLightParitySubsystem::MeasureOf(FSpot& At)
{
	switch (Pass)
	{
	case EPass::Body: return At.Body;
	case EPass::Switched: return At.Switched;
	case EPass::Back: return At.Back;
	default: return At.Sphere;
	}
}

const TCHAR* UScctLightParitySubsystem::PassName() const
{
	switch (Pass)
	{
	case EPass::Body: return TEXT("body");
	case EPass::Switched: return TEXT("switched");
	case EPass::Back: return TEXT("back");
	default: return TEXT("sphere");
	}
}

void UScctLightParitySubsystem::StartPass(EPass Next)
{
	Pass = Next;
	bBodyPass = Pass == EPass::Body;
	Order.Reset();
	OrderAt = 0;
	for (int32 Spot = 0; Spot < Spots.Num(); ++Spot)
	{
		const bool bSwitchSpot = Spots[Spot].Switch >= 0;
		if (Pass == EPass::Sphere || (Pass == EPass::Body ? !bSwitchSpot : bSwitchSpot))
		{
			Order.Add(Spot);
		}
	}
	if (Order.Num() == 0 || (Pass == EPass::Body && !bMeasureBody))
	{
		Order.Reset();
		NextPass();
		return;
	}
	Index = Order[0];
	if (Pass == EPass::Switched || Pass == EPass::Back)
	{
		// Every switched light toggles; the flicker of a light going out and the news reaching the picture take a moment.
		bFired = FireSwitches();
		Phase = EPhase::Fire;
		PhaseAt = FPlatformTime::Seconds();
		return;
	}
	Phase = EPhase::Aim;
}

void UScctLightParitySubsystem::NextPass()
{
	switch (Pass)
	{
	case EPass::Sphere: StartPass(EPass::Body); return;
	case EPass::Body: StartPass(EPass::Switched); return;
	case EPass::Switched:
		if (Order.Num() > 0)
		{
			StartPass(EPass::Back);
			return;
		}
		break;
	default: break;
	}
	if (bTone && BeginTone())
	{
		Phase = EPhase::ToneSettle;
		PhaseAt = FPlatformTime::Seconds();
		return;
	}
	Finish();
}

void UScctLightParitySubsystem::AimAt(int32 Spot)
{
	const FSpot& At = Spots[Spot];
	UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	Session->SetCheckCamera(At.Camera);
	Capture->FOVAngle = Session->GetView().Fov > 1.0f ? Session->GetView().Fov : 75.0f;
	Capture->SetWorldTransform(At.Camera);
	Sphere->SetVisibility(!bBodyPass);
	Sphere->SetWorldLocation(At.Centre);
	if (bBodyPass)
	{
		Api->PlacePlayer(At.Centre.X, At.Centre.Y, At.Centre.Z);
	}
	Index = Spot;
	Phase = EPhase::Settle;
	PhaseAt = FPlatformTime::Seconds();
}

template <typename TPixel, typename TLuminance>
void UScctLightParitySubsystem::MeasureRegions(const TArray<TPixel>& Pixels, int32 Width, int32 Height, FVector2D Centre, double PixelsPerCm, bool bBody,
	TLuminance Luminance, float& OutReference, float& OutAround)
{
	// In cm at the reference, x to the right and y up from its centre.
	//   sphere: the middle of the disc (its rim darkens), and the ring of background round it
	//   body:   the chest, and the strips of background to its left and right
	const double Reach = bBody ? 90.0 : 55.0;
	double ReferenceSum = 0.0, AroundSum = 0.0;
	int32 ReferenceCount = 0, AroundCount = 0;
	const int32 X0 = FMath::Max(0, FMath::FloorToInt32(Centre.X - Reach * PixelsPerCm));
	const int32 X1 = FMath::Min(Width - 1, FMath::CeilToInt32(Centre.X + Reach * PixelsPerCm));
	const int32 Y0 = FMath::Max(0, FMath::FloorToInt32(Centre.Y - Reach * PixelsPerCm));
	const int32 Y1 = FMath::Min(Height - 1, FMath::CeilToInt32(Centre.Y + Reach * PixelsPerCm));
	for (int32 Y = Y0; Y <= Y1; ++Y)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			const double Right = (X + 0.5 - Centre.X) / PixelsPerCm;
			const double Up = (Centre.Y - (Y + 0.5)) / PixelsPerCm;
			bool bReference, bAround;
			if (bBody)
			{
				bReference = FMath::Abs(Right) <= 8.0 && Up >= 5.0 && Up <= 42.0;
				bAround = FMath::Abs(Right) >= 35.0 && FMath::Abs(Right) <= 85.0 && Up >= -30.0 && Up <= 60.0;
			}
			else
			{
				const double Off = FMath::Sqrt(Right * Right + Up * Up);
				bReference = Off <= 0.6 * LightSphereRadius;
				bAround = Off >= 24.0 && Off <= 55.0;
			}
			if (bReference)
			{
				ReferenceSum += Luminance(Pixels[Y * Width + X]);
				++ReferenceCount;
			}
			else if (bAround)
			{
				AroundSum += Luminance(Pixels[Y * Width + X]);
				++AroundCount;
			}
		}
	}
	OutReference = ReferenceCount > 0 ? static_cast<float>(ReferenceSum / ReferenceCount) : -1.0f;
	OutAround = AroundCount > 0 ? static_cast<float>(AroundSum / AroundCount) : -1.0f;
}

void UScctLightParitySubsystem::MatchCaptureToView(const FSpot& At)
{
	// The game widens the simulation's field of view for the screen's shape, so the capture's angle is read off the
	// player's own projection: how many pixels 10 cm at the spot covers gives the focal length.
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	FVector2D Centre, Beside;
	int32 ViewWidth = 0, ViewHeight = 0;
	if (Player != nullptr)
	{
		Player->GetViewportSize(ViewWidth, ViewHeight);
	}
	const double Depth = At.Camera.InverseTransformPosition(At.Centre).X;
	if (Player == nullptr || ViewWidth <= 0 || Depth <= 1.0 || !Player->ProjectWorldLocationToScreen(At.Centre, Centre, false) ||
		!Player->ProjectWorldLocationToScreen(At.Centre + At.Camera.GetUnitAxis(EAxis::Y) * 10.0, Beside, false))
	{
		return;
	}
	const double Focal = FVector2D::Distance(Centre, Beside) / 10.0 * Depth;
	if (Focal > 1.0)
	{
		const float Angle = static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(0.5 * ViewWidth / Focal)));
		if (Angle > 10.0f && Angle < 160.0f && !FMath::IsNearlyEqual(Angle, Capture->FOVAngle, 0.05f))
		{
			Capture->FOVAngle = Angle;
		}
	}
}

bool UScctLightParitySubsystem::IsReferenceSeen() const
{
	// What the engine drew in the player's own view after its occlusion culling (not in the HDR capture, which keeps
	// a time of its own): a sphere wholly behind something has not been drawn for as long as the camera stood here.
	// The body goes by the sphere's verdict, it stands where the sphere hung: its own mesh is reported as drawn even
	// behind a closed door (tried: AquaE's spot at 5265,716,128 read the door as the body with the mesh "on screen").
	if (bBodyPass)
	{
		return Spots[Index].Sphere.bSeen;
	}
	return GetWorld()->GetTimeSeconds() - Sphere->GetLastRenderTimeOnScreen() <= 0.5f;
}

void UScctLightParitySubsystem::FindBody(FSpot& At) const
{
	At.Trunk.Reset();
	At.Bones.Reset();
	for (TActorIterator<AScctPawnProxy> It(GetWorld()); It; ++It)
	{
		UPoseableMeshComponent* Mesh = It->IsLocal() ? It->GetBodyMesh() : nullptr;
		if (Mesh == nullptr || Mesh->GetNumBones() <= 0)
		{
			continue;
		}
		for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
		{
			const FName BoneName = Mesh->GetBoneName(Bone);
			const FVector Place = Mesh->GetBoneLocationByName(BoneName, EBoneSpaces::WorldSpace);
			At.Bones.Add(Place);
			if (BoneName.ToString().Contains(TEXT("Spine"), ESearchCase::IgnoreCase))
			{
				At.Trunk.Add(Place);
			}
		}
		if (At.Trunk.Num() == 0 && At.Bones.Num() > 0)
		{
			// No spine by name: the bones of the middle heights near the body's own axis.
			const FBox Box(At.Bones);
			for (const FVector& Place : At.Bones)
			{
				const double Height = (Place.Z - Box.Min.Z) / FMath::Max(Box.Max.Z - Box.Min.Z, 1.0);
				if (Height > 0.5 && Height < 0.8 && FVector::DistXY(Place, Box.GetCenter()) < 12.0)
				{
					At.Trunk.Add(Place);
				}
			}
		}
		break;
	}
}

template <typename TPixel, typename TLuminance, typename TProject>
void UScctLightParitySubsystem::MeasureBody(const TArray<TPixel>& Pixels, int32 Width, int32 Height, const FSpot& At, double PixelsPerCm, TProject Project,
	TLuminance Luminance, float& OutReference, float& OutAround)
{
	// The trunk: within 4.5 cm of a spine bone (well inside the body from any side). The background: a frame round
	// the box of every bone, from 15 cm outside it (clear of the limbs' own thickness) to 60 cm.
	TArray<FVector2D> Discs;
	FBox2D Box(ForceInit);
	FVector2D Point;
	for (const FVector& Place : At.Trunk)
	{
		if (Project(Place, Point))
		{
			Discs.Add(Point);
		}
	}
	for (const FVector& Place : At.Bones)
	{
		if (Project(Place, Point))
		{
			Box += Point;
		}
	}
	OutReference = OutAround = -1.0f;
	if (Discs.Num() == 0 || !Box.bIsValid)
	{
		return;
	}
	const double Radius = 4.5 * PixelsPerCm;
	const FBox2D Inner = Box.ExpandBy(15.0 * PixelsPerCm);
	const FBox2D Outer = Box.ExpandBy(60.0 * PixelsPerCm);
	double ReferenceSum = 0.0, AroundSum = 0.0;
	int32 ReferenceCount = 0, AroundCount = 0;
	const int32 X0 = FMath::Max(0, FMath::FloorToInt32(Outer.Min.X)), X1 = FMath::Min(Width - 1, FMath::CeilToInt32(Outer.Max.X));
	const int32 Y0 = FMath::Max(0, FMath::FloorToInt32(Outer.Min.Y)), Y1 = FMath::Min(Height - 1, FMath::CeilToInt32(Outer.Max.Y));
	for (int32 Y = Y0; Y <= Y1; ++Y)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			const FVector2D Pixel(X + 0.5, Y + 0.5);
			if (!Inner.IsInside(Pixel))
			{
				AroundSum += Luminance(Pixels[Y * Width + X]);
				++AroundCount;
				continue;
			}
			for (const FVector2D& Disc : Discs)
			{
				if (FVector2D::DistSquared(Disc, Pixel) <= Radius * Radius)
				{
					ReferenceSum += Luminance(Pixels[Y * Width + X]);
					++ReferenceCount;
					break;
				}
			}
		}
	}
	OutReference = ReferenceCount > 0 ? static_cast<float>(ReferenceSum / ReferenceCount) : -1.0f;
	OutAround = AroundCount > 0 ? static_cast<float>(AroundSum / AroundCount) : -1.0f;
}

void UScctLightParitySubsystem::ReadCapture(FMeasure& Measure, const FSpot& At) const
{
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	TArray<FFloat16Color> Pixels;
	if (Resource == nullptr || !Resource->ReadFloat16Pixels(Pixels) || Pixels.Num() < LightCaptureWidth * LightCaptureHeight)
	{
		return;
	}
	if (bSaveCapture)
	{
		// The capture as a picture: scene colour times the view's exposure, no tone curve, so it can be laid beside the real one.
		TArray<FColor> Shown;
		Shown.Reserve(Pixels.Num());
		for (const FFloat16Color& Pixel : Pixels)
		{
			Shown.Add((FLinearColor(Pixel.R.GetFloat(), Pixel.G.GetFloat(), Pixel.B.GetFloat()) * Measure.Exposure).GetClamped().ToFColor(true));
			Shown.Last().A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(LightCaptureWidth, LightCaptureHeight, TArrayView64<const FColor>(Shown.GetData(), Shown.Num()), Png);
		FFileHelper::SaveArrayToFile(Png, *(Directory / FString::Printf(TEXT("%s_%02d_%s_scene.png"), *Name, Index, PassName())));
	}
	const FVector Local = At.Camera.InverseTransformPosition(At.Centre);
	if (Local.X <= 1.0)
	{
		return;
	}
	const double Focal = 0.5 * LightCaptureWidth / FMath::Tan(FMath::DegreesToRadians(0.5 * Capture->FOVAngle));
	const FVector2D Centre(0.5 * LightCaptureWidth + Local.Y / Local.X * Focal, 0.5 * LightCaptureHeight - Local.Z / Local.X * Focal);
	const auto Luminance = [](const FFloat16Color& Pixel)
	{
		return FMath::Max(0.0f, 0.2126f * Pixel.R.GetFloat() + 0.7152f * Pixel.G.GetFloat() + 0.0722f * Pixel.B.GetFloat());
	};
	if (bBodyPass && At.Trunk.Num() > 0)
	{
		MeasureBody(Pixels, LightCaptureWidth, LightCaptureHeight, At, Focal / Local.X,
			[&At, Focal](const FVector& World, FVector2D& Out)
			{
				const FVector Seen = At.Camera.InverseTransformPosition(World);
				Out = FVector2D(0.5 * LightCaptureWidth + Seen.Y / FMath::Max(Seen.X, 1.0) * Focal, 0.5 * LightCaptureHeight - Seen.Z / FMath::Max(Seen.X, 1.0) * Focal);
				return Seen.X > 1.0;
			},
			Luminance, Measure.Raw, Measure.RawAround);
		return;
	}
	MeasureRegions(Pixels, LightCaptureWidth, LightCaptureHeight, Centre, Focal / Local.X, bBodyPass, Luminance, Measure.Raw, Measure.RawAround);
}

void UScctLightParitySubsystem::OnCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (Phase == EPhase::ToneRead && !bCaptured && Width > 0 && Height > 0 && Bitmap.Num() >= Width * Height)
	{
		MeasureTone(Width, Height, Bitmap);
		bCaptured = true;
		return;
	}
	if (Phase != EPhase::Read || bCaptured || Width <= 0 || Height <= 0 || Bitmap.Num() < Width * Height || !Spots.IsValidIndex(Index))
	{
		return;
	}
	FSpot& At = Spots[Index];
	FMeasure& Measure = MeasureOf(At);

	// Where the reference is in the picture, by the player's own view (whatever its field of view and framing).
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	FVector2D Centre, Beside;
	int32 ViewWidth = 0, ViewHeight = 0;
	if (Player != nullptr)
	{
		Player->GetViewportSize(ViewWidth, ViewHeight);
	}
	if (Player != nullptr && ViewWidth > 0 && Player->ProjectWorldLocationToScreen(At.Centre, Centre, false) &&
		Player->ProjectWorldLocationToScreen(At.Centre + At.Camera.GetUnitAxis(EAxis::Y) * 10.0, Beside, false))
	{
		const double Scale = static_cast<double>(Width) / ViewWidth;
		const double PixelsPerCm = FVector2D::Distance(Centre, Beside) * Scale / 10.0;
		const auto Luma = [](const FColor& Pixel) { return 0.3f * Pixel.R + 0.59f * Pixel.G + 0.11f * Pixel.B; };
		if (bBodyPass && At.Trunk.Num() > 0)
		{
			MeasureBody(Bitmap, Width, Height, At, PixelsPerCm,
				[Player, Scale](const FVector& World, FVector2D& Out)
				{
					const bool bInFront = Player->ProjectWorldLocationToScreen(World, Out, false);
					Out *= Scale;
					return bInFront;
				},
				Luma, Measure.Picture, Measure.PictureAround);
		}
		else
		{
			MeasureRegions(Bitmap, Width, Height, Centre * Scale, PixelsPerCm, bBodyPass, Luma, Measure.Picture, Measure.PictureAround);
		}
	}

	TArray<FColor> Opaque = Bitmap;
	for (FColor& Pixel : Opaque)
	{
		Pixel.A = 255;
	}
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	FFileHelper::SaveArrayToFile(Png, *(Directory / FString::Printf(TEXT("%s_%02d_%s.png"), *Name, Index, PassName())));
	bCaptured = true;
}

void UScctLightParitySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	UScctSessionSubsystem* Session = World != nullptr ? World->GetSubsystem<UScctSessionSubsystem>() : nullptr;
	if (Session == nullptr || Phase == EPhase::Done)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();

	switch (Phase)
	{
	case EPhase::WaitMatch:
		// A match that plays and has given the player a view, and a few seconds for the level to finish arriving.
		if (Session->GetBridge() == nullptr || Session->GetView().Valid == 0)
		{
			ReadyAt = -1.0;
			break;
		}
		if (ReadyAt < 0.0)
		{
			ReadyAt = Now;
		}
		if (Now - ReadyAt >= LightMatchSeconds)
		{
			if (Begin())
			{
				StartPass(EPass::Sphere);
			}
			else
			{
				Phase = EPhase::Done;
				FPlatformMisc::RequestExit(false);
			}
		}
		break;

	case EPhase::Aim:
		AimAt(Order[OrderAt]);
		break;

	case EPhase::Fire:
		if (Now - PhaseAt >= LightFireSeconds)
		{
			Phase = EPhase::Aim;
		}
		break;

	case EPhase::ToneSettle:
		if (Now - PhaseAt >= SettleSeconds + 1.0)
		{
			ToneExposure = FScctExposure::Get();
			bCaptured = false;
			FScreenshotRequest::RequestScreenshot(false);
			Phase = EPhase::ToneRead;
			PhaseAt = Now;
		}
		break;

	case EPhase::ToneRead:
		if (bCaptured || Now - PhaseAt >= LightReadSeconds)
		{
			if (!bCaptured)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("LightParity: the picture of the tone ramp could not be read back."));
			}
			Finish();
		}
		break;

	case EPhase::Settle:
		if (FScctLightPlan::IsOn() && !bBodyPass)
		{
			FScctPawnLighting::Sample(Spots[Index].Centre, LightPawnRadius).Apply(*Sphere);
		}
		MatchCaptureToView(Spots[Index]);
		if (Now - PhaseAt >= SettleSeconds)
		{
			FSpot& At = Spots[Index];
			FMeasure& Measure = MeasureOf(At);
			if (Pass == EPass::Switched || Pass == EPass::Back)
			{
				// What the simulation says of the spot now the lights have been fired.
				const FVector Eye = At.Camera.GetLocation();
				Api->Read(At.Centre.X, At.Centre.Y, At.Centre.Z, LightPawnRadius, Eye.X, Eye.Y, Eye.Z, Pass == EPass::Switched ? &At.ReadingSwitched : &At.ReadingBack);
				(Pass == EPass::Switched ? At.PredictedSwitched : At.PredictedBack) = Predict(At);
			}
			Measure.bSeen = IsReferenceSeen();
			Measure.Exposure = FScctExposure::Get();
			Measure.SceneLuminance = FScctExposure::PictureLevel() / Measure.Exposure;
			if (bBodyPass)
			{
				FindBody(At);
			}
			ReadCapture(Measure, At);
			bCaptured = false;
			FScreenshotRequest::RequestScreenshot(false);
			Phase = EPhase::Read;
			PhaseAt = Now;
		}
		break;

	case EPhase::Read:
		if (!bCaptured && Now - PhaseAt < LightReadSeconds)
		{
			break;
		}
		if (!bCaptured)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("LightParity: the picture of spot %d could not be read back."), Index);
		}
		if (++OrderAt >= Order.Num())
		{
			NextPass();
			break;
		}
		Phase = EPhase::Aim;
		break;

	default:
		break;
	}
}

void UScctLightParitySubsystem::Finish()
{
	Phase = EPhase::Done;
	FString Table = TEXT("name,index,kind,x,y,z,camera_cm,zone,sim,sim_facing,ambient,has_baked,baked,baked_r,baked_g,baked_b,")
		TEXT("sphere_raw,ground_raw,sphere_pic,ground_pic,sphere_exposure,sphere_scene,body_raw,behind_raw,body_pic,behind_pic,body_exposure,body_scene,pred,plan,play_floor,sphere_seen,body_seen,")
		TEXT("rig_level,rig_facing,rig_lights,rig_occluded,rig_spots,rig_live_level,rig_live_facing\n");
	float RigLevelOff = 0.0f, RigFacingOff = 0.0f, RigLiveOff = 0.0f;
	int32 RigSpots = 0;
	const int32 Plan = FScctLightPlan::Active();
	for (int32 Spot = 0; Spot < OrdinarySpots; ++Spot)
	{
		const FSpot& At = Spots[Spot];
		const FScctLightReading& R = At.Reading;
		const float Exposure = At.Sphere.Exposure;
		UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_LIGHT %s %02d kind=%s at=%.0f,%.0f,%.0f sim=%.3f facing=%.3f pred=%.1f baked=%s sphere=%.5f/%.1f ground=%.5f/%.1f body=%.5f/%.1f behind=%.5f/%.1f exposure=%.3f ev=%.2f seen=%d%d"),
			*Name, Spot, LightKindName(At.Spot.Kind), At.Spot.X, At.Spot.Y, At.Spot.Z, R.Level, R.LevelFacing, At.Predicted,
			R.HasBaked != 0 ? *FString::Printf(TEXT("%.3f"), R.Baked) : TEXT("-"),
			At.Sphere.Raw, At.Sphere.Picture, At.Sphere.RawAround, At.Sphere.PictureAround,
			At.Body.Raw, At.Body.Picture, At.Body.RawAround, At.Body.PictureAround,
			Exposure, Exposure > 0.0f ? FMath::Log2(1.0f / (1.2f * Exposure)) : 0.0f, At.Sphere.bSeen ? 1 : 0, At.Body.bSeen ? 1 : 0);
		Table += FString::Printf(TEXT("%s,%d,%s,%.1f,%.1f,%.1f,%.0f,%d,%.4f,%.4f,%.4f,%d,%.4f,%.4f,%.4f,%.4f,%.6f,%.6f,%.2f,%.2f,%.4f,%.6f,%.6f,%.6f,%.2f,%.2f,%.4f,%.6f,%.2f,%d,%d,%d,%d,%s\n"),
			*Name, Spot, LightKindName(At.Spot.Kind), At.Spot.X, At.Spot.Y, At.Spot.Z, At.CameraDistance, R.Zone, R.Level, R.LevelFacing, R.Ambient,
			R.HasBaked, R.Baked, R.BakedR, R.BakedG, R.BakedB,
			At.Sphere.Raw, At.Sphere.RawAround, At.Sphere.Picture, At.Sphere.PictureAround, At.Sphere.Exposure, At.Sphere.SceneLuminance,
			At.Body.Raw, At.Body.RawAround, At.Body.Picture, At.Body.PictureAround, At.Body.Exposure, At.Body.SceneLuminance,
			At.Predicted, Plan, Floor.PlayRule, At.Sphere.bSeen ? 1 : 0, At.Body.bSeen ? 1 : 0,
			At.bRig ? *FString::Printf(TEXT("%.6f,%.6f,%d,%d,%d,%.6f,%.6f"), At.Rig.Level, At.Rig.LevelFacing, At.RigLive.Count, At.RigLive.Occluded,
				At.RigLive.SpotCount, At.RigLive.Level, At.RigLive.LevelFacing) : TEXT(",,,,,,"));
		if (At.bRig)
		{
			++RigSpots;
			RigLevelOff = FMath::Max(RigLevelOff, FMath::Abs(At.Rig.Level - R.Level));
			RigFacingOff = FMath::Max(RigFacingOff, FMath::Abs(At.Rig.LevelFacing - R.LevelFacing));
			RigLiveOff = FMath::Max(RigLiveOff, FMath::Abs(At.RigLive.Level - R.Level));
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_LIGHT_RIG %s spots=%d of %d, rig level against the simulation's: largest difference %.6f, facing %.6f, as the game asks the rig %.6f"),
		*Name, RigSpots, OrdinarySpots, RigLevelOff, RigFacingOff, RigLiveOff);
	WriteSwitches();
	const FString File = Directory / (Name + TEXT(".csv"));
	FFileHelper::SaveStringToFile(Table, *File);
	UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_LIGHT_DONE %s %d spots, scct.Light.Plan %d, table %s"), *Name, OrdinarySpots, Plan, *File);
	if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->ClearCheckCamera();
	}
	FPlatformMisc::RequestExit(false);
}
