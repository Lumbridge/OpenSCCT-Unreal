#include "Hud/ScctVisionSubsystem.h"

#include "Components/PoseableMeshComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Hud/ScctHudStyle.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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

void UScctVisionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	int32 Forced = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctForceVision="), Forced))
	{
		ForcedMode = Forced;
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: mode %d held by -ScctForceVision, whatever the HUD says"), ForcedMode);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctVisionHeatOpacity="), HeatOpacityOverride))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: every pawn's HeatOpacity held at %.2f by -ScctVisionHeatOpacity"), HeatOpacityOverride);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctVisionEmfOpacity="), EmfOpacityOverride))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: every pawn's EMFOpacity held at %.2f by -ScctVisionEmfOpacity"), EmfOpacityOverride);
	}
}

const UScctVisionSubsystem* UScctVisionSubsystem::Find(const UWorld* World)
{
	return World != nullptr ? World->GetSubsystem<UScctVisionSubsystem>() : nullptr;
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
	Data.Refresh(DeltaSeconds);
	if (ForcedMode != INDEX_NONE)
	{
		VisionMode = ForcedMode;
	}
	ShownMode = VisionMode;
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
	ShownMode = 0;
	Data.Reset();
	MarkCharacters(false);
}

int32 UScctVisionSubsystem::PawnStencil(float Intensity, float Opacity)
{
	const int32 Level = FMath::RoundToInt(FMath::Clamp(Intensity, 0.0f, 1.0f) * 15.0f);
	// Only the original's "opaque" (0.99 and up) is the top step, so a body that is at all see-through stays so.
	const int32 Step = Opacity >= 0.99f ? 3 : FMath::Clamp(FMath::FloorToInt(Opacity * 3.0f), 0, 2);
	return StencilPawnFirst + Level * 4 + Step;
}

namespace
{
	/** Draws a primitive as the goggles want it this frame; the engine calls do nothing when nothing changes. */
	void SetPawnPrimitive(UPrimitiveComponent* Primitive, bool bMainPass, bool bCustomDepth, int32 Stencil)
	{
		// Out of the depth pass too, or the body still stands in the depth and comes out as a black cut-out.
		if (Primitive->bRenderInMainPass != bMainPass)
		{
			Primitive->SetRenderInMainPass(bMainPass);
		}
		if (Primitive->bRenderInDepthPass != bMainPass)
		{
			Primitive->SetRenderInDepthPass(bMainPass);
		}
		if (Primitive->bRenderCustomDepth != bCustomDepth)
		{
			Primitive->SetRenderCustomDepth(bCustomDepth);
		}
		if (Primitive->CustomDepthStencilValue != Stencil)
		{
			Primitive->SetCustomDepthStencilValue(Stencil);
		}
	}
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

	// The mode the bodies are marked for: the one furthest in, so a mode fading out keeps its marks to the end.
	int32 MarkMode = 0;
	float MarkAmount = 0.0f;
	for (int32 Index = 0; Index < ModeCount; ++Index)
	{
		if (bMark && Amounts[Index] > MarkAmount)
		{
			MarkAmount = Amounts[Index];
			MarkMode = Modes[Index].Mode;
		}
	}
	const bool bThermal = MarkMode == 36;
	const bool bEmf = MarkMode == 38;
	const bool bHasRows = Data.GetApi() != nullptr;

	// What a pawn carries that EMF draws by its own values (the spy's drawn pistol): the strongest glow per owner.
	TMap<int32, float> CarriedGlow;
	if (bEmf)
	{
		for (const FScctVisionActor& Actor : Data.GetActors())
		{
			if (Actor.OwnerPawnId != 0 && (Actor.Flags & EScctVisionActorFlags::EmfShows) != 0)
			{
				float& Glow = CarriedGlow.FindOrAdd(Actor.OwnerPawnId);
				Glow = FMath::Max(Glow, Actor.EmfGlow);
			}
		}
	}

	int32 Marked = 0;
	int32 Hidden = 0;
	int32 SeeThrough = 0;
	TArray<USceneComponent*> Attached;
	// Each pawn's marking, for the log line.
	struct FPawnLine
	{
		int32 ActorId, Team, Primitives;
		bool bMainPass, bCustomDepth;
		int32 Stencil, CarriedStencil;
	};
	TArray<FPawnLine, TInlineAllocator<8>> PawnLines;
	// Every frame while a mode is on, because pawns come and go and their values change.
	for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Session->GetProxies())
	{
		const AScctPawnProxy* Proxy = Pair.Value;
		UPoseableMeshComponent* Body = IsValid(Proxy) ? Proxy->GetBodyMesh() : nullptr;
		if (Body == nullptr)
		{
			continue;
		}

		// Plain sight, night vision and motion tracking: the body as it is, marked by side for motion tracking.
		bool bMainPass = true;
		bool bCustomDepth = bMark;
		int32 BodyStencil = bMark ? 1 + FMath::Clamp(Proxy->GetTeam(), 0, 1) : 0;
		int32 CarriedStencil = BodyStencil;

		if (bThermal || bEmf)
		{
			// The simulation's rule for the body in this mode (PawnVision, EmfVision.Shows, HeatOpacity). Without the
			// vision table the body is drawn as an ordinary hot or plain body, as before the table existed.
			const FScctVisionPawn* Row = Data.FindPawn(Proxy->GetActorId());
			float Intensity = bThermal ? 1.0f : 0.0f;
			float Opacity = 1.0f;
			bool bShows = true;
			if (Row != nullptr)
			{
				const bool bRowHidden = (Row->Flags & EScctVisionPawnFlags::Hidden) != 0;
				if (bThermal)
				{
					Intensity = Row->HeatIntensity;
					Opacity = HeatOpacityOverride >= 0.0f ? HeatOpacityOverride : Row->HeatOpacity;
					bShows = !bRowHidden && Opacity > 0.0f;
				}
				else
				{
					Intensity = Row->EmfGlow;
					Opacity = EmfOpacityOverride >= 0.0f ? EmfOpacityOverride : Row->EMFOpacity;
					// A spy shows only while TestAndSetEMFVisible makes him EMF visible: his drawn weapon and the rest.
					bShows = !bRowHidden && (Row->Flags & EScctVisionPawnFlags::EmfShows) != 0 && Opacity > 0.0f;
				}
			}
			else if (bHasRows)
			{
				// A body the bridge has no row for this frame (it has just gone): nothing the goggles would show.
				bShows = false;
			}

			if (!bShows)
			{
				// Not drawn at all, as the original discards it: the scene behind shows where the body stands.
				bMainPass = false;
				bCustomDepth = false;
				BodyStencil = 0;
				CarriedStencil = 0;
				++Hidden;
			}
			else
			{
				// A see-through body is drawn only into the custom depth, so the material blends it over the real
				// scene behind it by its opacity.
				const bool bSeeThrough = Opacity < 0.99f;
				bMainPass = !bSeeThrough;
				bCustomDepth = true;
				BodyStencil = PawnStencil(Intensity, Opacity);
				const float* Carried = CarriedGlow.Find(Proxy->GetActorId());
				CarriedStencil = PawnStencil(Carried != nullptr ? FMath::Max(Intensity, *Carried) : Intensity, Opacity);
				SeeThrough += bSeeThrough ? 1 : 0;
				++Marked;
			}
		}

		// The body, what the pawn draws besides it (its weapon and gadget models, the stand-in) and what hangs on the
		// body from other actors (attached map actors) go together.
		// (GetChildrenComponents empties the array it is given, so the body's children come first.)
		Body->GetChildrenComponents(true, Attached);
		Proxy->ForEachComponent<UPrimitiveComponent>(false, [&Attached](UPrimitiveComponent* Own) { Attached.AddUnique(Own); });
		int32 Primitives = 0;
		const bool bCastsShadow = bMainPass || bCustomDepth;
		for (USceneComponent* Child : Attached)
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Child);
			if (Primitive == nullptr)
			{
				continue;
			}
			SetPawnPrimitive(Primitive, bMainPass, bCustomDepth, Primitive == Body ? BodyStencil : CarriedStencil);
			// A body the mode hides leaves no shadow either. Only shadows this took away are given back.
			const TWeakObjectPtr<UPrimitiveComponent> Key(Primitive);
			if (!bCastsShadow && Primitive->CastShadow)
			{
				Primitive->SetCastShadow(false);
				ShadowsTaken.Add(Key);
			}
			else if (bCastsShadow && ShadowsTaken.Remove(Key) > 0)
			{
				Primitive->SetCastShadow(true);
			}
			++Primitives;
		}
		if (bThermal || bEmf)
		{
			PawnLines.Add({ Proxy->GetActorId(), Proxy->GetTeam(), Primitives, bMainPass, bCustomDepth, BodyStencil, CarriedStencil });
		}
	}

	// What the goggles do with the pawns, in the log when it changes and every few seconds while a mode is on.
	const int32 Summary = (MarkMode << 24) | (Marked << 16) | (Hidden << 8) | SeeThrough;
	const double Now = FPlatformTime::Seconds();
	if ((bThermal || bEmf) && (Summary != LastMarkSummary || Now - LastMarkLog > 3.0))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: %s marks %d pawns (%d hidden, %d see-through)%s"),
			bThermal ? TEXT("thermal") : TEXT("EMF"), Marked, Hidden, SeeThrough, bHasRows ? TEXT("") : TEXT(" without the vision table"));
		for (const FPawnLine& Line : PawnLines)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Vision:   pawn #%d team %d: %d primitives, main pass %d, custom depth %d, stencil %d (carried %d)"),
				Line.ActorId, Line.Team, Line.Primitives, Line.bMainPass ? 1 : 0, Line.bCustomDepth ? 1 : 0, Line.Stencil, Line.CarriedStencil);
		}
		LastMarkLog = Now;
	}
	LastMarkSummary = Summary;
}
