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
 *   -ScctLightParity[=N]       measure the map being played, N spots of each kind (default 16: 16 dark, 16 bright, 16
 *                              spread between and 16 in pairs across the sharpest steps of the level = 64), then quit
 *   -ScctLightAllFloor         sample any connected floor a map light hangs over (the first version's rule, which
 *                              took the floor inside a fish tank and roofs nobody reaches) instead of the play floor:
 *                              what can be walked or dropped to from a start, an objective, a flag zone or a switch
 *   -ScctLightNoSwitch         leave out the switch test
 *   -ScctLightNoTone           leave out the tone ramp
 *   -ScctLightToneMax=<code>   show the ramp's patches up to this display value only (default 510, the whole ramp)
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
 *   SCCT_LIGHT <name> <nn> kind=<kind> at=<x,y,z> sim=<level> facing=<level> pred=<picture> baked=<level or -> sphere=<raw>/<picture>
 *     ground=<raw>/<picture> body=<raw>/<picture> behind=<raw>/<picture> exposure=<scale> ev=<EV100> seen=<sphere><body>
 *
 * "seen" says whether the engine drew the sphere in the player's view at all (1) or culled it as hidden (0: the
 * camera's side was not open after all, and the readings of the spot are of what stands in the way; the second digit,
 * the body's, repeats the sphere's).
 * "pred" is the sphere's picture by the simulation's own pawn light term (the bridge's PredictSphere: a texture of the
 * sphere's display value lit per pixel by the zone ambient, the four strongest lights and the projectors, capped at
 * 2, over the measured disc). It is what the lighting plan draws a body-class surface as, so under the plan the
 * measured sphere is judged against it. The table also says which lighting the level was dressed with (plan,
 * scct.Light.Plan: Play/ScctLightPlan.h).
 *
 * The switch test (ScctLightParityChecks.cpp): for each switched light of the map (up to 16) the sphere at the floor
 * spot where a pawn's level changes most and at one beyond the light's radius where it does not change, measured as
 * the level starts, after every switched light was fired once (the bridge's FireLights: the lit ones go dark, the
 * others light) and after a second firing (back as before). SCCT_SWITCH lines and <name>_switch.csv.
 *
 * The tone ramp (the same file): a board of 24 grey patches a metre in front of the camera of the first spot, unlit,
 * each glowing with the scene light of one display value of the original (code/255 to the power 2.2; above 255 the
 * overbright range the original's light term reaches before it clips), measured in the finished picture. It gives the
 * net transfer of exposure, tone curve and bloom: what display value the game shows for a surface the original shows
 * at a known one. SCCT_TONE lines and <name>_tone.csv, the picture as <name>_tone.png.
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
	enum class EPhase : uint8 { WaitMatch, Aim, Settle, Read, Fire, ToneSettle, ToneRead, Done };

	/** What is being measured round the spots: the sphere, the stood body, and the sphere again after each firing of the switched lights. */
	enum class EPass : uint8 { Sphere, Body, Switched, Back };

	/** What one picture gave: the reference and what is round it, before exposure and in the finished picture. */
	struct FMeasure
	{
		float Raw = -1.0f;        // scene luminance of the reference
		float RawAround = -1.0f;  // and of the background round it
		float Picture = -1.0f;    // display luma of the reference, of 255
		float PictureAround = -1.0f;
		float Exposure = 0.0f;    // the view's exposure scale when it was read
		float SceneLuminance = 0.0f; // the view's average scene luminance
		/**
		 * The sphere was drawn in the player's view when it was read (the engine's own record of what it drew after
		 * occlusion culling). False for a sphere the camera cannot see at all: a spot behind a door the simulation's
		 * trace did not stop at. Such a reading is of whatever is in the way and the report leaves the spot out. The
		 * body's is the sphere's (it stands in the same place).
		 */
		bool bSeen = true;
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
		/** The sphere's picture by the simulation's term, of 255 (PredictSphere), when the spot was first measured. */
		float Predicted = -1.0f;
		/** A spot of the switch test: the index of its light in Switches (-1 an ordinary spot), and whether it is the one beyond the radius. */
		int32 Switch = -1;
		bool bFar = false;
		/** The switch test's two later states: every switched light fired once, and fired again. */
		FMeasure Switched;
		FMeasure Back;
		FScctLightReading ReadingSwitched = {};
		FScctLightReading ReadingBack = {};
		float PredictedSwitched = -1.0f;
		float PredictedBack = -1.0f;
		/** Where the stood body is when it is measured: points inside its trunk (spine bones), and every bone. */
		TArray<FVector> Trunk;
		TArray<FVector> Bones;
	};

	bool Begin();
	void AimAt(int32 Index);

	/** Places the camera for a spot (the side with the most room) and takes the simulation's reading and prediction there. */
	void PrepareSpot(FSpot& Entry);
	float Predict(const FSpot& At) const;
	FMeasure& MeasureOf(FSpot& At);
	const TCHAR* PassName() const;

	/** Starts the pass, or the one after it when it has nothing to measure; the tone ramp and the end after the last. */
	void StartPass(EPass Next);
	void NextPass();

	// The switch test and the tone ramp (ScctLightParityChecks.cpp).
	void AddSwitchSpots();
	bool FireSwitches();
	void WriteSwitches() const;
	bool BeginTone();
	void MeasureTone(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	void ReadCapture(FMeasure& Measure, const FSpot& At) const;
	void FindBody(FSpot& At) const;
	bool IsReferenceSeen() const;
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
	EPass Pass = EPass::Sphere;
	/** The spots of the pass in the order measured, and where the pass is in it. */
	TArray<int32> Order;
	int32 OrderAt = 0;
	bool bBodyPass = false;
	bool bMeasureBody = true;
	bool bAllFloor = false;
	bool bSwitchTest = true;
	bool bTone = true;
	bool bFired = false;
	float ToneMax = 510.0f;
	float ToneExposure = 0.0f;
	int32 OrdinarySpots = 0;
	FScctLightFloor Floor = {};
	TArray<FScctLightSwitch> Switches;

	struct FTonePatch
	{
		float Code = 0.0f;
		float Emissive = 0.0f;
		FVector Centre = FVector::ZeroVector;
	};
	TArray<FTonePatch> TonePatches;
	FTransform ToneCamera;
	bool bSaveCapture = false;
	bool bCaptured = false;
	double PhaseAt = 0.0;
	double ReadyAt = -1.0;
	double SettleSeconds = 2.5;
	int32 PerKind = 16;
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

	/** The tone ramp's board: its patches and their materials. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ToneBoard;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> ToneMaterials;
};
