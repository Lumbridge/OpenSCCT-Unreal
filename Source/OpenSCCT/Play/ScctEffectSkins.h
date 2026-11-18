#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctEffectSkinApi.h"
#include "UObject/GCObject.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTexture2D;
struct FScctEventsApi;

/**
 * The run-time skins of the effect meshes, as the Godot client's EffectViews.ApplySkins draws them: a slot of a thrown
 * or placed gadget's mesh whose skin is an additive or translucent material of the effects' texture table draws that
 * picture added and unlit, cell by cell when it is a flipbook; every other slot keeps the mesh's own material. The
 * proximity mine's status light is the one case in the game: a dim grey dot until the mine is armed and while it is
 * chaffed (proxy_mine_Off_final), a red dot blinking twice a second while it is live (proxy_mine_On).
 *
 * The pictures are original content, read from the content folder (shared/fx/textures) as the match needs them and
 * drawn by M_ScctFxSkin (Tools/Effects/build_effect_assets.py); neither is ever in the project's content.
 */
class FScctEffectSkins : public FGCObject
{
public:
	/** What a mesh wears now, slot by slot: the bridge's skin name ids, 0 for the mesh's own material. */
	struct FWorn
	{
		int32 SkinIds[ScctEffectSkinSlots] = {};
	};

	/** Once a frame for each drawn effect mesh: puts on the skins that changed and turns the flipbooks' cells. */
	void Apply(int32 ActorId, UStaticMeshComponent* Component, FWorn& Worn, const FScctEventsApi* Events, bool bLog);

	/**
	 * The check of -ScctMineLight, once a frame of a local match: 2 s in, two proximity mines hang side by side in front
	 * of the view (their lights are dim for the five seconds they take to arm, then blink red); 10 s in, the first is
	 * chaffed for 30 s and goes dim again while the other blinks on. Does nothing without the switch.
	 */
	void RunCheck(float MatchSeconds, const FVector& Eye, const FVector& Forward);

	/** For a mesh component handed to another effect actor: back to the mesh's own materials. */
	static void Clear(UStaticMeshComponent* Component);

	/** How many times a slot was given a skin since the match began (the summary's number). */
	int32 NumApplied = 0;

	/** A new match: the check starts over. */
	void ResetMatch() { NumApplied = 0; CheckStep = CheckStep > 0 ? 1 : CheckStep; }

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FScctEffectSkins"); }

private:
	struct FSkin
	{
		TObjectPtr<UMaterialInstanceDynamic> Material;
		TObjectPtr<UTexture2D> Picture;
		int32 Columns = 1;
		int32 Rows = 1;
		int32 Frame = -1;
	};

	/** The skin's material, made on first use; null when its picture or M_ScctFxSkin is missing (said once in the log). */
	FSkin* FindSkin(int32 SkinId, const FScctEventsApi* Events);

	const FScctEffectSkinApi* FindApi();

	const FScctEffectSkinApi* Api = nullptr;
	bool bApiTried = false;
	int32 CheckStep = -1; // -1: the switch was not looked for yet; 0: off; 1: mines to place; 2: mine to chaff; 3: done
	TObjectPtr<UMaterialInterface> Base;
	bool bBaseTried = false;
	TMap<int32, FSkin> Skins;
};
