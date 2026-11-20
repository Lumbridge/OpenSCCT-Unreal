#pragma once

#include "CoreMinimal.h"
#include "Play/ScctEffectSkins.h"
#include "Play/ScctEffectsParticles.h"
#include "Play/ScctEffectsSmokeVolume.h"
#include "Runtime/ScctEventsApi.h"
#include "Runtime/ScctFxTraceApi.h"
#include "Runtime/ScctProjectionsApi.h"
#include "Runtime/ScctWorldApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctEffectsSubsystem.generated.h"

class AScctPawnProxy;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UScctSessionSubsystem;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

/**
 * Draws the match's transient effects from the bridge's events feature (Runtime/ScctEventsApi.h), each frame on
 * the session subsystem's OnFrame:
 *
 *   - shots: a muzzle flash with its light where the shooter's barrel ends (asked of the equipment subsystem,
 *     which draws the guns), and tracers as fast thin streaks. The local player's own first-person flash is the
 *     equipment's while it draws a view model (UScctEquipmentSubsystem::DrawsLocalMuzzleFlash); only when there
 *     is no view model is it drawn here, at the simulation's muzzle estimate. A shot that throws a casing throws
 *     it from the gun's ejection port, and it bounces, lies and goes (Play/ScctEffectsCasings.cpp);
 *   - impact marks: a decal per live mark and a burst of sparks, dust, splinters or spray by the surface. A mark
 *     on a map actor (a door, a lift, a pane of glass) moves with that actor and is hidden while it is hidden or
 *     gone; a mark on the level's own geometry stays where it landed;
 *   - abandoned projections (footprints, a flare's burn mark, shotgun pellet projectors): a decal of the original
 *     texture each, laid on the surface the bridge found for it, riding that surface like an impact mark and fading
 *     out at the end of its life (Play/ScctEffectsProjections.cpp);
 *   - effect actors: grenades, mines and gadgets as their original meshes (imported by
 *     Tools/Effects/build_effect_assets.py; a small glowing stand-in shape where a mesh is missing), explosions,
 *     smoke, sparks, flares and trails as particles, and the lights the simulation gives them. The PS2 modem's
 *     destruction preset (Ps2ModemEffect) is drawn as its own three emitters (Play/ScctEffectsDevices.cpp);
 *   - the spy's smoke grenade cloud (SSmokeEmitter) as the original emitter makes it: fifteen sprites of the game's
 *     own picture let out at once, standing for ten seconds and fading over the last two (EmitGrenadeCloud; the
 *     picture is read from the content folder and drawn by M_ScctFxCloud, lit by the room). Thermal vision keeps a
 *     fifth of it, EMF vision none;
 *   - responsive smoke: on a match that runs it, every cloud of the shared smoke field is a ray-marched volume over
 *     the field's bounds, and a smoke grenade's own sprite cloud is not drawn (Play/ScctEffectsSmokeVolume.h);
 *   - device faults: a network camera a spy's shocker has put out shows an orange cross on its lens with sparks and
 *     smoke for as long as it is out, read from the level state's markers (Play/ScctEffectsDevices.cpp).
 *
 * Particles are the subsystem's own pooled sprites (see FScctFxSprites for why not Niagara); lights, meshes and
 * decals come from pools made once per world. Nothing is allocated per frame.
 *
 * Unattended switches (off by default):
 *   -ScctAutoFire          the local player fires in half-second bursts from 3 s into the match and uses the
 *                          alternate fire every 6 s (a merc's grenade), so shots can be seen with nobody playing
 *   -ScctFxTimeScale=K     effects run at K times real speed (0.1: a flash lasts a second), so a screenshot
 *                          taken at any moment shows them
 *   -ScctFxLog             log every flash, tracer, impact, casing and effect actor as it starts or ends
 *   -ScctMineLight         two proximity mines hang in front of the view 2 s into a local match and the first is
 *                          chaffed at 10 s, so the status light can be seen in each state (Play/ScctEffectSkins.h)
 *   -ScctKnockCheck        not an effect, only run from this subsystem's frame: the local player is knocked down
 *                          3 s into a match and the log says where the view is against the head bone
 *                          (Runtime/ScctCameraBoneApi.h)
 * A camera fault ('camnet fault <name>') and a modem's destruction ('Ps2ModemEffect start') are always logged.
 */
UCLASS()
class OPENSCCT_API UScctEffectsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * For other features (the level state): a lamp or a device giving up, as a short burst of Count electric sparks
	 * thrown along Normal with a brief cold flash of light. Does nothing before the first match's effects are ready.
	 */
	void SpawnSparkBurst(const FVector& At, const FVector& Normal, int32 Count);

	/**
	 * For other features: a pane of glass going at At, its glints and dust thrown along Direction (zero: it just
	 * drops). Does nothing before the first match's effects are ready.
	 */
	void SpawnGlassBurst(const FVector& At, const FVector& Direction);

	/**
	 * For the goggles: the mesh drawing an effect actor (a mine, a sticky cam, a grenade) by its simulation ActorId,
	 * or null when it draws none or is a beam. Read only; the goggles mark it for thermal and EMF and give it back as
	 * it was.
	 */
	UStaticMeshComponent* FindActorMesh(int32 ActorId) const;

	/** What this match has drawn so far, for the per-feature checks (Test/Features); read only. */
	struct FStats
	{
		int32 MuzzleFlashes = 0;          // drawn here
		int32 FlashesLeftToViewModel = 0; // the local player's own, drawn on the view model instead
		int32 Tracers = 0;
		int32 Impacts = 0;                // impact marks that arrived (each with its burst)
		int32 DecalsShown = 0;            // impact decals drawn now
		int32 EffectActors = 0;           // effect actors started
		int32 EffectActorsByKind[8] = {}; // by EScctEffectKind (1 emitter, 2 projectile, 3 mine, 4 gadget, 5 beam, 6 decal, 7 light)
		int32 Explosions = 0;             // explosion bursts (frag grenades, mines)
		int32 LiveEffectActors = 0;
	};
	FStats GetStats() const;

	/** How many effect actors of each simulation class ("SProjSmoke", "SSmokeEmitter") this match started. */
	const TMap<FString, int32>& GetStartedByClass() const { return StartedByClass; }

	/**
	 * For the warm-up behind the loading page (Play/ScctEffectsWarmup.h): every look this subsystem draws, once, at
	 * At in front of the camera looking along Forward: each particle style (explosion, smoke, gas, wisp, sparks,
	 * electric arcs, chaff, flare, flash bang, water, dust, muzzle flashes, impacts by surface, the glass burst, the
	 * modem's preset), timed lights with and without shadows, a bullet-mark decal and a projection decal on Backdrop,
	 * a casing, a tracer streak, and every imported effect mesh in a small grid. Returns the number of meshes shown,
	 * INDEX_NONE while the effects are not ready yet. Play/ScctEffectsWarmup.cpp.
	 */
	int32 SpawnWarmupSamples(const FVector& At, const FVector& Forward, const FVector& Backdrop);

	/** Takes every warm-up sample away again: its components, its timed lights and every live particle. */
	void ClearWarmupSamples();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** How an emitter class looks; chosen from its class name. */
	enum class EStyle : uint8
	{
		None,       // nothing drawn (an editor helper, or something another feature draws)
		Explosion,
		Smoke,      // a thick lasting cloud
		Wisp,       // thin smoke from a launch trail
		BarrelSmoke,
		Sparks,
		Electric,
		Chaff,
		Flare,
		FlashBang,
		Water,
		Gas,
		StickyGas,
		Puff,       // any other emitter: one small puff of dust
		Ps2Modem,   // the PS2 modem's destruction preset: two smokes and a flash, once (Play/ScctEffectsDevices.cpp)
	};

	struct FFlareVisual
	{
		TWeakObjectPtr<UStaticMeshComponent> Flame;
		TWeakObjectPtr<UStaticMeshComponent> Cartridge;
		FVector ContactOffset = FVector::ZeroVector;
		FVector ContactNormal = FVector::ZeroVector;
		FVector FlightDirection = FVector::ForwardVector;
		bool bFlying = false;
	};

	struct FActorFx
	{
		int32 ClassId = 0;
		int32 Kind = 0;
		EStyle Style = EStyle::None;
		EStyle TrailStyle = EStyle::None;
		FVector Location = FVector::ZeroVector;
		float EmitDebt = 0.0f;   // particles owed by a continuous emitter
		float Age = 0.0f;
		int32 Mesh = INDEX_NONE; // index into MeshPool
		int32 Light = INDEX_NONE; // index into Lights
		uint32 SeenFrame = 0;
		bool bStandIn = false;
		TUniquePtr<FFlareVisual> Flare;
		FScctEffectSkins::FWorn Skins; // the run-time skins its mesh wears (the proximity mine's status light)

		// One-shot emitters keep track of how many sprites they have already released.
		bool bGrenadeCloud = false;
		int32 EmittedSprites = 0;

		// Rain on a pawn (SFlicFlocEmitter, Play/ScctEffectsFlicFloc.cpp): the script's restarts and puff triggers
		// already applied, each sub-emitter's spawns owed and the spawn budget left since its restart.
		bool bFlicFloc = false;
		int32 FlicSplashStarts = 0;
		int32 FlicDripStarts = 0;
		int32 FlicPuffTriggers = 0;
		int32 FlicSplashBudget = 0;
		int32 FlicDripBudget = 0;
		float FlicSplashDebt = 0.0f;
		float FlicDripDebt = 0.0f;
		bool bFlicSeen = false;
	};

	struct FLight
	{
		float Life = 0.0f;      // a timed flash: seconds it lasts in all; 0 for a light an effect actor holds
		float Age = 0.0f;
		float Candelas = 0.0f;
		bool bUsed = false;
	};

	struct FTracer
	{
		FVector Start = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		float Distance = 0.0f;  // how far it flies before it is hidden
		float Travelled = 0.0f;
		float Speed = 0.0f;
		bool bLive = false;
	};

	void HandleSessionChanged(int32 OldId, int32 NewId);
	void HandleMatchStarted();
	void HandleBeforeTicks(float DeltaSeconds);
	void HandleFrame(float DeltaSeconds);

	bool EnsureScene();
	void ResetMatch();

	void DrainShots();
	void UpdateTracers(float DeltaSeconds);
	void UpdateImpactMarks();

	/**
	 * A mark on a map actor (a door, a lift, a pane): its decal is kept in that actor's frame, as the Godot client's
	 * CombatFxView.AttachDecal does, and drawn only while the actor is. Play/ScctEffectsMarks.cpp.
	 */
	struct FMarkSurface
	{
		bool bPlaced = false;
		/** The level's actor for the surface; unset when the frame is the simulation's own pose of it. */
		TWeakObjectPtr<AActor> Actor;
		/** The decal's place in the surface's frame (location and turn, no scale). */
		FTransform Local;
		/** The simulation's id of the surface; 0 for the level's own geometry. */
		int32 ActorId = 0;
		bool bAttached = false;
		/** No level actor stands for the surface: its frame is the simulation's pose of it. */
		bool bSimulationFrame = false;
		/** Hidden with its surface last frame (for the log). */
		bool bHiddenWithSurface = false;
	};

	/** Ties a slot's freshly placed decal to the actor its mark hit (nothing for the level's geometry). */
	void AttachMark(int32 Slot, const FScctImpactMark& Mark, UDecalComponent& Decal);
	/** Carries a tied decal with its actor; false while the decal must not be drawn (its surface is hidden or gone). */
	bool FollowMarkSurface(int32 Slot, UDecalComponent& Decal);
	/** The two above for any decal; What and Index name it in the log. */
	void AttachToSurface(FMarkSurface& Surface, int32 SurfaceActorId, UDecalComponent& Decal, const TCHAR* What, int32 Index);
	bool FollowSurface(FMarkSurface& Surface, UDecalComponent& Decal, const TCHAR* What, int32 Index);

	/**
	 * The abandoned projections of the client's world (Runtime/ScctProjectionsApi.h), each a pooled decal of its
	 * texture on the surface the bridge placed it on. Play/ScctEffectsProjections.cpp.
	 */
	struct FProjectionSlot
	{
		int32 Serial = 0;
		uint32 SeenFrame = 0;
		float Opacity = -1.0f;
		FMarkSurface Surface;
	};

	void UpdateProjections();
	void ResetProjections();
	/** The imported texture of a projection by the bridge's name id of its asset name; null when it is not imported. */
	UTexture2D* FindProjectionTexture(int32 AssetId);

	/**
	 * A spent shell casing thrown from a gun's ejection port (SBase.SDouilleEmitter): one pooled mesh flying with the
	 * emitter's velocity and spin under its gravity, bouncing off what a line trace meets, lying where it settles and
	 * shrinking away at the end of its life. Play/ScctEffectsCasings.cpp.
	 */
	struct FCasing
	{
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		/** The port's turn when it was thrown; the spin is added to it. */
		FRotator Base = FRotator::ZeroRotator;
		/** Degrees a second about the rotator's pitch, yaw and roll; stopped by the first bounce. */
		FRotator Spin = FRotator::ZeroRotator;
		/** How far it has turned so far. */
		FRotator Turned = FRotator::ZeroRotator;
		/** Its turn once it lies still. */
		FQuat Rest = FQuat::Identity;
		float Age = 0.0f;
		float StartZ = 0.0f;
		int32 Bounces = 0;
		bool bResting = false;
		bool bLive = false;
	};

	/** Throws a casing for a shot that has one, from the shooter's ejection port (else the drawn muzzle). */
	void SpawnCasing(const FScctShot& Shot, const FVector& Direction);
	void UpdateCasings(float DeltaSeconds);
	void ResetCasings();
	/** The casing mesh: the game's own imported, else a small brass cylinder (said once in the log). */
	bool EnsureCasingMesh();

	void UpdateEffectActors(float DeltaSeconds);
	/**
	 * Rain splashing on a pawn, as the Godot client's EffectViews.FlicFloc draws it: splashes spread over the drawn
	 * body along the original's 11 bone segments while in the rain, drips from the head carrying the pawn's speed
	 * after it, the puff on each trigger, following the script's rates, restarts and triggers. Play/ScctEffectsFlicFloc.cpp.
	 * Returns false when the bridge has no table for it (the plain Water style is drawn then).
	 */
	bool UpdateFlicFloc(FActorFx& Fx, const FScctEffectActor& Record, const FVector& Head, bool bHidden, float DeltaSeconds);
	void UpdateLights(float DeltaSeconds);

	/**
	 * A network camera a spy's shocker has put out, drawn as the Godot client's CamnetDisabledView draws it: an
	 * orange cross on the lens that throbs, a few sparks every 1.6 s and a thin wisp of smoke, while the camera is
	 * out. Play/ScctEffectsDevices.cpp.
	 */
	struct FCamnetFault
	{
		TWeakObjectPtr<UStaticMeshComponent> Bars[2];
		TWeakObjectPtr<UMaterialInstanceDynamic> Look;
		/** The level's actor for the camera, looked for when the fault starts; unset when the level has none. */
		TWeakObjectPtr<AActor> Camera;
		FString Name;
		float Age = 0.0f;
		float NextSpark = 0.0f;
		float SmokeDebt = 0.0f;
		uint32 SeenFrame = 0;
		bool bOut = false;
	};

	/** The marked devices' own effects: the shocked network cameras' faults. Play/ScctEffectsDevices.cpp. */
	void UpdateDevices(float DeltaSeconds);
	void ResetDevices();
	/** Ps2ModemEffect's three sprite emitters, as the original preset sets them, from At turned by Rotation. */
	void SpawnPs2ModemEffect(const FVector& At, const FRotator& Rotation);

	void StartActor(FActorFx& Fx, const FScctEffectActor& Record);
	void EndActor(FActorFx& Fx, bool bKilled);
	void PlaceActorMesh(FActorFx& Fx, const FScctEffectActor& Record);
	void Emit(EStyle Style, const FVector& At, const FVector& Velocity, float DeltaSeconds, float& Debt);
	/** Lets out what a smoke grenade's cloud owes by its age: the original emitter's fifteen sprites, once. */
	void EmitGrenadeCloud(FActorFx& Fx);
	void EmitBarrelSmoke(FActorFx& Fx, float EmissionAge);
	void EmitStickyGas(FActorFx& Fx, const FQuat& Facing);
	FVector UpdateFlare(FActorFx& Fx, const FScctEffectActor& Record, const FTransform& Place,
		const FVector& PreviousLocation, float DeltaSeconds, bool bHidden, bool bRestoring, bool bHistorical);
	bool FindFlareContact(const FVector& Origin, FVector& Contact, FVector& Normal);

	// The looks.
	void SpawnMuzzleFlash(const FVector& At, const FVector& Direction, float Scale, bool bSticky, bool bSuppressed, bool bShotgun);
	void SpawnImpact(const FVector& At, const FVector& Normal, const FString& SparkClass, const FString& TypeClass);
	/** Colours an impact slot's decal for the surface that was hit (the impact's class names say which). */
	/** Colours a bullet mark for the surface that was hit; returns half its width in cm. */
	float TintDecal(UMaterialInstanceDynamic* Tint, const FString& TypeClass, const FString& SparkClass);
	void SpawnExplosion(const FVector& At);
	void SpawnFlashBang(const FVector& At);
	void SpawnSparks(const FVector& At, const FVector& Normal, int32 Count, const FLinearColor& Color, float Speed);
	void SpawnDust(const FVector& At, const FVector& Normal, int32 Count, const FLinearColor& Color, float Size);

	/** A light for Life seconds (0: until ReleaseLight). INDEX_NONE when every light of the pool is in use. */
	int32 AcquireLight(const FVector& At, const FLinearColor& Color, float Candelas, float Radius, float Life, bool bShadows);
	void ReleaseLight(int32 Index);

	/** Where a shooter's drawn gun ends: its hand on the drawn body, pushed along the aim. */
	FVector FindMuzzle(const FScctShot& Shot) const;

	const FString& GetName(int32 Id);

	/**
	 * The drawn body's own name for the bone the bridge names BoneId, NAME_None when the body has none. Looked up
	 * once per bone and pawn kind: effects on a bone ask every frame.
	 */
	FName FindDrawnBone(const AScctPawnProxy& Proxy, int32 BoneId);
	EStyle FindStyle(int32 ClassId);
	UStaticMesh* FindMesh(int32 MeshId);
	FVector RandomCone(const FVector& Axis, float HalfAngleDegrees);

	UPROPERTY(Transient)
	TObjectPtr<UScctSessionSubsystem> Session;

	/** The actor every pooled component hangs on. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> Host;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> LightComponents;

	/**
	 * Two soft lights on the bodies of the local player's own side, and on nothing else (their own lighting
	 * channel). A spy's suit is nearly black: seen from behind with the room's light in front of him he was a flat
	 * black cut-out in his own picture, and so was the team mate beside him. BodyFill comes from above the camera's
	 * shoulder, so it falls across the body and shades its form; BodyRim stands behind the local body, opposite
	 * the camera, and draws the bright edge that sets a dark figure off against a dark wall. Both follow the
	 * picture's exposure, so the figure reads the same in a cellar and in a lit hall. They never fall on the
	 * other side (a spy in the dark stays in the dark for the merc who looks his way) and they light no wall.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> BodyFill;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> BodyRim;

	UPointLightComponent* MakeBodyLight(float Radius, float Specular);
	void UpdateBodyFill(float DeltaSeconds);

	/** The exposure factor of the body lights, eased so a jump of the exposure is not a jump of the figure. */
	float BodyLightScale = 0.0f;

	/**
	 * The local player's body thins out of the picture close to the camera: a wall behind the spy holds the
	 * camera within reach of his head, and with the gun drawn it looks over his shoulder from a hand's width.
	 * What is nearer the camera than scct.BodyFade centimetres fades, and is gone at half of that; his arm and gun
	 * further out stay. Its shadow stays whole. Every other body is drawn whole at any distance.
	 */
	void UpdateBodyFade();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MeshPool;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> Decals;

	/** The material instance of each impact slot's decal (same index as Decals). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DecalTints;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> DecalMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StandInMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> StandInMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> FlareCard;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FlareMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> FlareCartridge;

	/** The imported effect meshes by the bridge's mesh name id; null for one that was looked for and not found. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UStaticMesh>> Meshes;

	const FScctEventsApi* Events = nullptr;
	FScctFxSprites Sprites;
	/** The cloud layer draws the game's own picture of the smoke grenade's cloud (else plain grey discs). */
	bool bCloudPicture = false;
	FScctSmokeVolumes SmokeVolumes;
	/** The run-time skins of the effect meshes (Play/ScctEffectSkins.h). */
	FScctEffectSkins EffectSkins;
	FRandomStream Random;

	TMap<int32, FActorFx> Actors;
	TArray<FLight> Lights;
	TArray<bool> MeshUsed;
	TArray<FTracer> Tracers;
	TArray<int32> ImpactSerials;
	TArray<uint32> ImpactSeenFrame;
	/** Same index as Decals. */
	TArray<FMarkSurface> MarkSurfaces;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> ProjectionDecals;

	/** The material instance of each projection decal (same index as ProjectionDecals). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ProjectionLooks;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ProjectionMaterial;

	/** By the bridge's name id of the asset name; null for one that was looked for and not found. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UTexture2D>> ProjectionTextures;

	/** The bars of the network cameras' fault crosses, two per fault, kept for the next fault when one ends. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FaultBarPool;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FaultLookPool;

	/** The casings' meshes, one per slot of Casings, made as the pool first fills. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CasingPool;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CasingMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CasingStandInMaterial;

	TArray<FCasing> Casings;

	/** The warm-up's own components (meshes, decals), destroyed by ClearWarmupSamples. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> WarmupComponents;
	/** The client world's collision, which the casings bounce off (the level has no Unreal collision). */
	const FScctFxTraceApi* FxTrace = nullptr;
	/** The rain-on-pawn table (Runtime/ScctFlicFlocApi.h), looked for once. */
	const struct FScctFlicFlocApi* FlicFlocApi = nullptr;
	bool bFlicFlocApiTried = false;
	int32 NumFlicSplashes = 0;
	int32 NumFlicDrips = 0;
	int32 NumFlicPuffs = 0;
	/** The casing mesh's scale, its long axis (0 X, 1 Y, 2 Z) and half its thickness once scaled, in cm. */
	FVector CasingScale = FVector::OneVector;
	int32 CasingLongAxis = 0;
	float CasingRadius = 0.5f;
	bool bCasingMeshLooked = false;
	int32 NextCasing = 0;
	int32 NumCasings = 0;
	int32 NumCasingsFromPort = 0;
	int32 NumCasingBounces = 0;

	const FScctWorldApi* WorldApi = nullptr;
	TArray<FScctWorldMarker> DeviceMarkers;
	TMap<int32, FCamnetFault> CamnetFaults;
	int32 NumCamnetFaults = 0;
	int32 NumModemEffects = 0;

	const FScctProjectionsApi* Projections = nullptr;
	TArray<FProjectionSlot> ProjectionSlots;
	TMap<int32, int32> ProjectionBySerial;
	TArray<FScctProjection> ProjectionBuffer;
	int32 ProjectionCount = 0;
	bool bProjectionsReported = false;
	int32 NumProjections = 0;
	int32 NumProjectionsUndrawn = 0;
	TMap<int32, FString> Names;
	TMap<int32, EStyle> Styles;
	TMap<int64, FName> DrawnBones;
	TMap<FString, FSoftObjectPath> MeshAssets;

	TArray<FScctShot> ShotBuffer;
	TArray<FScctEffectActor> ActorBuffer;
	TArray<FScctImpactMark> MarkBuffer;

	uint32 Frame = 0;

	/** What the log last said about the effects flagged bHideInNormalVision: listed, and shown by the goggles. */
	int32 LoggedVisionOnly = 0;
	int32 LoggedVisionOnlyShown = 0;

	float TimeScale = 1.0f;
	float MatchSeconds = 0.0f;
	bool bSceneReady = false;
	bool bLog = false;
	bool bAutoFire = false;
	bool bFireHeld = false;
	bool bAltFireHeld = false;

	// Counters for the summary line when the match ends.
	int32 NumFlashes = 0;
	int32 NumFlashesLeftToViewModel = 0;
	int32 NumTracers = 0;
	int32 NumImpacts = 0;
	int32 NumActors = 0;
	int32 NumStandIns = 0;
	int32 PeakSprites = 0;
	int32 NumExplosions = 0;
	int32 NumActorsByKind[8] = {};
	TMap<FString, int32> StartedByClass;
};
