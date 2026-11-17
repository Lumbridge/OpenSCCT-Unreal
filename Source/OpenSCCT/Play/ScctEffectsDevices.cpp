// The marked devices' own effects (UScctEffectsSubsystem::UpdateDevices): a network camera of the merc ring that a
// spy's sticky shocker has put out shows a fault on its lens, as the Godot client's Fx/CamnetDisabledView.cs draws
// it; and the PS2 sabotage modem's destruction preset (SpawnPs2ModemEffect), the three sprite emitters of
// src/OpenScct.Game/SBase/Ps2ModemEffect.cs that the Godot client runs in Fx/EffectViews.Ps2.cs.

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OpenSCCT.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctExposure.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	const TCHAR* const FaultSolidMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxSolid.M_ScctFxSolid");
	const TCHAR* const FaultBarMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	// The globe camera's lens faces the mesh's +X and ends 19.96 units out (radius 11.64). The cross is two narrow
	// bars on that lens, at 45 degrees either way, in the camera's own (scaled) frame: 0.6 deep, 18 long, 1.6 wide.
	const FVector FaultCrossAt(20.5, 0.0, 0.0);
	const FVector FaultBarScale(0.006, 0.18, 0.016); // of the engine's 100 unit cube
	constexpr float FaultBarRoll = 45.0f;

	// CamnetDisabledView: orange, at full strength for the first 0.16 s and again from 0.28 to 0.44 s of every 1.6 s,
	// at 0.48 the rest; a burst of seven sparks at the start of each 1.6 s; five puffs of smoke alive at a time.
	const FLinearColor FaultColor(1.0f, 0.42f, 0.055f);
	constexpr float FaultCycle = 1.6f;
	constexpr float FaultDim = 0.48f;
	constexpr int32 FaultSparks = 7;
	constexpr float FaultSmokeLife = 1.6f;
	constexpr float FaultSmokePerSecond = 5.0f / FaultSmokeLife;
	const FVector FaultSparksAt(21.0, 0.0, 0.0);
	const FVector FaultSmokeAt(14.0, 0.0, 12.0);

	// The cross is drawn unlit, as a brightness in the finished picture: its glow is divided by the exposure, so it
	// reads as the same orange in a dark corridor and in a lit hall instead of burning white in the dark.
	constexpr float FaultPictureBrightness = 0.85f;
	constexpr float FaultGlowMost = 4000.0f;

	/** How often the level's actor for a camera is looked for again while it has none. */
	constexpr float FaultLookAgainSeconds = 2.0f;
}

void UScctEffectsSubsystem::ResetDevices()
{
	if (NumCamnetFaults > 0 || NumModemEffects > 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: devices this match: %d network camera faults, %d modem destructions"), NumCamnetFaults, NumModemEffects);
	}
	for (TPair<int32, FCamnetFault>& Pair : CamnetFaults)
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& Bar : Pair.Value.Bars)
		{
			if (UStaticMeshComponent* Component = Bar.Get())
			{
				Component->SetVisibility(false);
				FaultBarPool.Add(Component);
			}
		}
		if (UMaterialInstanceDynamic* Look = Pair.Value.Look.Get())
		{
			FaultLookPool.Add(Look);
		}
	}
	CamnetFaults.Reset();
	NumCamnetFaults = 0;
	NumModemEffects = 0;
}

void UScctEffectsSubsystem::UpdateDevices(float DeltaSeconds)
{
	if (WorldApi == nullptr)
	{
		WorldApi = ScctFeature<FScctWorldApi>();
		if (WorldApi == nullptr)
		{
			return;
		}
	}

	int32 Total = WorldApi->GetMarkers(DeviceMarkers.GetData(), DeviceMarkers.Num());
	if (Total > DeviceMarkers.Num())
	{
		DeviceMarkers.SetNumUninitialized(Total + 32);
		Total = WorldApi->GetMarkers(DeviceMarkers.GetData(), DeviceMarkers.Num());
	}
	const int32 Count = FMath::Clamp(Total, 0, DeviceMarkers.Num());

	UScctLevelStateSubsystem* LevelState = GetWorld()->GetSubsystem<UScctLevelStateSubsystem>();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctWorldMarker& Marker = DeviceMarkers[Index];
		if (Marker.Kind != EScctWorldMarkerKind::CamNetwork)
		{
			continue;
		}

		// CamnetDisabledView.Update: shown while the camera is shot out and neither hidden nor gone.
		const bool bOut = (Marker.Flags & EScctWorldMarkerFlags::Disabled) != 0 &&
			(Marker.Flags & (EScctWorldMarkerFlags::Hidden | EScctWorldMarkerFlags::Gone)) == 0;
		FCamnetFault* Fault = CamnetFaults.Find(Marker.NameId);
		if (!bOut)
		{
			if (Fault != nullptr && Fault->bOut)
			{
				Fault->bOut = false;
				for (const TWeakObjectPtr<UStaticMeshComponent>& Bar : Fault->Bars)
				{
					if (UStaticMeshComponent* Component = Bar.Get())
					{
						Component->SetVisibility(false);
					}
				}
				UE_LOG(LogOpenSCCT, Log, TEXT("Effects: camnet fault %s cleared"), *Fault->Name);
			}
			continue;
		}

		if (Fault == nullptr)
		{
			Fault = &CamnetFaults.Add(Marker.NameId);
			Fault->Name = LevelState != nullptr ? LevelState->GetName(Marker.NameId) : FString::FromInt(Marker.NameId);
		}
		Fault->SeenFrame = Frame;
		if (!Fault->bOut)
		{
			// The fault begins (or comes back): the cross from the pool, the first sparks at once.
			Fault->bOut = true;
			Fault->Age = 0.0f;
			Fault->NextSpark = 0.0f;
			Fault->SmokeDebt = 0.0f;
			++NumCamnetFaults;
			for (int32 Bar = 0; Bar < 2; ++Bar)
			{
				if (Fault->Bars[Bar].IsValid())
				{
					continue;
				}
				UStaticMeshComponent* Component = FaultBarPool.Num() > 0 ? FaultBarPool.Pop().Get() : nullptr;
				if (Component == nullptr)
				{
					Component = NewObject<UStaticMeshComponent>(Host);
					Component->SetMobility(EComponentMobility::Movable);
					Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Component->SetCastShadow(false);
					Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, FaultBarMeshPath));
					Component->SetupAttachment(Host->GetRootComponent());
					Component->RegisterComponent();
				}
				Fault->Bars[Bar] = Component;
			}
			if (!Fault->Look.IsValid())
			{
				UMaterialInstanceDynamic* Look = FaultLookPool.Num() > 0 ? FaultLookPool.Pop().Get() : nullptr;
				if (Look == nullptr)
				{
					if (UMaterialInterface* Solid = LoadObject<UMaterialInterface>(nullptr, FaultSolidMaterialPath))
					{
						Look = UMaterialInstanceDynamic::Create(Solid, this);
						Look->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.02f, 0.008f, 0.001f));
					}
				}
				Fault->Look = Look;
			}
			for (const TWeakObjectPtr<UStaticMeshComponent>& Bar : Fault->Bars)
			{
				if (UStaticMeshComponent* Component = Bar.Get())
				{
					Component->SetMaterial(0, Fault->Look.Get());
				}
			}
		}

		// The camera's frame: its level actor, which the level state turns with the simulation (a merc looking
		// through it turns the globe). Without one the fault stands where the simulation has the camera.
		AActor* Camera = Fault->Camera.Get();
		if (Camera == nullptr && LevelState != nullptr && (Fault->Age == 0.0f || FMath::Fmod(Fault->Age, FaultLookAgainSeconds) < DeltaSeconds))
		{
			Camera = LevelState->FindLevelActor(Fault->Name);
			Fault->Camera = Camera;
		}
		const FTransform Frame3D = Camera != nullptr ? Camera->GetActorTransform() : FTransform(FVector(Marker.X, Marker.Y, Marker.Z));
		if (Fault->Age == 0.0f)
		{
			const FVector Lens = Frame3D.TransformPosition(FaultCrossAt);
			UE_LOG(LogOpenSCCT, Log, TEXT("Effects: camnet fault %s (out for %.0f s) at lens (%.0f, %.0f, %.0f)%s"), *Fault->Name, Marker.TimeLeft,
				Lens.X, Lens.Y, Lens.Z, Camera != nullptr ? TEXT("") : TEXT(", no level actor: at the simulation's place, unturned"));
		}

		// The cross, throbbing.
		for (int32 Bar = 0; Bar < 2; ++Bar)
		{
			if (UStaticMeshComponent* Component = Fault->Bars[Bar].Get())
			{
				const FTransform Local(FRotator(0.0f, 0.0f, Bar == 0 ? -FaultBarRoll : FaultBarRoll), FaultCrossAt, FaultBarScale);
				Component->SetWorldTransform(Local * Frame3D);
				if (!Component->IsVisible())
				{
					Component->SetVisibility(true);
				}
			}
		}
		const float Phase = FMath::Fmod(Fault->Age, FaultCycle);
		const float Strength = Phase < 0.16f || (Phase > 0.28f && Phase < 0.44f) ? 1.0f : FaultDim;
		if (UMaterialInstanceDynamic* Look = Fault->Look.Get())
		{
			const float Glow = FMath::Min(FaultPictureBrightness / FMath::Max(FScctExposure::Get(), 1.0e-4f), FaultGlowMost);
			Look->SetVectorParameterValue(TEXT("Glow"), FaultColor * (Strength * Glow));
		}

		const FVector Out = Frame3D.GetUnitAxis(EAxis::X);
		if (Fault->Age >= Fault->NextSpark)
		{
			// Seven sparks out of the lens, falling, gone in under half a second.
			const FVector At = Frame3D.TransformPosition(FaultSparksAt);
			for (int32 Spark = 0; Spark < FaultSparks; ++Spark)
			{
				FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Glow);
				Particle.Position = At;
				Particle.Velocity = RandomCone(Out, 55.0f) * Random.FRandRange(25.0f, 70.0f);
				Particle.Color = FLinearColor(14.0f, 6.0f, 1.2f, 1.0f);
				Particle.Life = 0.38f * Random.FRandRange(0.85f, 1.0f);
				Particle.SizeStart = Random.FRandRange(0.9f, 1.6f);
				Particle.SizeEnd = 0.5f;
				Particle.Stretch = 0.03f;
				Particle.Gravity = 170.0f;
				Particle.FadeIn = 0.01f;
			}
			Fault->NextSpark = Fault->Age + FaultCycle;
		}

		// The smoke rises slowly from the top of the globe.
		Fault->SmokeDebt += FaultSmokePerSecond * DeltaSeconds;
		for (; Fault->SmokeDebt >= 1.0f; Fault->SmokeDebt -= 1.0f)
		{
			FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Smoke);
			Particle.Position = Frame3D.TransformPosition(FaultSmokeAt);
			Particle.Velocity = RandomCone(FVector::UpVector, 15.0f) * Random.FRandRange(8.0f, 16.0f);
			Particle.Acceleration = FVector(0.0, 0.0, 4.0);
			Particle.Color = FLinearColor(0.3f, 0.3f, 0.32f, 0.36f);
			Particle.Life = FaultSmokeLife;
			Particle.SizeStart = 12.0f * Random.FRandRange(0.45f, 1.1f);
			Particle.SizeEnd = Particle.SizeStart * 1.2f;
			Particle.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
			Particle.FadeIn = 0.25f;
		}

		Fault->Age += DeltaSeconds;
	}

	// A camera no longer listed (a new map binding) takes its fault with it.
	for (auto It = CamnetFaults.CreateIterator(); It; ++It)
	{
		if (It.Value().SeenFrame != Frame && It.Value().bOut)
		{
			It.Value().bOut = false;
			for (const TWeakObjectPtr<UStaticMeshComponent>& Bar : It.Value().Bars)
			{
				if (UStaticMeshComponent* Component = Bar.Get())
				{
					Component->SetVisibility(false);
				}
			}
		}
	}
}

void UScctEffectsSubsystem::SpawnPs2ModemEffect(const FVector& At, const FRotator& Rotation)
{
	// Ps2ModemEffect.SourceEmitters, in their order. A sprite of the original is Size across each way from its
	// middle, so a size here (the whole width) is twice the original's; a size scale runs from birth to death.
	// The two emitters that take the actor's rotation (PTRS_Actor) throw and push along the modem's facing, out of
	// the wall; the short smoke goes by the world's axes. None of them respawns: they fire once and die out.
	const FQuat Turn = Rotation.Quaternion();
	++NumModemEffects;

	// SmokeLong: eight grey puffs blown out of the wall and up, swelling from 1.5 to 7 times their start over six to
	// eight seconds, paling and thinning as they go.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Smoke);
		const float Size = Random.FRandRange(3.0f, 7.0f) * 2.0f;
		Particle.Position = At;
		Particle.Velocity = Turn.RotateVector(FVector(Random.FRandRange(100.0f, 300.0f), Random.FRandRange(-70.0f, 70.0f), Random.FRandRange(-70.0f, 70.0f)));
		Particle.Acceleration = Turn.RotateVector(FVector(150.0, 0.0, 30.0));
		Particle.Drag = 7.0f; // VelocityLoss 10 along, 5 across
		Particle.Color = FLinearColor(0.5f, 0.5f, 0.5f, 0.8f); // 93 grey paling toward white, half way by its death
		Particle.Life = Random.FRandRange(6.0f, 8.0f);
		Particle.SizeStart = Size * 1.5f;
		Particle.SizeEnd = Size * 7.0f;
		Particle.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
		Particle.Spin = Random.FRandRange(-0.1f, 0.1f) * UE_TWO_PI;
		Particle.FadeIn = 0.02f;
	}

	// SmokeShort: ten black puffs that burst and are gone in a second, growing to ten times their start.
	for (int32 Index = 0; Index < 10; ++Index)
	{
		FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Smoke);
		const float Size = Random.FRandRange(1.5f, 3.0f) * 2.0f;
		Particle.Position = At;
		Particle.Velocity = FVector(Random.FRandRange(30.0f, 100.0f), Random.FRandRange(-70.0f, 70.0f), Random.FRandRange(-70.0f, 70.0f));
		Particle.Acceleration = FVector(0.0, 0.0, 30.0);
		Particle.Drag = 4.0f; // VelocityLoss 5 sideways, 1 up
		Particle.Color = FLinearColor(0.08f, 0.08f, 0.08f, 0.9f); // black, greying as it fades
		Particle.Life = 1.0f;
		Particle.SizeStart = Size * 1.5f;
		Particle.SizeEnd = Size * 10.0f;
		Particle.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
		Particle.Spin = Random.FRandRange(-0.1f, 0.1f) * UE_TWO_PI;
		Particle.FadeIn = 0.1f; // FadeOutStartTime 0.1
	}

	// Flash: two bright fire sprites (drawn brightening) pushed a little out of the wall for half a second.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FScctFxParticle& Particle = Sprites.Spawn(EScctFxLayer::Glow);
		Particle.Position = At;
		Particle.Velocity = Turn.RotateVector(FVector(Random.FRandRange(15.0f, 70.0f), 0.0, 0.0));
		Particle.Acceleration = Turn.RotateVector(FVector(50.0, 0.0, 30.0));
		Particle.Color = FLinearColor(20.0f, 9.0f, 2.5f, 1.0f);
		Particle.Life = Random.FRandRange(0.5f, 0.7f);
		const float Size = Random.FRandRange(15.0f, 30.0f) * 2.0f;
		Particle.SizeStart = Size;
		Particle.SizeEnd = Size;
		Particle.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
		Particle.FadeIn = 0.02f;
	}

	const FVector Facing = Turn.GetForwardVector();
	UE_LOG(LogOpenSCCT, Log, TEXT("Effects: Ps2ModemEffect start at (%.0f, %.0f, %.0f) facing (%.2f, %.2f, %.2f): 8 long smoke, 10 short smoke, 2 flash sprites"),
		At.X, At.Y, At.Z, Facing.X, Facing.Y, Facing.Z);
}
