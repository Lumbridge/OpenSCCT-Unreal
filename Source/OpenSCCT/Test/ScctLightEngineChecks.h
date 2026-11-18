#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctLightEngineChecks.generated.h"

class AActor;
class APostProcessVolume;
class UMaterial;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UPoseableMeshComponent;
class UPrimitiveComponent;
class USpotLightComponent;
class UStaticMeshComponent;

/**
 * The engine behaviours the lighting plan leans on, measured in the game's own finished picture before any step relies
 * on one (the plan's step 1). A closed grey room is built 500 m beside the map being played (out of reach of its lights
 * and of the ray-traced scene round it), the camera stands inside, and one arrangement after another is lit and read:
 *
 *   ramp       glowing patches of known scene light: the transfer from scene light to display value, by which every
 *              later reading is turned back into scene light
 *   falloff    a point light without the inverse square law at exponents 1, 2.2 and 4: matte cards at six distances
 *              against (1 - (d/R)^2)^exponent, and the constant K between intensity x albedo and scene light
 *   diffuse    a light's diffuse scale and specular scale: a matte card and a mirror-like one with each at 0
 *   channels   a light on lighting channels 0+1, on 0 only and on 1 only: a sphere on channel 1 only (a body of the
 *              plan), one such behind a channel-0 block (does the world shadow a light the body alone receives), a
 *              channel-0 card, and a channel-0 card behind the channel-1 sphere (does the body still cast a shadow)
 *   bounce     a lamp on the floor, spheres out of its cone with the material's ambient occlusion pin at 1, 0.5 and 0
 *              (and one of them on channel 1 only): does the bounce light obey the pin; the same under the sky light
 *              with the ceiling off
 *   fog        a beam through volumetric fog from a light on channel 0, on channel 1 only, on channel 2 only (nothing
 *              receives it), with diffuse and specular scale 0, with diffuse scale 0 alone, with both scales at 0.01 and
 *              a hundred times the scattering, and the first two again as lights MegaLights may not take: does the beam
 *              still show, and is its landing card dark
 *   cpd        custom primitive data floats 0, 30 and 35 read by a material on a skinned (poseable) mesh
 *   ramp again with this measurement's own exposure a stop up (which volume sets the exposure)
 *
 *   -ScctLightEngineChecks     run them on the map being played, then quit
 *   -ScctLightDir=<folder>     where the pictures and the table go (default Saved/ScctLight)
 *
 * The measurement brings a post-process volume of its own (a manual exposure, no local exposure, bloom, lens flares,
 * vignette, grain). The exposure's scale is still the game's (Play/ScctExposure.cpp's volume is applied after every
 * other: 0.59 with scct.Light.Plan 0, the plan's fixed scale with 1; the last arrangement, the ramp with this volume's
 * exposure a stop up, shows it by reading the same). The readings as scene light do not depend on the scale: every
 * picture is turned back into scene light by the ramp read at the same exposure, and they agreed between a run at 0.59
 * and one at 1.2. One SCCT_ENGINE line per reading and engine_checks.csv; a picture per arrangement
 * (engine_<nn>_<name>.png); the volumes of the view in the order they are applied are logged with the first picture.
 * Needs an editor build (the materials are made in code).
 */
UCLASS()
class OPENSCCT_API UScctLightEngineChecks : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	enum class EPhase : uint8 { WaitMatch, Settle, Read, Done };

	struct FRegion
	{
		FString Label;
		FVector At = FVector::ZeroVector;
		float Half = 5.0f;
		/** What the surface would be for a light term of 1 (albedo x intensity x falloff), 0 when there is no such figure. */
		float Expected = 0.0f;
	};

	bool Build();
	void Apply(int32 Config);
	void Regions(int32 Config, TArray<FRegion>& Out) const;
	void OnCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	void Finish();
	float LightOf(float Luma) const;

	UMaterialInstanceDynamic* Lit(float Albedo, float Occlusion = 1.0f, float Roughness = 1.0f, float Specular = 0.0f, float Metallic = 0.0f);
	UStaticMeshComponent* AddShape(int32 Group, const TCHAR* Mesh, const FTransform& Place, UMaterialInstanceDynamic* Material);
	UStaticMeshComponent* AddCard(int32 Group, const FVector& At, const FVector& Facing, float Size, UMaterialInstanceDynamic* Material);
	UStaticMeshComponent* AddSphere(int32 Group, const FVector& At, float Radius, UMaterialInstanceDynamic* Material);
	void Point(const FVector& At, float Radius, float Exponent, float Intensity, bool bShadow, bool bChannel0, bool bChannel1, bool bChannel2);
	void Spot(const FVector& At, const FVector& Towards, float Cone, float Radius, float Intensity, float Scatter, bool bChannel0, bool bChannel1, bool bChannel2);

	EPhase Phase = EPhase::WaitMatch;
	int32 Config = 0;
	double PhaseAt = 0.0;
	double ReadyAt = -1.0;
	bool bCaptured = false;
	FString Directory;
	FString Table;
	FDelegateHandle CaptureHandle;
	FVector Origin = FVector::ZeroVector;
	FTransform Camera;

	/** The ramp as read: scene light of each patch and the display luma it showed. */
	TArray<float> RampLight;
	TArray<float> RampLuma;

	/** What the checks changed in the level and put back at the end. */
	float SkyIntensityWas = -1.0f;
	float FogDensityWas = -1.0f;
	float FogFalloffWas = 0.0f;
	bool bFogVolumetricWas = false;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Rig;

	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> Volume;

	UPROPERTY(Transient)
	TObjectPtr<UMaterial> LitMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterial> GlowMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Kept;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WallMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Ceiling;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> PointLight;

	UPROPERTY(Transient)
	TObjectPtr<USpotLightComponent> SpotLight;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPoseableMeshComponent>> Skinned;

	/** The pieces of each arrangement, shown only while it is measured. */
	TArray<TArray<TWeakObjectPtr<UPrimitiveComponent>>> Groups;
};
