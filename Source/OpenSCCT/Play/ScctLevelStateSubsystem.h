#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctWorldApi.h"
#include "Runtime/ScctWorldEmitterApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctLevelStateSubsystem.generated.h"

class ASkeletalMeshActor;
class ULightComponent;
class UNiagaraComponent;
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
 *   - What the importer's map effects hang on a light goes with it. Its picture and shape (the light function
 *     with the cookie) and its shaft in the fog (the light's own volumetric scattering) are part of the light
 *     component, so they dim, go out and move with it by themselves. Its corona (the importer's card tagged
 *     "ScctCoronaOf:" + the light's name, also for lights the level has no light for) and the beam and flare
 *     cards beside a switched light (glow cards) are dimmed, hidden and moved here.
 *   - One owner for a light's rhythm. The importer gives pulsing, blinking and flickering lights a light function
 *     that animates them with no game code (tag "ScctLightType:<n>"), which is what a level opened without a
 *     match shows. In a match the simulation computes the same rhythm, in step for every player and for the
 *     corona; so while a match plays that light's function is held steady (a copy with LightType 1) and the
 *     simulation's value is applied, never both.
 *   - The map's own particle emitters (the importer's Niagara actors, tagged like everything else) follow the
 *     simulation's triggers: one that waits for its event starts on it, a running one stops, a burst fires again.
 *     The wet ground under a rain volume (the decal tagged "ScctWetOf:" + the volume's name) is hidden while the
 *     rain of that name is off.
 *   - Skeletal map props (the importer's SkeletalMeshActors) are shown and hidden with the simulation, follow its
 *     place (the mesh's own rotation origin, which the importer put in, is kept on top) and play the sequence it
 *     names at the frame it gives (a fish loops its sequence at its own pace). A light the map hangs on a prop's
 *     bone follows that bone, keeping where it was imported against it.
 *   - Destructibles swap to the mesh and the skins the simulation names; a mesh that was not imported is logged
 *     once and the old one stays.
 *   - A level mesh that was imported as a Nanite mesh but wears glass or a glowing material (Nanite cannot draw
 *     those and shows the engine's checkerboard instead) is switched to ordinary drawing when the match starts.
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
 *                              breakable props nearest the camera, "emitters" fires every map Emitter's tag
 *                              (the waiting ones start, the running ones stop), "shootmovers" puts bullet marks on
 *                              the two movers nearest the camera and "shootprops" on the three breakable props
 *                              nearest it (to see marks follow a door and go with a pane), "camnetoff" puts every
 *                              network camera out as a spy's shocker does (a spy must be playing), "modem" places a
 *                              PS2 sabotage modem (at the map's PS2 target, else on the wall ahead of the camera
 *                              with a stand-in mesh), "modemhit" shoots every modem (its smoke and flash) and
 *                              "breakglass" breaks every pane drawn now with a hit off its middle from the
 *                              camera's side (the log names a camera on each pane for -ScctCams) and "rain"
 *                              stands every pawn in the map's biggest rain volume (rain splashing on bodies).
 *                              Example: -ScctLevelDo="movers@8;breaklights@14"
 *   -ScctMarkTest              the same as -ScctLevelDo="shootmovers@4;shootprops@4.5;movers@9;props@15"
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
		bool bVisibilityOnly = false; // a skeletal prop: shown and hidden, never placed
		bool bSpawnTried = false;   // a stand-in was looked for (it is looked for once)
		bool bSkinned = false;      // a material was replaced on it
		bool bMobilityChanged = false;
	};

	/** A corona card of a light, as the importer placed it. */
	struct FCorona
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStaticMeshComponent> Card;
		FVector OriginalLocation = FVector::ZeroVector;
		FLinearColor OriginalColor = FLinearColor::White;
		bool bOriginalHidden = false;
	};

	/** A lamp mesh beside a switched light: one of its glowing materials. */
	struct FFixture
	{
		/** The scalar that carries the glow: "Emissive" on a lamp, "Brightness" on a beam or flare card. */
		FName Parameter;
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		/** What the slot's override was before the lamp took it (none on a level actor as imported). */
		TWeakObjectPtr<UMaterialInterface> OriginalOverride;
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
		TArray<FCorona> Coronas;

		/** The animating light function the importer gave it, while a steady copy stands in for the match. */
		TWeakObjectPtr<UMaterialInterface> OriginalLightFunction;
		bool bLightFunctionHeld = false;

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
		float AppliedCorona = -1.0f;
		bool bCoronaShown = true;
		float MinScale = 0.0f;
		float MaxScale = 0.0f;
		int32 Switches = 0;
		bool bSeen = false;
		bool bOn = false;
		bool bBreaking = false;     // in its break flicker last frame
		bool bShotOut = false;      // went dark out of a break flicker: no fade
		bool bMoved = false;

		/** It hangs on a skeletal prop's bone: where it was imported against that bone when first seen. */
		FTransform BoneOffset;
		bool bOnBone = false;
	};

	/** One skeletal map prop the table lists (GetAnimated), by its name id. */
	struct FPropState
	{
		TWeakObjectPtr<ASkeletalMeshActor> Actor;
		bool bBound = false;

		// As imported: the turn of the mesh's own rotation origin (the importer's rotation less the table's first
		// one), its scale against the table's first scale, and what it played.
		FQuat OriginQuat = FQuat::Identity;
		FVector ScaleRatio = FVector::OneVector;
		FVector FirstLocation = FVector::ZeroVector;
		FQuat FirstQuat = FQuat::Identity;
		/** Where the importer put the actor against the table's first place, in the prop's own unscaled frame. */
		FVector LocalOffset = FVector::ZeroVector;
		TWeakObjectPtr<class UAnimationAsset> OriginalAnimation;
		bool bOriginalPlaying = false;
		bool bOriginalLooping = false;

		// What is applied now.
		int32 SequenceId = -1;
		TWeakObjectPtr<class UAnimSequenceBase> Sequence;
		/** The table's frame last set (negative: none), so a prop that stands on one frame is not set again. */
		float AppliedFrame = -1.0f;
		bool bMoved = false;
		bool bDriven = false;
	};

	/** One sub-emitter of a map Emitter: the importer's Niagara actor. */
	struct FLevelEmitter
	{
		TWeakObjectPtr<UNiagaraComponent> Component;
		bool bBurst = false;          // fires afresh on every trigger instead of toggling (tag "ScctEmitterBurst")
		bool bOriginalActive = false; // as imported: running from the start, or waiting for its event
		bool bOn = false;
	};

	/** A map Emitter that rides a mover or hangs on a bone (FScctWorldEmitter), by its name id. */
	struct FRidingEmitter
	{
		/** The importer's particle actors of that Emitter, where each was imported, and that place in the Emitter's placed frame. */
		struct FPart
		{
			TWeakObjectPtr<AActor> Actor;
			FTransform Original;
			FTransform InEmitter;
		};
		TArray<FPart> Parts;
		/** On a bone: the bone's turn when first seen, so the Emitter keeps its placed turn against the bone's axes. */
		FQuat FirstBoneQuat = FQuat::Identity;
		FVector FirstLocation = FVector::ZeroVector;
		FVector LastLogged = FVector::ZeroVector;
		float LastLogSeconds = -100.0f;
		float MaxTravel = 0.0f;
		bool bBound = false;
		bool bBoneSeen = false;
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
	/** Moves and plays the skeletal map props as the table has them (place, sequence, frame). */
	void UpdateProps();
	void BindProp(FPropState& State, const FScctWorldAnimated& Record);
	/** The imported sequence of a prop's mesh for the simulation's sequence name, or null. */
	class UAnimSequenceBase* FindPropSequence(const ASkeletalMeshActor& Actor, const FString& Sequence);
	/** Places a light that hangs on a prop's bone where that bone is now; false when the prop or the bone is not there. */
	bool FollowBone(FLightState& State, const FScctWorldLight& Record);
	/** Moves the particle actors of the map Emitters that ride a mover or hang on a bone (Play/ScctLevelStateEmitters.cpp). */
	void UpdateAttachedEmitters();
	/** Puts those particle actors back where they were imported. */
	void RestoreAttachedEmitters(bool bTearingDown);
	/** The summary line of the riding emitters. */
	void LogAttachedEmitters();
	void DrainEvents();
	void RunDebugActions();
	/** -ScctLevelDo "breakglass": every pane drawn now is broken on the local server, hit off its middle from the camera's side; returns 1 when any was queued. */
	int32 BreakEveryPane(const FVector& Eye);
	/** A pane broke: its glints, and its drawn mesh cut into shards (ScctGlassShards). */
	void BreakPaneShards(const FScctWorldEvent& Event);

	void FirstSight(FActorState& State, const FScctWorldActor& Record);
	/** Takes a carried actor off its pawn's bone and back into the world. */
	void Detach(FActorState& State);
	/** Spawns an actor for a followed actor the level has none for; false when no imported mesh fits. */
	bool SpawnStandIn(FActorState& State, const FScctWorldActor& Record);
	void ApplyMesh(FActorState& State, int32 MeshId);
	void ApplySkin(FActorState& State, int32 Slot, int32 SkinId);
	/** Emitter.Trigger on the importer's Niagara actors of that map Emitter; returns how many it reached. */
	int32 TriggerEmitters(const FString& Name);
	void BindLight(FLightState& State, const FScctWorldLight& Record);
	void BindFixtures(FLightState& State);
	/** The glowing material a switched light put in this slot of a lamp mesh, or null. */
	UMaterialInstanceDynamic* FindFixtureMaterial(const UStaticMeshComponent* Component, int32 Slot) const;

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

	/**
	 * The skeletal props' sequences loaded so far and what each prop played as imported, held so the garbage
	 * collector keeps them: PropSequences and FPropState only point at them weakly, and a sequence nothing else
	 * held was gone by the time the simulation asked for it (a prop that changes sequence, or the match's end
	 * putting back what the importer gave it).
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> PropAssets;

	const FScctWorldApi* World = nullptr;
	const FScctWorldDebugApi* Debug = nullptr;

	/** By name id. The strings never move when the table grows, so a name handed out stays good all match. */
	TIndirectArray<FString> Names;
	/** The level's tagged mesh actors and, apart from them, its tagged lights, by tag. Actors spawned here are added to the first. */
	TMap<FName, TWeakObjectPtr<AActor>> TaggedActors;
	TMap<FName, TWeakObjectPtr<AActor>> TaggedLights;
	/** The importer's corona cards by the name of their light. */
	TMultiMap<FName, TWeakObjectPtr<AActor>> TaggedCoronas;
	/** The importer's particle emitters by the tag of their map Emitter, and the wet-ground decals by their rain volume's. */
	TMultiMap<FName, FLevelEmitter> TaggedEmitters;
	TMultiMap<FName, TWeakObjectPtr<AActor>> TaggedWetGround;

	/** The level's tagged mesh actors that can move (the importer makes movers and gameplay meshes movable). */
	TSet<FName> MovableTags;
	TSet<FName> MentionedTags;

	TMap<FString, FSoftObjectPath> MeshAssets;
	TMap<FString, FSoftObjectPath> MaterialAssets;

	TArray<FActorState> ActorStates;
	TArray<FLightState> LightStates;
	TArray<FScctWorldActor> ActorBuffer;
	TArray<FScctWorldLight> LightBuffer;
	TMap<int32, FPropState> PropStates;
	TArray<FScctWorldAnimated> PropBuffer;
	/** The imported sequences by the folder of a prop's mesh, loaded once. */
	TMap<FString, TArray<TWeakObjectPtr<class UAnimSequenceBase>>> PropSequences;
	TArray<FScctWorldEvent> EventBuffer;
	TArray<FDebugAction> DebugActions;
	/** The bridge's attached-emitter table (Runtime/ScctWorldEmitterApi.h), the riding emitters by name id, and the read buffer. */
	const FScctWorldEmitterApi* EmitterApi = nullptr;
	bool bEmitterApiTried = false;
	TMap<int32, FRidingEmitter> RidingEmitters;
	TArray<FScctWorldEmitter> EmitterBuffer;

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
	int32 NumCoronasBound = 0;
	int32 NumRhythmsHeld = 0;
	int32 NumGlowCards = 0;
	int32 NumEffectActors = 0;
	int32 NumMobilityChanged = 0;
	int32 NumEvents[10] = {};
	int32 NumEmittersReached = 0;
	int32 NumPropsBound = 0;
	int32 NumPropsPlayed = 0;
	int32 NumBoneLights = 0;
	TSet<FString> PropsReported;
	float PlacementError = 0.0f;
	float PlacementTurnError = 0.0f;
	int32 PlacementChecked = 0;
};
