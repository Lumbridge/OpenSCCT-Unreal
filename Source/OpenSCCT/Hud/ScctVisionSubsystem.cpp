#include "Hud/ScctVisionSubsystem.h"

#include "Components/PoseableMeshComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Hud/ScctHudStyle.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OpenSCCT.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"

namespace
{
	struct FVisionMode
	{
		int32 Mode; // FScctHudState::VisionMode
		const TCHAR* Material;
	};

	const FVisionMode Modes[] = {
		{ 34, TEXT("/Game/OpenSCCT/UI/Vision/M_ScctVisionNight.M_ScctVisionNight") },
		{ 36, TEXT("/Game/OpenSCCT/UI/Vision/M_ScctVisionThermal.M_ScctVisionThermal") },
		{ 37, TEXT("/Game/OpenSCCT/UI/Vision/M_ScctVisionMotion.M_ScctVisionMotion") },
		{ 38, TEXT("/Game/OpenSCCT/UI/Vision/M_ScctVisionEmf.M_ScctVisionEmf") },
	};

	const FName AmountParameter(TEXT("Amount"));
}

bool UScctVisionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctVisionSubsystem::Deinitialize()
{
	Clear();
	Super::Deinitialize();
}

bool UScctVisionSubsystem::EnsureVolume()
{
	if (IsValid(Volume))
	{
		return true;
	}
	if (bTriedMaterials)
	{
		return false;
	}
	bTriedMaterials = true;

	static_assert(UE_ARRAY_COUNT(Modes) == ModeCount, "One material per mode");
	TArray<UMaterialInterface*> Loaded;
	for (const FVisionMode& Mode : Modes)
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Mode.Material);
		if (Material == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Vision: %s is missing; run Tools/Materials/Build-VisionMaterials.ps1. The goggles will not change the picture."), Mode.Material);
			return false;
		}
		Loaded.Add(Material);
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	Volume = GetWorld()->SpawnActor<APostProcessVolume>(Parameters);
	if (!IsValid(Volume))
	{
		return false;
	}
	Volume->bUnbound = true;
	Volume->BlendWeight = 1.0f;
	// Above the level's own volumes, so the goggles are the last word on the picture.
	Volume->Priority = 1000.0f;

	Materials.Reset();
	for (UMaterialInterface* Material : Loaded)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		Instance->SetScalarParameterValue(AmountParameter, 0.0f);
		Materials.Add(Instance);
		// Weight 0 keeps a material out of the frame entirely until its mode is on.
		Volume->Settings.WeightedBlendables.Array.Emplace(0.0f, Instance);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Vision: %d post-process materials ready"), Materials.Num());
	return true;
}

void UScctVisionSubsystem::Present(int32 VisionMode, float DeltaSeconds)
{
	bool bAnyMode = false;
	for (const FVisionMode& Mode : Modes)
	{
		bAnyMode |= Mode.Mode == VisionMode;
	}
	if (VisionMode != LastMode)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: mode %d -> %d"), LastMode, VisionMode);
		LastMode = VisionMode;
	}

	// Nothing is spawned or loaded until the goggles are first used.
	if (!bAnyMode && !IsValid(Volume))
	{
		return;
	}
	if (!EnsureVolume())
	{
		return;
	}

	bool bAnyShown = false;
	for (int32 Index = 0; Index < ModeCount; ++Index)
	{
		const float Target = Modes[Index].Mode == VisionMode ? 1.0f : 0.0f;
		float& Amount = Amounts[Index];
		// Goggles come on fast and go off a little faster.
		Amount = ScctHudStyle::Ease(Amount, Target, DeltaSeconds, Target > 0.5f ? 9.0f : 14.0f);
		if (FMath::Abs(Amount - Target) < 0.004f)
		{
			Amount = Target;
		}
		Materials[Index]->SetScalarParameterValue(AmountParameter, Amount);
		Volume->Settings.WeightedBlendables.Array[Index].Weight = Amount > 0.0f ? 1.0f : 0.0f;
		bAnyShown |= Amount > 0.0f;
	}
	MarkCharacters(bAnyShown);
}

void UScctVisionSubsystem::Clear()
{
	for (int32 Index = 0; Index < ModeCount; ++Index)
	{
		Amounts[Index] = 0.0f;
		if (IsValid(Volume) && Materials.IsValidIndex(Index))
		{
			Materials[Index]->SetScalarParameterValue(AmountParameter, 0.0f);
			Volume->Settings.WeightedBlendables.Array[Index].Weight = 0.0f;
		}
	}
	LastMode = 0;
	MarkCharacters(false);
}

void UScctVisionSubsystem::MarkCharacters(bool bMark)
{
	if (!bMark && !bCharactersMarked)
	{
		return;
	}
	bCharactersMarked = bMark;

	const UWorld* World = GetWorld();
	const UScctSessionSubsystem* Session = World != nullptr ? World->GetSubsystem<UScctSessionSubsystem>() : nullptr;
	if (Session == nullptr)
	{
		return;
	}

	// Every frame while a mode is on, because pawns come and go; the calls do nothing when nothing changes.
	for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Session->GetProxies())
	{
		const AScctPawnProxy* Proxy = Pair.Value;
		if (UPoseableMeshComponent* Body = IsValid(Proxy) ? Proxy->GetBodyMesh() : nullptr)
		{
			Body->SetRenderCustomDepth(bMark);
			Body->SetCustomDepthStencilValue(bMark ? 1 + FMath::Clamp(Proxy->GetTeam(), 0, 1) : 0);
		}
	}
}
