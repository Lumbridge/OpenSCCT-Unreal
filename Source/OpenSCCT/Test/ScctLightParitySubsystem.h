#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Test/ScctLightProbeApi.h"
#include "ScctLightParitySubsystem.generated.h"

class AActor;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * The light parity measurement: does a place the original shows dark come out dark in Unreal's picture, and a lit
 * one lit? Light and dark decide the game, and the original has no light rule but the picture itself, so the
 * measurement compares pictures, spot by spot, against the one model of the original's light the simulation holds
 * (the bots' light field: the original's pawn lighting) and the original's baked light under the spot.
 *
 *   -ScctLightParity[=N]       measure the map being played, N spots of each kind (default 8: 8 dark, 8 bright, 8
 *                              spread between and 8 in pairs across the sharpest steps of the level = 32), then quit
 *   -ScctLightDir=<folder>     where the pictures and the table go (default Saved/ScctLight)
 *   -ScctLightName=<name>      the name in the lines and file names (default the level's name; give the map's for
 *                              a map drawn in the runtime level), -ScctLightTag=<tag> is added to it
 *   -ScctLightSettle=<s>       seconds the camera rests on a spot before it is measured (default 2.5), so the
 *                              exposure and the bounce light have settled
 *   -ScctLightNoBody           measure the grey sphere only
 *   -ScctLightSaveScene        also save each HDR capture (times the view's exposure, no tone curve) as *_scene.png
 *
 * Run it with Tools\Run-Offscreen.ps1 -Map <Map> -Shots 0 -Options "?Side=spy?Bots=0" -Extra "-ScctLightParity"
 * (add -Runtime for the runtime level) and read the result with Tools/Test/light_parity_report.py.
 *
 * The bridge picks the spots (walkable floor: LightProbeExports.FindSpots) and gives each one's level. At
 * each spot a matte sphere of 18 % grey, 30 cm across, hangs at the height of a standing pawn's centre, and the
 * camera goes 1 to 4 m off it on the side with the most room. Measured in the picture of the game's own view (its
 * post-processing and exposure, read back as a screenshot without the HUD): the sphere and the ring of background
 * round it, as display luma of 255. Measured in a second, HDR capture from the same point (scene colour before
 * exposure and tone curve): the same two as scene luminance. The capture is a view of its own: it agrees with the
 * game's view on what a lamp lights directly, but its bounce and sky light are not the game view's (measured on an
 * unlit sphere: a tenth of it in AquaE, five times it on BankP's roof), so for a place no lamp reaches go by the
 * picture and the exposure (the report's "picture as light / exposure"). Then the same again with the local player's own spy
 * stood on the spot instead of the sphere (its trunk, found by its spine bones, and a frame of background round the
 * whole body), which gives how well a body shows against what is behind it.
 *
 * One line per spot in the log, and the same as <name>.csv beside the pictures <name>_<nn>_sphere.png / _body.png:
 *
 *   SCCT_LIGHT <name> <nn> kind=<kind> at=<x,y,z> sim=<level> facing=<level> baked=<level or -> sphere=<raw>/<picture>
 *     ground=<raw>/<picture> body=<raw>/<picture> behind=<raw>/<picture> exposure=<scale> ev=<EV100>
 */
UCLASS()
class OPENSCCT_API UScctLightParitySubsystem : public UTickableWorldSubsystem
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
	enum class EPhase : uint8 { WaitMatch, Aim, Settle, Read, Done };

	/** What one picture gave: the reference and what is round it, before exposure and in the finished picture. */
	struct FMeasure
	{
		float Raw = -1.0f;        // scene luminance of the reference
		float RawAround = -1.0f;  // and of the background round it
		float Picture = -1.0f;    // display luma of the reference, of 255
		float PictureAround = -1.0f;
		float Exposure = 0.0f;    // the view's exposure scale when it was read
		float SceneLuminance = 0.0f; // the view's average scene luminance
	};

	struct FSpot
	{
		FScctLightSpot Spot;
		FScctLightReading Reading;
		FVector Centre = FVector::ZeroVector;
		FTransform Camera;
		float CameraDistance = 0.0f;
		FMeasure Sphere;
		FMeasure Body;
		/** Where the stood body is when it is measured: points inside its trunk (spine bones), and every bone. */
		TArray<FVector> Trunk;
		TArray<FVector> Bones;
	};

	bool Begin();
	void AimAt(int32 Index);
	void ReadCapture(FMeasure& Measure, const FSpot& At) const;
	void FindBody(FSpot& At) const;
	void MatchCaptureToView(const FSpot& At);

	/** The body's region (discs round its trunk points) and its background (a frame round all of it) in a picture. */
	template <typename TPixel, typename TLuminance, typename TProject>
	static void MeasureBody(const TArray<TPixel>& Pixels, int32 Width, int32 Height, const FSpot& At, double PixelsPerCm, TProject Project,
		TLuminance Luminance, float& OutReference, float& OutAround);
	void OnCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	void Finish();
	UMaterialInterface* MakeGrey();

	/** The reference's region and its background in a picture: Centre in pixels, PixelsPerCm the scale at the reference. */
	template <typename TPixel, typename TLuminance>
	static void MeasureRegions(const TArray<TPixel>& Pixels, int32 Width, int32 Height, FVector2D Centre, double PixelsPerCm, bool bBody,
		TLuminance Luminance, float& OutReference, float& OutAround);

	const FScctLightProbeApi* Api = nullptr;
	TArray<FSpot> Spots;
	EPhase Phase = EPhase::WaitMatch;
	int32 Index = 0;
	bool bBodyPass = false;
	bool bMeasureBody = true;
	bool bSaveCapture = false;
	bool bCaptured = false;
	double PhaseAt = 0.0;
	double ReadyAt = -1.0;
	double SettleSeconds = 2.5;
	int32 PerKind = 8;
	FString Name;
	FString Directory;
	FDelegateHandle CaptureHandle;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Rig;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Sphere;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Grey;
};
