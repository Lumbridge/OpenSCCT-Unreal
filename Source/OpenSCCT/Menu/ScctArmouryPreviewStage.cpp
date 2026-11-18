#include "Menu/ScctArmouryPreviewStage.h"

#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Play/ScctCosmeticMaterial.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	const FName ArmouryStageBaseColor(TEXT("BaseColor"));

	// The view: the Godot preview's 28 degrees top to bottom, here across the target's wider side.
	constexpr float ArmouryStageHalfFov = 14.0f;
	constexpr float ArmouryStageTurnPerSecond = 24.0f;
	constexpr int32 ArmouryStageMaxBones = 512;

	/** Where the stage stands: far above anything a map has. */
	const FVector ArmouryStageHome(0.0, 0.0, 400000.0);

	/** A lamp: where it hangs from the model (cm, the camera is on +X), its colour and its light on the model in lux. */
	struct FArmouryStageLamp
	{
		FVector Towards;
		FLinearColor Colour;
		float Lux;
	};
	constexpr float ArmouryStageLampDistance = 400.0f;
	const FArmouryStageLamp ArmouryStageLamps[] = {
		{ FVector(0.9, -0.5, 0.7), FLinearColor(1.0f, 0.96f, 0.9f), 3.2f },   // the warm key from the upper front
		{ FVector(0.4, 0.8, 0.2), FLinearColor(0.55f, 0.6f, 0.7f), 1.3f },    // the cool fill from the other side
		{ FVector(-1.0, 0.3, 0.5), FLinearColor(0.49f, 1.0f, 0.58f), 2.0f },  // the green rim from behind
		{ FVector(1.0, 0.0, 0.1), FLinearColor(0.9f, 0.95f, 0.92f), 0.9f },   // a soft front light in place of an ambient
	};

	FString ArmouryStageKey(const FString& Name)
	{
		FString Key;
		for (const TCHAR Character : Name)
		{
			if (FChar::IsAlnum(Character))
			{
				Key.AppendChar(FChar::ToLower(Character));
			}
		}
		return Key;
	}

	/** The package folder of an imported texture ("SPerso" for /Game/Imported/Chars/SPerso/...), or empty. */
	FString ArmouryStagePackageOf(const UTexture& Texture)
	{
		TArray<FString> Parts;
		Texture.GetPathName().ParseIntoArray(Parts, TEXT("/"));
		const int32 Chars = Parts.IndexOfByKey(FString(TEXT("Chars")));
		return Chars != INDEX_NONE && Chars + 1 < Parts.Num() ? Parts[Chars + 1] : FString();
	}

	void ArmouryStageSetUpLamp(UPointLightComponent& Lamp, const FLinearColor& Colour, float Candelas, float Radius)
	{
		Lamp.SetMobility(EComponentMobility::Movable);
		Lamp.IntensityUnits = ELightUnits::Candelas;
		Lamp.Intensity = Candelas;
		Lamp.LightColor = Colour.ToFColor(true);
		Lamp.AttenuationRadius = Radius;
		Lamp.CastShadows = false;
		Lamp.IndirectLightingIntensity = 0.0f;
		Lamp.VolumetricScatteringIntensity = 0.0f;
		Lamp.bAffectTranslucentLighting = false;
		// The preview has its own view bit. Normal world lamps can use both world receiver
		// channels without reaching the turntable, even with a directional light in the map.
		Lamp.ViewLightingChannels.bViewChannel0 = false;
		Lamp.ViewLightingChannels.bViewChannel1 = true;
		Lamp.LightingChannels.bChannel0 = false;
		Lamp.LightingChannels.bChannel1 = false;
		Lamp.LightingChannels.bChannel2 = true;
	}

	TMap<FString, TStrongObjectPtr<UTexture2D>>& ArmouryStagePictures()
	{
		static TMap<FString, TStrongObjectPtr<UTexture2D>> Pictures;
		static bool bHooked = false;
		if (!bHooked)
		{
			bHooked = true;
			FCoreDelegates::OnPreExit.AddLambda([] { Pictures.Empty(); });
		}
		return Pictures;
	}
}

AScctArmouryPreviewStage::AScctArmouryPreviewStage()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Turntable = CreateDefaultSubobject<USceneComponent>(TEXT("Turntable"));
	Turntable->SetupAttachment(Root);

	Body = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Turntable);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	Body->bVisibleInSceneCaptureOnly = true;
	Body->LightingChannels.bChannel0 = false;
	Body->LightingChannels.bChannel2 = true;
	// The idle pose leaves the rest pose's bounds a little; the capture must never cull the model.
	Body->BoundsScale = 4.0f;

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->ViewLightingChannels.bViewChannel0 = false;
	Capture->ViewLightingChannels.bViewChannel1 = true;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = true;
	Capture->bCaptureOnMovement = false;
	const float Aspect = static_cast<float>(TargetWidth) / static_cast<float>(TargetHeight);
	Capture->FOVAngle = 2.0f * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(ArmouryStageHalfFov)) * FMath::Max(Aspect, 1.0f)));
	// Only the model under the stage's lamps: nothing of the world's sky, fog, bounce light or screen effects.
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetSkyLighting(false);
	Capture->ShowFlags.SetGlobalIllumination(false);
	Capture->ShowFlags.SetLumenGlobalIllumination(false);
	Capture->ShowFlags.SetLumenReflections(false);
	Capture->ShowFlags.SetReflectionEnvironment(false);
	Capture->ShowFlags.SetScreenSpaceReflections(false);
	Capture->ShowFlags.SetAmbientOcclusion(false);
	Capture->ShowFlags.SetDynamicShadows(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetBloom(false);
	Capture->ShowFlags.SetLensFlares(false);
	Capture->ShowFlags.SetVignette(false);
	Capture->ShowFlags.SetGrain(false);
	Capture->ShowFlags.SetDepthOfField(false);
	Capture->ShowFlags.SetTemporalAA(false);
	// A fixed exposure: the lamps are set in lux for it.
	Capture->PostProcessBlendWeight = 1.0f;
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = 0.7f;
	Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;

	const float Metres = ArmouryStageLampDistance / 100.0f;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ArmouryStageLamps); ++Index)
	{
		const FArmouryStageLamp& Each = ArmouryStageLamps[Index];
		UPointLightComponent* Lamp = CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("Lamp%d"), Index));
		Lamp->SetupAttachment(Root);
		Lamp->SetRelativeLocation(Each.Towards.GetSafeNormal() * ArmouryStageLampDistance);
		ArmouryStageSetUpLamp(*Lamp, Each.Colour, Each.Lux * Metres * Metres, 1200.0f);
		Lamps.Add(Lamp);
	}

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Turntable);
	ArmouryStageSetUpLamp(*Glow, FLinearColor::White, 1.2f, 150.0f);
}

AScctArmouryPreviewStage* AScctArmouryPreviewStage::Make(UWorld* World)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AScctArmouryPreviewStage* Stage = World->SpawnActor<AScctArmouryPreviewStage>(ArmouryStageHome, FRotator::ZeroRotator, Params);
	if (Stage == nullptr)
	{
		return nullptr;
	}

	Stage->Target = NewObject<UTextureRenderTarget2D>(Stage, TEXT("ArmouryPreviewTarget"), RF_Transient);
	Stage->Target->RenderTargetFormat = RTF_RGBA8_SRGB;
	Stage->Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	Stage->Target->InitAutoFormat(TargetWidth, TargetHeight);
	Stage->Target->UpdateResourceImmediate(true);
	Stage->Capture->TextureTarget = Stage->Target;
	Stage->Capture->ShowOnlyComponent(Stage->Body);
	Stage->Body->SetVisibility(false);
	Stage->Glow->SetVisibility(false);
	return Stage;
}

UTexture2D* AScctArmouryPreviewStage::LoadPicture(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	TMap<FString, TStrongObjectPtr<UTexture2D>>& Pictures = ArmouryStagePictures();
	if (const TStrongObjectPtr<UTexture2D>* Found = Pictures.Find(Path))
	{
		return Found->Get();
	}
	UTexture2D* Texture = FPaths::FileExists(Path) ? FImageUtils::ImportFileAsTexture2D(Path) : nullptr;
	UE_CLOG(Texture == nullptr, LogOpenSCCT, Warning, TEXT("Armoury preview: could not read %s"), *Path);
	Pictures.Add(Path, TStrongObjectPtr<UTexture2D>(Texture));
	return Texture;
}

void AScctArmouryPreviewStage::Show(int32 Side, int32 Slot, const FString& PatternItem, const FString& LightItem)
{
	const FScctArmouryPreviewApi* Api = ScctFeature<FScctArmouryPreviewApi>();
	if (Api == nullptr)
	{
		return;
	}
	const FString Key = FString::Printf(TEXT("%d|%d|%s|%s"), Side, Slot, *PatternItem, *LightItem);
	if (Key == ShownKey)
	{
		return;
	}
	ShownKey = Key;

	TCHAR Asset[128] = {};
	const int32 View = Api->SlotModel(Side, Slot, Asset, UE_ARRAY_COUNT(Asset));
	if (ShownAsset != Asset || View != ShownView)
	{
		LoadModel(Asset, View == EScctArmouryView::Gun ? INDEX_NONE : FMath::Clamp(Side, 0, 1), View);
	}
	if (!bHasModel)
	{
		return;
	}

	FScctArmouryPreviewItem Item = {};
	TCHAR Pattern[128] = {};
	TCHAR Thumb[1024] = {};
	if (PatternItem.IsEmpty() || Api->GetItem(*PatternItem, &Item, Pattern, UE_ARRAY_COUNT(Pattern), Thumb, UE_ARRAY_COUNT(Thumb)) == 0)
	{
		Pattern[0] = 0;
	}
	Dress(Pattern);

	// A light item: the head lit in its colour (the goggle lights' own mesh is not on the stage).
	TCHAR None[4] = {};
	const bool bGlow = View != EScctArmouryView::Gun && !LightItem.IsEmpty() &&
		Api->GetItem(*LightItem, &Item, None, UE_ARRAY_COUNT(None), None, UE_ARRAY_COUNT(None)) != 0 && Item.HasHue != 0;
	if (bGlow)
	{
		Glow->SetLightColor(FLinearColor(FMath::Fmod(Item.Hue + 720.0f, 360.0f), 0.85f, 1.0f).HSVToLinearRGB());
	}
	Glow->SetVisibility(bGlow);
}

void AScctArmouryPreviewStage::LoadModel(const FString& Asset, int32 Kind, int32 View)
{
	ShownAsset = Asset;
	ShownView = View;
	PosedKind = INDEX_NONE;
	BoneMap.Reset();
	Sources.Reset();
	bHasModel = false;

	FString Package, Name;
	USkeletalMesh* Model = Asset.Split(TEXT("."), &Package, &Name)
		? LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/Imported/Chars/%s/%s/SkeletalMeshes/%s.%s"), *Package, *Name, *Name, *Name), nullptr, LOAD_NoWarn)
		: nullptr;
	if (Model == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Armoury preview: no imported model for %s (Tools/Import/import_characters.py); the item's flat picture is shown"), *Asset);
		Body->SetVisibility(false);
		return;
	}

	Body->EmptyOverrideMaterials();
	Body->SetSkinnedAssetAndUpdate(Model);
	// The characters face the file's +Z, which the importer turns into +Y: a quarter turn from the stage's front.
	Body->SetRelativeRotation(Kind != INDEX_NONE ? FRotator(0.0, -90.0, 0.0) : FRotator::ZeroRotator);
	for (const FSkeletalMaterial& Material : Model->GetMaterials())
	{
		Sources.Add(Material.MaterialInterface);
	}
	Body->SetVisibility(true);
	bHasModel = true;

	const FScctArmouryPreviewApi* Api = ScctFeature<FScctArmouryPreviewApi>();
	if (Kind != INDEX_NONE && Api != nullptr)
	{
		TArray<FScctBridgeBoneInfo> Skeleton;
		Skeleton.SetNumZeroed(ArmouryStageMaxBones);
		const int32 Count = FMath::Clamp(Api->GetSkeleton(Kind, Skeleton.GetData(), Skeleton.Num()), 0, Skeleton.Num());
		const FReferenceSkeleton& Reference = Model->GetRefSkeleton();
		TMap<FString, int32> ByKey;
		for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
		{
			ByKey.Add(ArmouryStageKey(Reference.GetBoneName(Index).ToString()), Index);
		}
		int32 Matched = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32* Found = ByKey.Find(ArmouryStageKey(FString(Skeleton[Index].Name)));
			BoneMap.Add(Found != nullptr ? *Found : INDEX_NONE);
			Matched += Found != nullptr ? 1 : 0;
		}
		Pose.SetNumZeroed(Count);
		PosedKind = Count > 0 ? Kind : INDEX_NONE;
		UE_LOG(LogOpenSCCT, Log, TEXT("Armoury preview: %s on the stage, %d of the bridge's %d bones matched%s"), *Asset, Matched, Count,
			Count > 0 ? TEXT("") : TEXT(" (no character file in the content: the model stands in its rest pose)"));
	}
	else
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Armoury preview: %s on the stage"), *Asset);
	}
	Frame(View);
}

void AScctArmouryPreviewStage::Frame(int32 View)
{
	const USkinnedAsset* Model = Body->GetSkinnedAsset();
	if (Model == nullptr)
	{
		return;
	}
	// The model's rest bounds as it stands on the table, its middle put on the table's axis.
	const FBoxSphereBounds Bounds = Model->GetBounds();
	const FRotator Turn = Body->GetRelativeRotation();
	FVector Centre = Turn.RotateVector(Bounds.Origin);
	const float Height = Bounds.BoxExtent.Z * 2.0f;
	const float Across = 2.0f * FMath::Sqrt(FMath::Square(Bounds.BoxExtent.X) + FMath::Square(Bounds.BoxExtent.Y));
	const float Top = Centre.Z + Bounds.BoxExtent.Z;
	float Radius = FMath::Max(FMath::Max(Height, Across) * 0.5f, 1.0f);
	const float HeadZ = Top - Height * 0.1f;
	if (View == EScctArmouryView::Head)
	{
		// The lights sit on the head: the top of the character.
		Centre.Z = HeadZ;
		Radius = Height * 0.14f;
	}
	Body->SetRelativeLocation(-Centre);
	Glow->SetRelativeLocation(FVector(45.0, 0.0, HeadZ - Centre.Z));

	const float Distance = Radius / FMath::Sin(FMath::DegreesToRadians(ArmouryStageHalfFov)) * 1.05f;
	const FVector Eye(Distance, 0.0, Radius * 0.25f);
	Capture->SetRelativeLocationAndRotation(Eye, (-Eye).Rotation());
}

void AScctArmouryPreviewStage::Dress(const FString& Pattern)
{
	const FScctArmouryPreviewApi* Api = ScctFeature<FScctArmouryPreviewApi>();
	int32 Dressed = 0;
	for (int32 Slot = 0; Slot < Sources.Num(); ++Slot)
	{
		UMaterialInterface* Source = Sources[Slot];
		UTexture* Retail = nullptr;
		UTexture2D* Made = nullptr;
		TCHAR Path[1024] = {};
		if (Api != nullptr && Source != nullptr && !Pattern.IsEmpty() && Source->GetTextureParameterValue(ArmouryStageBaseColor, Retail) && Retail != nullptr)
		{
			const FString Package = ArmouryStagePackageOf(*Retail);
			if (!Package.IsEmpty() && Api->CosmeticTexture(*Pattern, *Package, *Retail->GetName(), Path, UE_ARRAY_COUNT(Path)) > 0)
			{
				Made = LoadPicture(Path);
			}
		}
		if (Made != nullptr)
		{
			UMaterialInstanceDynamic* Instance = ScctCosmeticMaterial::Create(this, Source, Made, Pattern, Path);
			Body->SetMaterial(Slot, Instance);
			++Dressed;
		}
		else
		{
			Body->SetMaterial(Slot, Source);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Armoury preview: %s wears '%s' on %d of %d material slots"), *ShownAsset, Pattern.IsEmpty() ? TEXT("retail") : *Pattern, Dressed, Sources.Num());
}

void AScctArmouryPreviewStage::Advance(float DeltaSeconds, bool bHeld)
{
	Time += DeltaSeconds;
	if (!bHeld)
	{
		Yaw = FMath::Fmod(Yaw + ArmouryStageTurnPerSecond * DeltaSeconds, 360.0f);
	}
	Turntable->SetRelativeRotation(FRotator(0.0, Yaw, 0.0));

	const FScctArmouryPreviewApi* Api = ScctFeature<FScctArmouryPreviewApi>();
	if (!bHasModel || PosedKind == INDEX_NONE || Api == nullptr || Pose.Num() == 0)
	{
		return;
	}
	const int32 Count = FMath::Min(Api->GetIdlePose(PosedKind, Time, Pose.GetData(), Pose.Num()), FMath::Min(Pose.Num(), BoneMap.Num()));
	TArray<FTransform>& Local = Body->BoneSpaceTransforms;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 MeshBone = BoneMap[Index];
		if (Local.IsValidIndex(MeshBone))
		{
			Local[MeshBone] = FScctPose::ToUnreal(Pose[Index]);
		}
	}
	if (Count > 0)
	{
		Body->RefreshBoneTransforms();
	}
}

void AScctArmouryPreviewStage::Turn(float Degrees)
{
	Yaw = FMath::Fmod(Yaw + Degrees, 360.0f);
}
