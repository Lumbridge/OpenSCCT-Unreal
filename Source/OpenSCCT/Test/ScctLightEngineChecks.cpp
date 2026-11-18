// The engine behaviours the lighting plan leans on, measured (Test/ScctLightEngineChecks.h says what and why).

#include "Test/ScctLightEngineChecks.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Play/ScctExposure.h"
#include "Play/ScctLightPlan.h"
#include "Play/ScctSessionSubsystem.h"
#include "UnrealClient.h"

namespace
{
	// The room, inside: 8 m deep (x), 8 m wide (y), 4 m high, the camera at the near end looking down its length.
	constexpr float EngineCheckDepth = 800.0f;
	constexpr float EngineCheckHalfWidth = 400.0f;
	constexpr float EngineCheckHeight = 400.0f;
	constexpr float EngineCheckAside = 50000.0f;

	constexpr double EngineCheckMatchSeconds = 5.0;
	constexpr double EngineCheckFirstSettle = 8.0;
	constexpr double EngineCheckSettle = 4.0;
	constexpr double EngineCheckReadSeconds = 20.0;

	enum EEngineCheckGroup { EngineGroupRamp, EngineGroupFalloff, EngineGroupDiffuse, EngineGroupChannels, EngineGroupBounce, EngineGroupFog, EngineGroupCpd, EngineGroupCount };

	struct FEngineCheckConfig
	{
		const TCHAR* Name;
		int32 Group;
	};
	const FEngineCheckConfig EngineCheckConfigs[] = {
		{ TEXT("ramp"), EngineGroupRamp },
		{ TEXT("falloff_e1.0"), EngineGroupFalloff },
		{ TEXT("falloff_e2.2"), EngineGroupFalloff },
		{ TEXT("falloff_e4.0"), EngineGroupFalloff },
		{ TEXT("diffuse1_specular1"), EngineGroupDiffuse },
		{ TEXT("diffuse0_specular1"), EngineGroupDiffuse },
		{ TEXT("diffuse1_specular0"), EngineGroupDiffuse },
		{ TEXT("channels_light01"), EngineGroupChannels },
		{ TEXT("channels_light0"), EngineGroupChannels },
		{ TEXT("channels_light1"), EngineGroupChannels },
		{ TEXT("bounce_lamp"), EngineGroupBounce },
		{ TEXT("bounce_sky"), EngineGroupBounce },
		{ TEXT("fog_channel0"), EngineGroupFog },
		{ TEXT("fog_channel1"), EngineGroupFog },
		{ TEXT("fog_channel2"), EngineGroupFog },
		{ TEXT("fog_noscales"), EngineGroupFog },
		{ TEXT("fog_lightoff"), EngineGroupFog },
		{ TEXT("cpd_set"), EngineGroupCpd },
		{ TEXT("cpd_zero"), EngineGroupCpd },
		{ TEXT("fog_diffuse0_specular1"), EngineGroupFog },
		{ TEXT("fog_scales0.01_scatter3000"), EngineGroupFog },
		{ TEXT("fog_channel1_nomegalights"), EngineGroupFog },
		{ TEXT("fog_noscales_nomegalights"), EngineGroupFog },
		{ TEXT("ramp_one_stop_up"), EngineGroupRamp },
	};

	// Scene light of the ramp's patches.
	const float EngineCheckRamp[] = { 0.0f, 0.001f, 0.002f, 0.004f, 0.008f, 0.015f, 0.03f, 0.06f, 0.1f, 0.18f, 0.3f, 0.5f, 0.8f, 1.2f, 2.0f };

	// The falloff cards: distance from the light as a share of its radius, and the bearing they stand at (degrees).
	constexpr float EngineFalloffRadius = 500.0f;
	constexpr float EngineFalloffIntensity = 4.0f;
	constexpr float EngineFalloffAlbedo = 0.5f;
	const float EngineFalloffShare[] = { 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f };
	const float EngineFalloffBearing[] = { 70.0f, -40.0f, -20.0f, 40.0f, 20.0f, 0.0f };
	const float EngineFalloffExponents[] = { 1.0f, 2.2f, 4.0f };
	const FVector EngineFalloffLight(300.0, 0.0, 100.0);

	const FVector EngineDiffuseLight(300.0, 0.0, 300.0);
	const FVector EngineDiffuseMatte(500.0, -100.0, 150.0);
	const FVector EngineDiffuseGloss(500.0, 100.0, 150.0);

	const FVector EngineChannelLight(250.0, 0.0, 350.0);
	const FVector EngineChannelSphereA(550.0, -150.0, 150.0);
	const FVector EngineChannelSphereB(550.0, 150.0, 150.0);
	const FVector EngineChannelCard(600.0, 0.0, 120.0);
	constexpr float EngineChannelRadius = 800.0f;
	constexpr float EngineChannelIntensity = 4.0f;

	const FVector EngineBounceLamp(400.0, 0.0, 390.0);
	// The two that are compared for the pin's share stand the same distance either side of the pool's middle.
	const float EngineBounceOcclusion[] = { 0.0f, 0.5f, 1.0f, 0.0f };
	const TCHAR* EngineBounceNames[] = { TEXT("sphere ao=0 ch0"), TEXT("sphere ao=0.5 ch0"), TEXT("sphere ao=1 ch0"), TEXT("sphere ao=0 ch1 only") };

	const FVector EngineFogLight(450.0, -390.0, 200.0);
	const FVector EngineFogCard(450.0, 300.0, 200.0);
	const FVector EngineFogBeam(450.0, 0.0, 200.0);

	const int32 EngineCpdIndex[] = { 0, 30, 35 };
	constexpr float EngineCpdValue = 0.2f;

	const FName EngineParamAlbedo(TEXT("Albedo"));
	const FName EngineParamOcclusion(TEXT("Occlusion"));
	const FName EngineParamRoughness(TEXT("Roughness"));
	const FName EngineParamSpecular(TEXT("Specular"));
	const FName EngineParamMetallic(TEXT("Metallic"));
	const FName EngineParamLevel(TEXT("Level"));

	FVector EngineFalloffCard(int32 Card)
	{
		const float Bearing = FMath::DegreesToRadians(EngineFalloffBearing[Card]);
		return EngineFalloffLight + FVector(FMath::Cos(Bearing), FMath::Sin(Bearing), 0.0) * (EngineFalloffShare[Card] * EngineFalloffRadius);
	}

#if WITH_EDITOR
	UMaterialExpressionScalarParameter* EngineCheckParameter(UMaterial* Material, FName Name, float Value)
	{
		UMaterialExpressionScalarParameter* Parameter = NewObject<UMaterialExpressionScalarParameter>(Material);
		Parameter->ParameterName = Name;
		Parameter->DefaultValue = Value;
		Material->GetExpressionCollection().AddExpression(Parameter);
		return Parameter;
	}

	void EngineCheckFinishMaterial(UMaterial* Material)
	{
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->EnsureIsComplete();
	}
#endif

	float EngineCheckLuma(const FColor& Pixel)
	{
		return 0.3f * Pixel.R + 0.59f * Pixel.G + 0.11f * Pixel.B;
	}

	void EngineCheckSetVariable(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}
}

bool UScctLightEngineChecks::ShouldCreateSubsystem(UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("ScctLightEngineChecks")) && Super::ShouldCreateSubsystem(Outer);
}

bool UScctLightEngineChecks::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UScctLightEngineChecks::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctLightEngineChecks, STATGROUP_Tickables);
}

void UScctLightEngineChecks::Deinitialize()
{
	if (CaptureHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
		CaptureHandle.Reset();
	}
	Super::Deinitialize();
}

UMaterialInstanceDynamic* UScctLightEngineChecks::Lit(float Albedo, float Occlusion, float Roughness, float Specular, float Metallic)
{
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(LitMaterial, this);
	Instance->SetScalarParameterValue(EngineParamAlbedo, Albedo);
	Instance->SetScalarParameterValue(EngineParamOcclusion, Occlusion);
	Instance->SetScalarParameterValue(EngineParamRoughness, Roughness);
	Instance->SetScalarParameterValue(EngineParamSpecular, Specular);
	Instance->SetScalarParameterValue(EngineParamMetallic, Metallic);
	Kept.Add(Instance);
	return Instance;
}

UStaticMeshComponent* UScctLightEngineChecks::AddShape(int32 Group, const TCHAR* Mesh, const FTransform& Place, UMaterialInstanceDynamic* Material)
{
	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(Rig);
	Shape->SetMobility(EComponentMobility::Movable);
	Shape->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	Shape->SetMaterial(0, Material);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetWorldTransform(FTransform(Place.GetRotation(), Origin + Place.GetLocation(), Place.GetScale3D()));
	Shape->RegisterComponent();
	if (Groups.IsValidIndex(Group))
	{
		Groups[Group].Add(Shape);
		Shape->SetVisibility(false);
	}
	return Shape;
}

UStaticMeshComponent* UScctLightEngineChecks::AddCard(int32 Group, const FVector& At, const FVector& Facing, float Size, UMaterialInstanceDynamic* Material)
{
	// The engine's plane lies in its own XY and faces +Z.
	return AddShape(Group, TEXT("/Engine/BasicShapes/Plane.Plane"),
		FTransform(FRotationMatrix::MakeFromZ(Facing.GetSafeNormal()).ToQuat(), At, FVector(Size / 100.0, Size / 100.0, 1.0)), Material);
}

UStaticMeshComponent* UScctLightEngineChecks::AddSphere(int32 Group, const FVector& At, float Radius, UMaterialInstanceDynamic* Material)
{
	return AddShape(Group, TEXT("/Engine/BasicShapes/Sphere.Sphere"), FTransform(FQuat::Identity, At, FVector(Radius / 50.0)), Material);
}

void UScctLightEngineChecks::Point(const FVector& At, float Radius, float Exponent, float Intensity, bool bShadow, bool bChannel0, bool bChannel1, bool bChannel2)
{
	PointLight->SetWorldLocation(Origin + At);
	PointLight->SetAttenuationRadius(Radius);
	PointLight->SetLightFalloffExponent(Exponent);
	PointLight->SetIntensity(Intensity);
	PointLight->SetCastShadows(bShadow);
	PointLight->SetLightingChannels(bChannel0, bChannel1, bChannel2);
	PointLight->SetDiffuseScale(1.0f);
	PointLight->SetSpecularScale(1.0f);
	PointLight->SetVisibility(true);
}

void UScctLightEngineChecks::Spot(const FVector& At, const FVector& Towards, float Cone, float Radius, float Intensity, float Scatter, bool bChannel0, bool bChannel1, bool bChannel2)
{
	SpotLight->SetWorldLocationAndRotation(Origin + At, (Towards - At).Rotation());
	SpotLight->SetOuterConeAngle(Cone);
	SpotLight->SetInnerConeAngle(Cone * 0.8f);
	SpotLight->SetAttenuationRadius(Radius);
	SpotLight->SetIntensity(Intensity);
	SpotLight->SetVolumetricScatteringIntensity(Scatter);
	SpotLight->SetLightingChannels(bChannel0, bChannel1, bChannel2);
	SpotLight->SetDiffuseScale(1.0f);
	SpotLight->SetSpecularScale(1.0f);
	if (!SpotLight->bAllowMegaLights)
	{
		SpotLight->bAllowMegaLights = true;
		SpotLight->MarkRenderStateDirty();
	}
	SpotLight->SetVisibility(true);
}

bool UScctLightEngineChecks::Build()
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	UScctSessionSubsystem* Session = World->GetSubsystem<UScctSessionSubsystem>();
	Directory = FPaths::ProjectSavedDir() / TEXT("ScctLight");
	FParse::Value(FCommandLine::Get(), TEXT("ScctLightDir="), Directory);
	IFileManager::Get().MakeDirectory(*Directory, true);

	const FScctBridgeView& View = Session->GetView();
	Origin = FVector(FMath::RoundToDouble(View.X) + EngineCheckAside, FMath::RoundToDouble(View.Y), FMath::RoundToDouble(View.Z));

	// The own side's body lights stand at the camera and fall on channel 1: they would light the channel-1 spheres.
	EngineCheckSetVariable(TEXT("scct.BodyFill"), 0.0f);
	EngineCheckSetVariable(TEXT("scct.BodyRim"), 0.0f);

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	Rig = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	USceneComponent* Root = NewObject<USceneComponent>(Rig);
	Rig->SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
	Root->RegisterComponent();

	// The picture: one exposure (scale 1), and nothing that spreads light from one part of the picture to another.
	Volume = World->SpawnActor<APostProcessVolume>(Parameters);
	Volume->bUnbound = true;
	Volume->BlendWeight = 1.0f;
	// The world places a volume by its priority when it is added, and this one was added with none: taken out and put
	// back with its own. That still does not put it after the game's exposure volume (Play/ScctExposure.cpp): that one
	// was added with priority 0 and given 10 afterwards, the list is not sorted again, and it is applied last of all
	// (the listing this check logs with the first picture shows the order). So the exposure's scale here is the game's:
	// 2^-0.75 = 0.59 with scct.Light.Plan 0 (this volume's manual method, that volume's bias) and the plan's fixed scale
	// with 1. The readings as scene light do not depend on it: the ramp is read in the same picture at the same exposure.
	Volume->Priority = 500.0f;
	World->RemovePostProcessVolume(Volume);
	World->AddPostProcessVolume(TWeakInterfacePtr<IInterface_PostProcessVolume>(Volume.Get()));
	FPostProcessSettings& Settings = Volume->Settings;
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = 0.0f; // a manual exposure's scale is 2^bias (Play/ScctExposure.cpp); overridden by the game's volume, see below
	Settings.bOverride_LocalExposureHighlightContrastScale = true;
	Settings.LocalExposureHighlightContrastScale = 1.0f;
	Settings.bOverride_LocalExposureShadowContrastScale = true;
	Settings.LocalExposureShadowContrastScale = 1.0f;
	Settings.bOverride_LocalExposureDetailStrength = true;
	Settings.LocalExposureDetailStrength = 1.0f;
	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = 0.0f;
	Settings.bOverride_LensFlareIntensity = true;
	Settings.LensFlareIntensity = 0.0f;
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = 0.0f;
	Settings.bOverride_FilmGrainIntensity = true;
	Settings.FilmGrainIntensity = 0.0f;
	Settings.bOverride_SceneFringeIntensity = true;
	Settings.SceneFringeIntensity = 0.0f;
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.0f;

	// A lit material with everything a check varies as a parameter, and an unlit one that glows by a known amount.
	LitMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	LitMaterial->GetEditorOnlyData()->BaseColor.Expression = EngineCheckParameter(LitMaterial, EngineParamAlbedo, 0.5f);
	LitMaterial->GetEditorOnlyData()->AmbientOcclusion.Expression = EngineCheckParameter(LitMaterial, EngineParamOcclusion, 1.0f);
	LitMaterial->GetEditorOnlyData()->Roughness.Expression = EngineCheckParameter(LitMaterial, EngineParamRoughness, 1.0f);
	LitMaterial->GetEditorOnlyData()->Specular.Expression = EngineCheckParameter(LitMaterial, EngineParamSpecular, 0.0f);
	LitMaterial->GetEditorOnlyData()->Metallic.Expression = EngineCheckParameter(LitMaterial, EngineParamMetallic, 0.0f);
	EngineCheckFinishMaterial(LitMaterial);

	GlowMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	GlowMaterial->GetEditorOnlyData()->EmissiveColor.Expression = EngineCheckParameter(GlowMaterial, EngineParamLevel, 0.0f);
	GlowMaterial->SetShadingModel(MSM_Unlit);
	EngineCheckFinishMaterial(GlowMaterial);

	Groups.SetNum(EngineGroupCount);

	// The room: slabs 20 cm thick round the inside, the camera's end closed too.
	WallMaterial = Lit(0.5f);
	const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FVector Middle(0.5 * EngineCheckDepth, 0.0, 0.5 * EngineCheckHeight);
	const FVector Slab(EngineCheckDepth / 100.0 + 0.4, 2.0 * EngineCheckHalfWidth / 100.0 + 0.4, 0.2);
	AddShape(INDEX_NONE, Cube, FTransform(FQuat::Identity, FVector(Middle.X, 0.0, -10.0), Slab), WallMaterial);
	Ceiling = AddShape(INDEX_NONE, Cube, FTransform(FQuat::Identity, FVector(Middle.X, 0.0, EngineCheckHeight + 10.0), Slab), WallMaterial);
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		AddShape(INDEX_NONE, Cube, FTransform(FQuat::Identity, FVector(Middle.X, Side * (EngineCheckHalfWidth + 10.0), Middle.Z),
			FVector(EngineCheckDepth / 100.0 + 0.4, 0.2, EngineCheckHeight / 100.0)), WallMaterial);
		AddShape(INDEX_NONE, Cube, FTransform(FQuat::Identity, FVector(Side > 0 ? EngineCheckDepth + 10.0 : -10.0, 0.0, Middle.Z),
			FVector(0.2, 2.0 * EngineCheckHalfWidth / 100.0, EngineCheckHeight / 100.0)), WallMaterial);
	}

	const FVector Eye(40.0, 0.0, 200.0);
	Camera = FTransform((FVector(600.0, 0.0, 150.0) - Eye).Rotation(), Origin + Eye);

	// The ramp: on the far wall at the camera's height.
	for (int32 Patch = 0; Patch < UE_ARRAY_COUNT(EngineCheckRamp); ++Patch)
	{
		UMaterialInstanceDynamic* Glow = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		Glow->SetScalarParameterValue(EngineParamLevel, EngineCheckRamp[Patch]);
		Kept.Add(Glow);
		UStaticMeshComponent* Card = AddCard(EngineGroupRamp, FVector(EngineCheckDepth - 2.0, -350.0 + 50.0 * Patch, 200.0), FVector(-1.0, 0.0, 0.0), 30.0f, Glow);
		Card->SetCastShadow(false);
		Card->bAffectDynamicIndirectLighting = false;
		Card->bAffectDistanceFieldLighting = false;
	}

	// Falloff: matte cards facing the light, each at its own bearing so none stands in another's light.
	for (int32 Card = 0; Card < UE_ARRAY_COUNT(EngineFalloffShare); ++Card)
	{
		const FVector At = EngineFalloffCard(Card);
		AddCard(EngineGroupFalloff, At, EngineFalloffLight - At, 30.0f, Lit(EngineFalloffAlbedo))->SetCastShadow(false);
	}

	// Diffuse and specular scale: a matte card facing the light, and a polished metal one turned to mirror it at the camera.
	AddCard(EngineGroupDiffuse, EngineDiffuseMatte, EngineDiffuseLight - EngineDiffuseMatte, 60.0f, Lit(0.5f));
	AddCard(EngineGroupDiffuse, EngineDiffuseGloss, (EngineDiffuseLight - EngineDiffuseGloss).GetSafeNormal() + (Eye - EngineDiffuseGloss).GetSafeNormal(), 60.0f,
		Lit(0.9f, 1.0f, 0.3f, 0.5f, 1.0f));

	// Channels: two spheres on channel 1 only, one of them behind a channel-0 block; a channel-0 card in the open, and
	// one behind the open sphere.
	AddSphere(EngineGroupChannels, EngineChannelSphereA, 25.0f, Lit(0.5f))->SetLightingChannels(false, true, false);
	AddSphere(EngineGroupChannels, EngineChannelSphereB, 25.0f, Lit(0.5f))->SetLightingChannels(false, true, false);
	AddShape(EngineGroupChannels, Cube, FTransform(FQuat::Identity, 0.5 * (EngineChannelLight + EngineChannelSphereB), FVector(0.6)), Lit(0.5f));
	AddCard(EngineGroupChannels, EngineChannelCard, EngineChannelLight - EngineChannelCard, 50.0f, Lit(0.5f));
	const FVector Behind = EngineChannelSphereA + (EngineChannelSphereA - EngineChannelLight).GetSafeNormal() * 120.0;
	AddCard(EngineGroupChannels, Behind, EngineChannelLight - Behind, 100.0f, Lit(0.5f));

	// Bounce: spheres beyond the lamp's pool on the floor.
	for (int32 Sphere = 0; Sphere < UE_ARRAY_COUNT(EngineBounceOcclusion); ++Sphere)
	{
		UStaticMeshComponent* Shape = AddSphere(EngineGroupBounce, FVector(650.0, -225.0 + 150.0 * Sphere, 150.0), 30.0f, Lit(0.5f, EngineBounceOcclusion[Sphere]));
		if (Sphere == 3)
		{
			Shape->SetLightingChannels(false, true, false);
		}
	}

	// Fog: the card the beam lands on.
	AddCard(EngineGroupFog, EngineFogCard, EngineFogLight - EngineFogCard, 60.0f, Lit(0.5f));

	// Custom primitive data on a skinned mesh: three poseable cubes, each glowing by one float of its own data.
	USkeletalMesh* SkinnedCube = LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
	for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(EngineCpdIndex) && SkinnedCube != nullptr; ++Slot)
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		UMaterialExpressionScalarParameter* Value = EngineCheckParameter(Material, EngineParamLevel, 0.0f);
		Value->bUseCustomPrimitiveData = true;
		Value->PrimitiveDataIndex = static_cast<uint8>(EngineCpdIndex[Slot]);
		Material->GetEditorOnlyData()->EmissiveColor.Expression = Value;
		Material->SetShadingModel(MSM_Unlit);
		Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
		EngineCheckFinishMaterial(Material);
		Kept.Add(Material);

		UPoseableMeshComponent* Body = NewObject<UPoseableMeshComponent>(Rig);
		Body->SetMobility(EComponentMobility::Movable);
		Body->SetSkinnedAssetAndUpdate(SkinnedCube);
		Body->SetMaterial(0, Material);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetCastShadow(false);
		const float Radius = FMath::Max(SkinnedCube->GetBounds().SphereRadius, 1.0f);
		Body->SetWorldTransform(FTransform(FQuat::Identity, Origin + FVector(600.0, -120.0 + 120.0 * Slot, 200.0), FVector(40.0f / Radius)));
		Body->RegisterComponent();
		Body->SetVisibility(false);
		Groups[EngineGroupCpd].Add(Body);
		Skinned.Add(Body);
	}
	if (SkinnedCube == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("EngineChecks: the engine's skinned cube is not there; no custom primitive data check."));
	}

	PointLight = NewObject<UPointLightComponent>(Rig);
	PointLight->SetMobility(EComponentMobility::Movable);
	PointLight->bUseInverseSquaredFalloff = false;
	PointLight->SourceRadius = 0.0f;
	PointLight->RegisterComponent();
	PointLight->SetVisibility(false);
	SpotLight = NewObject<USpotLightComponent>(Rig);
	SpotLight->SetMobility(EComponentMobility::Movable);
	SpotLight->bUseInverseSquaredFalloff = false;
	SpotLight->LightFalloffExponent = 1.0f;
	SpotLight->SourceRadius = 0.0f;
	SpotLight->RegisterComponent();
	SpotLight->SetVisibility(false);

	Table = TEXT("config,name,region,code,peak,r,g,b,light,peak_light,expected,ratio,plan\n");
	CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UScctLightEngineChecks::OnCaptured);
	UE_LOG(LogOpenSCCT, Log, TEXT("EngineChecks: the room stands at %s, %d arrangements, scct.Light.Plan %d."), *Origin.ToCompactString(),
		static_cast<int32>(UE_ARRAY_COUNT(EngineCheckConfigs)), FScctLightPlan::Active());
	return true;
#else
	UE_LOG(LogOpenSCCT, Error, TEXT("EngineChecks: this build cannot make the materials of the checks (an editor build can)."));
	return false;
#endif
}

void UScctLightEngineChecks::Apply(int32 Index)
{
	UWorld* World = GetWorld();
	const FEngineCheckConfig& Entry = EngineCheckConfigs[Index];
	for (int32 Group = 0; Group < Groups.Num(); ++Group)
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& Piece : Groups[Group])
		{
			if (UPrimitiveComponent* Shown = Piece.Get())
			{
				Shown->SetVisibility(Group == Entry.Group);
			}
		}
	}
	PointLight->SetVisibility(false);
	SpotLight->SetVisibility(false);
	Ceiling->SetVisibility(true);

	// Dark walls wherever a check reads a lamp's own light, so the room's bounce does not join it.
	WallMaterial->SetScalarParameterValue(EngineParamAlbedo, Entry.Group == EngineGroupBounce ? 0.5f : 0.02f);

	// The level's sky light and fog, as the level has them except where a check needs more of one.
	USkyLightComponent* Sky = nullptr;
	for (TObjectIterator<USkyLightComponent> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsRegistered())
		{
			Sky = *It;
			break;
		}
	}
	UExponentialHeightFogComponent* Fog = nullptr;
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		Fog = It->GetComponent();
		break;
	}
	if (Sky != nullptr)
	{
		if (SkyIntensityWas < 0.0f)
		{
			SkyIntensityWas = Sky->Intensity;
		}
		Sky->SetIntensity(Index == 11 ? FMath::Max(SkyIntensityWas * 50.0f, 10.0f) : SkyIntensityWas);
	}
	if (Fog != nullptr)
	{
		if (FogDensityWas < 0.0f)
		{
			FogDensityWas = Fog->FogDensity;
			FogFalloffWas = Fog->FogHeightFalloff;
			bFogVolumetricWas = Fog->bEnableVolumetricFog;
		}
		const bool bThick = Entry.Group == EngineGroupFog;
		Fog->SetFogDensity(bThick ? 0.05f : FogDensityWas);
		Fog->SetFogHeightFalloff(bThick ? 0.001f : FogFalloffWas);
		Fog->SetVolumetricFog(bThick ? true : bFogVolumetricWas);
	}

	switch (Index)
	{
	case 1: case 2: case 3:
		Point(EngineFalloffLight, EngineFalloffRadius, EngineFalloffExponents[Index - 1], EngineFalloffIntensity, false, true, false, false);
		break;
	case 4: case 5: case 6:
		Point(EngineDiffuseLight, 600.0f, 1.0f, 4.0f, false, true, false, false);
		PointLight->SetDiffuseScale(Index == 5 ? 0.0f : 1.0f);
		PointLight->SetSpecularScale(Index == 6 ? 0.0f : 1.0f);
		break;
	case 7: case 8: case 9:
		Point(EngineChannelLight, EngineChannelRadius, 1.0f, EngineChannelIntensity, true, Index != 9, Index != 8, false);
		break;
	case 10:
		Spot(EngineBounceLamp, FVector(EngineBounceLamp.X, EngineBounceLamp.Y, 0.0), 30.0f, 700.0f, 20.0f, 0.0f, true, false, false);
		break;
	case 11:
		Ceiling->SetVisibility(false);
		break;
	case 12: case 13: case 14: case 15:
		Spot(EngineFogLight, EngineFogCard, 12.0f, 900.0f, 30.0f, 30.0f, Index == 12 || Index == 15, Index == 13, Index == 14);
		if (Index == 15)
		{
			SpotLight->SetDiffuseScale(0.0f);
			SpotLight->SetSpecularScale(0.0f);
		}
		break;
	case 19:
		Spot(EngineFogLight, EngineFogCard, 12.0f, 900.0f, 30.0f, 30.0f, true, false, false);
		SpotLight->SetDiffuseScale(0.0f);
		break;
	case 20:
		Spot(EngineFogLight, EngineFogCard, 12.0f, 900.0f, 30.0f, 3000.0f, true, false, false);
		SpotLight->SetDiffuseScale(0.01f);
		SpotLight->SetSpecularScale(0.01f);
		break;
	case 21: case 22:
		Spot(EngineFogLight, EngineFogCard, 12.0f, 900.0f, 30.0f, 30.0f, Index == 22, Index == 21, false);
		if (Index == 22)
		{
			SpotLight->SetDiffuseScale(0.0f);
			SpotLight->SetSpecularScale(0.0f);
		}
		SpotLight->bAllowMegaLights = false;
		SpotLight->MarkRenderStateDirty();
		break;
	case 23:
		// The ramp again with this measurement's own exposure a stop up: its patches would read at twice the scene
		// light if this volume were the one that sets the picture's exposure. They read the same (run with Plan 0):
		// the game's exposure volume is applied after this one.
		Volume->Settings.AutoExposureBias = 1.0f;
		break;
	case 17: case 18:
		for (int32 Slot = 0; Slot < Skinned.Num(); ++Slot)
		{
			Skinned[Slot]->SetCustomPrimitiveDataFloat(EngineCpdIndex[Slot], Index == 17 ? EngineCpdValue : 0.0f);
		}
		break;
	default:
		break;
	}

	if (UScctSessionSubsystem* Session = World->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->SetCheckCamera(Camera);
	}
	Config = Index;
	Phase = EPhase::Settle;
	PhaseAt = FPlatformTime::Seconds();
}

void UScctLightEngineChecks::Regions(int32 Index, TArray<FRegion>& Out) const
{
	const auto Add = [&Out](const FString& Label, const FVector& At, float Half, float Expected = 0.0f)
	{
		FRegion& Region = Out.AddDefaulted_GetRef();
		Region.Label = Label;
		Region.At = At;
		Region.Half = Half;
		Region.Expected = Expected;
	};
	// A dark piece of the far wall in every arrangement: what the room itself shows.
	Add(TEXT("far wall"), FVector(EngineCheckDepth - 1.0, 0.0, 330.0), 12.0f);
	switch (EngineCheckConfigs[Index].Group)
	{
	case EngineGroupRamp:
		for (int32 Patch = 0; Patch < UE_ARRAY_COUNT(EngineCheckRamp); ++Patch)
		{
			Add(FString::Printf(TEXT("patch %.3f"), EngineCheckRamp[Patch]), FVector(EngineCheckDepth - 2.0, -350.0 + 50.0 * Patch, 200.0), 8.0f, EngineCheckRamp[Patch]);
		}
		break;
	case EngineGroupFalloff:
		for (int32 Card = 0; Card < UE_ARRAY_COUNT(EngineFalloffShare); ++Card)
		{
			const float Share = EngineFalloffShare[Card];
			const float Falloff = FMath::Pow(1.0f - Share * Share, EngineFalloffExponents[Index - 1]);
			Add(FString::Printf(TEXT("card d/R=%.2f falloff=%.4f"), Share, Falloff), EngineFalloffCard(Card), 7.0f, EngineFalloffAlbedo * EngineFalloffIntensity * Falloff);
		}
		break;
	case EngineGroupDiffuse:
		Add(TEXT("matte card"), EngineDiffuseMatte, 12.0f);
		Add(TEXT("polished card"), EngineDiffuseGloss, 24.0f);
		break;
	case EngineGroupChannels:
		Add(TEXT("sphere ch1 in the open"), EngineChannelSphereA, 9.0f);
		Add(TEXT("sphere ch1 behind a ch0 block"), EngineChannelSphereB, 9.0f);
		Add(TEXT("card ch0 in the open"), EngineChannelCard, 10.0f);
		Add(TEXT("card ch0 behind the ch1 sphere"), EngineChannelSphereA + (EngineChannelSphereA - EngineChannelLight).GetSafeNormal() * 120.0, 6.0f);
		break;
	case EngineGroupBounce:
		Add(TEXT("floor in the pool"), FVector(EngineBounceLamp.X, 0.0, 0.5), 20.0f);
		for (int32 Sphere = 0; Sphere < UE_ARRAY_COUNT(EngineBounceOcclusion); ++Sphere)
		{
			Add(EngineBounceNames[Sphere], FVector(650.0, -225.0 + 150.0 * Sphere, 150.0), 10.0f);
		}
		break;
	case EngineGroupFog:
		Add(TEXT("beam in the air"), EngineFogBeam, 15.0f);
		Add(TEXT("air beside the beam"), EngineFogBeam + FVector(0.0, 0.0, 150.0), 15.0f);
		Add(TEXT("landing card ch0"), EngineFogCard, 10.0f);
		break;
	case EngineGroupCpd:
		// The engine's skinned cube stands on its origin: its middle is some 22 cm above where it was put.
		for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(EngineCpdIndex); ++Slot)
		{
			Add(FString::Printf(TEXT("skinned cube float %d"), EngineCpdIndex[Slot]), FVector(600.0, -120.0 + 120.0 * Slot, 222.0), 5.0f, Index == 17 ? EngineCpdValue : 0.0f);
		}
		break;
	default:
		break;
	}
}

float UScctLightEngineChecks::LightOf(float Luma) const
{
	// The ramp read backwards: between two patches by the logarithm of their light (straight from black to the first).
	const int32 Count = RampLuma.Num();
	if (Count < 2 || Luma < 0.0f)
	{
		return -1.0f;
	}
	if (Luma <= RampLuma[0])
	{
		return 0.0f;
	}
	for (int32 Patch = 1; Patch < Count; ++Patch)
	{
		if (Luma <= RampLuma[Patch] && RampLuma[Patch] > RampLuma[Patch - 1])
		{
			const float Share = (Luma - RampLuma[Patch - 1]) / (RampLuma[Patch] - RampLuma[Patch - 1]);
			if (RampLight[Patch - 1] <= 0.0f)
			{
				return Share * RampLight[Patch];
			}
			return RampLight[Patch - 1] * FMath::Pow(RampLight[Patch] / RampLight[Patch - 1], Share);
		}
	}
	return RampLight.Last();
}

void UScctLightEngineChecks::OnCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (Phase != EPhase::Read || bCaptured || Width <= 0 || Height <= 0 || Bitmap.Num() < Width * Height)
	{
		return;
	}
	bCaptured = true;
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
	const FEngineCheckConfig& Entry = EngineCheckConfigs[Config];
	TArray<FRegion> Wanted;
	Regions(Config, Wanted);

	struct FRead { float Luma = -1.0f, Peak = -1.0f, R = 0.0f, G = 0.0f, B = 0.0f; };
	TArray<FRead> Reads;
	for (const FRegion& Region : Wanted)
	{
		FRead& Read = Reads.AddDefaulted_GetRef();
		const FVector At = Origin + Region.At;
		FVector2D Centre, Beside;
		if (!Player->ProjectWorldLocationToScreen(At, Centre, false) || !Player->ProjectWorldLocationToScreen(At + Camera.GetUnitAxis(EAxis::Y) * 10.0, Beside, false))
		{
			continue;
		}
		const double Reach = Region.Half * FVector2D::Distance(Centre, Beside) * Scale / 10.0;
		const int32 X0 = FMath::Max(0, FMath::FloorToInt32(Centre.X * Scale - Reach)), X1 = FMath::Min(Width - 1, FMath::CeilToInt32(Centre.X * Scale + Reach));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt32(Centre.Y * Scale - Reach)), Y1 = FMath::Min(Height - 1, FMath::CeilToInt32(Centre.Y * Scale + Reach));
		double Sum = 0.0, R = 0.0, G = 0.0, B = 0.0;
		float Peak = 0.0f;
		int32 Count = 0;
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FColor& Pixel = Bitmap[Y * Width + X];
				const float Luma = EngineCheckLuma(Pixel);
				Sum += Luma;
				Peak = FMath::Max(Peak, Luma);
				R += Pixel.R;
				G += Pixel.G;
				B += Pixel.B;
				++Count;
			}
		}
		if (Count > 0)
		{
			Read.Luma = static_cast<float>(Sum / Count);
			Read.Peak = Peak;
			Read.R = static_cast<float>(R / Count);
			Read.G = static_cast<float>(G / Count);
			Read.B = static_cast<float>(B / Count);
		}
	}

	// Which volumes the view is made of, in the order they are applied (the last to set a value wins), once.
	if (Config == 0)
	{
		for (IInterface_PostProcessVolume& Each : GetWorld()->GetPostProcessVolumeIterator())
		{
			const FPostProcessVolumeProperties Properties = Each.GetProperties();
			const FPostProcessSettings* Set = Properties.Settings;
			const FString Method = Set != nullptr && Set->bOverride_AutoExposureMethod ? FString::FromInt(static_cast<int32>(Set->AutoExposureMethod.GetValue())) : FString(TEXT("-"));
			const FString Bias = Set != nullptr && Set->bOverride_AutoExposureBias ? FString::SanitizeFloat(Set->AutoExposureBias) : FString(TEXT("-"));
			UE_LOG(LogOpenSCCT, Log, TEXT("EngineChecks: volume priority %.0f unbound %d weight %.2f enabled %d: method %s, bias %s"), Properties.Priority,
				Properties.bIsUnbound ? 1 : 0, Properties.BlendWeight, Properties.bIsEnabled ? 1 : 0, *Method, *Bias);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("EngineChecks: %02d %s read at exposure scale %.4f."), Config, Entry.Name, FScctExposure::Get());

	if (Config == 0)
	{
		RampLight.Reset();
		RampLuma.Reset();
		for (int32 Region = 1; Region < Wanted.Num(); ++Region)
		{
			RampLight.Add(Wanted[Region].Expected);
			RampLuma.Add(Reads[Region].Luma);
		}
	}

	const int32 Plan = FScctLightPlan::Active();
	for (int32 Region = 0; Region < Wanted.Num(); ++Region)
	{
		const FRead& Read = Reads[Region];
		const float Light = LightOf(Read.Luma);
		const float PeakLight = LightOf(Read.Peak);
		const float Expected = Wanted[Region].Expected;
		const float Ratio = Expected > 0.0f && Light >= 0.0f ? Light / Expected : -1.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_ENGINE %02d %s | %s | code=%.1f peak=%.1f rgb=%.0f,%.0f,%.0f light=%.5f peak_light=%.5f expected=%.5f ratio=%.4f"),
			Config, Entry.Name, *Wanted[Region].Label, Read.Luma, Read.Peak, Read.R, Read.G, Read.B, Light, PeakLight, Expected, Ratio);
		Table += FString::Printf(TEXT("%d,%s,%s,%.2f,%.2f,%.1f,%.1f,%.1f,%.6f,%.6f,%.6f,%.5f,%d\n"), Config, Entry.Name, *Wanted[Region].Label, Read.Luma, Read.Peak,
			Read.R, Read.G, Read.B, Light, PeakLight, Expected, Ratio, Plan);
	}

	TArray<FColor> Opaque = Bitmap;
	for (FColor& Pixel : Opaque)
	{
		Pixel.A = 255;
	}
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	FFileHelper::SaveArrayToFile(Png, *(Directory / FString::Printf(TEXT("engine_%02d_%s.png"), Config, Entry.Name)));
}

void UScctLightEngineChecks::Tick(float DeltaTime)
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
		if (Session->GetBridge() == nullptr || Session->GetView().Valid == 0)
		{
			ReadyAt = -1.0;
			break;
		}
		if (ReadyAt < 0.0)
		{
			ReadyAt = Now;
		}
		if (Now - ReadyAt >= EngineCheckMatchSeconds)
		{
			if (Build())
			{
				Apply(0);
			}
			else
			{
				Phase = EPhase::Done;
				FPlatformMisc::RequestExit(false);
			}
		}
		break;

	case EPhase::Settle:
		if (Now - PhaseAt >= (Config == 0 ? EngineCheckFirstSettle : EngineCheckSettle))
		{
			bCaptured = false;
			FScreenshotRequest::RequestScreenshot(false);
			Phase = EPhase::Read;
			PhaseAt = Now;
		}
		break;

	case EPhase::Read:
		if (!bCaptured && Now - PhaseAt < EngineCheckReadSeconds)
		{
			break;
		}
		if (!bCaptured)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("EngineChecks: the picture of arrangement %d could not be read back."), Config);
		}
		if (Config + 1 < UE_ARRAY_COUNT(EngineCheckConfigs))
		{
			Apply(Config + 1);
		}
		else
		{
			Finish();
		}
		break;

	default:
		break;
	}
}

void UScctLightEngineChecks::Finish()
{
	Phase = EPhase::Done;
	const FString File = Directory / TEXT("engine_checks.csv");
	FFileHelper::SaveStringToFile(Table, *File);
	UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_ENGINE_DONE %d arrangements, table %s"), static_cast<int32>(UE_ARRAY_COUNT(EngineCheckConfigs)), *File);
	if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->ClearCheckCamera();
	}
	FPlatformMisc::RequestExit(false);
}
