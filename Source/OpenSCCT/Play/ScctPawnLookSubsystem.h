#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctPawnLookApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctPawnLookSubsystem.generated.h"

class AScctPawnProxy;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UScctSessionSubsystem;
class UTexture2D;

/**
 * How each pawn looks on this machine (Runtime/ScctPawnLookApi.h): the players' cosmetics and the map's outfits on
 * the pawn bodies, on what hangs on them and on the local player's first-person arms and gun.
 *
 * Each frame, after the equipment feature has drawn its attachments and the view model (it binds to OnFrame after
 * them), every material slot whose texture has a made camo or tint for the pawn's pattern (the content builder's
 * cosmetic_<pattern>_<retail texture>.png beside the retail texture) gets a dynamic instance of its own material with
 * that texture: the suit's pattern on the suits and the sleeves, the gun's on the guns. A map outfit's body and head
 * textures go on the stock slots they replace, and its model is what the pawn proxy loads (OutfitModelPath). The
 * goggle lenses of a pawn with a light item are drawn with M_ScctGoggleGlowHue turned to its colour, and a merc's
 * torch beam takes the same turn. A slot another feature changes (the camo suit, a vision skin, the rate-of-fire
 * display) is left to it and looked at again once it is back.
 *
 * Options (Online page, Cosmetics; also chips on the Armoury page): "Hide my cosmetics", "Hide others' cosmetics".
 *
 * Unattended switches (off by default):
 *   -ScctLook=<look>      once the player has a pawn, puts the look on his player info on the local server
 *                         ("woodland+blue": names as the Armoury's items end, or "hex:..."), and uses it as his
 *                         own look for "Hide my cosmetics"
 *   -ScctLookOthers       every other player on the local server (the bots) gets a look of its own
 *   -ScctLookWhere        logs where every pawn is once a second (to aim -ScctCams at the bots)
 */
UCLASS()
class OPENSCCT_API UScctPawnLookSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Registers the cosmetics options (also done by every subsystem start; registering twice is harmless). */
	static void RegisterSettings();

	/** Absolute path to the installed team outfit GLB, or empty when the map uses the stock character. */
	static FString OutfitModelPath(int32 Kind);

	/** The look of a pawn as of this frame, or null. */
	const FScctPawnLook* FindLook(int32 ActorId) const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** A material slot a look was put on: the material it had and the instance put over it. */
	struct FLookedSlot
	{
		TWeakObjectPtr<UMaterialInterface> Source;
		TWeakObjectPtr<UMaterialInstanceDynamic> Mine;
		/** Source and look that produced the current material. */
		uint32 Key = 0;
	};

	/** What a pawn's slots are dressed with. */
	struct FDress
	{
		int32 Kind = 0;
		int32 BodyPattern = 0;
		int32 GunPattern = 0;
		/** The outfit's slot textures (name ids of full paths), when the team wears them. */
		int32 BodySlot = 0;
		int32 HeadSlot = 0;
		/** Arms of a first-person view: the outfit's body texture goes only on the spy's own arms layout. */
		bool bFirstPerson = false;
		/** A first-person mesh's arms in the map outfit (the bridge's made texture, full path); empty for none. */
		FString ArmsPath;
	};

	void OnMatchStarted();
	void OnSessionChanged(int32 OldSessionId, int32 NewSessionId);
	void OnFrame(float DeltaSeconds);
	void OnSettingChanged(FName Key);
	void PushOptions() const;

	/** Puts a dress on every material slot of a mesh; returns how many slots wear a made texture. */
	int32 Dress(UMeshComponent& Mesh, const FDress& Look);
	void TurnGoggles(UMeshComponent& Mesh, float HueTurn, bool bTurn);

	/** The texture file a slot whose material draws Retail (in Package) wears, or empty for its own. */
	FString TextureFor(const FScctPawnLookApi& Api, const FString& Retail, const FString& Package, const FDress& Look, FString& Finish) const;

	UTexture2D* LoadTexture(const FString& Path);
	FString Name(const FScctPawnLookApi& Api, int32 Id) const;
	void LogLook(const FScctPawnLookApi& Api, const FScctPawnLook& Look, int32 Dressed) const;
	void Forget();

	/** The outfit arms texture of a first-person mesh for a team (FScctOutfitArmsApi), asked once per mesh asset. */
	FString OutfitArmsFor(const UMeshComponent& Mesh, int32 Kind);

	TWeakObjectPtr<UScctSessionSubsystem> Session;
	FDelegateHandle StartedHandle;
	FDelegateHandle ChangedHandle;
	FDelegateHandle FrameHandle;
	FDelegateHandle SettingHandle;

	TArray<FScctPawnLook> Looks;
	TMap<int32, FScctPawnLook> Logged;
	TMap<int32, int32> LoggedDressed;
	TMap<TWeakObjectPtr<UMeshComponent>, TArray<FLookedSlot>> Looked;
	TMap<TWeakObjectPtr<UMeshComponent>, TArray<FLookedSlot>> Turned;
	FScctKindOutfit Outfits[2] = {};
	mutable TMap<int32, FString> NameCache;
	TSet<FString> FailedTextures;
	TMap<FString, FString> OutfitArms;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Textures;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> HueMaterial;

	/** -ScctLook and -ScctLookOthers, and whether they were handed to the bridge in this session. */
	FString ForcedLook;
	bool bLookOthers = false;
	bool bForced = false;
	bool bReportedNoTable = false;
	bool bMissingHueReported = false;
	int32 LoggedViewDressed = -1;
	bool bLogWhere = false;
	float WhereTimer = 0.0f;
};
