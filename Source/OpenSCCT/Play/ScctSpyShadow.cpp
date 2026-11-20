#include "Play/ScctSpyShadow.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Hud/ScctVisionSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "OpenSCCT.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	TAutoConsoleVariable<bool> CVarSpyShadow(TEXT("scct.Light.SpyShadow"), true,
		TEXT("Project the local spy shadow on baked surfaces."));
	constexpr float CastRange = 300.0f;
	constexpr float ViewRange = 600.0f;
	constexpr float CaptureDistance = 300.0f;
	constexpr float EdgeMargin = 8.0f;

	// The ordinary ATT_01 skeleton and the equivalent joints of map outfits. The capsules describe the
	// width of the suit in centimetres. This uses the displayed pose, including crawls and climbs.
	const struct FShadowPart
	{
		const TCHAR* First;
		const TCHAR* Last;
		float Radius;
	} ShadowParts[] = {
		{TEXT("B Pelvis"), TEXT("B Neck"), 17.0f},
		{TEXT("B L UpperArm"), TEXT("B R UpperArm"), 10.0f},
		{TEXT("B Head"), TEXT("B Head"), 12.0f},
		{TEXT("B L UpperArm"), TEXT("B L ForeArm"), 7.5f},
		{TEXT("B L ForeArm"), TEXT("B L Hand"), 6.0f},
		{TEXT("B R UpperArm"), TEXT("B R ForeArm"), 7.5f},
		{TEXT("B R ForeArm"), TEXT("B R Hand"), 6.0f},
		{TEXT("B L Thigh"), TEXT("B L Calf"), 9.0f},
		{TEXT("B L Calf"), TEXT("B L Foot"), 7.0f},
		{TEXT("B L Foot"), TEXT("B L Toe0"), 5.5f},
		{TEXT("B R Thigh"), TEXT("B R Calf"), 9.0f},
		{TEXT("B R Calf"), TEXT("B R Foot"), 7.0f},
		{TEXT("B R Foot"), TEXT("B R Toe0"), 5.5f},
	};

	constexpr int32 PartCount = UE_ARRAY_COUNT(ShadowParts);

	const TArray<FName>& PartParameters(const TCHAR* Prefix, TArray<FName>& Names)
	{
		if (Names.IsEmpty())
		{
			for (int32 Part = 0; Part < PartCount; ++Part)
			{
				Names.Emplace(*FString::Printf(TEXT("%s%d"), Prefix, Part));
			}
		}
		return Names;
	}
}

bool UScctSpyShadow::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UScctSpyShadow::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (auto* Session = Collection.InitializeDependency<UScctSessionSubsystem>())
	{
		Session->OnFrame.AddUObject(this, &UScctSpyShadow::Present);
		Session->OnMatchStopped.AddUObject(this, &UScctSpyShadow::EndSession);
	}
}

void UScctSpyShadow::Deinitialize()
{
	if (auto* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->OnFrame.RemoveAll(this);
		Session->OnMatchStopped.RemoveAll(this);
	}
	EndSession();
	Super::Deinitialize();
}

void UScctSpyShadow::EndSession()
{
	if (LocalProjection.Projection) LocalProjection.Projection->Destroy();
	LocalProjection = {};
}

bool UScctSpyShadow::CreateProjection(FScctPawnProjection& State)
{
	auto& Projection = State.Projection;
	auto& Material = State.Material;
	static const TCHAR* MaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctSpyShadow.M_ScctSpyShadow");
	auto* Parent = LoadObject<UMaterialInterface>(nullptr, MaterialPath, nullptr, LOAD_NoWarn);
	auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Parent || !Cube)
	{
		bAssetMissing = true;
		UE_LOG(LogOpenSCCT, Warning, TEXT("Spy shadow material missing (Tools/Materials/build_spy_shadow.py)"));
		return false;
	}

	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags = RF_Transient;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Projection = GetWorld()->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	if (!Projection)
	{
		return false;
	}
	Projection->SetActorHiddenInGame(true);
	UStaticMeshComponent* Mesh = Projection->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(Cube);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);
	Mesh->SetAffectDistanceFieldLighting(false);
	Mesh->SetAffectDynamicIndirectLighting(false);
	Mesh->SetHiddenInSceneCapture(true);
	Mesh->SetReceivesDecals(false);
	Mesh->SetReverseCulling(true);
	// Multiply the opaque scene before the ordinary translucent glass, smoke and laser effects.
	Mesh->SetTranslucentSortPriority(-100);
	Material = Mesh->CreateDynamicMaterialInstance(0, Parent);
	if (!Material)
	{
		Projection->Destroy();
		Projection = nullptr;
	}
	return Material != nullptr;
}

void UScctSpyShadow::Present(float)
{
	const auto* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	const auto* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>();
	const bool bEnabled = Session && Session->GetView().Valid && CVarSpyShadow.GetValueOnGameThread()
		&& !bAssetMissing && !(Vision && Vision->IsHeatOrEmf());
	const AScctPawnProxy* Local = Session ? Session->FindLocalProxy() : nullptr;
	if (!Local || Local->GetKind() != 0)
	{
		if (LocalProjection.Projection) EndSession();
		return;
	}
	const FScctBridgePawn& Pawn = Local->GetPawn();
	const FVector Origin(Pawn.X, Pawn.Y, Pawn.Z + 5.0f);
	const FScctBridgeView& View = Session->GetView();
	const bool bCanDraw = bEnabled && Local->HasModel() && Local->IsShown() && Pawn.Health > 0
		&& FVector::DistSquared(Origin, FVector(View.X, View.Y, View.Z)) <= FMath::Square(ViewRange);
	const auto* Api = bCanDraw ? ScctFeature<FScctLightPlanApi>() : nullptr;
	FScctShadowLighting Light{};
	if (!Api || !Api->GetShadowLighting(&Light) || !Light.Valid || Light.Darkness <= 0)
	{
		if (LocalProjection.Projection && !LocalProjection.Projection->IsHidden())
		{
			LocalProjection.Projection->SetActorHiddenInGame(true);
		}
		return;
	}
	LocalProjection.Darkness = Light.Darkness / 255.0f;
	LocalProjection.ToLight = FVector(Light.DirX, Light.DirY, Light.DirZ).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	if (LocalProjection.Material || CreateProjection(LocalProjection))
	{
		Draw(LocalProjection, *Local, Origin);
	}
}

void UScctSpyShadow::Draw(FScctPawnProjection& State, const AScctPawnProxy& Pawn, const FVector& Origin)
{
	auto& Material = State.Material;
	auto& Projection = State.Projection;
	auto& ResolvedSkeleton = State.ResolvedSkeleton;
	auto& PartBones = State.PartBones;
	const FVector Direction = -State.ToLight;
	const FMatrix Basis = FRotationMatrix::MakeFromXZ(Direction, FVector::UpVector);
	const FVector Across = Basis.GetUnitAxis(EAxis::Y);
	const FVector Up = Basis.GetUnitAxis(EAxis::Z);
	const UPoseableMeshComponent& Body = *Pawn.GetBodyMesh();
	if (ResolvedSkeleton.Get() != Body.GetSkinnedAsset() || PartBones.IsEmpty())
	{
		ResolvedSkeleton = Body.GetSkinnedAsset();
		PartBones.Reset(PartCount);
		for (const FShadowPart& Shape : ShadowParts)
		{
			PartBones.Emplace(Pawn.FindMeshBoneName(Shape.First), Pawn.FindMeshBoneName(Shape.Last));
		}
	}
	static TArray<FName> FirstParameters;
	static TArray<FName> LastParameters;
	const auto& FirstNames = PartParameters(TEXT("PartStart"), FirstParameters);
	const auto& LastNames = PartParameters(TEXT("PartEnd"), LastParameters);
	double HalfWidth = EdgeMargin;
	double HalfHeight = EdgeMargin;
	const auto ProjectBone = [&](FName Bone, float Radius)
	{
		const FVector Offset = Body.GetSocketLocation(Bone) - Origin;
		const double Depth = FVector::DotProduct(Offset, Direction);
		// The original mask has a perspective capture at the light and is projected along the weighted axis.
		const double Scale = CaptureDistance / FMath::Max(CaptureDistance + Depth, 100.0);
		const double X = FVector::DotProduct(Offset, Across) * Scale;
		const double Y = FVector::DotProduct(Offset, Up) * Scale;
		const double Extent = Radius * Scale + EdgeMargin;
		HalfWidth = FMath::Max(HalfWidth, FMath::Abs(X) + Extent);
		HalfHeight = FMath::Max(HalfHeight, FMath::Abs(Y) + Extent);
		return FLinearColor(X, Y, Depth, Radius * Scale);
	};
	for (int32 Part = 0; Part < PartCount; ++Part)
	{
		const FShadowPart& Shape = ShadowParts[Part];
		const FName First = PartBones[Part].Key;
		const FName Last = PartBones[Part].Value;
		const bool bHasPart = !First.IsNone() && !Last.IsNone() && (Part != 2 || !Pawn.IsHeadHidden());
		Material->SetVectorParameterValue(FirstNames[Part], bHasPart ? ProjectBone(First, Shape.Radius) : FLinearColor::Transparent);
		Material->SetVectorParameterValue(LastNames[Part], bHasPart ? ProjectBone(Last, Shape.Radius) : FLinearColor::Transparent);
	}
	Material->SetVectorParameterValue(TEXT("CastOrigin"), FLinearColor(Origin.X, Origin.Y, Origin.Z));
	Material->SetVectorParameterValue(TEXT("CastAlong"), FLinearColor(Direction.X, Direction.Y, Direction.Z));
	Material->SetVectorParameterValue(TEXT("CastAcross"), FLinearColor(Across.X, Across.Y, Across.Z));
	Material->SetVectorParameterValue(TEXT("CastUp"), FLinearColor(Up.X, Up.Y, Up.Z));
	Material->SetScalarParameterValue(TEXT("Darkness"), State.Darkness);
	Material->SetScalarParameterValue(TEXT("CastRange"), CastRange);
	Projection->SetActorTransform(FTransform(Basis.ToQuat(), Origin + Direction * (CastRange * 0.5f),
		FVector((CastRange + 2.0) / 100.0, HalfWidth * 0.02, HalfHeight * 0.02)));
	Projection->SetActorHiddenInGame(false);
}
