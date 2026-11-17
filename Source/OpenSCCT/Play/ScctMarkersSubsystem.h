#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctWorldApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctMarkersSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UScctLevelStateSubsystem;
class UScctSessionSubsystem;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Draws the gameplay markers of the bridge's world table (Runtime/ScctWorldApi.h) in the world, each frame on the
 * session subsystem's OnFrame:
 *
 *   - objectives (terminals, bomb spots, the disk and its drop zone, Disk Hunt tags) as a small holographic sign
 *     floating over them, turned to the camera: a ring, a glyph for the kind and an arc that fills while someone
 *     takes the objective. Its colour is the objective's state: cyan open, amber being taken (it throbs), red
 *     locked or armed, green taken, grey not in play yet;
 *   - security cameras and room sensors as a small status lamp on top of the device: green watching, red
 *     alarmed, amber locked;
 *   - detector beams (tripwires, mine beams) as a hair-thin bright core inside a faint wide glow that shimmers
 *     like dust in the light, with a small bloom where the beam starts and where it lands.
 *
 * The looks are two materials built by Tools/Level/build_level_assets.py (/Game/OpenSCCT/Level) on engine shapes;
 * without them nothing is drawn and the log says so.
 *
 * While the goggles show thermal or EMF (Hud/ScctVisionSubsystem), every beam is drawn for the goggles instead, as
 * the original's vision-only beam node is: a cylinder that is not drawn in the picture, only into the custom depth
 * stencil, which the mode's material paints: in thermal at the beam's HeatIntensity, in EMF as a dark grey streak.
 * Beams the map hides from the naked eye (bHideInNormalVision: most tripwires, every mine beam) show only then.
 *
 * Unattended switch (off by default):
 *   -ScctShowVisionLasers   draw the beams that only show in thermal and EMF vision in plain sight too, dimmer, so
 *                           their placement can be checked in a normal picture
 *   -ScctLevelLog           (shared with the level state) list every marker at the first frame and every beam
 *                           as it appears, with their places
 */
UCLASS()
class OPENSCCT_API UScctMarkersSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** A set of like components made as they are needed and handed out again when their marker goes. */
	struct FPool
	{
		TWeakObjectPtr<UStaticMesh> Mesh;
		TWeakObjectPtr<UMaterialInterface> Material;
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Components;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
		TArray<bool> Used;
	};

	/** How a marker is drawn this frame. */
	struct FLook
	{
		FLinearColor Color = FLinearColor::White;
		float Intensity = 0.0f;
		float Pulse = 0.0f;
		float Progress = 0.0f;
		float Shape = 0.0f;   // 0 diamond, 1 square, 2 circle
		float Size = 1.0f;    // of the usual sign
		float Lift = 38.0f;   // cm above the marker's place, when the thing it marks is not drawn
		bool bSign = false;   // a holographic sign
		bool bLamp = false;   // a small status lamp on top of the device's body (at the marker's place when the device is not drawn)
	};

	struct FDrawn
	{
		int32 Slot = INDEX_NONE;
		bool bLamp = false;
		uint32 SeenFrame = 0;

		/** The actor that is drawn for the marked thing, and when it was last looked for. */
		TWeakObjectPtr<AActor> Object;
		uint32 LookedFrame = 0;
	};

	struct FBeam
	{
		int32 Core = INDEX_NONE;
		int32 Halo = INDEX_NONE;
		int32 Ends[2] = { INDEX_NONE, INDEX_NONE };
		int32 Vision = INDEX_NONE;   // the goggles' stencil-only cylinder, while thermal or EMF is shown
		uint32 SeenFrame = 0;
	};

	void HandleSessionChanged(int32 OldId, int32 NewId);
	void HandleMatchStarted();
	void HandleFrame(float DeltaSeconds);

	bool EnsureScene();
	void ResetMatch();
	void UpdateMarkers(const FVector& ViewLocation);
	void UpdateLasers();

	static FLook LookOf(const FScctWorldMarker& Marker);

	int32 Acquire(FPool& Pool);
	void Release(FPool& Pool, int32& Slot);

	UPROPERTY(Transient)
	TObjectPtr<UScctSessionSubsystem> Session;

	UPROPERTY(Transient)
	TObjectPtr<UScctLevelStateSubsystem> LevelState;

	/** The actor every marker component hangs on (it keeps them, and their materials, alive). */
	UPROPERTY(Transient)
	TObjectPtr<AActor> Host;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> MarkerMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BeamMaterial;

	/** The engine shapes the pools draw with: kept here, since a pool that has made no component yet holds nothing alive. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PlaneMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;

	const FScctWorldApi* World = nullptr;

	FPool Signs;
	FPool Lamps;
	FPool Cores;
	FPool Halos;
	FPool VisionBeams;

	TMap<int32, FDrawn> Drawn;   // by the marker's name id
	TMap<int32, FBeam> Beams;    // by the beam's id
	TArray<FScctWorldMarker> MarkerBuffer;
	TArray<FScctWorldLaser> LaserBuffer;

	uint32 Frame = 0;
	float MatchSeconds = 0.0f;
	bool bSceneReady = false;
	bool bSceneFailed = false;
	bool bShowVisionLasers = false;
	bool bReported = false;
	bool bLog = false;

	// For the summary.
	int32 PeakSigns = 0;
	int32 PeakLamps = 0;
	int32 PeakBeams = 0;
	int32 NumVisionOnlyBeams = 0;
	int32 PeakGoggleBeams = 0;

	/** What the log last said about the beams drawn for the goggles: their count and the mode. */
	int32 LoggedGoggleBeams = 0;
	int32 LoggedGoggleMode = 0;
};
