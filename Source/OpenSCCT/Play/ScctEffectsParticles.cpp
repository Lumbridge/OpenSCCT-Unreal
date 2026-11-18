#include "Play/ScctEffectsParticles.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// Sprites per layer. An explosion with its smoke uses about 150; a fire fight a few hundred.
	// The cloud layer holds eight smoke grenades' clouds of fifteen sprites.
	constexpr int32 LayerCapacity[] = { 1536, 768, 120, 120 };

	// The five floats each instance hands its material: colour, opacity, and the distance (cm) over which a glow
	// sprite dims into a surface behind it (M_ScctFxGlow; the smoke material has a fixed one). A cloud sprite's fifth
	// is the cell of its picture instead (M_ScctFxCloud).
	constexpr int32 CustomFloats = 5;

	/** A big fireball meets the floor softly; a spark next to a wall keeps its brightness. */
	float SoftEdge(float Size)
	{
		return FMath::Clamp(Size * 0.3f, 1.0f, 60.0f);
	}

	// The engine's plane is 100 units across.
	constexpr double QuadSize = 100.0;

	const FTransform HiddenInstance(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);

	/** A quad at Center whose X and Y edges lie along AxisX and AxisY (unit, at right angles), SizeX by SizeY cm. */
	FTransform QuadTransform(const FVector& Center, const FVector& AxisX, const FVector& AxisY, double SizeX, double SizeY)
	{
		const FMatrix Basis(AxisX, AxisY, FVector::CrossProduct(AxisX, AxisY), FVector::ZeroVector);
		return FTransform(Basis.ToQuat(), Center, FVector(SizeX / QuadSize, SizeY / QuadSize, 1.0));
	}

	/** A quad along a line, as wide as Width and turned about the line to face the viewer. */
	bool StreakTransform(const FVector& From, const FVector& To, double Width, const FVector& ViewLocation, FTransform& OutTransform)
	{
		const FVector Along = To - From;
		const double Length = Along.Size();
		if (Length < UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const FVector AxisX = Along / Length;
		const FVector Center = (From + To) * 0.5;
		FVector AxisY = FVector::CrossProduct(Center - ViewLocation, AxisX);
		if (!AxisY.Normalize())
		{
			return false;
		}
		// The streak's glow is round at its ends, so the quad is a width longer than the line.
		OutTransform = QuadTransform(Center, AxisX, AxisY, Length + Width, Width);
		return true;
	}
}

void FScctFxSprites::SetLayerOpacity(EScctFxLayer Layer, float Opacity)
{
	Layers[static_cast<int32>(Layer)].Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
}

void FScctFxSprites::Init(AActor* Host, UStaticMesh* Quad, UMaterialInterface* GlowMaterial, UMaterialInterface* SmokeMaterial,
	UMaterialInterface* CloudMaterial, UMaterialInterface* StickyGasMaterial)
{
	static_assert(UE_ARRAY_COUNT(LayerCapacity) == static_cast<int32>(EScctFxLayer::Count), "a capacity for every sprite layer");
	UMaterialInterface* const Materials[] = { GlowMaterial, SmokeMaterial,
		CloudMaterial ? CloudMaterial : SmokeMaterial, StickyGasMaterial ? StickyGasMaterial : SmokeMaterial };
	const TCHAR* const Names[] = { TEXT("FxGlow"), TEXT("FxSmoke"), TEXT("FxCloud"), TEXT("FxStickyGas") };
	Streaks.Reserve(MaxStreaks);

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Layers); ++Index)
	{
		FLayer& Layer = Layers[Index];
		// The glow layer's last instances are the streaks of the frame.
		const int32 Instances = LayerCapacity[Index] + (Index == 0 ? MaxStreaks : 0);
		Layer.Particles.SetNum(LayerCapacity[Index]);
		Layer.Transforms.Init(HiddenInstance, Instances);
		Layer.CustomData.SetNumZeroed(Instances * CustomFloats);
		Layer.Next = 0;
		Layer.Live = 0;
		Layer.Opacity = 1.0f;
		Layer.bUsesAtlas = (Index == static_cast<int32>(EScctFxLayer::Cloud) && CloudMaterial)
			|| (Index == static_cast<int32>(EScctFxLayer::StickyGas) && StickyGasMaterial);
		Layer.bDrawnEmpty = true;
		Layer.Component = nullptr;
		if (Host == nullptr || Quad == nullptr || Materials[Index] == nullptr)
		{
			continue;
		}

		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Host, Names[Index]);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Quad);
		Component->SetMaterial(0, Materials[Index]);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->bAffectDynamicIndirectLighting = false;
		Component->bAffectDistanceFieldLighting = false;
		Component->bUseAsOccluder = false;
		Component->bReceivesDecals = false;
		Component->SetNumCustomDataFloats(CustomFloats);
		Component->SetupAttachment(Host->GetRootComponent());
		Component->RegisterComponent();
		Component->AddInstances(Layer.Transforms, false, true, false);
		Layer.Component = Component;
	}
}

FScctFxParticle& FScctFxSprites::Spawn(EScctFxLayer LayerId)
{
	FLayer& Layer = Layers[static_cast<int32>(LayerId)];
	FScctFxParticle& Particle = Layer.Particles[Layer.Next];
	Layer.Next = (Layer.Next + 1) % Layer.Particles.Num();
	Particle = FScctFxParticle();
	Particle.SeekAge = SpawnAge;
	return Particle;
}

void FScctFxSprites::DrawStreak(const FVector& From, const FVector& To, float Width, const FLinearColor& Color)
{
	if (Streaks.Num() < MaxStreaks)
	{
		Streaks.Add({ From, To, Width, Color });
	}
}

void FScctFxSprites::Clear()
{
	for (FLayer& Layer : Layers)
	{
		for (FScctFxParticle& Particle : Layer.Particles)
		{
			Particle.Life = 0.0f;
		}
	}
	Streaks.Reset();

	// No frame may follow (the match is over and the session raises none), so what was drawn is taken away now.
	NumLive = 0;
	for (FLayer& Layer : Layers)
	{
		Layer.Live = 0;
		UInstancedStaticMeshComponent* Component = Layer.Component.Get();
		if (Component != nullptr && !Layer.bDrawnEmpty)
		{
			for (FTransform& Transform : Layer.Transforms)
			{
				Transform = HiddenInstance;
			}
			Component->BatchUpdateInstancesTransforms(0, TArrayView<const FTransform>(Layer.Transforms), true, true, true);
			Layer.bDrawnEmpty = true;
		}
	}
}

static void AdvanceSprite(FScctFxParticle& Particle, float DeltaSeconds)
{
	Particle.Velocity += Particle.Acceleration * DeltaSeconds;
	Particle.Velocity.Z -= Particle.Gravity * DeltaSeconds;
	if (Particle.LossMax.IsZero())
	{
		Particle.Velocity *= FMath::Max(0.0f, 1.0f - Particle.Drag * DeltaSeconds);
		Particle.Position += Particle.Velocity * DeltaSeconds;
	}
	else
	{
		// The original emitter's order: the move first, then each axis loses its share (never more than all of it,
		// where the original's would turn round on a long frame).
		Particle.Position += Particle.Velocity * DeltaSeconds;
		Particle.Velocity.X *= FMath::Max(0.0, 1.0 - FMath::FRandRange(Particle.LossMin.X, Particle.LossMax.X) * DeltaSeconds);
		Particle.Velocity.Y *= FMath::Max(0.0, 1.0 - FMath::FRandRange(Particle.LossMin.Y, Particle.LossMax.Y) * DeltaSeconds);
		Particle.Velocity.Z *= FMath::Max(0.0, 1.0 - FMath::FRandRange(Particle.LossMin.Z, Particle.LossMax.Z) * DeltaSeconds);
	}
	Particle.Rotation += Particle.Spin * DeltaSeconds;
}

void FScctFxSprites::Tick(float DeltaSeconds, const FVector& ViewLocation, const FQuat& ViewRotation)
{
	// A sprite faces the camera: its edges lie along the camera's right and up.
	const FVector ViewRight = ViewRotation.GetRightVector();
	const FVector ViewUp = ViewRotation.GetUpVector();
	NumLive = 0;

	for (int32 LayerIndex = 0; LayerIndex < UE_ARRAY_COUNT(Layers); ++LayerIndex)
	{
		FLayer& Layer = Layers[LayerIndex];
		const int32 Capacity = Layer.Particles.Num();
		Layer.Live = 0;

		for (int32 Index = 0; Index < Capacity; ++Index)
		{
			FScctFxParticle& Particle = Layer.Particles[Index];
			FTransform& Transform = Layer.Transforms[Index];
			if (Particle.Age >= Particle.Life)
			{
				Transform = HiddenInstance;
				continue;
			}

			Particle.Age += Particle.SeekAge + DeltaSeconds;
			if (Particle.Age >= Particle.Life)
			{
				Transform = HiddenInstance;
				continue;
			}
			// Reproduce drag, acceleration and rotation in simulation-sized steps.
			while (Particle.SeekAge > 0.0f)
			{
				const float Step = FMath::Min(Particle.SeekAge, 1.0f / 60.0f);
				AdvanceSprite(Particle, Step);
				Particle.SeekAge -= Step;
			}
			AdvanceSprite(Particle, DeltaSeconds);
			++Layer.Live;

			const float Time = Particle.Age / Particle.Life;
			const float Size = FMath::Lerp(Particle.SizeStart, Particle.SizeEnd, Time);
			const float Fade = Time < Particle.FadeIn ? Time / Particle.FadeIn
				: Particle.FadeOutStart < 0.0f ? 1.0f - (Time - Particle.FadeIn) / (1.0f - Particle.FadeIn)
				: Time > Particle.FadeOutStart ? 1.0f - (Time - Particle.FadeOutStart) / FMath::Max(1.0f - Particle.FadeOutStart, UE_KINDA_SMALL_NUMBER)
				: 1.0f;

			bool bDrawn = true;
			if (Particle.Stretch > 0.0f)
			{
				const FVector Tail = Particle.Position - Particle.Velocity * Particle.Stretch;
				bDrawn = StreakTransform(Tail, Particle.Position, Size, ViewLocation, Transform);
			}
			else
			{
				float Sin, Cos;
				FMath::SinCos(&Sin, &Cos, Particle.Rotation);
				Transform = QuadTransform(Particle.Position, ViewRight * Cos + ViewUp * Sin, ViewUp * Cos - ViewRight * Sin, Size, Size);
			}
			if (!bDrawn)
			{
				Transform = HiddenInstance;
			}

			float* Custom = &Layer.CustomData[Index * CustomFloats];
			Custom[0] = Particle.Color.R;
			Custom[1] = Particle.Color.G;
			Custom[2] = Particle.Color.B;
			Custom[3] = Particle.Color.A * FMath::Clamp(Fade, 0.0f, 1.0f) * Layer.Opacity;
			Custom[4] = Layer.bUsesAtlas ? Particle.Cell : SoftEdge(Size);
		}

		int32 Drawn = Layer.Live;
		if (LayerIndex == 0)
		{
			for (int32 Index = 0; Index < MaxStreaks; ++Index)
			{
				FTransform& Transform = Layer.Transforms[Capacity + Index];
				Transform = HiddenInstance;
				if (Index < Streaks.Num() && StreakTransform(Streaks[Index].From, Streaks[Index].To, Streaks[Index].Width, ViewLocation, Transform))
				{
					float* Custom = &Layer.CustomData[(Capacity + Index) * CustomFloats];
					Custom[0] = Streaks[Index].Color.R;
					Custom[1] = Streaks[Index].Color.G;
					Custom[2] = Streaks[Index].Color.B;
					Custom[3] = Streaks[Index].Color.A;
					Custom[4] = SoftEdge(Streaks[Index].Width);
					++Drawn;
				}
			}
			Streaks.Reset();
		}
		NumLive += Layer.Live;

		// Nothing to show and the buffers already show nothing: the render thread is left alone.
		UInstancedStaticMeshComponent* Component = Layer.Component.Get();
		if (Component != nullptr && !(Drawn == 0 && Layer.bDrawnEmpty))
		{
			Component->SetCustomData(0, Layer.Transforms.Num() - 1, Layer.CustomData, false);
			Component->BatchUpdateInstancesTransforms(0, TArrayView<const FTransform>(Layer.Transforms), true, true, true);
			Layer.bDrawnEmpty = Drawn == 0;
		}
	}
}
