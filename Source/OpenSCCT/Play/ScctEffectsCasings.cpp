// Spent shell casings (UScctEffectsSubsystem::SpawnCasing): every shot the bridge marks with a casing (shot flag
// Casing: the merc guns and sidearms, not the spy's shocker or the revolver, which keeps its shells) throws one
// SBase.SDouilleEmitter casing, as the Godot client's Fx/CombatFxView.cs starts one per shot. The original spawns the
// emitter at the gun's FX_DOUILLE bone with that bone's turn (SWeaponDefense.SpawnSFX, spec/equipment/merc-rifle.md);
// the equipment subsystem says where that bone is on the view model or on the gun a body holds.
//
// The emitter's one mesh particle (SGameplayObjects.Balles.Douille, imported by Tools/Effects/build_effect_assets.py)
// is flown here with the emitter's own numbers: 400 to 500 cm/s along the port's X and 50 to 100 up its Z, 900 cm/s^2
// of gravity, a loss of 5 a second of the speed along the world's X (VelocityLossRange is in world axes), a spin of
// 0.2 to 0.8 turns a second about yaw and 0.2 to 0.5 about pitch and roll each way at random, a size of 1.5, four
// seconds of life with the fade from the third. The original's emitter runs in its owner's frame (CoordinateSystem
// Relative), where the engine skips collision, so its casings fell through the floor; here each one meets the level
// along a line through the client world's collision (Runtime/ScctFxTraceApi.h: the imported level has no Unreal
// collision, and the Godot client's colliding particles ask the same) and bounces with the emitter's damping (0.2 to 0.3 of its speed kept, the spin stopped:
// DampRotation with no rotation damping), then lies flat where it settles and shrinks away over its last second (its
// material is opaque, so it cannot thin out). The emitter's collision sound has no volume, so none is played.
//
// The pool holds MaxCasings; a shot when all are in use takes the oldest.

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OpenSCCT.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctEquipmentSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	constexpr int32 MaxCasings = 48;

	// SDouilleEmitter0's numbers (shared/fx/effects.json).
	constexpr float CasingSpeedForwardMin = 400.0f;
	constexpr float CasingSpeedForwardMax = 500.0f;
	constexpr float CasingSpeedUpMin = 50.0f;
	constexpr float CasingSpeedUpMax = 100.0f;
	constexpr float CasingGravity = -900.0f;
	constexpr float CasingLossX = 5.0f;
	constexpr float CasingDampingMin = 0.2f;
	constexpr float CasingDampingMax = 0.3f;
	const FVector CasingSpinMin(0.2f, 0.2f, 0.2f); // turns a second about X, Y, Z of the emitter's spin
	const FVector CasingSpinMax(0.8f, 0.5f, 0.5f);
	constexpr float CasingSize = 1.5f;
	constexpr float CasingLife = 4.0f;
	constexpr float CasingFadeStart = 3.0f;

	// The settling this port adds: a casing comes to rest on ground (a surface facing up more than this) once it is
	// slower than this after a bounce, or at its fourth bounce there; a casing that falls this far below where it was
	// thrown (out of the level) is dropped.
	constexpr float CasingGroundNormalZ = 0.6f;
	constexpr float CasingRestSpeed = 60.0f;
	constexpr int32 CasingMostBounces = 4;
	constexpr float CasingLostDrop = 3000.0f;
	constexpr float CasingPushOut = 0.2f;
	constexpr float CasingLongestStep = 1.0f / 60.0f;

	// The stand-in when the game's mesh was not imported: the engine's cylinder (100 across, 100 long along Z) made a
	// rifle casing 0.9 cm across and 2.4 cm long, in brass.
	const TCHAR* const CasingStandInMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const CasingStandInMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxSolid.M_ScctFxSolid");
	const FVector CasingStandInScale(0.009f, 0.009f, 0.024f);
	const FLinearColor CasingBrass(0.55f, 0.36f, 0.09f);

	/** The imported mesh's name reduced as the effects match asset names ("SGameplayObjects_Douille"). */
	const TCHAR* const CasingMeshKey = TEXT("sgameplayobjectsdouille");

	FVector AxisVector(int32 Axis)
	{
		return Axis == 0 ? FVector::XAxisVector : Axis == 1 ? FVector::YAxisVector : FVector::ZAxisVector;
	}
}

bool UScctEffectsSubsystem::EnsureCasingMesh()
{
	if (bCasingMeshLooked)
	{
		return CasingMesh != nullptr;
	}
	bCasingMeshLooked = true;

	FVector Scale(CasingSize);
	if (const FSoftObjectPath* Asset = MeshAssets.Find(CasingMeshKey))
	{
		CasingMesh = Cast<UStaticMesh>(Asset->TryLoad());
	}
	if (CasingMesh == nullptr)
	{
		CasingMesh = LoadObject<UStaticMesh>(nullptr, CasingStandInMeshPath);
		Scale = CasingStandInScale;
		if (UMaterialInterface* Solid = LoadObject<UMaterialInterface>(nullptr, CasingStandInMaterialPath))
		{
			CasingStandInMaterial = UMaterialInstanceDynamic::Create(Solid, this);
			CasingStandInMaterial->SetVectorParameterValue(TEXT("Color"), CasingBrass);
			CasingStandInMaterial->SetVectorParameterValue(TEXT("Glow"), FLinearColor::Black);
		}
		UE_LOG(LogOpenSCCT, Warning, TEXT("Effects: no imported casing mesh SGameplayObjects_Douille (Tools/Effects/build_effect_assets.py imports it); casings are small brass cylinders"));
	}
	if (CasingMesh == nullptr)
	{
		return false;
	}

	// Which way the casing is long and how thick it is: it lies on its side once it settles.
	const FBoxSphereBounds Bounds = CasingMesh->GetBounds();
	const FVector Extent = Bounds.BoxExtent * Scale;
	CasingLongAxis = Extent.X >= Extent.Y && Extent.X >= Extent.Z ? 0 : Extent.Y >= Extent.Z ? 1 : 2;
	CasingRadius = FMath::Max(CasingLongAxis == 0 ? FMath::Max(Extent.Y, Extent.Z) : CasingLongAxis == 1 ? FMath::Max(Extent.X, Extent.Z) : FMath::Max(Extent.X, Extent.Y), 0.2f);
	CasingScale = Scale;
	UE_LOG(LogOpenSCCT, Log, TEXT("Effects: casing mesh %s, %.1f x %.1f x %.1f cm drawn, long along %c"),
		*CasingMesh->GetName(), Extent.X * 2.0f, Extent.Y * 2.0f, Extent.Z * 2.0f, TEXT("XYZ")[CasingLongAxis]);

	Casings.SetNum(MaxCasings);
	CasingPool.SetNum(MaxCasings);
	return true;
}

void UScctEffectsSubsystem::ResetCasings()
{
	if (NumCasings > 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: %d shell casings thrown (%d from the gun's ejection port, the rest from its muzzle), %d bounces"),
			NumCasings, NumCasingsFromPort, NumCasingBounces);
	}
	for (FCasing& Casing : Casings)
	{
		Casing = FCasing();
	}
	for (UStaticMeshComponent* Component : CasingPool)
	{
		if (Component != nullptr)
		{
			Component->SetVisibility(false);
		}
	}
	NextCasing = 0;
	NumCasings = NumCasingsFromPort = NumCasingBounces = 0;
}

void UScctEffectsSubsystem::SpawnCasing(const FScctShot& Shot, const FVector& Direction)
{
	if (!EnsureCasingMesh())
	{
		return;
	}

	// The port and its turn; without one (no gun drawn, a replacement model) the drawn muzzle and the aim, as the
	// Godot client throws every casing.
	FTransform Port;
	const UScctEquipmentSubsystem* Equipment = GetWorld()->GetSubsystem<UScctEquipmentSubsystem>();
	const bool bFromPort = Equipment != nullptr && Equipment->FindCasingPort(Shot.ShooterId, Port);
	if (!bFromPort)
	{
		Port = FTransform(Direction.Rotation(), FindMuzzle(Shot));
	}

	// The oldest slot goes when every slot is in use.
	int32 Slot = INDEX_NONE;
	for (int32 Step = 0; Step < MaxCasings; ++Step)
	{
		const int32 Index = (NextCasing + Step) % MaxCasings;
		if (!Casings[Index].bLive)
		{
			Slot = Index;
			break;
		}
	}
	if (Slot == INDEX_NONE)
	{
		Slot = NextCasing;
	}
	NextCasing = (Slot + 1) % MaxCasings;

	TObjectPtr<UStaticMeshComponent>& Component = CasingPool[Slot];
	if (Component == nullptr)
	{
		Component = NewObject<UStaticMeshComponent>(Host);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetStaticMesh(CasingMesh);
		if (CasingStandInMaterial != nullptr)
		{
			Component->SetMaterial(0, CasingStandInMaterial);
		}
		Component->SetupAttachment(Host->GetRootComponent());
		Component->RegisterComponent();
	}

	FCasing& Casing = Casings[Slot];
	Casing = FCasing();
	Casing.Location = Port.GetLocation();
	Casing.StartZ = Casing.Location.Z;
	Casing.Base = Port.Rotator();
	Casing.Velocity = Port.GetRotation().RotateVector(FVector(
		Random.FRandRange(CasingSpeedForwardMin, CasingSpeedForwardMax), 0.0f, Random.FRandRange(CasingSpeedUpMin, CasingSpeedUpMax)));
	// The mesh emitter's spin rotator: pitch about the spin's Y, yaw about its X, roll about its Z, each either way.
	auto SpinAbout = [this](int32 Axis)
	{
		const float Turns = Random.FRandRange(CasingSpinMin[Axis], CasingSpinMax[Axis]);
		return (Random.FRand() < 0.5f ? -Turns : Turns) * 360.0f;
	};
	Casing.Spin = FRotator(SpinAbout(1), SpinAbout(0), SpinAbout(2));
	Casing.bLive = true;

	++NumCasings;
	NumCasingsFromPort += bFromPort ? 1 : 0;
	if (bLog)
	{
		const FVector Thrown = Casing.Velocity.GetSafeNormal();
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: casing spawn %d of pawn %d (shot kind %d) at (%.0f, %.0f, %.0f) from the %s, thrown at %.0f cm/s, %.0f degrees off the aim and %.0f to its right"),
			Slot, Shot.ShooterId, Shot.Kind, Casing.Location.X, Casing.Location.Y, Casing.Location.Z,
			bFromPort ? TEXT("ejection port") : TEXT("muzzle"), Casing.Velocity.Size(),
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Thrown, Direction), -1.0, 1.0))),
			FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Thrown, FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal()), -1.0, 1.0))));
	}
}

void UScctEffectsSubsystem::UpdateCasings(float DeltaSeconds)
{
	if (Casings.Num() == 0)
	{
		return;
	}

	if (FxTrace == nullptr)
	{
		FString Error;
		FxTrace = ScctFeature<FScctFxTraceApi>(&Error);
		static bool bReported = false;
		if (FxTrace == nullptr && !bReported)
		{
			bReported = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Effects: casings fall through the floor, the bridge has no effect trace: %s"), *Error);
		}
	}

	const FVector LongAxis = AxisVector(CasingLongAxis);
	const FVector Pivot = CasingMesh != nullptr ? CasingMesh->GetBounds().Origin * CasingScale : FVector::ZeroVector;
	for (int32 Slot = 0; Slot < Casings.Num(); ++Slot)
	{
		FCasing& Casing = Casings[Slot];
		UStaticMeshComponent* Component = CasingPool[Slot];
		if (!Casing.bLive || Component == nullptr)
		{
			continue;
		}

		Casing.Age += DeltaSeconds;
		if (Casing.Age >= CasingLife || Casing.Location.Z < Casing.StartZ - CasingLostDrop)
		{
			Casing.bLive = false;
			Component->SetVisibility(false);
			continue;
		}

		// Whole steps short enough that a casing cannot pass through a floor between two traces.
		float Left = Casing.bResting ? 0.0f : DeltaSeconds;
		while (Left > 0.0f && !Casing.bResting)
		{
			const float Step = FMath::Min(Left, CasingLongestStep);
			Left -= Step;

			Casing.Velocity.Z += CasingGravity * Step;
			Casing.Velocity.X -= CasingLossX * Casing.Velocity.X * Step;
			Casing.Turned += Casing.Spin * Step;
			const FVector From = Casing.Location;
			const FVector To = From + Casing.Velocity * Step;
			const FVector Heading = (To - From).GetSafeNormal();

			const FVector Reach = To + Heading * CasingRadius;
			FScctFxHit Hit = {};
			if (Heading.IsNearlyZero() || FxTrace == nullptr || FxTrace->Trace(From.X, From.Y, From.Z, Reach.X, Reach.Y, Reach.Z, &Hit) == 0)
			{
				Casing.Location = To;
				continue;
			}

			// The bounce: the half step of gravity taken back, the speed mirrored by the surface and damped, the spin
			// stopped.
			const FVector Normal = FVector(Hit.NormalX, Hit.NormalY, Hit.NormalZ).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
			const FVector HitPoint(Hit.X, Hit.Y, Hit.Z);
			Casing.Velocity.Z -= CasingGravity * Step * 0.5f;
			Casing.Velocity = Casing.Velocity.MirrorByVector(Normal) * FVector(
				Random.FRandRange(CasingDampingMin, CasingDampingMax), Random.FRandRange(CasingDampingMin, CasingDampingMax), Random.FRandRange(CasingDampingMin, CasingDampingMax));
			Casing.Spin = FRotator::ZeroRotator;
			Casing.Location = HitPoint + Normal * (CasingRadius + CasingPushOut);
			++Casing.Bounces;
			++NumCasingBounces;

			const bool bGround = Normal.Z > CasingGroundNormalZ;
			if (bGround && (Casing.Velocity.Size() < CasingRestSpeed || Casing.Bounces >= CasingMostBounces))
			{
				// Lying on its side: its long axis kept as it fell but laid in the surface, its thickness above it.
				const FQuat Turn = (Casing.Base + Casing.Turned).Quaternion();
				FVector Along = FVector::VectorPlaneProject(Turn.RotateVector(LongAxis), Normal).GetSafeNormal();
				if (Along.IsNearlyZero())
				{
					Along = FVector::VectorPlaneProject(Casing.Base.Vector(), Normal).GetSafeNormal(UE_SMALL_NUMBER, FVector::XAxisVector);
				}
				const FMatrix Laid = CasingLongAxis == 0 ? FRotationMatrix::MakeFromXZ(Along, Normal)
					: CasingLongAxis == 1 ? FRotationMatrix::MakeFromYZ(Along, Normal)
					: FRotationMatrix::MakeFromZX(Along, Normal);
				Casing.Rest = Laid.ToQuat();
				Casing.Location = HitPoint + Normal * CasingRadius;
				Casing.Velocity = FVector::ZeroVector;
				Casing.bResting = true;
			}
			if (bLog)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Effects: casing %d bounce %d on actor %d (0: the level) at (%.0f, %.0f, %.0f), normal z %.2f, %.0f cm below where it was thrown%s"),
					Slot, Casing.Bounces, Hit.ActorId, HitPoint.X, HitPoint.Y, HitPoint.Z, Normal.Z, Casing.StartZ - HitPoint.Z,
					Casing.bResting ? TEXT(", lies there") : TEXT(""));
			}
		}

		// Shrinks away over the end of its life.
		const float Left01 = Casing.Age > CasingFadeStart ? FMath::Clamp(1.0f - (Casing.Age - CasingFadeStart) / (CasingLife - CasingFadeStart), 0.0f, 1.0f) : 1.0f;
		const FQuat Turn = Casing.bResting ? Casing.Rest : (Casing.Base + Casing.Turned).Quaternion();
		const FVector Scale = CasingScale * Left01;
		Component->SetWorldTransform(FTransform(Turn, Casing.Location - Turn.RotateVector(Pivot * Left01), Scale));
		if (!Component->IsVisible())
		{
			Component->SetVisibility(true);
		}
	}
}
