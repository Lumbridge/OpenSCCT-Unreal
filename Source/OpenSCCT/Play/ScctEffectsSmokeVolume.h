#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctSmokeApi.h"
#include "UObject/StrongObjectPtr.h"

class AActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

/**
 * Responsive smoke drawn as volumes: when the match runs SvmResponsiveSmoke, every cloud of the shared smoke field
 * (Runtime/ScctSmokeApi.h) is a box over the field's bounds with a ray-marched material (M_ScctFxSmokeVolume, built by
 * Tools/Effects/build_effect_assets.py). The bridge samples the cloud's density onto a grid a few times a second, the
 * same density gas, coughing and AI sight read, so the walls that hold it back, the way it grows into the free air and
 * the holes shots push through it are on screen where the simulation has them. The grid goes into a texture atlas
 * (one slice per tile); the material mixes the last two grids over the time between them, so growth and refilling run
 * smoothly, and moves what it reads by a slow rolling warp so the cloud is never still. It is lit by the light at the
 * cloud: the map's lights that reach its middle (direct, with the cloud's own shadow toward them) and a share of the
 * lights around as ambient, as the Godot client's ResponsiveSmokeLighting does with its probes.
 *
 * Quality (the Graphics page's 'Responsive smoke quality', applied at once; console scct.SmokeQuality 0|1|2):
 *   Low     16^3 grid, 10 updates a second, 32 march steps across the box, no self-shadow
 *   Medium  32^3 grid, 15 updates a second, 56 steps, 2 shadow steps
 *   High    48^3 grid, 20 updates a second, 88 steps, 4 shadow steps
 * Lighting quality never changes the cloud itself: the grid is the simulation's density at any quality.
 *
 * What differs from the Godot client: the material has no depth test of its own (it stops at the scene's opaque depth
 * itself), so it is sorted with other see-through things as a whole box: glass or a cloaked spy inside or behind the
 * cloud is not mixed into it at its own depth, as the Godot client's glass and cloak shaders do. Particles are drawn
 * over or under the cloud the same way. The light comes from one probe at the cloud's middle, not eight. The density
 * is the simulation's sampled on a grid and interpolated, where the Godot client evaluates the field at every step;
 * the shots' holes, finer than any grid, are carved exactly from the bridge's list of them.
 */
class FScctSmokeVolumes
{
public:
	/** The quality option's key in the settings registry (Settings/ScctSettings.h): 0 Low, 1 Medium, 2 High. */
	static const FName QualitySetting;

	/** Registers the quality option on the Graphics page. Safe to call more than once. */
	static void RegisterSettings();

	FScctSmokeVolumes();
	~FScctSmokeVolumes();

	/** Every box hangs on Host. */
	void Init(AActor* InHost);

	/** Once a frame: finds the clouds, uploads their grids when due, moves and lights their boxes. */
	void Update(float DeltaSeconds);

	/** The match ended: the clouds go and the summary is logged. */
	void Reset();

	/** True while the match runs responsive smoke. */
	bool IsOn() const { return bOn; }

	/**
	 * True when the sprites of an effect actor must not be drawn because its cloud is drawn as a volume: its id is a
	 * cloud's emitter, or it is a smoke emitter (bSmokeEmitter) younger than a second while responsive smoke is on
	 * (its cloud reaches the client a few ticks after the emitter does).
	 */
	bool CoversEmitter(int32 ActorId, bool bSmokeEmitter, float Age) const;

private:
	struct FCloud
	{
		int32 EmitterId = 0;
		TStrongObjectPtr<UStaticMeshComponent> Box;
		TStrongObjectPtr<UMaterialInstanceDynamic> Look;
		/** The two latest grids: Grids[Newest] the last one, the other the one before. */
		TStrongObjectPtr<UTexture2D> Grids[2];
		/** The open shot holes, three texels a row (start and radius, end and strength, feather), one row a hole. */
		TStrongObjectPtr<UTexture2D> Holes;
		int32 HoleCount = 0;
		uint32 HoleKey = 0;
		int32 PeakHoles = 0;
		int32 Newest = 0;
		int32 Resolution = 0;
		float SinceUpload = 0.0f;
		float SinceProbe = 1.0e6f;
		uint32 SeenFrame = 0;
		int32 Uploads = 0;
		float Coverage = 0.0f;
		FScctSmokeField Last = {};
	};

	struct FQuality
	{
		int32 Resolution;
		float Interval;
		float Steps;
		float ShadowSteps;
		const TCHAR* Name;
	};

	static const FQuality& QualityOf(int32 Level);

	void HandleSettingChanged(FName Key);
	void ApplyQuality(bool bLog);
	FCloud& StartCloud(const FScctSmokeField& Field);
	void EndCloud(FCloud& Cloud);
	void Upload(FCloud& Cloud);
	/** Hands the cloud's open shot holes to its material, every frame (a hole opens and refills in half a second). */
	void UpdateHoles(FCloud& Cloud);
	void ProbeLight(FCloud& Cloud);

	TWeakObjectPtr<AActor> Host;
	TStrongObjectPtr<UMaterialInterface> Material;
	TStrongObjectPtr<UStaticMesh> Cube;
	const FScctSmokeApi* Api = nullptr;
	FDelegateHandle SettingHandle;
	TArray<FCloud> Clouds;
	TArray<FScctSmokeField> FieldBuffer;
	TArray<FScctSmokeHole> HoleBuffer;
	TArray<uint8> Grid;
	TArray<uint8> Atlas;
	int32 Quality = 1;
	uint32 Frame = 0;
	float SinceReport = 0.0f;
	bool bOn = false;
	bool bReported = false;
	bool bMaterialMissing = false;

	// For the summary.
	int32 NumClouds = 0;
	int32 NumUploads = 0;
	int32 NumHolesDrawn = 0;
	int32 PeakClouds = 0;
	double SampleMilliseconds = 0.0;
};
