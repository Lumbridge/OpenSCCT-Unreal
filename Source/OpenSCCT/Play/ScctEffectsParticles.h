#pragma once

#include "CoreMinimal.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** Which of the sprite materials a particle is drawn with. */
enum class EScctFxLayer : uint8
{
	Glow,  // added light, unlit: fire, sparks, flashes, tracers
	Smoke, // soft and lit by the scene: smoke, dust, mist
	Cloud, // a cell of the smoke grenade's own picture, lit by the scene: the spy's smoke cloud
	StickyGas, // a cell of the sticky camera's smoke_extincteur atlas
	Count,
};

/** One sprite of the effects' own particle system. Every field can be set by whoever spawns it. */
struct FScctFxParticle
{
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	/** cm/s2 added to the velocity, besides Gravity (an original emitter's Acceleration, turned into the world). */
	FVector Acceleration = FVector::ZeroVector;

	/** Colour, and the opacity at its fullest. Glow sprites add Colour x Alpha to the picture (values above 1 bloom). */
	FLinearColor Color = FLinearColor::White;

	float Age = 0.0f;
	float Life = 0.0f;       // seconds; a particle with Age >= Life is free
	float SizeStart = 10.0f; // cm across, at birth and at death
	float SizeEnd = 10.0f;
	float Stretch = 0.0f;    // > 0: drawn as a streak along its velocity, this many seconds of travel long
	float Drag = 0.0f;       // velocity lost per second (1 / s)
	float Gravity = 0.0f;    // cm/s2 downwards; negative rises
	float Rotation = 0.0f;   // radians about the view axis
	float Spin = 0.0f;       // radians per second
	float FadeIn = 0.05f;    // fraction of the life over which it appears; it fades out over the rest
	/** 0 or more: the fraction of the life at which it starts to fade out, at full strength until then (an original emitter's FadeOutStartTime). */
	float FadeOutStart = -1.0f;
	/**
	 * An original emitter's VelocityLossRange, 1 / s per world axis: each frame, after the move, every axis of the
	 * velocity loses a pick between the two times itself times the frame's seconds. Used instead of Drag when LossMax
	 * is not zero.
	 */
	FVector LossMin = FVector::ZeroVector;
	FVector LossMax = FVector::ZeroVector;
	/** Cloud layer: which cell of the picture's grid it shows, counted along the rows. */
	float Cell = 0.0f;
};

/**
 * The particle system of the effects: sprites simulated here on the game thread and drawn as instances of one
 * quad per layer, turned to the camera each frame. Everything is allocated once: a spawn takes the next slot
 * of a ring (the oldest sprite goes when the ring is full) and a frame writes the instance buffers in place.
 *
 * Why not Niagara: a Niagara system is an asset whose emitters are graphs compiled by the editor. There is no
 * supported way to author one from code in a headless run and none at all at run time, and the engine's stock
 * emitters cannot be re-tuned without those graphs. The game's effects are small (hundreds of sprites), so
 * simulating them here costs little and keeps every effect's look in C++ next to the event that triggers it.
 */
class FScctFxSprites
{
public:
	/** Creates the instanced components on Host. A missing material or mesh leaves that layer undrawn. */
	void Init(AActor* Host, UStaticMesh* Quad, UMaterialInterface* GlowMaterial, UMaterialInterface* SmokeMaterial,
		UMaterialInterface* CloudMaterial, UMaterialInterface* StickyGasMaterial);

	/** How much of its opacity a layer's sprites are drawn with from the next Tick on (1 as spawned, 0 not at all): what a vision mode lets through. */
	void SetLayerOpacity(EScctFxLayer Layer, float Opacity);

	/** A new particle to fill in. The reference is valid until the next Spawn. */
	FScctFxParticle& Spawn(EScctFxLayer Layer);

	/** A glowing line for this frame only (a tracer, a beam): drawn by the next Tick and then forgotten. */
	void DrawStreak(const FVector& From, const FVector& To, float Width, const FLinearColor& Color);

	/** Moves the particles on and writes what the camera at ViewLocation sees. */
	void Tick(float DeltaSeconds, const FVector& ViewLocation, const FQuat& ViewRotation);

	/** Removes every particle at once and stops drawing them, without waiting for a Tick (a match ended). */
	void Clear();

	int32 GetNumLive() const { return NumLive; }
	bool IsDrawable() const { return Layers[0].Component.IsValid() || Layers[1].Component.IsValid(); }

private:
	struct FStreak
	{
		FVector From;
		FVector To;
		float Width;
		FLinearColor Color;
	};

	struct FLayer
	{
		TArray<FScctFxParticle> Particles;
		TArray<FTransform> Transforms;
		TArray<float> CustomData;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Component; // owned by the host actor, which may go first
		int32 Next = 0;
		int32 Live = 0;
		float Opacity = 1.0f;
		bool bUsesAtlas = false;
		bool bDrawnEmpty = false; // the instance buffers already show nothing
	};

	static constexpr int32 MaxStreaks = 96;

	FLayer Layers[static_cast<int32>(EScctFxLayer::Count)];
	TArray<FStreak> Streaks;
	int32 NumLive = 0;
};
