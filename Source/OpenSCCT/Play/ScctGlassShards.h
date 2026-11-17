#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctGlassShards.generated.h"

class AActor;
class UMaterialInterface;
class UProceduralMeshComponent;
class UScctSessionSubsystem;
class UStaticMeshComponent;
struct FScctFxTraceApi;

/**
 * A broken pane comes apart in shards shaped from the pane itself, as the Godot client's GlassBreakViews draws them:
 * when the level state hears that a pane (SVitre) broke, the pane mesh it draws is cut into deterministic Voronoi
 * cells around the hit (small chips at the impact, larger irregular sheets across the rest; GlassFracture's math,
 * ported in ScctGlassFracture), clipped to the pane's own triangles with their texture coordinates and extruded to
 * the pane's thickness, wearing the pane's material. Each shard leaves after a delay that grows with its distance
 * from the hit, flies away from it along the hit's direction (the pane's normal when the simulation gave none) and
 * outwards, spins, falls at 9.8 m/s^2, bounces off the client world's collision (Runtime/ScctFxTraceApi.h: the
 * imported level has no Unreal collision) with the Godot client's damping, then lies flat on what it landed on. A
 * burst lasts 4.5 s and its shards shrink away over the last 0.8 s; at most six bursts are kept, the oldest going
 * first. The shape of the shards comes from the pane's name and impact serial, so every player sees the same pieces.
 *
 * What differs from the Godot client: the shards shrink away rather than fading their material's opacity (the
 * imported glass materials have no opacity parameter to fade), and they are lit by Unreal's lights as movable meshes.
 * The pane's triangles are read from the drawn static mesh's render data on the CPU, which an uncooked game always
 * has; a cooked build needs the pane meshes imported with Allow CPU Access, else a pane keeps only its glint burst
 * (logged once).
 *
 * Unattended switches: -ScctFxTimeScale=K runs the shards at K times real speed, as the other effects;
 * -ScctLevelDo="breakglass@S" (the level state) breaks every pane S seconds into a local match.
 */
UCLASS()
class OPENSCCT_API UScctGlassShardsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Breaks the drawn pane into shards. Name is the pane's map name and Serial its impact serial (both seed the
	 * shape); Impact is where it was hit and Direction the hit's direction, or zero when the simulation gave none
	 * (the middle of the pane and its normal are used). Returns the number of shards, 0 when the mesh is not a pane.
	 */
	int32 Break(const FString& Name, UStaticMeshComponent* Pane, const FVector& Impact, const FVector& Direction, int32 Serial);

	/** The middle of a pane's flat face and its normal in the world, for a check that picks a point to hit; false when it is not a pane. */
	static bool FindPaneFace(const UStaticMeshComponent* Pane, FVector& OutCenter, FVector& OutNormal, FVector& OutAcross);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FPiece
	{
		TWeakObjectPtr<UProceduralMeshComponent> Mesh;
		FVector Location = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		FVector Velocity = FVector::ZeroVector;
		/** Radians a second about its axis. */
		FVector Spin = FVector::ZeroVector;
		FVector PlaneNormal = FVector::UpVector;
		float Delay = 0.0f;
		float RestAngle = 0.0f;
		int32 Bounces = 0;
		float StartZ = 0.0f;
	};

	struct FBurst
	{
		FString Name;
		TWeakObjectPtr<AActor> Host;
		TArray<FPiece> Pieces;
		float Age = 0.0f;
		bool bReported = false;
	};

	void HandleFrame(float DeltaSeconds);
	void HandleSessionChanged(int32 OldId, int32 NewId);
	void Step(FBurst& Burst, float Seconds);
	void Report(FBurst& Burst);
	void Remove(int32 Index);
	void Clear();

	UPROPERTY(Transient)
	TObjectPtr<UScctSessionSubsystem> Session;

	const FScctFxTraceApi* FxTrace = nullptr;
	TArray<FBurst> Bursts;
	float TimeScale = 1.0f;
	int32 NumBursts = 0;
	int32 NumShards = 0;
	bool bReportedNoCpuData = false;
	bool bReportedNoTrace = false;
};
