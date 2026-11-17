#pragma once

#include "CoreMinimal.h"

class AActor;
class FScctVisionData;
class UNiagaraComponent;
class UPrimitiveComponent;
class UWorld;

/**
 * What thermal and EMF vision show besides the pawns (part of the goggles, UScctVisionSubsystem): the gadgets, devices
 * and the level's own hot or electric things, each by its own heat and EMF values, as the original draws every actor
 * by its HeatIntensity / HeatOpacity and its EMF glow / EMFOpacity (Godot: EffectViews, GameplayObjects).
 *
 * Where the values come from, the live one winning:
 *   - the bridge's vision rows (FScctVisionData::GetActors): objective computers, ammo crates, cameras, doors, flags,
 *     mines, sticky cams and grenades, emitters, whatever the simulation gives values other than the Actor default.
 *     A placed actor is found by its map name (the importer's "Scct:" tag, or the stand-in the level state spawned),
 *     one spawned in play by its ActorId among the effect actors' meshes (UScctEffectsSubsystem::FindActorMesh);
 *   - the importer's tags on the level's placed meshes (ScctHeat, ScctHeatOpacity, ScctEmf, ScctEmfOpacity and
 *     ScctEmfOn, Tools/Import/scct_fx.py instance_tags), read once per map, for meshes the simulation does not list.
 *
 * Each mesh is marked in the custom depth stencil with the pawns' encoding (UScctVisionSubsystem::PawnStencil: the
 * mode's level in 16 steps and the opacity in 4), so the mode's material draws it as it draws a body. Opacity 0 takes
 * the mesh out of the picture (the original discards it); a see-through one is drawn only into the custom depth so the
 * material lays it over the scene behind. In EMF a mesh is marked while it glows (EMFIntensity while bEMFIsOn) or is
 * see-through. In thermal it is marked when it is see-through or hotter than the room's own heat could show
 * (HeatIntensity 0.2 and up): the room's heat here comes from its brightness (the original's per-surface heat textures
 * were not imported), so marking a cold door at its 0.05 would only cut a black hole in the picture.
 *
 * The map's particle emitters (the importer's Niagara actors) keep only HeatOpacity of their strength in thermal, as
 * the original's particle shader keeps that much of each particle's alpha: their Tint is scaled by it, and at 0 they
 * are not drawn. Everything is given back as it was when the mode goes.
 */
class OPENSCCT_API FScctVisionObjects
{
public:
	/** Marks for Mode (36 thermal, 38 EMF; anything else unmarks everything). Call once a frame. */
	void Update(UWorld* World, int32 Mode, const FScctVisionData& Data);

	/** Gives every mesh and emitter back as it was and forgets the map (the match or the world is gone). */
	void Reset();

private:
	/** A level mesh the importer tagged with vision values. */
	struct FTagged
	{
		TWeakObjectPtr<AActor> Actor;
		float Heat = 0.0f;
		float HeatOpacity = 1.0f;
		float Emf = 0.0f;
		float EmfOpacity = 1.0f;
		bool bEmfOn = false;
	};

	/** How a primitive was drawn before the goggles marked it. */
	struct FOriginal
	{
		bool bMainPass = true;
		bool bDepthPass = true;
		bool bCustomDepth = false;
		int32 Stencil = 0;
	};

	/** How a map emitter was drawn before thermal thinned it. */
	struct FEmitterOriginal
	{
		FLinearColor Tint = FLinearColor::White;
		bool bVisible = true;
		/** The share of Tint set now (negative until first set). */
		float Applied = -1.0f;
	};

	/** What a primitive should be this frame. */
	struct FWanted
	{
		bool bMainPass = true;
		bool bCustomDepth = false;
		int32 Stencil = 0;
	};

	void IndexLevel(UWorld* World);
	void RestoreAll();

	/**
	 * The marking of something with Intensity and Opacity in the mode: false when it is left as it is. Adds its
	 * primitives to Wanted and counts it.
	 */
	bool Want(AActor* Actor, UPrimitiveComponent* Single, bool bThermal, float Intensity, float Opacity);

	TWeakObjectPtr<UWorld> IndexedWorld;
	TArray<FTagged> Tagged;
	/** The importer's particle emitters by their map Emitter's name. */
	TMultiMap<FString, TWeakObjectPtr<UNiagaraComponent>> Emitters;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, FOriginal> Marked;
	TMap<TWeakObjectPtr<UNiagaraComponent>, FEmitterOriginal> Thinned;

	// This frame's work.
	TMap<UPrimitiveComponent*, FWanted> Wanted;
	TSet<const AActor*> LiveActors;
	int32 NumHidden = 0;
	int32 NumSeeThrough = 0;

	/** Rows already listed in the log (where each live object is and how it is marked), by ActorId. */
	TSet<int32> Listed;
	TSet<int32> ReportedMissing;
	int32 LastSummary = -1;
	double LastLog = 0.0;
};
