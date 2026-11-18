#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"

#include "ScctRuntimeMapActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UTexture2D;
class FJsonObject;

/**
 * Draws a converted map that the editor never imported, from its render files, in the runtime level
 * (UScctOnlineSubsystem::RuntimeLevel, opened with ?ScctMap=<Map>). It reads svmap/<Map>/render/world.glb with the
 * engine's glTF reader (GLTFCore) into static meshes built at run time, gives every surface the converted map's
 * base-colour texture under the surface material the importer uses (a material instance per blend mode, made by
 * Tools/Import/make_runtime_map.py), places the level geometry and every placed mesh as the importer does
 * (Tools/Import/import_map.py), with the same "Scct:<actor>" tags so the level state follows the simulation (doors,
 * lifts, hidden and broken things), and turns render/lighting.json into the same dynamic lights with the importer's
 * falloff. The level asset brings the sky, the sky light, the fog and the post-process of an imported level.
 *
 * What it lacks against an import in the editor: no baked lighting of Unreal's own (the original's lightmaps and
 * vertex colours are given back as the importer's faint fill), no Nanite, no derived normal maps or detail layer of
 * a surface class (every surface gets the plain class), no glass or water materials, no skyline backdrop, no
 * emitters, coronas, beams, projectors or rain (the Niagara and decal dressing of import_map.py), and one sky for
 * every map. Collision is the simulation's either way.
 */
UCLASS()
class OPENSCCT_API AScctRuntimeMapActor : public AActor
{
	GENERATED_BODY()

public:
	AScctRuntimeMapActor();

	/** Builds the map whose folder (holding render/) is MapFolder. Returns false, having logged why, when it cannot. */
	bool Build(const FString& Map, const FString& MapFolder);

private:
	UMaterialInterface* MaterialFor(int32 GltfMaterial);
	UTexture2D* TextureFor(const FString& RelativePath, bool bSRGB = true);
	void BuildMaterials(const TSharedPtr<FJsonObject>& Gltf, const TSharedPtr<FJsonObject>& MaterialDoc);
	void PlaceLights(const TSharedPtr<FJsonObject>& Lighting);
	AActor* PlaceMesh(UStaticMesh* Mesh, const FString& Name, const FTransform& Transform, bool bMovable, float VertexLightKind);

	FString RenderFolder;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Textures;

	int32 NumTexturesMissing = 0;
	int32 NumLights = 0;
	int32 NumPlaced = 0;
};

/**
 * Builds the runtime level's map before the match starts: when a game world of the runtime level begins play, it
 * spawns an AScctRuntimeMapActor and builds the map of the URL's ?ScctMap=, so the level is drawn before the game
 * mode starts the session and the level state indexes the level's actors.
 */
UCLASS()
class OPENSCCT_API UScctRuntimeMapSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
};
