#pragma once

#include "CoreMinimal.h"
#include "Play/ScctEffectsParticles.h"
#include "Runtime/ScctEventsApi.h"
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

/**
 * Draws the match's transient effects from the bridge's events feature (Runtime/ScctEventsApi.h), each frame on
 * the session subsystem's OnFrame:
 *
 *   - shots: a muzzle flash with its light where the shooter's barrel ends (asked of the equipment subsystem,
 *     which draws the guns), and tracers as fast thin streaks. The local player's own first-person flash is the
 *     equipment's while it draws a view model (UScctEquipmentSubsystem::DrawsLocalMuzzleFlash); only when there
 *     is no view model is it drawn here, at the simulation's muzzle estimate;
 *   - impact marks: a decal per live mark and a burst of sparks, dust, splinters or spray by the surface;
 *   - effect actors: grenades, mines and gadgets as their original meshes (imported by
 *     Tools/Effects/build_effect_assets.py; a small glowing stand-in shape where a mesh is missing), explosions,
 *     smoke, sparks, flares and trails as particles, and the lights the simulation gives them.
 *
 * Particles are the subsystem's own pooled sprites (see FScctFxSprites for why not Niagara); lights, meshes and
 * decals come from pools made once per world. Nothing is allocated per frame.
 *
 * Unattended switches (off by default):
 *   -ScctAutoFire          the local player fires in half-second bursts from 3 s into the match and uses the
 *                          alternate fire every 6 s (a merc's grenade), so shots can be seen with nobody playing
 *   -ScctFxTimeScale=K     effects run at K times real speed (0.1: a flash lasts a second), so a screenshot
 *                          taken at any moment shows them
 *   -ScctFxLog             log every flash, tracer, impact and effect actor as it starts or ends
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

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** How an emitter class looks; chosen from its class name. */
	enum class EStyle : uint8
	{
		None,       // nothing drawn (an editor helper, or something another feature draws)
		Explosion,
		Smoke,      // a thick lasting cloud
		Wisp,       // thin smoke: a gun barrel, a launch trail
		Sparks,
		Electric,
		Chaff,
		Flare,
		FlashBang,
		Water,
		Gas,
		Puff,       // any other emitter: one small puff of dust
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
	void UpdateEffectActors(float DeltaSeconds);
	void UpdateLights(float DeltaSeconds);

	void StartActor(FActorFx& Fx, const FScctEffectActor& Record);
	void EndActor(FActorFx& Fx, bool bKilled);
	void PlaceActorMesh(FActorFx& Fx, const FScctEffectActor& Record);
	void Emit(EStyle Style, const FVector& At, const FVector& Velocity, float DeltaSeconds, float& Debt);

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
	 * A soft light from the camera on the bodies of the local player's own side, and on nothing else (its own
	 * lighting channel). A spy's suit is nearly black: seen from behind with the room's light in front of him he
	 * was a flat black cut-out in his own picture, and so was the team mate beside him. The light follows the
	 * picture's exposure, so it is the same faint sheen in a cellar and in a lit hall. It never falls on the other
	 * side (a spy in the dark stays in the dark for the merc who looks his way) and it lights no wall.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> BodyFill;

	void UpdateBodyFill();

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

	/** The imported effect meshes by the bridge's mesh name id; null for one that was looked for and not found. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UStaticMesh>> Meshes;

	const FScctEventsApi* Events = nullptr;
	FScctFxSprites Sprites;
	FRandomStream Random;

	TMap<int32, FActorFx> Actors;
	TArray<FLight> Lights;
	TArray<bool> MeshUsed;
	TArray<FTracer> Tracers;
	TArray<int32> ImpactSerials;
	TArray<uint32> ImpactSeenFrame;
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
};
