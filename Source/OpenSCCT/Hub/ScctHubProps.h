#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class FScctHubActivities;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;
struct FScctHubActivityMark;

/**
 * The activities' things in the world (an OpenSCCT addition; port of the Godot client's game/Scripts/Hub/HubProps.cs),
 * drawn from what the server said (the bridge's activity table, read by Hub/ScctHubActivities.h): the courses' gates
 * (start green, checkpoints amber with the run's next one pulsing and the passed ones gone, finish white, a post at
 * each so it shows from afar), the travel points' blue discs, the aim range's firing line and its pop-up targets
 * (the player's own round's red with a yellow head and glowing, other rounds' grey) flipping up and down about their
 * foot, the jukebox, the plaza ball and its two goals, and each match pad's laptop on its desk with a square on the
 * floor where to stand. Plain shapes of our own (the engine's cube, cylinder and sphere) with two materials made from
 * nothing but maths (M_ScctHubProp, M_ScctHubGlow, Tools/Hub/build_hub_assets.py); no game art. A glow is a brightness
 * in the finished picture (divided by the eye's exposure each frame), so a ring reads the same in the sunny plaza and
 * under a dark hall's exposure. Never touches the simulation.
 */
class FScctHubProps : public TSharedFromThis<FScctHubProps>
{
public:
	explicit FScctHubProps(TSharedPtr<FScctHubActivities> InActivities);

	/** One frame of the drawn hub: the marks rebuilt when the server changed them, the pulse, the targets, the ball. */
	void Tick(UWorld* World, float DeltaSeconds, bool bVerbose);

	/** The hub is no longer drawn: the world's actors go with the world, the bookkeeping is reset. */
	void Stop();

private:
	struct FMarkView
	{
		TWeakObjectPtr<USceneComponent> Root;
		int32 Kind = 0;
	};

	struct FTargetView
	{
		TWeakObjectPtr<USceneComponent> Root;
		bool bMine = false;
	};

	bool EnsureScene(UWorld* World);
	void BuildMarks();
	void BuildMark(const FScctHubActivityMark& Mark);
	void BuildLaptop(USceneComponent* Root, const FVector& At);
	void UpdateMarks(float Now);
	void UpdateTargets();
	void UpdateBall();
	void UpdateGlow();

	USceneComponent* MakeRoot(const FVector& At);

	/** A box of UE extents (Size, uu) with its middle at Centre (world), under Parent. */
	UStaticMeshComponent* Box(USceneComponent* Parent, const FVector& Centre, const FVector& Size, UMaterialInstanceDynamic* Material);
	UStaticMeshComponent* Shape(UStaticMesh* Mesh, USceneComponent* Parent, const FVector& Centre, const FVector& Scale, UMaterialInstanceDynamic* Material);

	/** A solid lit colour with Glow of it as its own light (the brightness in the finished picture). */
	UMaterialInstanceDynamic* Solid(const FLinearColor& Colour, float Glow = 0.0f);

	/** A see-through unlit glowing colour (Colour.A is the opacity). */
	UMaterialInstanceDynamic* Glow(const FLinearColor& Colour, float Brightness = 0.6f);

	TSharedPtr<FScctHubActivities> Activities;
	TWeakObjectPtr<UWorld> SceneWorld;
	TWeakObjectPtr<AActor> Host;
	bool bSceneFailed = false;

	TMap<FString, FMarkView> MarkViews;
	int32 BuiltRevision = 0;
	bool bBuilt = false;
	TMap<int32, FTargetView> TargetViews;
	TWeakObjectPtr<UStaticMeshComponent> Ball;

	/** The materials made so far, by kind and colour, with the brightness each glows with. */
	struct FGlowMaterial
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		float Brightness = 0.0f;
	};
	TMap<FString, FGlowMaterial> Materials;

	TWeakObjectPtr<UStaticMesh> Cube;
	TWeakObjectPtr<UStaticMesh> Cylinder;
	TWeakObjectPtr<UStaticMesh> Sphere;

	float LogTimer = 0.0f;
};
