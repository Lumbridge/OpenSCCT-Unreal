#include "Play/ScctMercCameraOptics.h"

#include "Engine/Scene.h"
#include "Materials/MaterialInterface.h"
#include "OpenSCCT.h"

namespace
{
	TWeakObjectPtr<UMaterialInterface> LensMaterial;
	float Curvature = 0.0f;
	bool bFailedToLoad = false;
}

bool ScctMercCameraOptics::Apply(FPostProcessSettings& Settings, bool bActive)
{
	UMaterialInterface* Lens = LensMaterial.Get();
	if (bActive && !Lens && !bFailedToLoad)
	{
		Lens = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Materials/M_ScctMercCameraLens.M_ScctMercCameraLens"));
		if (!Lens || !Lens->GetScalarParameterValue(TEXT("Curvature"), Curvature))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Could not load the mercenary camera lens and its curvature."));
			bFailedToLoad = true;
			return false;
		}
		Curvature = FMath::Clamp(Curvature, 0.0f, 0.45f);
		LensMaterial = Lens;
	}
	if (!Lens)
	{
		return false;
	}

	for (FWeightedBlendable& Slot : Settings.WeightedBlendables.Array)
	{
		if (Slot.Object == Lens)
		{
			Slot.Weight = bActive ? 1.0f : 0.0f;
			return bActive;
		}
	}
	if (bActive)
	{
		Settings.WeightedBlendables.Array.Emplace(1.0f, Lens);
	}
	return bActive;
}

float ScctMercCameraOptics::CompensateFov(float OriginalFov, float HorizontalFov, float HorizontalCap)
{
	if (HorizontalFov <= 1.f || HorizontalCap <= HorizontalFov)
	{
		return OriginalFov;
	}
	const float HalfAngle = FMath::DegreesToRadians(OriginalFov) * 0.5f;
	const float ZoomLimit = FMath::Tan(FMath::DegreesToRadians(HorizontalCap * 0.5f))
		/ FMath::Tan(FMath::DegreesToRadians(HorizontalFov * 0.5f));
	const float LensScale = FMath::Min(1.f / (1.f - Curvature), ZoomLimit);
	return FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HalfAngle) * LensScale));
}

FVector2D ScctMercCameraOptics::WarpScreenPoint(FVector2D Screen, const FIntRect& Rect)
{
	const FVector2D HalfSize = FVector2D(Rect.Size()) * 0.5;
	const FVector2D Center = FVector2D(Rect.Min) + HalfSize;
	const FVector2D Offset = Screen - Center;
	if (HalfSize.IsNearlyZero())
	{
		return Screen;
	}
	// Invert the material's sampling function rScene = rDisplay * (1-k) / (1-k*rDisplay²).
	const double RadiusSquared = Offset.SizeSquared() / HalfSize.SizeSquared();
	const double A = 1.0 - Curvature;
	const double Scale = 2.0 / (A + FMath::Sqrt(A * A + 4.0 * Curvature * RadiusSquared));
	return Center + Offset * Scale;
}
