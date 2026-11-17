#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctSoftBodyApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctLevelStateSoftBodies.generated.h"

class UProceduralMeshComponent;
class UScctSessionSubsystem;
class UStaticMeshComponent;
struct FProcMeshTangent;

/**
 * The map's soft bodies, moving: the original's cloth patches (flags, awnings, banners) and strip curtains, which the
 * importer places as plain meshes tagged "ScctSoftBody:<patch or strip>" (and "Scct:" + the map actor's name, as every
 * placed mesh). When a match starts, each body the bridge built (Runtime/ScctSoftBodyApi.h) takes the place of its
 * level mesh: a procedural mesh with the same material and the same place, its triangles and texture coordinates from
 * the map's render data, whose vertices are rewritten every frame from the bridge's simulation. The bridge steps the
 * bodies drawn in the last five seconds (this subsystem tells it which ones were drawn), so pawns walking through a
 * curtain push its strips aside, shots push the cell they cross, the wind keeps a flag moving, and the collision and
 * shot sounds come through the audio subsystem like any positional sound of the match. The level meshes come back
 * when the match ends.
 *
 * What differs from the Godot client: the bodies are lit by Unreal's lights like any movable mesh (the Godot client
 * lights them as actors from their rest bounds), and the back face takes the front face's normal as there.
 *
 * Unattended switches (off by default):
 *   -ScctSoftBodyLog                 a line a second while a body moves: the farthest a particle is from rest, the
 *                                    bodies stepped, the pawns pushing and the sounds played
 *   -ScctSoftBodyTest="what@S;..."   S seconds into the match: "walk" sends a pawn-sized cylinder through every body
 *                                    pawns push, "shoot" puts a shot through every body's middle. The log names a
 *                                    camera for each body that -ScctCams takes.
 *   -ScctSoftBodyOff                 leave the level's still meshes in place (to compare)
 */
UCLASS()
class OPENSCCT_API UScctSoftBodySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FBody
	{
		FString Name;
		FScctSoftBodyInfo Info = {};
		TWeakObjectPtr<UStaticMeshComponent> LevelMesh;
		TWeakObjectPtr<UProceduralMeshComponent> Mesh;
		bool bLevelMeshWasVisible = true;
		TArray<FVector2D> UVs;
		TArray<FVector> Positions;
		TArray<FVector> Normals;
		float LastSeenSeconds = -100.0f;
	};

	struct FTestAction
	{
		FString What;
		float Seconds = 0.0f;
		bool bDone = false;
	};

	void HandleMatchStarted();
	void HandleMatchStopped();
	void HandleSessionChanged(int32 OldId, int32 NewId);
	void HandleFrame(float DeltaSeconds);

	void Build();
	void Restore();
	void RunTests();
	void LogSecond();

	UPROPERTY(Transient)
	TObjectPtr<UScctSessionSubsystem> Session;

	const FScctSoftBodyApi* Api = nullptr;
	TArray<FBody> Bodies;
	TArray<uint8> Seen;
	TArray<float> VertexBuffer;
	TArray<FTestAction> Tests;

	bool bBuilt = false;
	bool bLog = false;
	float MatchSeconds = 0.0f;
	float LogTimer = 0.0f;
	float SecondMaxDisplacement = 0.0f;
	int32 SecondStepped = 0;
	int32 SecondColliders = 0;
	int32 LoggedCollSounds = 0;
	int32 LoggedShotSounds = 0;
	int32 LoggedShotHits = 0;
	float PeakDisplacement = 0.0f;
	int32 Rewrites = 0;
};
