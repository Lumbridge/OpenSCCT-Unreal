#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctWorldApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctLevelStateSubsystem.generated.h"

class ULightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UScctSessionSubsystem;
class UStaticMesh;
class UStaticMeshComponent;
struct FScctWorldDebugApi;

/**
 * Makes the imported level follow the match: each frame, on the session subsystem's OnFrame, it reads the bridge's
 * world table (Runtime/ScctWorldApi.h) and applies it to the level's own actors.
 *
 *   - Doors, lifts and every other placed mesh the simulation moves, hides or shows are found by the tag the
 *     importer gave them ("Scct:" + the simulation actor's name) and placed where the table says. The table's
 *     place already has the mesh pivot taken off, exactly as the importer placed the actor.
 *   - A followed actor the level has no actor for (the importer leaves out meshes the map hides, and the gameplay
 *     classes whose mesh is not in the map's own file: cameras, tripwire boxes, drop-zone cases, glass panes) is
 *     spawned here when the simulation first shows it, from the map's imported meshes or from the effect meshes
 *     (/Game/Imported/Fx), whichever has it.
 *   - Lights follow their on/off state, brightness and colour. A switched light fades over a fifth of a second
 *     like a lamp, and the lamp mesh beside it glows or goes dark with it. A light that is shot out flickers as
 *     the simulation says, throws sparks and is dark at once.
 *   - Destructibles swap to the mesh and the skins the simulation names; a mesh that was not imported is logged
 *     once and the old one stays.
 *
 * Everything is restored when the match ends, so a second match in the same world starts from the level as it
 * was imported. Names the table sends that the level has nothing for, and movable level actors the table never
 * mentions, are logged once.
 *
 * Unattended switches (off by default):
 *   -ScctLevelLog              log every world event as it arrives (marker and skin changes included)
 *   -ScctLevelDo="what@S;..."  S seconds into the match, make something happen on the local match's server, as
 *                              a player or the map's script would: "movers" fires every mover's tag (doors open),
 *                              "lights" fires every switched light's tag (each toggles), "breaklights" shoots out
 *                              the three lit switched lights nearest the camera, "props" breaks the eight
 *                              breakable props nearest the camera. Example: -ScctLevelDo="movers@8;breaklights@14"
 */
UCLASS()
class OPENSCCT_API UScctLevelStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The name behind one of the world table's name ids this session; empty for 0 or an id it does not have. */
	const FString& GetName(int32 Id);

	/** The mesh actor that stands for a simulation actor's name: the level's own (by the importer's tag) or the one spawned here; or null. */
	AActor* FindLevelActor(const FString& Name) const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** One followed simulation actor, by its name id. */
	struct FActorState
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		FScctWorldActorInfo Info = {};

		// What the level's own actor was like before the match touched it.
		FTransform OriginalTransform;
		TWeakObjectPtr<UStaticMesh> OriginalMesh;
		bool bOriginalHidden = false;

		// What is applied now.
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		int32 MeshId = 0;
		int32 AttachedPawnId = 0;
		bool bShown = true;

		// For the travel report.
		FVector FirstLocation = FVector::ZeroVector;
		FQuat FirstRotation = FQuat::Identity;
		float MaxOffset = 0.0f;
		float MaxTurn = 0.0f;
		int32 Starts = 0;

		bool bSeen = false;         // listed by the table this session
		bool bLevelActor = false;   // Actor is the level's own, not spawned here
		bool bSpawnTried = false;   // a stand-in was looked for (it is looked for once)
		bool bSkinned = false;      // a material was replaced on it
		bool bMobilityChanged = false;
	};

	/** A lamp mesh beside a switched light: one of its glowing materials. */
	struct FFixture
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		TWeakObjectPtr<UMaterialInterface> OriginalMaterial;
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		int32 Slot = 0;
		float Emissive = 0.0f;
	};

	/** One light of the table, by its place in the table's list. */
	struct FLightState
	{
		int32 NameId = 0;
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<ULightComponent> Light;
		TArray<FFixture> Fixtures;

		// The light as it was imported.
		FTransform OriginalTransform;
		FLinearColor OriginalColor = FLinearColor::White;
		float OriginalIntensity = 0.0f;
		bool bOriginalVisible = true;

		// The table's colour when the light was first seen: only a colour that changes from it is applied.
		FLinearColor FirstColor = FLinearColor::White;
		FLinearColor AppliedColor = FLinearColor::White;

		float Shown = 0.0f;         // the brightness scale drawn now (follows the table's, with the fade)
		float AppliedScale = -1.0f;
		float AppliedFixture = -1.0f;
		float MinScale = 0.0f;
		float MaxScale = 0.0f;
		int32 Switches = 0;
		bool bSeen = false;
		bool bOn = false;
		bool bBreaking = false;     // in its break flicker last frame
		bool bShotOut = false;      // went dark out of a break flicker: no fade
		bool bMoved = false;
	};

	struct FDebugAction
	{
		FString What;
		float Seconds = 0.0f;
		bool bDone = false;
	};

	void HandleSessionChanged(int32 OldId, int32 NewId);
	void HandleMatchStarted();
	void HandleFrame(float DeltaSeconds);

	/** Finds the level's tagged actors and the imported meshes and materials of this map. */
	void IndexLevel();

	/** Puts every level actor and light back as it was imported and removes what was spawned here. */
	void RestoreLevel();

	void RefreshNames();
	void UpdateActors();
	void UpdateLights(float DeltaSeconds);
	void DrainEvents();
	void RunDebugActions();

	void FirstSight(FActorState& State, const FScctWorldActor& Record);
	/** Spawns an actor for a followed actor the level has none for; false when no imported mesh fits. */
	bool SpawnStandIn(FActorState& State, const FScctWorldActor& Record);
	void ApplyMesh(FActorState& State, int32 MeshId);
	void ApplySkin(FActorState& State, int32 Slot, int32 SkinId);
	void BindLight(FLightState& State, const FScctWorldLight& Record);
	void BindFixtures(FLightState& State);

	/** The imported mesh for a mesh as the simulation or the map's render file names it, or null. */
	UStaticMesh* FindMesh(const FString& Name);
	UMaterialInterface* FindMaterial(const FString& Name);

	void LogBound();
	void LogSummary();

	UPROPERTY(Transient)
	TObjectPtr<UScctSessionSubsystem> Session;

	/** The stand-ins spawned this session. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Spawned;

	/** Loaded meshes and materials by their key, kept so they are looked up once; null for one that is not there. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UStaticMesh>> LoadedMeshes;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> LoadedMaterials;

	const FScctWorldApi* World = nullptr;
	const FScctWorldDebugApi* Debug = nullptr;

	TArray<FString> Names;
	/** The level's tagged mesh actors and, apart from them, its tagged lights, by tag. Actors spawned here are added to the first. */
	TMap<FName, TWeakObjectPtr<AActor>> TaggedActors;
	TMap<FName, TWeakObjectPtr<AActor>> TaggedLights;

	/** The level's tagged mesh actors that can move (the importer makes movers and gameplay meshes movable). */
	TSet<FName> MovableTags;
	TSet<FName> MentionedTags;

	TMap<FString, FSoftObjectPath> MeshAssets;
	TMap<FString, FSoftObjectPath> MaterialAssets;

	TArray<FActorState> ActorStates;
	TArray<FLightState> LightStates;
	TArray<FScctWorldActor> ActorBuffer;
	TArray<FScctWorldLight> LightBuffer;
	TArray<FScctWorldEvent> EventBuffer;
	TArray<FDebugAction> DebugActions;

	/** Names already reported as missing, so each is logged once. */
	TSet<FString> ReportedMissing;
	TArray<FString> NamesWithoutActor;

	float MatchSeconds = 0.0f;
	bool bIndexed = false;
	bool bBound = false;
	bool bLog = false;

	// Counters for the summary.
	int32 NumLevelActors = 0;
	int32 NumStandIns = 0;
	int32 NumNothingToDraw = 0;
	int32 NumHiddenAtStart = 0;
	int32 NumLightsOffAtStart = 0;
	int32 NumLightsFound = 0;
	int32 NumMobilityChanged = 0;
	int32 NumEvents[9] = {};
	float PlacementError = 0.0f;
	int32 PlacementChecked = 0;
};
