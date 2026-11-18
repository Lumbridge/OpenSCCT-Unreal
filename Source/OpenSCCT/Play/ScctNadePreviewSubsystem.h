#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Hud/SScctNadePreviewPip.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctNadePreviewSubsystem.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UStaticMesh;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class SWidget;
struct FScctNadePreview;
struct FScctNadePreviewApi;

/**
 * The grenade preview (an OpenSCCT aid, not in the original): while the local player has a grenade gadget ready (the
 * merc's rifle grenades, the spy's thrown grenades with the pistol out) it draws
 *   - the path the projectile would fly if the throw button were let go now, from the muzzle of the gun as drawn
 *     this frame, through its bounces, to where it goes off or catches;
 *   - a ring on the surface there, and a second ring of the effect's reach (blast, cloud) when it has one;
 *   - a small picture of the landing point at the right edge of the screen, from a camera above and behind it.
 * The path is the simulation's own answer (the bridge follows the flight by the simulation's launch, falling, bounce
 * and fuse rules against its collision: Runtime/ScctNadePreviewApi.h), read every rendered frame after the frame's
 * view is final, so it follows the camera and the charge gauge without a frame of delay. The line is green when the
 * throw is ready, amber while the button is held (a tap throws at full strength, so the line is long until the tap
 * window of a quarter second is over, then it starts short and grows for a second), and red when the simulation
 * would refuse the throw now. The simulation rounds a throw's direction to 1.4 degree steps, and so does the line.
 *
 * It works in matches played on this machine and in practice rounds; on another server it is refused with a line on
 * the screen and in the log, as it shows the landing point round corners. Off by default; while it is off nothing
 * here runs per frame.
 *
 * Ways to turn it on or off:
 *   NadePreview [0|1]        the console command (scct.NadePreview is the same); without an argument it toggles.
 *                            It is remembered until the game is closed, across maps.
 *   -ScctNadePreview         on the command line: on from the start (for screenshots)
 * Unattended checks:
 *   -ScctNadePreviewHold=<s> s seconds after the first path is drawn the throw button is held for 2 s and two
 *                            screenshots with the HUD are saved beside the run's others (NadePreviewCharge.png
 *                            0.8 s into the hold, NadePreviewFull.png at 1.6 s), each with a log line
 *   -ScctNadePreviewOff=<s>  s seconds after the first path is drawn the command is turned off, and 2 s later the
 *                            log says how many frames were worked on since (none)
 *
 * While it is shown the log gets one line a second: "NadePreview: frag v 1.00, 61 points, lands (x y z) after 2.0 s...".
 */
UCLASS()
class OPENSCCT_API UScctNadePreviewSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UScctNadePreviewSubsystem();

	/** Asks for the preview: 1 on, 0 off, -1 the other way round. Returns what happened, for the console. */
	FString Request(int32 Mode);

	/** The player wants it on (it is drawn while a grenade gadget is ready). */
	static bool IsWanted();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleSessionChanged(int32 OldSessionId, int32 NewSessionId);
	void HandleMatchStopped();
	void HandlePostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	/** The per-frame hook is there only while the preview is wanted. */
	void Bind();
	void Unbind();

	void UpdateFrame(float DeltaSeconds);
	void DrawPath(const FScctNadePreview& Head, const TArrayView<const FVector>& Points, const TArrayView<const int32>& PointFlags, const FVector& Camera);
	void UpdatePicture(const FScctNadePreview& Head, float DeltaSeconds);
	void TickChecks(float DeltaSeconds);

	bool EnsureStage();
	void HideDrawn();
	void DestroyStage();

	UStaticMeshComponent* Place(TArray<TObjectPtr<UStaticMeshComponent>>& Pool, int32 Index, UStaticMesh* Mesh, UMaterialInstanceDynamic* Material, const FTransform& Transform);
	static void Trim(TArray<TObjectPtr<UStaticMeshComponent>>& Pool, int32 Used);
	void Segment(TArray<TObjectPtr<UStaticMeshComponent>>& Pool, int32& Used, UMaterialInstanceDynamic* Material, const FVector& From, const FVector& To, double Width);
	void Ring(int32& Used, const FVector& Centre, const FVector& Normal, double Radius, int32 Segments, const FVector& Camera);

	void ShowWidget();
	void HideWidget();
	void Notify(const FString& Text);

	/** The transient actor the line, the marker and the capture camera hang on. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> Stage;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LineParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BounceParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MarkerParts;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LineMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MarkerMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	TSharedRef<FScctNadePreviewPipModel> Model;
	TSharedPtr<SScctNadePreviewPip> Widget;
	TSharedPtr<SWidget> ViewportContent;
	const FScctNadePreviewApi* Api = nullptr;
	FDelegateHandle TickHandle;

	/** The colour state the line's material was last given (-1: none yet). */
	int32 LineState = -1;

	/** Something of the path is visible (so hiding has work to do). */
	bool bDrawn = false;

	/** The session refused the preview and the player was told; asked again in the next session. */
	bool bRefused = false;

	FVector PictureCamera = FVector::ZeroVector;
	bool bPictureCamera = false;
	float CaptureTimer = 0.0f;
	int32 CapturesMade = 0;

	float LogTimer = 0.0f;
	int32 FramesWorked = 0;
	int32 FramesWithPath = 0;
	double ReadSeconds = 0.0;
	double WorkSeconds = 0.0;
	int32 TimedFrames = 0;

	// The unattended checks.
	float HoldAt = -1.0f;
	float OffAt = -1.0f;
	float ShownFor = 0.0f;
	int32 HoldStage = 0;
	bool bOffDone = false;
	FTimerHandle OffReport;
};
