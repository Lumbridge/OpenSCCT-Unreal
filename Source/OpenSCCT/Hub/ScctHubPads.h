#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctHubPadsApi.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class SScctHubLaptopPage;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UScctHubSubsystem;
class UStaticMeshComponent;
class UTexture2D;
class UWorld;

/**
 * The hub's match pads as the player sees them (an OpenSCCT addition; port of the Godot client's
 * game/Scripts/Hub/HubPadView.cs, HubPadThemes.cs, HubLaptopPanel.cs and the HubHud's pad panel). It belongs to the hub
 * subsystem and runs while the hub level presents the visit, reading the bridge's pads table (Runtime/ScctHubPadsApi.h):
 *   - a floor overlay on every pad, coloured by its state (white idle, yellow waiting, green counting down and
 *     pulsing, cyan starting, grey off): a thin tint over the pad and a solid border (M_ScctHubPad, lit by the hub's
 *     light, Tools/Hub/build_hub_assets.py);
 *   - the map's picture (its list picture, read from the PNG the catalog names) over the pad's sign: on a board's top
 *     edge when the sign is mounted, else standing over it facing the camera (M_ScctHubPicture);
 *   - the pad's theme kit for its map: the hub map carries a kit per theme for every pad (HubTheme_<pad>_<theme>_*);
 *     the level holds each pad's default kit, the others are added from the imported meshes, and only the theme of
 *     the pad's current map is shown;
 *   - the pad laptop (SScctHubLaptopPage), opened when the hub opens it (U at a pad's laptop): the pad's next match as
 *     a lobby's settings, a map picker of the installed maps, Apply.
 * The queue line ("Queued on Pad 2 as a merc: 3/4") is drawn by the hub HUD in the colour of the pad's state
 * (GetQueueColour). Not ported: map downloads from the laptop (the hub this PC hosts has no content store, so only
 * installed maps are offered), the alternate rules page.
 */
class FScctHubPads : public TSharedFromThis<FScctHubPads>
{
public:
	explicit FScctHubPads(UScctHubSubsystem* InOwner);
	~FScctHubPads();

	/** The table, fetched on first use; null when the bridge has none. */
	const FScctHubPadsApi* GetApi();

	/** One frame of the drawn hub: reads the table, keeps the overlays, pictures, kits and the laptop up to date. */
	void Tick(UWorld* World, float DeltaSeconds, bool bVerbose);

	/** The hub is no longer drawn: the laptop closes; the world's actors go with the world. */
	void Stop();

	/** The unattended pad run (-ScctHubPad): queue on pad Number as a spy (0) or merc (1); the laptop takes Options first. */
	bool QueueOnPad(int32 Number, int32 Side, const FString& Options);

	// ---- what the HUD and the laptop page read ---------------------------------------------------------------

	const FScctHubPadsState& GetState() const { return State; }
	const TArray<FScctHubPadRow>& GetPads() const { return Pads; }
	FString ReadText(EScctHubPadsText Kind) const;

	/** The colour of the pad the player is queued on (its state's), or white. */
	FLinearColor GetQueueColour() const;

	/** A map picture read from a PNG on this PC (kept for the run), or null when it cannot be read. */
	UTexture2D* GetPicture(const FString& Path);

	static FLinearColor ColourOf(uint32 Rgba);

	// ---- what the laptop page calls ----------------------------------------------------------------------------

	void CloseLaptop();

private:
	struct FPadView
	{
		TWeakObjectPtr<UStaticMeshComponent> Overlay;
		TWeakObjectPtr<UMaterialInstanceDynamic> OverlayMaterial;
		TWeakObjectPtr<UStaticMeshComponent> Picture;
		TWeakObjectPtr<UMaterialInstanceDynamic> PictureMaterial;
		FString PicturePath;
		FString Theme;
		int32 State = -1;
	};

	struct FKitPart
	{
		FString Pad;
		FString Theme;
		TWeakObjectPtr<UPrimitiveComponent> Component;
	};

	bool EnsureScene(UWorld* World);
	void UpdatePad(const FScctHubPadRow& Row, FPadView& View, const FVector& Eye, float Now);
	void GatherKits(UWorld* World);
	void ShowTheme(const FString& Pad, const FString& Theme);
	void FollowLaptop(UWorld* World);
	void LogLine(float DeltaSeconds);

	UScctHubSubsystem* Owner = nullptr;
	const FScctHubPadsApi* Api = nullptr;
	bool bReportedNoApi = false;

	FScctHubPadsState State = {};
	TArray<FScctHubPadRow> Pads;

	TWeakObjectPtr<UWorld> SceneWorld;
	TWeakObjectPtr<AActor> Host;
	bool bSceneFailed = false;
	TMap<FString, FPadView> Views;

	TArray<FKitPart> Kits;
	bool bKitsGathered = false;

	TMap<FString, TStrongObjectPtr<UTexture2D>> Pictures;
	TSet<FString> BadPictures;

	TSharedPtr<SScctHubLaptopPage> Laptop;
	int32 SeenLaptopSerial = 0;

	bool bVerbose = false;
	float LogTimer = 0.0f;
	FString LastLoggedStates;
};
