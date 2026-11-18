#include "Play/ScctRuntimeMapActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Play/ScctAuthoredMaterial.h"
#include "Play/ScctMeshMaterial.h"
#include "Play/ScctAuthoredDirections.h"

#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/TextureCube.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "GameFramework/PlayerController.h"
#include "StaticMeshOperations.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GLTFAsset.h"
#include "GLTFMesh.h"
#include "GLTFMeshFactory.h"
#include "GLTFReader.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Online/ScctMapFolders.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "Streaming/LevelStreamingDelegates.h"

namespace
{
	// Tools/Import/import_map.py, so a map looks the same built here as imported in the editor.
	constexpr float RadiusUnits = 25.0f;              // world radius = 25 * (LightRadius + 1), as the original engine
	constexpr float LightFalloff = 1.6f;              // light = (1 - (d / R)^2) ^ this
	constexpr float LightPerBrightness = 5.0f / 255.0f;
	constexpr float LightSaturation = 0.55f;
	constexpr float SmallLightRadius = 300.0f;        // below this reach a light is weakened and its edge softened
	constexpr float SmallLightFalloff = 3.0f;
	constexpr float AmbientFloor = 0.012f;            // the least light of a lit surface (the original's zone ambient)
	// A lamp the original baked its lightmaps from (apply_static) is already in the baked fill below. Measured in
	// BankP's guard room (103 of its 108 lights are such lamps): the fill alone gives a scene luminance of 0.010,
	// the lamps another 0.070, twice what the whole baked light stood for. They keep this share, for the shadows,
	// the shine and the characters.
	constexpr float StaticLightShare = 0.4f;
	// A light the original put on characters only (apply_ingame without apply_static) stands at a real fixture in
	// the maps whose baking lamps are gone from the file, and is the best record of the room's light there. In a
	// map that still has its baking lamps (more than half of its lights) it is a fill for the characters and lit
	// no wall: BankP's roof, dark in its lightmaps, was flooded pale grey by one hung 5 m over it. There it keeps
	// this share.
	constexpr float CharacterFillShare = 0.25f;
	constexpr float EmissiveUnlit = 0.9f;             // screens, lamps and signs that were unlit in the original
	constexpr float EmissiveAdditive = 0.35f;         // flare and glow sprites, holograms
	constexpr float EmissiveSelfIllum = 2.0f;         // the self-lit part of a lit surface
	constexpr float GlowCardBrightness = 0.2f;        // beam and flare cards modelled into the level
	constexpr float EmissiveCaustics = 0.08f;         // additive sheets drawing light under water
	constexpr float BakedFill = 0.5f;                 // the share of the original's baked light given back

	// Look experiments (read when the map is built; set them before, e.g. -dpcvars=scct.RuntimeMap.StaticLights=0).
	TAutoConsoleVariable<float> CVarRuntimeStaticLights(TEXT("scct.RuntimeMap.StaticLights"), StaticLightShare,
		TEXT("Share of its strength a lamp the original baked its lightmaps from (apply_static) keeps in a map built at run time: 1 all, 0 none."));
	TAutoConsoleVariable<float> CVarRuntimeCharacterFills(TEXT("scct.RuntimeMap.CharacterFills"), CharacterFillShare,
		TEXT("Share of its strength a characters-only light keeps in a map built at run time that still has its baking lamps: 1 all, 0 none."));
	TAutoConsoleVariable<float> CVarRuntimeBakedFill(TEXT("scct.RuntimeMap.BakedFill"), BakedFill,
		TEXT("The share of the original's baked light (lightmaps, vertex colours) a map built at run time gives back as a glow."));

	// Custom primitive data slots of the surface material (Tools/Import/scct_materials.py).
	constexpr int32 LocalDetailSlot = 0;
	constexpr int32 BakedVertexSlot = 1;
	constexpr float AuthoredVertexColour = 2.0f;

	/** Where Tools/Import/make_runtime_map.py puts the runtime level's surface materials. */
	const TCHAR* const MaterialFolder = TEXT("/Game/OpenSCCT/RuntimeMap");
	const TCHAR* const SurfaceMaterial = TEXT("/Game/OpenSCCT/Materials/M_ScctSurface.M_ScctSurface");

	/** A name reduced to lower-case letters and digits, as the importer matches the map's names (import_map.py key). */
	FString Key(const FString& Name)
	{
		FString Out;
		Out.Reserve(Name.Len());
		for (const TCHAR Char : Name)
		{
			if (FChar::IsAlnum(Char))
			{
				Out.AppendChar(FChar::ToLower(Char));
			}
		}
		return Out;
	}

	bool ContainsAny(const FString& Text, std::initializer_list<const TCHAR*> Words)
	{
		for (const TCHAR* Word : Words)
		{
			if (Text.Contains(Word, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	TSharedPtr<FJsonObject> ReadJsonFile(const FString& Path)
	{
		FString Text;
		return FFileHelper::LoadFileToString(Text, *Path) ? ParseJson(Text) : nullptr;
	}

	/** The JSON chunk of a binary glTF: the parts the engine's reader does not keep (node roles, placed instances). */
	TSharedPtr<FJsonObject> ReadGlbJson(const FString& Path)
	{
		TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
		if (!File.IsValid() || File->TotalSize() < 20)
		{
			return nullptr;
		}
		uint32 Header[5] = {};
		File->Serialize(Header, sizeof(Header));
		// "glTF", version, length; then the first chunk's length and type "JSON".
		if (Header[0] != 0x46546C67 || Header[4] != 0x4E4F534A || Header[3] == 0 || Header[3] > File->TotalSize())
		{
			return nullptr;
		}
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Header[3]);
		File->Serialize(Bytes.GetData(), Bytes.Num());
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return ParseJson(FString(Text.Length(), Text.Get()));
	}

	FRotator RotatorOfUnits(double Pitch, double Yaw, double Roll)
	{
		constexpr double Scale = 360.0 / 65536.0;
		return FRotator(Pitch * Scale, Yaw * Scale, Roll * Scale);
	}

	FVector VectorOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FVector& Default)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(Field, Values) || Values->Num() < 3)
		{
			return Default;
		}
		return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
	}

	bool BoolOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bDefault = false)
	{
		bool bValue = bDefault;
		return Object.IsValid() && Object->TryGetBoolField(Field, bValue) ? bValue : bDefault;
	}

	FString StringOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : FString();
	}

	double NumberOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Default = 0.0)
	{
		double Value = Default;
		return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? Value : Default;
	}

	TSharedPtr<FJsonObject> ObjectOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Object.IsValid() && Object->TryGetObjectField(Field, Value) ? *Value : nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>& ArrayOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		static const TArray<TSharedPtr<FJsonValue>> None;
		const TArray<TSharedPtr<FJsonValue>>* Value = nullptr;
		return Object.IsValid() && Object->TryGetArrayField(Field, Value) ? *Value : None;
	}

	TArray<FString> AuthoredMeshNames(const TSharedPtr<FJsonObject>& MaterialDoc)
	{
		TArray<FString> Names;
		if (MaterialDoc.IsValid())
		{
			MaterialDoc->TryGetStringArrayField(TEXT("authoredColorMeshes"), Names);
		}
		return Names;
	}

	/** Restore the missing role on components of older imports, including cooked map packs. */
	void MarkImportedAuthoredColours(ULevel& Level, const FString& Package)
	{
		const FString Map = FPackageName::GetShortName(Package);
		if (Package != UScctOnlineSubsystem::ImportedLevelOf(Map))
		{
			return;
		}
		const FString Folder = ScctMapFolders::FolderOf(Map);
		if (Folder.IsEmpty())
		{
			return;
		}

		const FString MeshFolder = FPackageName::GetLongPackagePath(Package) / TEXT("world/StaticMeshes");
		TSet<FName> Packages;
		for (FString Name : AuthoredMeshNames(ReadJsonFile(Folder / TEXT("render/materials.json"))))
		{
			// import_map.py sanitize(): retain the complete variant suffix and match only this map's mesh assets.
			for (TCHAR& Ch : Name)
			{
				if (!(Ch < 128 && FChar::IsAlnum(Ch)))
				{
					Ch = TEXT('_');
				}
			}
			Packages.Add(FName(*(MeshFolder / Name)));
		}
		if (Packages.IsEmpty())
		{
			return;
		}

		for (AActor* Actor : Level.Actors)
		{
			if (IsValid(Actor))
			{
				for (UStaticMeshComponent* Component : TInlineComponentArray<UStaticMeshComponent*>(Actor))
				{
					const UStaticMesh* Mesh = Component->GetStaticMesh();
					if (Mesh && Packages.Contains(Mesh->GetPackage()->GetFName()))
					{
						Component->SetCustomPrimitiveDataFloat(BakedVertexSlot, AuthoredVertexColour);
					}
				}
			}
		}
	}

	/** The original's texture scrolling as UV units per second (import_map.py pan_speed); only straight panners. */
	FVector2D PanSpeed(const TSharedPtr<FJsonObject>& Animation)
	{
		if (!Animation.IsValid() || StringOf(Animation, TEXT("type")) != TEXT("panner"))
		{
			return FVector2D::ZeroVector;
		}
		const FVector Direction = VectorOf(Animation, TEXT("direction"), FVector::ZeroVector);
		const double Yaw = Direction.Y / 65536.0 * 2.0 * PI;
		const double Rate = NumberOf(Animation, TEXT("rate"));
		return FVector2D(Rate * FMath::Cos(Yaw), Rate * FMath::Sin(Yaw));
	}

	/** Halves a BGRA8 picture (a box filter), for the next mip. */
	TArray<uint8> HalfSize(const TArray<uint8>& Source, int32 Width, int32 Height, int32& OutWidth, int32& OutHeight)
	{
		OutWidth = FMath::Max(1, Width / 2);
		OutHeight = FMath::Max(1, Height / 2);
		TArray<uint8> Out;
		Out.SetNumUninitialized(OutWidth * OutHeight * 4);
		for (int32 Y = 0; Y < OutHeight; ++Y)
		{
			const int32 Y0 = FMath::Min(Y * 2, Height - 1);
			const int32 Y1 = FMath::Min(Y * 2 + 1, Height - 1);
			for (int32 X = 0; X < OutWidth; ++X)
			{
				const int32 X0 = FMath::Min(X * 2, Width - 1);
				const int32 X1 = FMath::Min(X * 2 + 1, Width - 1);
				for (int32 Channel = 0; Channel < 4; ++Channel)
				{
					const int32 Sum = Source[(Y0 * Width + X0) * 4 + Channel] + Source[(Y0 * Width + X1) * 4 + Channel]
						+ Source[(Y1 * Width + X0) * 4 + Channel] + Source[(Y1 * Width + X1) * 4 + Channel];
					Out[(Y * OutWidth + X) * 4 + Channel] = static_cast<uint8>((Sum + 2) / 4);
				}
			}
		}
		return Out;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Subsystem
// ---------------------------------------------------------------------------------------------------------------

bool UScctRuntimeMapSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UScctRuntimeMapSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const FString Package = UWorld::RemovePIEPrefix(InWorld.GetOutermost()->GetName());
	// A streamed instance has a generated package name. Its streaming object retains the original package.
	FLevelStreamingDelegates::OnLevelStreamingStateChanged.AddWeakLambda(this,
		[this](UWorld* World, const ULevelStreaming* Streaming, ULevel* Level, ELevelStreamingState Previous, ELevelStreamingState Current)
		{
			if (World == GetWorld() && Level && (Previous == ELevelStreamingState::Loading || Previous == ELevelStreamingState::Unloaded)
				&& (Current == ELevelStreamingState::LoadedNotVisible || Current == ELevelStreamingState::LoadedVisible))
			{
				const FString Source = Streaming->PackageNameToLoad.IsNone() ? Streaming->GetWorldAssetPackageName() : Streaming->PackageNameToLoad.ToString();
				MarkImportedAuthoredColours(*Level, UWorld::RemovePIEPrefix(Source));
			}
		});
	if (Package != UScctOnlineSubsystem::RuntimeLevel)
	{
		MarkImportedAuthoredColours(*InWorld.PersistentLevel, Package);
		return;
	}

	const FString Map = InWorld.URL.GetOption(TEXT("ScctMap="), TEXT(""));
	if (Map.IsEmpty())
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Runtime map: the runtime level was opened without ?ScctMap=<Map>; nothing to build"));
		return;
	}
	const FString Folder = ScctMapFolders::FolderOf(Map);
	if (Folder.IsEmpty())
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Runtime map: no map folder has %s (searched: %s)"), *Map, *FString::Join(ScctMapFolders::Roots(), TEXT("; ")));
		return;
	}

	FActorSpawnParameters Params;
	Params.Name = TEXT("ScctRuntimeMap");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AScctRuntimeMapActor* Builder = InWorld.SpawnActor<AScctRuntimeMapActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Builder != nullptr)
	{
		Builder->Build(Map, Folder);
	}
}

void UScctRuntimeMapSubsystem::Deinitialize()
{
	FLevelStreamingDelegates::OnLevelStreamingStateChanged.RemoveAll(this);
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------------------------------------------

AScctRuntimeMapActor::AScctRuntimeMapActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

namespace
{
	UStaticMesh* BuildMapMesh(UObject* Outer, const GLTF::FMesh& SourceMesh, const TArray<TSharedPtr<FJsonValue>>& JsonPrims,
		const TArray<GLTF::FAccessor>& Accessors, bool bDevice, TFunctionRef<UMaterialInterface*(int32)> MaterialFor,
		int32& NumSections, int32& NumTriangles, bool& bVertexDirections)
	{
		UStaticMesh* Mesh = nullptr;
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		GLTF::FMeshFactory Factory;
		// Metres to centimetres; the reader already turned glTF's Y up into Unreal's Z up.
		Factory.SetUniformScale(100.0f);
		// GLTFCore expands normalized COLOR_0 per corner and linearizes RGB. The fast static-mesh build
		// packs it back to sRGB bytes (alpha stays linear), preserving the original vertex mask.
		const auto& Primitives = SourceMesh.Primitives;
		// A split section cannot claim these channels if a sibling uses them as genuine UVs.
		const bool bAvailableUVs = !SourceMesh.HasTexCoords(2) && !SourceMesh.HasTexCoords(3);
		bDevice &= JsonPrims.Num() == Primitives.Num();
		TMap<int32, int32> SourceByMaterial;
		bool bSplit = false;
		if (bDevice)
		{
			for (int32 P = 0; P < Primitives.Num(); ++P)
			{
				const int32 Source = NumberOf(ObjectOf(JsonPrims[P]->AsObject(), TEXT("extras")), TEXT("scctSection"), P);
				const int32 Material = Primitives[P].MaterialIndex;
				if (const int32* Prior = SourceByMaterial.Find(Material); Prior && *Prior != Source) bSplit = true;
				SourceByMaterial.Add(Material, Source);
			}
		}
		if (bSplit)
		{
			// GLTFCore normally merges primitives using the same material. They must remain independently
			// skinnable even when the intact model happens to draw them with the same texture.
			FStaticMeshOperations::FAppendSettings Append;
			for (bool& Channel : Append.bMergeUVChannels) Channel = true;
			for (int32 P = 0; P < Primitives.Num(); ++P)
			{
				GLTF::FMesh Part;
				Part.Primitives.Add(Primitives[P]);
				FMeshDescription Section;
				FStaticMeshAttributes SectionAttrs(Section);
				SectionAttrs.Register();
				Factory.FillMeshDescription(Part, FTransform::Identity, &Section);
				if (!Section.Triangles().Num()) continue;
				if (bAvailableUVs)
					bVertexDirections |= ScctAuthored::CopyDirections(Factory, Accessors, MakeArrayView(&JsonPrims[P], 1), Section);
				const int32 SourceSection = NumberOf(ObjectOf(JsonPrims[P]->AsObject(), TEXT("extras")), TEXT("scctSection"), P);
				for (const auto Group : Section.PolygonGroups().GetElementIDs())
				{
					SectionAttrs.GetPolygonGroupMaterialSlotNames()[Group] = FName(*FString::Printf(TEXT("%d:ScctSection:%d"), Primitives[P].MaterialIndex, SourceSection));
				}
				FStaticMeshOperations::AppendMeshDescription(Section, Description, Append);
			}
		}
		else
		{
			Factory.FillMeshDescription(SourceMesh, FTransform::Identity, &Description);
			if (bAvailableUVs && JsonPrims.Num() == Primitives.Num())
				bVertexDirections = ScctAuthored::CopyDirections(Factory, Accessors, JsonPrims, Description);
			if (bDevice)
			{
				auto Names = Attributes.GetPolygonGroupMaterialSlotNames();
				for (const auto Group : Description.PolygonGroups().GetElementIDs())
				{
					const int32 Id = FCString::Atoi(*Names[Group].ToString());
					Names[Group] = FName(*FString::Printf(TEXT("%d:ScctSection:%d"), Id, SourceByMaterial.FindChecked(Id)));
				}
			}
		}
		if (Description.Triangles().Num() > 0)
		{
			Mesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
			const TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
			for (const FPolygonGroupID Group : Description.PolygonGroups().GetElementIDs())
			{
				// The engine's mesh factory names each slot after the glTF material index.
				const FName Slot = SlotNames[Group];
				// The imported slot name exists only in editor builds (FStaticMaterial's WITH_EDITORONLY_DATA argument).
#if WITH_EDITORONLY_DATA
				Mesh->GetStaticMaterials().Add(FStaticMaterial(MaterialFor(FCString::Atoi(*Slot.ToString())), Slot, Slot));
#else
				Mesh->GetStaticMaterials().Add(FStaticMaterial(MaterialFor(FCString::Atoi(*Slot.ToString())), Slot));
#endif
			}
			UStaticMesh::FBuildMeshDescriptionsParams Params;
			Params.bFastBuild = true;
			Params.bMarkPackageDirty = false;
			Params.bCommitMeshDescription = false;
			Params.bBuildSimpleCollision = false;
			Mesh->BuildFromMeshDescriptions({ &Description }, Params);
			NumSections += Description.PolygonGroups().Num();
			NumTriangles += Description.Triangles().Num();
		}
		return Mesh;
	}
}

void AScctRuntimeMapActor::ReadSectionIndex(const FString& MapFolder, TMap<FName, TMap<FName, int32>>& Sections)
{
	if (MapFolder.IsEmpty()) return;
	const auto Doc = ReadGlbJson(MapFolder / TEXT("render/world.glb"));
	const auto& Mats = ArrayOf(Doc, TEXT("materials"));
	const auto& MeshList = ArrayOf(Doc, TEXT("meshes"));
	for (const auto& NodeValue : ArrayOf(Doc, TEXT("nodes")))
	{
		const auto Node = NodeValue->AsObject();
		const int32 Index = NumberOf(Node, TEXT("mesh"), -1);
		if (!MeshList.IsValidIndex(Index)) continue;
		const auto Mesh = MeshList[Index]->AsObject();
		const auto& Parts = ArrayOf(Mesh, TEXT("primitives"));
		if (Parts.Num() < 1) continue;
		auto& Slots = Sections.FindOrAdd(FName(*Key(StringOf(Node, TEXT("name")))));
		for (int32 P = 0; P < Parts.Num(); ++P)
		{
			const auto Part = Parts[P]->AsObject();
			const int32 Material = NumberOf(Part, TEXT("material"), -1);
			if (Mats.IsValidIndex(Material))
			{
				const int32 Section = NumberOf(ObjectOf(Part, TEXT("extras")), TEXT("scctSection"), P);
				const FName MatKey(*Key(StringOf(Mats[Material]->AsObject(), TEXT("name"))));
				if (const int32* Prior = Slots.Find(MatKey); Prior && *Prior != Section) Slots.Add(TEXT("#split"), Index);
				Slots.FindOrAdd(MatKey, Section);
				Slots.Add(FName(*FString::Printf(TEXT("#%d"), P)), Section);
			}
		}
	}
}

// An older cooked mesh can have a single material slot shared by two different source sections.
// Rebuild just that mesh, on its first skin change, retaining the cooked materials and authored vertices.
UStaticMesh* AScctRuntimeMapActor::SplitSkinMesh(UObject* Outer, UStaticMesh* Imported, const FString& MapFolder, int32 Index)
{
	const FString File = MapFolder / TEXT("render/world.glb");
	const auto Doc = ReadGlbJson(File);
	const auto& MeshList = ArrayOf(Doc, TEXT("meshes"));
	if (!MeshList.IsValidIndex(Index)) return nullptr;
	GLTF::FFileReader Reader;
	GLTF::FAsset Asset;
	Reader.ReadFile(File, false, false, Asset);
	if (!Asset.Meshes.IsValidIndex(Index)) return nullptr;
	const auto& Mats = ArrayOf(Doc, TEXT("materials"));
	TMap<FString, UMaterialInterface*> ByName;
	for (const auto& Slot : Imported->GetStaticMaterials())
	{
		ByName.Add(Key(Slot.MaterialSlotName.ToString()), Slot.MaterialInterface);
		if (Slot.MaterialInterface)
		{
			FString Name = Slot.MaterialInterface->GetName();
			Name.RemoveFromStart(TEXT("MI_"));
			ByName.Add(Key(Name), Slot.MaterialInterface);
		}
	}
	int32 Sections = 0, Triangles = 0;
	bool bVertexDirections = false;
	return BuildMapMesh(Outer, Asset.Meshes[Index], ArrayOf(MeshList[Index]->AsObject(), TEXT("primitives")), Asset.Accessors, true,
		[&](int32 M) -> UMaterialInterface*
		{
			if (Mats.IsValidIndex(M))
			{
				const auto* Found = ByName.Find(Key(StringOf(Mats[M]->AsObject(), TEXT("name"))));
				if (Found) return *Found;
			}
			return Imported->GetMaterial(0);
		}, Sections, Triangles, bVertexDirections);
}

void AScctRuntimeMapActor::ConfigureSky(const FString& Map)
{
	const auto Rules = ReadJsonFile(FPaths::ProjectDir() / TEXT("Tools/Import/map_skies.json"));
	FString Kind = StringOf(ObjectOf(Rules, TEXT("maps")), *Map);
	if (Kind.IsEmpty()) Kind = StringOf(Rules, TEXT("default"));
	const auto Look = ObjectOf(ObjectOf(Rules, TEXT("kinds")), *Kind);
	if (!Look) return;
	const FString Panorama = TEXT("/Game/OpenSCCT/ThirdParty/PolyHaven/") + StringOf(Look, TEXT("panorama"));
	auto Colour = [](const FVector& RGB) { return FLinearColor(RGB.X, RGB.Y, RGB.Z); };
	const FLinearColor Tint = Colour(VectorOf(Look, TEXT("tint"), FVector::OneVector));
	const float Angle = NumberOf(Look, TEXT("rotation"));
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		auto* Dome = It->GetStaticMeshComponent();
		auto* Base = Dome->GetMaterial(0);
		if (!Base || Base->GetName() != TEXT("MI_ScctRuntimeSky")) continue;
		if (auto* Mid = Dome->CreateDynamicMaterialInstance(0, Base))
		{
			if (auto* Image = LoadObject<UTexture2D>(nullptr, *Panorama)) Mid->SetTextureParameterValue(TEXT("Panorama"), Image);
			Mid->SetVectorParameterValue(TEXT("Tint"), Tint);
			Mid->SetScalarParameterValue(TEXT("Rotation"), Angle);
			Mid->SetScalarParameterValue(TEXT("Brightness"), NumberOf(Look, TEXT("brightness")));
			Mid->SetScalarParameterValue(TEXT("StarBrightness"), NumberOf(Look, TEXT("stars")));
		}
		// The fallback dome belongs behind the complete authored sky diorama.
		if (SkyRoot)
		{
			Dome->SetMobility(EComponentMobility::Movable);
			It->SetActorScale3D(FVector(1000000.0));
		}
		break;
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		auto* Light = It->GetLightComponent();
		if (auto* Cube = LoadObject<UTextureCube>(nullptr, *(Panorama + TEXT("_light")))) Light->SetCubemap(Cube);
		Light->SetSourceCubemapAngle(Angle);
		Light->SetIntensity(NumberOf(Look, TEXT("sky_light")));
		Light->SetLightColor(Tint);
	}
	if (const auto Moon = ObjectOf(Look, TEXT("moon")))
	{
		const FRotator Rotation(NumberOf(Moon, TEXT("pitch")), NumberOf(Moon, TEXT("yaw")), 0);
		if (auto* Actor = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 4000), Rotation))
		{
			auto* Light = CastChecked<UDirectionalLightComponent>(Actor->GetLightComponent());
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensity(NumberOf(Moon, TEXT("lux")));
			Light->SetLightColor(Colour(VectorOf(Moon, TEXT("colour"), FVector::OneVector)));
			Light->SetLightSourceAngle(2.f);
		}
	}
	const auto Fog = ObjectOf(Look, TEXT("fog"));
	for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
	{
		auto* Component = It->GetComponent();
		Component->SetFogDensity(NumberOf(Fog, TEXT("density"), 0.006));
		Component->SetFogInscatteringColor(Colour(VectorOf(Fog, TEXT("colour"), FVector(0.02, 0.03, 0.05))));
	}
}

void AScctRuntimeMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (SkyRoot && PC && PC->PlayerCameraManager)
	{
		SkyRoot->SetWorldLocation(PC->PlayerCameraManager->GetCameraLocation());
	}
}

bool AScctRuntimeMapActor::Build(const FString& Map, const FString& MapFolder)
{
	const double Start = FPlatformTime::Seconds();
	RenderFolder = MapFolder / TEXT("render");
	const FString GlbPath = RenderFolder / TEXT("world.glb");
	if (!FPaths::FileExists(GlbPath))
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Runtime map %s: %s has no render files (%s)"), *Map, *MapFolder, *GlbPath);
		return false;
	}

	// The roles of the nodes and the placed instances are glTF extras, which the engine's reader does not keep.
	const TSharedPtr<FJsonObject> Gltf = ReadGlbJson(GlbPath);
	if (!Gltf.IsValid())
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Runtime map %s: %s is not a binary glTF file"), *Map, *GlbPath);
		return false;
	}

	// The geometry through the engine's glTF reader: accessors, buffers and the conversion into Unreal's space.
	GLTF::FFileReader Reader;
	GLTF::FAsset Asset;
	Reader.ReadFile(GlbPath, false, false, Asset);
	int32 NumReaderErrors = 0;
	for (const GLTF::FLogMessage& Message : Reader.GetLogMessages())
	{
		if (Message.Get<0>() == GLTF::EMessageSeverity::Error)
		{
			++NumReaderErrors;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Runtime map %s: glTF reader: %s"), *Map, *Message.Get<1>().ToString());
		}
	}
	if (Asset.Meshes.Num() == 0)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Runtime map %s: the engine's glTF reader found no meshes in %s (%d errors)"), *Map, *GlbPath, NumReaderErrors);
		return false;
	}
	const double ReadSeconds = FPlatformTime::Seconds() - Start;

	const TSharedPtr<FJsonObject> MaterialDoc = ReadJsonFile(RenderFolder / TEXT("materials.json"));
	BuildMaterials(Gltf, MaterialDoc);
	TSet<FString> VertexLit;
	for (const TSharedPtr<FJsonValue>& Entry : ArrayOf(MaterialDoc, TEXT("vertexLitMeshes")))
	{
		VertexLit.Add(StringOf(Entry->AsObject(), TEXT("name")));
	}
	const TArray<FString> Authored = AuthoredMeshNames(MaterialDoc);
	auto VertexLightKind = [&](const FString& Name, const TSharedPtr<FJsonObject>& Instance = nullptr)
	{
		if (BoolOf(Instance, TEXT("authoredColors")) || Authored.Contains(Name))
		{
			return AuthoredVertexColour;
		}
		return (BoolOf(Instance, TEXT("vertexLit")) || VertexLit.Contains(Name)) ? 1.0f : 0.0f;
	};

	// The library meshes by their node names (an asset path, with "#variant" for differently lit copies).
	TMap<FString, int32> MeshOfNode;
	TArray<int32> LevelMeshes, SkyMeshes;
	const auto Lighting = ReadJsonFile(RenderFolder / TEXT("lighting.json"));
	const auto Sky = ObjectOf(Lighting, TEXT("sky"));
	const FVector SkyEye = VectorOf(Sky, TEXT("location"), FVector::ZeroVector);
	TSet<int32> LibraryIndices;
	for (const TSharedPtr<FJsonValue>& NodeValue : ArrayOf(Gltf, TEXT("nodes")))
	{
		const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
		double MeshIndex = -1.0;
		if (!Node.IsValid() || !Node->TryGetNumberField(TEXT("mesh"), MeshIndex))
		{
			continue;
		}
		const TSharedPtr<FJsonObject> NodeRole = ObjectOf(ObjectOf(Node, TEXT("extras")), TEXT("svmap"));
		const FString RoleName = StringOf(NodeRole, TEXT("role"));
		if (RoleName == TEXT("bsp") || RoleName == TEXT("world"))
		{
			// The level's own surfaces: under the World node, or on it in the older layout (the synthetic maps).
			(BoolOf(NodeRole, TEXT("sky")) ? SkyMeshes : LevelMeshes).Add(static_cast<int32>(MeshIndex));
		}
		else
		{
			MeshOfNode.Add(StringOf(Node, TEXT("name")), static_cast<int32>(MeshIndex));
			if (RoleName == TEXT("libraryMesh"))
			{
				LibraryIndices.Add(static_cast<int32>(MeshIndex));
			}
		}
	}

	// Each glTF mesh becomes a static mesh, built once however many times it is placed.
	TMap<int32, UStaticMesh*> Built;
	int32 NumSections = 0;
	int32 NumTriangles = 0;
	auto MeshFor = [&](int32 Index) -> UStaticMesh*
	{
		if (UStaticMesh** Found = Built.Find(Index))
		{
			return *Found;
		}
		UStaticMesh* Mesh = nullptr;
		if (Asset.Meshes.IsValidIndex(Index))
		{
			const auto& JsonMeshes = ArrayOf(Gltf, TEXT("meshes"));
			const auto& Prims = ArrayOf(JsonMeshes[Index]->AsObject(), TEXT("primitives"));
			bool bVertexDirections = false;
			Mesh = BuildMapMesh(this, Asset.Meshes[Index], Prims, Asset.Accessors, !LevelMeshes.Contains(Index) && !SkyMeshes.Contains(Index),
				[this](int32 M) { return MaterialFor(M); }, NumSections, NumTriangles, bVertexDirections);
			if (Mesh && bVertexDirections) DirectionMeshes.Add(Mesh);
			if (Mesh) Meshes.Add(Mesh);
		}
		Built.Add(Index, Mesh);
		return Mesh;
	};

	// ---- the level's own surfaces, at the origin
	for (const int32 Index : LevelMeshes)
	{
		if (UStaticMesh* Mesh = MeshFor(Index))
		{
			PlaceMesh(Mesh, FString::Printf(TEXT("BSP_%s"), *Map), FTransform::Identity, false, VertexLightKind(Asset.Meshes[Index].Name));
		}
	}

	// Keep the sky zone at its authored viewing direction as the player moves. Uniform enlargement
	// preserves perspective about the SkyZoneInfo while putting the diorama beyond playable geometry.
	constexpr double SkyScale = 256.0;
	TSharedPtr<FJsonObject> SkyZone;
	for (const auto& Value : ArrayOf(Lighting, TEXT("zones")))
	{
		const auto Zone = Value->AsObject();
		if (Sky && NumberOf(Zone, TEXT("zoneIndex"), -2) == NumberOf(Sky, TEXT("zone"), -1))
			SkyZone = Zone;
	}
	// Fog distances are measured from the source eye before the diorama is enlarged. In maps
	// such as Aquarius the box is entirely behind the end of the fog and supplies a solid sky.
	const double FogEnd = FMath::Max(NumberOf(SkyZone, TEXT("fog_end")), NumberOf(SkyZone, TEXT("fog_start")));
	const FVector FogRGB = VectorOf(SkyZone, TEXT("fog_color"), FVector::ZeroVector);
	const FLinearColor FogTint(FColor(uint8(FogRGB.X), uint8(FogRGB.Y), uint8(FogRGB.Z)));
	auto PlaceSky = [&](int32 Index, UStaticMesh* Mesh, FTransform Frame)
	{
		if (!SkyRoot)
		{
			SkyRoot = NewObject<USceneComponent>(this, TEXT("SkyZone"));
			SkyRoot->SetupAttachment(GetRootComponent());
			SkyRoot->RegisterComponent();
			SetActorTickEnabled(true);
		}
		TSet<int32> ClearMaterials;
		if (BoolOf(SkyZone, TEXT("fog")) && FogEnd > 0)
		{
			for (const auto& Prim : Asset.Meshes[Index].Primitives)
			{
				if (!Prim.IsValid()) continue;
				TArray<FVector3f> Points;
				TArray<uint32> Triangles;
				Prim.GetPositions(Points);
				Prim.GetTriangleIndices(Triangles);
				for (int32 T = 0; T + 2 < Triangles.Num(); T += 3)
				{
					auto At = [&](int32 Corner) { return Frame.TransformPosition(FVector(Points[Triangles[Corner]]) * 100.0); };
					if (FVector::DistSquared(SkyEye, FMath::ClosestPointOnTriangleToPoint(SkyEye, At(T), At(T+1), At(T+2))) <= FMath::Square(FogEnd))
					{
						ClearMaterials.Add(Prim.MaterialIndex);
						break;
					}
				}
			}
		}
		Frame.SetTranslation((Frame.GetTranslation() - SkyEye) * SkyScale);
		Frame.SetScale3D(Frame.GetScale3D() * SkyScale);
		auto* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetupAttachment(SkyRoot);
		Part->SetRelativeTransform(Frame);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->bAffectDistanceFieldLighting = false;
		Part->bAffectDynamicIndirectLighting = false;
		Part->SetVisibleInRayTracing(false);
		for (int32 S = 0; S < Mesh->GetStaticMaterials().Num(); ++S)
		{
			const int32 Material = FCString::Atoi(*Mesh->GetStaticMaterials()[S].MaterialSlotName.ToString());
			if (auto* Mid = ScctMeshMaterial::CreateSlotOverride(*Part, S))
			{
				Mid->SetScalarParameterValue(TEXT("Emissive"), 1.f);
				if (BoolOf(SkyZone, TEXT("fog")) && FogEnd > 0 && !ClearMaterials.Contains(Material) && Mid->GetBlendMode() == BLEND_Opaque)
				{
					auto* White = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
					if (White) Mid->SetTextureParameterValue(TEXT("BaseColor"), White);
					Mid->SetVectorParameterValue(TEXT("Tint"), FogTint);
					Mid->SetScalarParameterValue(TEXT("SelfIllum"), 0.f);
					Mid->SetScalarParameterValue(TEXT("BakedFill"), 0.f);
				}
			}
		}
		Part->RegisterComponent();
	};
	for (int32 Index : SkyMeshes)
	{
		if (Sky && MeshFor(Index)) PlaceSky(Index, MeshFor(Index), FTransform::Identity);
	}

	// ---- every placed mesh: world = ((local - PrePivot) * Scale) rotated + Location
	int32 NumWithoutMesh = 0;
	int32 NumSkipped = 0;
	for (const TSharedPtr<FJsonValue>& Value : ArrayOf(ObjectOf(ObjectOf(Gltf, TEXT("extras")), TEXT("svmap")), TEXT("instances")))
	{
		const TSharedPtr<FJsonObject> Instance = Value->AsObject();
		// Hidden actors and projector copies are not ordinary scene geometry.
		if (!Instance.IsValid() || BoolOf(Instance, TEXT("hidden")) || Instance->HasField(TEXT("projectorOf")))
		{
			++NumSkipped;
			continue;
		}
		const FString MeshName = StringOf(Instance, TEXT("mesh"));
		const int32* Index = MeshOfNode.Find(MeshName);
		UStaticMesh* Mesh = Index != nullptr ? MeshFor(*Index) : nullptr;
		if (Mesh == nullptr)
		{
			++NumWithoutMesh;
			continue;
		}

		const TSharedPtr<FJsonObject> Rotation = ObjectOf(Instance, TEXT("rotation"));
		const FRotator Rotator = RotatorOfUnits(NumberOf(Rotation, TEXT("Pitch")), NumberOf(Rotation, TEXT("Yaw")), NumberOf(Rotation, TEXT("Roll")));
		const FVector Scale = VectorOf(Instance, TEXT("scale"), FVector::OneVector);
		FVector Location = VectorOf(Instance, TEXT("location"), FVector::ZeroVector);
		const FVector Pivot = VectorOf(Instance, TEXT("prePivot"), FVector::ZeroVector);
		Location -= Rotator.RotateVector(Pivot * Scale);

		if (BoolOf(Instance, TEXT("skyZone")))
		{
			if (Sky) PlaceSky(*Index, Mesh, FTransform(Rotator, Location, Scale));
			continue;
		}
		const bool bMovable = StringOf(Instance, TEXT("kind")) == TEXT("mover") || BoolOf(Instance, TEXT("gameplay"));
		AActor* Placed = PlaceMesh(Mesh, StringOf(Instance, TEXT("actor")), FTransform(Rotator, Location, Scale), bMovable, VertexLightKind(MeshName, Instance));
		if (const auto Soft = ObjectOf(Instance, TEXT("softBody")); Soft && Placed)
		{
			const FString Kind = StringOf(Soft, TEXT("kind"));
			Placed->Tags.Add(FName(*(TEXT("ScctSoftBody:") + (Kind.IsEmpty() ? FString(TEXT("patch")) : Kind))));
		}
	}

	// A render file without placed instances (the older layout of the synthetic maps): the static meshes of the
	// map's actor records, as the Godot client's MapBuilder.PlaceStaticMeshes places them.
	if (!ObjectOf(ObjectOf(Gltf, TEXT("extras")), TEXT("svmap")).IsValid())
	{
		auto PropVector = [](const TSharedPtr<FJsonObject>& Props, const TCHAR* Field, const FVector& Default)
		{
			const TSharedPtr<FJsonObject> Value = ObjectOf(Props, Field);
			return Value.IsValid() ? FVector(NumberOf(Value, TEXT("X")), NumberOf(Value, TEXT("Y")), NumberOf(Value, TEXT("Z"))) : Default;
		};
		// Held here: the loop would otherwise run over the array of a temporary that is already gone.
		const TSharedPtr<FJsonObject> Gameplay = ReadJsonFile(MapFolder / TEXT("sim/gameplay.json"));
		for (const TSharedPtr<FJsonValue>& Value : ArrayOf(Gameplay, TEXT("actors")))
		{
			const TSharedPtr<FJsonObject> Record = Value->AsObject();
			const TSharedPtr<FJsonObject> Props = ObjectOf(Record, TEXT("props"));
			const FString MeshAsset = StringOf(ObjectOf(Props, TEXT("StaticMesh")), TEXT("$asset"));
			if (MeshAsset.IsEmpty() || BoolOf(Props, TEXT("bHidden")))
			{
				continue;
			}
			const int32* Index = MeshOfNode.Find(MeshAsset);
			UStaticMesh* Mesh = Index != nullptr ? MeshFor(*Index) : nullptr;
			if (Mesh == nullptr)
			{
				++NumWithoutMesh;
				continue;
			}
			const TSharedPtr<FJsonObject> Rotation = ObjectOf(Props, TEXT("Rotation"));
			const FRotator Rotator = RotatorOfUnits(NumberOf(Rotation, TEXT("Pitch")), NumberOf(Rotation, TEXT("Yaw")), NumberOf(Rotation, TEXT("Roll")));
			const FVector Scale = PropVector(Props, TEXT("DrawScale3D"), FVector::OneVector) * NumberOf(Props, TEXT("DrawScale"), 1.0);
			const FVector Location = PropVector(Props, TEXT("Location"), FVector::ZeroVector) - Rotator.RotateVector(PropVector(Props, TEXT("PrePivot"), FVector::ZeroVector) * Scale);
			const bool bMovable = StringOf(Record, TEXT("class")).Contains(TEXT("Mover"));
			PlaceMesh(Mesh, StringOf(Record, TEXT("id")), FTransform(Rotator, Location, Scale), bMovable, VertexLightKind(MeshAsset));
		}
	}

	for (const auto& Entry : MeshOfNode)
	{
		// Particle and conveyor item meshes can be referenced without a placed instance.
		if (Built.Contains(Entry.Value) || LibraryIndices.Contains(Entry.Value))
		{
			UStaticMesh* Mesh = MeshFor(Entry.Value);
			Library.Add(FName(*Entry.Key), Mesh);
			FString Plain, Variant;
			if (Entry.Key.Split(TEXT("#"), &Plain, &Variant)) Library.FindOrAdd(FName(*Plain), Mesh);
		}
	}
	ConfigureSky(Map);
	PlaceLights(Lighting);

	UE_LOG(LogOpenSCCT, Log, TEXT("Runtime map %s: built %d meshes (%d sections, %d triangles) from %s with the engine's glTF reader (%d reader errors), %d materials, %d textures loaded (%d missing), placed %d meshes (%d level surfaces; %d instances skipped as hidden or projector copies, %d without a mesh), %d lights; read in %.2f s, built in %.2f s"),
		*Map, Meshes.Num(), NumSections, NumTriangles, *GlbPath, NumReaderErrors, Materials.Num(), Textures.Num(), NumTexturesMissing,
		NumPlaced, LevelMeshes.Num(), NumSkipped, NumWithoutMesh, NumLights, ReadSeconds, FPlatformTime::Seconds() - Start);
	return true;
}

ASkeletalMeshActor* AScctRuntimeMapActor::SpawnAnimatedProp(const FString& MeshName, const FTransform& Placement)
{
	// world.glb contains static geometry. The prop and its clips live in a separate file shared by map variants.
	FString Package, Name;
	if (!MeshName.Split(TEXT("."), &Package, &Name) || !FPackageName::IsValidLongPackageName(FString(TEXT("/Game")) / Package / Name)
		|| Package.Contains(TEXT("/")) || Name.Contains(TEXT("/")))
	{
		return nullptr;
	}
	const auto Document = ReadGlbJson(RenderFolder / TEXT("chars") / Package / (Name + TEXT(".glb")));
	if (!Document)
	{
		return nullptr;
	}
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FString> Folders;
	const FString Root = TEXT("/Game/Imported/Maps");
	Registry.GetSubPaths(Root, Folders, false);
	Folders.Sort();
	const FString OwnFolder = Root / FPaths::GetCleanFilename(FPaths::GetPath(RenderFolder));
	Folders.Remove(OwnFolder);
	Folders.Insert(OwnFolder, 0);
	const FString Suffix = FString(TEXT("Props")) / Package / Name / TEXT("SkeletalMeshes") / (Name + TEXT(".") + Name);
	USkeletalMesh* Mesh = nullptr;
	for (const FString& Folder : Folders)
	{
		const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(Folder / Suffix));
		if (Asset.IsValid() && (Mesh = Cast<USkeletalMesh>(Asset.GetAsset())) != nullptr)
		{
			break;
		}
	}
	if (!Mesh)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Runtime map: could not find the cooked skeletal prop %s"), *MeshName);
		return nullptr;
	}
	const auto Metadata = ObjectOf(ObjectOf(Document, TEXT("extras")), TEXT("svmap"));
	const FVector Origin = VectorOf(Metadata, TEXT("rotOrigin"), FVector::ZeroVector);
	const FQuat Rotation = Placement.GetRotation() * RotatorOfUnits(Origin.X, Origin.Y, Origin.Z).Quaternion();
	const FVector Scale = Placement.GetScale3D() * VectorOf(Metadata, TEXT("meshScale"), FVector::OneVector);
	const FTransform Transform(Rotation, Placement.GetLocation(), Scale);
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Actor = GetWorld()->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), Transform, Params);
	if (Actor)
	{
		USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetSkeletalMeshAsset(Mesh);
	}
	return Actor;
}

AActor* AScctRuntimeMapActor::PlaceMesh(UStaticMesh* Mesh, const FString& Name, const FTransform& Transform, bool bMovable, float VertexLightKind)
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, Params);
	if (Actor == nullptr)
	{
		return nullptr;
	}

	// A registered static component takes no new mesh: it is set up unregistered, then registered again.
	UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
	Component->UnregisterComponent();
	Component->SetMobility(bMovable ? EComponentMobility::Movable : EComponentMobility::Static);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); // the simulation owns collision
	Component->SetGenerateOverlapEvents(false);
	if (bMovable)
	{
		// The surface material's detail layer travels with what moves.
		Component->SetDefaultCustomPrimitiveDataFloat(LocalDetailSlot, 1.0f);
	}
	if (VertexLightKind != 0.0f)
	{
		Component->SetDefaultCustomPrimitiveDataFloat(BakedVertexSlot, VertexLightKind);
		if (DirectionMeshes.Contains(Mesh))
			Component->SetDefaultCustomPrimitiveDataFloat(ScctAuthored::DirectionFlagSlot, 1.0f);
	}
	Component->RegisterComponent();
	Actor->SetActorTransform(Transform);
	Actor->Tags.Add(FName(*(FString(TEXT("Scct:")) + Name)));
	++NumPlaced;
	return Actor;
}

// ---------------------------------------------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------------------------------------------

UMaterialInterface* AScctRuntimeMapActor::MaterialFor(int32 GltfMaterial)
{
	return Materials.IsValidIndex(GltfMaterial) ? Materials[GltfMaterial].Get() : nullptr;
}

UTexture2D* AScctRuntimeMapActor::TextureFor(const FString& RelativePath, bool bSRGB)
{
	if (RelativePath.IsEmpty())
	{
		return nullptr;
	}
	const FString KeyName = RelativePath + (bSRGB ? TEXT("") : TEXT("#linear"));
	if (const TObjectPtr<UTexture2D>* Found = Textures.Find(KeyName))
	{
		return Found->Get();
	}

	UTexture2D* Texture = nullptr;
	TArray<uint8> Compressed;
	const FString Path = RenderFolder / RelativePath;
	IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	if (FFileHelper::LoadFileToArray(Compressed, *Path))
	{
		const EImageFormat Format = Images.DetectImageFormat(Compressed.GetData(), Compressed.Num());
		const TSharedPtr<IImageWrapper> Wrapper = Format != EImageFormat::Invalid ? Images.CreateImageWrapper(Format) : nullptr;
		TArray<uint8> Raw;
		if (Wrapper.IsValid() && Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num()) && Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			int32 Width = Wrapper->GetWidth();
			int32 Height = Wrapper->GetHeight();
			Texture = NewObject<UTexture2D>(this, NAME_None, RF_Transient);
			FTexturePlatformData* Data = new FTexturePlatformData();
			Data->SizeX = Width;
			Data->SizeY = Height;
			Data->SetNumSlices(1);
			Data->PixelFormat = PF_B8G8R8A8;
			// A full mip chain, or distant surfaces shimmer.
			while (true)
			{
				FTexture2DMipMap* Mip = new FTexture2DMipMap(Width, Height, 1);
				Data->Mips.Add(Mip);
				Mip->BulkData.Lock(LOCK_READ_WRITE);
				FMemory::Memcpy(Mip->BulkData.Realloc(Raw.Num()), Raw.GetData(), Raw.Num());
				Mip->BulkData.Unlock();
				if (Width == 1 && Height == 1)
				{
					break;
				}
				int32 NextWidth = 0;
				int32 NextHeight = 0;
				Raw = HalfSize(Raw, Width, Height, NextWidth, NextHeight);
				Width = NextWidth;
				Height = NextHeight;
			}
			Texture->SetPlatformData(Data);
			Texture->bNotOfflineProcessed = true;
			Texture->NeverStream = true;
			Texture->SRGB = bSRGB;
			Texture->AddressX = TA_Wrap;
			Texture->AddressY = TA_Wrap;
			Texture->Filter = TF_Trilinear;
			Texture->UpdateResource();
		}
	}
	if (Texture == nullptr)
	{
		++NumTexturesMissing;
		UE_LOG(LogOpenSCCT, Verbose, TEXT("Runtime map: texture %s could not be read"), *Path);
	}
	Textures.Add(KeyName, Texture);
	return Texture;
}

void AScctRuntimeMapActor::BuildMaterials(const TSharedPtr<FJsonObject>& Gltf, const TSharedPtr<FJsonObject>& MaterialDoc)
{
	const TSharedPtr<FJsonObject> Records = ObjectOf(MaterialDoc, TEXT("materials"));
	TSet<FString> Additive;
	for (const TSharedPtr<FJsonValue>& Entry : ArrayOf(MaterialDoc, TEXT("additive")))
	{
		const TSharedPtr<FJsonObject> Object = Entry->AsObject();
		Additive.Add(Key(StringOf(Object, TEXT("key"))));
		Additive.Add(Key(StringOf(Object, TEXT("name"))));
	}
	Additive.Remove(FString());
	TMap<FString, FString> LightmapOf;
	for (const TSharedPtr<FJsonValue>& Entry : ArrayOf(MaterialDoc, TEXT("lightmapped")))
	{
		const TSharedPtr<FJsonObject> Object = Entry->AsObject();
		LightmapOf.Add(Key(StringOf(Object, TEXT("name"))), StringOf(Object, TEXT("lightmap")));
	}

	const TArray<TSharedPtr<FJsonValue>>& Images = ArrayOf(Gltf, TEXT("images"));
	const TArray<TSharedPtr<FJsonValue>>& GltfTextures = ArrayOf(Gltf, TEXT("textures"));
	auto ImageOfTexture = [&](int32 TextureIndex) -> FString
	{
		if (!GltfTextures.IsValidIndex(TextureIndex))
		{
			return FString();
		}
		const int32 Source = static_cast<int32>(NumberOf(GltfTextures[TextureIndex]->AsObject(), TEXT("source"), -1.0));
		return Images.IsValidIndex(Source) ? StringOf(Images[Source]->AsObject(), TEXT("uri")) : FString();
	};

	// The surface material under each blend mode the original used, two-sided or not, lit or not.
	UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, SurfaceMaterial);
	TMap<FString, UMaterialInterface*> Parents;
	int32 NumParentsMissing = 0;
	auto ParentOf = [&](const TCHAR* Blend, bool bUnlit, bool bTwoSided) -> UMaterialInterface*
	{
		const FString Name = FString::Printf(TEXT("MI_ScctRuntime_%s_%s%s"), Blend, bUnlit ? TEXT("Unlit") : TEXT("Lit"), bTwoSided ? TEXT("_TwoSided") : TEXT(""));
		if (UMaterialInterface** Found = Parents.Find(Name))
		{
			return *Found;
		}
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), MaterialFolder, *Name, *Name));
		if (Parent == nullptr)
		{
			++NumParentsMissing;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Runtime map: material %s/%s is missing (run Tools/Import/Make-RuntimeMap.ps1); the plain surface material stands in"), MaterialFolder, *Name);
			Parent = Surface;
		}
		Parents.Add(Name, Parent);
		return Parent;
	};

	const TArray<TSharedPtr<FJsonValue>>& GltfMaterials = ArrayOf(Gltf, TEXT("materials"));
	TMap<FString, int32> Counts;
	for (const TSharedPtr<FJsonValue>& Value : GltfMaterials)
	{
		const TSharedPtr<FJsonObject> GltfMaterial = Value->AsObject();
		const FString Name = StringOf(GltfMaterial, TEXT("name"));
		const TSharedPtr<FJsonObject> Record = ObjectOf(Records, *Name);
		const TSharedPtr<FJsonObject> Flags = ObjectOf(Record, TEXT("flags"));
		const FString Source = StringOf(Record, TEXT("source")).IsEmpty() ? Name : StringOf(Record, TEXT("source"));

		// import_map.py build_materials, for the surface kind (the class table of glass and water is the editor's).
		const bool bAdditive = Additive.Contains(Key(Name));
		const bool bUnlit = BoolOf(Flags, TEXT("unlit")) || bAdditive;
		const bool bTranslucent = BoolOf(Flags, TEXT("translucent"));
		const bool bMasked = BoolOf(Flags, TEXT("masked"));
		const bool bTwoSided = BoolOf(Flags, TEXT("two_sided"));
		const TSharedPtr<FJsonObject> Flipbook = ObjectOf(Flags, TEXT("flipbook"));
		const bool bGlowCard = bAdditive && !Flipbook.IsValid() && ContainsAny(Name, { TEXT("beam"), TEXT("rayon"), TEXT("flare"), TEXT("lenseffect"), TEXT("dca_fx") });
		const bool bCaustics = bAdditive && Source.Contains(TEXT("caustic"), ESearchCase::IgnoreCase);
		const TCHAR* Blend = bAdditive ? TEXT("Additive") : bTranslucent ? TEXT("Translucent") : bMasked ? TEXT("Masked") : TEXT("Opaque");
		Counts.FindOrAdd(Blend)++;

		UMaterialInterface* Parent = ParentOf(Blend, bUnlit, bTwoSided);
		UMaterialInstanceDynamic* Instance = Parent != nullptr ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (Instance == nullptr)
		{
			Materials.Add(nullptr);
			continue;
		}

		FString TextureFile = StringOf(Flags, TEXT("texture"));
		if (TextureFile.IsEmpty())
		{
			const TSharedPtr<FJsonObject> Pbr = ObjectOf(GltfMaterial, TEXT("pbrMetallicRoughness"));
			TextureFile = ImageOfTexture(static_cast<int32>(NumberOf(ObjectOf(Pbr, TEXT("baseColorTexture")), TEXT("index"), -1.0)));
		}
		if (UTexture2D* Base = TextureFor(TextureFile))
		{
			Instance->SetTextureParameterValue(TEXT("BaseColor"), Base);
		}
		Instance->SetScalarParameterValue(TEXT("Emissive"), bGlowCard ? GlowCardBrightness : bCaustics ? EmissiveCaustics
			: bAdditive ? EmissiveAdditive : bUnlit ? EmissiveUnlit : 0.0f);

		const FVector2D Pan = PanSpeed(ObjectOf(Flags, TEXT("uv_anim")));
		Instance->SetScalarParameterValue(TEXT("PanU"), Pan.X);
		Instance->SetScalarParameterValue(TEXT("PanV"), Pan.Y);
		if (Flipbook.IsValid())
		{
			if (UTexture2D* Sheet = TextureFor(StringOf(Flipbook, TEXT("texture"))))
			{
				Instance->SetTextureParameterValue(TEXT("BaseColor"), Sheet);
				Instance->SetScalarParameterValue(TEXT("FlipColumns"), NumberOf(Flipbook, TEXT("cols"), 1.0));
				Instance->SetScalarParameterValue(TEXT("FlipRows"), NumberOf(Flipbook, TEXT("rows"), 1.0));
				Instance->SetScalarParameterValue(TEXT("FlipRate"), NumberOf(Flipbook, TEXT("fps"), 0.0));
				Instance->SetScalarParameterValue(TEXT("FlipFrames"), NumberOf(Flipbook, TEXT("frames"), 1.0));
			}
		}

		if (!bUnlit)
		{
			// The self-lit part: lamp glass, screens, lit windows.
			FString SelfLit;
			if (Flags.IsValid() && Flags->TryGetStringField(TEXT("self_illum"), SelfLit))
			{
				if (UTexture2D* Glow = TextureFor(SelfLit))
				{
					Instance->SetTextureParameterValue(TEXT("SelfIllumTexture"), Glow);
					FString Mask;
					if (Flags->TryGetStringField(TEXT("self_illum_mask"), Mask))
					{
						if (UTexture2D* MaskTexture = TextureFor(Mask))
						{
							Instance->SetTextureParameterValue(TEXT("SelfIllumMask"), MaskTexture);
						}
					}
					Instance->SetScalarParameterValue(TEXT("SelfIllum"), EmissiveSelfIllum);
				}
			}

			// The original's baked light as a faint fill: its lightmap page on the level surfaces (second UV set),
			// and the baked vertex colours of placed meshes (custom primitive data slot 1 on their components).
			Instance->SetScalarParameterValue(TEXT("BakedFill"), FMath::Max(CVarRuntimeBakedFill.GetValueOnGameThread(), 0.0f));
			Instance->SetScalarParameterValue(TEXT("AmbientFloor"), AmbientFloor);
			if (const FString* Page = LightmapOf.Find(Key(Name)))
			{
				if (UTexture2D* Lightmap = TextureFor(*Page))
				{
					Instance->SetTextureParameterValue(TEXT("Lightmap"), Lightmap);
				}
			}
		}
		if (Flags && Flags->HasField(TEXT("authored")))
		{
			Instance = ScctAuthored::MaterialFor(this, Flags, LightmapOf.FindRef(Key(Name)), RenderFolder, Textures, Name, Instance);
		}
		Materials.Add(Instance);
		SurfaceByName.Add(FName(*Name), Instance);
		SurfaceByName.FindOrAdd(FName(*Source), Instance);
	}

	TArray<FString> Summary;
	for (const TPair<FString, int32>& Count : Counts)
	{
		Summary.Add(FString::Printf(TEXT("%s %d"), *Count.Key, Count.Value));
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Runtime map: %d materials (%s)%s"), Materials.Num(), *FString::Join(Summary, TEXT(", ")),
		NumParentsMissing > 0 ? TEXT("; some blend modes fell back to the plain surface material") : TEXT(""));
}

// ---------------------------------------------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------------------------------------------

void AScctRuntimeMapActor::PlaceLights(const TSharedPtr<FJsonObject>& Lighting)
{
	// Every light of the map, as import_map.py places them: the original's falloff kept (a little tighter), its
	// brightness in the units the post-process exposure was set for, its colour partly given up toward white, a
	// light of small reach weakened and softened, and the lights the baked fill already stands for held back.
	// That is the released picture (scct.Light.Plan 0). Under the lighting plan the lights placed here are dressed
	// again when the match starts, by their tags, exactly as an imported level's are (Play/ScctLightRules.h): the
	// strengths, shares and falloffs below are replaced there, and the materials' BakedFill and AmbientFloor are
	// ignored by the surface material.
	int32 NumToPlace = 0;
	int32 NumBaking = 0;
	for (const TSharedPtr<FJsonValue>& Value : ArrayOf(Lighting, TEXT("lights")))
	{
		const TSharedPtr<FJsonObject> Light = Value->AsObject();
		if (Light.IsValid() && !BoolOf(Light, TEXT("hidden")))
		{
			++NumToPlace;
			NumBaking += BoolOf(Light, TEXT("apply_static")) ? 1 : 0;
		}
	}
	const bool bBakingLampsKept = NumBaking * 2 > NumToPlace;
	const float StaticShare = FMath::Max(CVarRuntimeStaticLights.GetValueOnGameThread(), 0.0f);
	const float FillShare = bBakingLampsKept ? FMath::Max(CVarRuntimeCharacterFills.GetValueOnGameThread(), 0.0f) : 1.0f;
	UE_LOG(LogOpenSCCT, Log, TEXT("Runtime map: %d of %d lights are lamps the lightmaps were baked from (share %.2f); characters-only lights keep %.2f"),
		NumBaking, NumToPlace, StaticShare, FillShare);

	for (const TSharedPtr<FJsonValue>& Value : ArrayOf(Lighting, TEXT("lights")))
	{
		const TSharedPtr<FJsonObject> Light = Value->AsObject();
		if (!Light.IsValid() || BoolOf(Light, TEXT("hidden")))
		{
			continue;
		}
		const bool bSpot = StringOf(Light, TEXT("light_type")) == TEXT("spot");
		const FVector Location = VectorOf(Light, TEXT("location"), FVector::ZeroVector);
		const FVector Units = VectorOf(Light, TEXT("rotation"), FVector::ZeroVector);
		const FRotator Rotation = RotatorOfUnits(Units.X, Units.Y, Units.Z);

		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = bSpot
			? static_cast<AActor*>(GetWorld()->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Location, Rotation, Params))
			: static_cast<AActor*>(GetWorld()->SpawnActor<APointLight>(APointLight::StaticClass(), Location, Rotation, Params));
		if (Actor == nullptr)
		{
			continue;
		}
		Actor->SetActorRotation(Rotation);
		Actor->Tags.Add(FName(*(FString(TEXT("Scct:")) + StringOf(Light, TEXT("name")))));
		FString Tag;
		if (Light->TryGetStringField(TEXT("tag"), Tag))
		{
			Actor->Tags.Add(FName(*(FString(TEXT("ScctTag:")) + Tag)));
		}

		UPointLightComponent* Component = Actor->FindComponentByClass<UPointLightComponent>();
		if (Component == nullptr)
		{
			continue;
		}
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetUseInverseSquaredFalloff(false);
		const float Radius = RadiusUnits * (NumberOf(Light, TEXT("radius")) + 1.0);
		const float Small = FMath::Min(Radius / SmallLightRadius, 1.0f);
		Component->SetLightFalloffExponent(LightFalloff + (SmallLightFalloff - LightFalloff) * (1.0f - Small));
		const float Share = BoolOf(Light, TEXT("apply_static")) ? StaticShare : BoolOf(Light, TEXT("apply_ingame")) ? FillShare : 1.0f;
		Component->SetIntensity(NumberOf(Light, TEXT("brightness")) * LightPerBrightness * Small * Share);
		FVector Colour = VectorOf(Light, TEXT("color"), FVector::OneVector);
		Colour = FVector(FMath::Pow(Colour.X, 2.2), FMath::Pow(Colour.Y, 2.2), FMath::Pow(Colour.Z, 2.2));
		const double Grey = 0.2126 * Colour.X + 0.7152 * Colour.Y + 0.0722 * Colour.Z;
		Colour = FVector(Grey) + (Colour - FVector(Grey)) * LightSaturation;
		Component->SetLightColor(FLinearColor(Colour.X, Colour.Y, Colour.Z));
		Component->SetAttenuationRadius(Radius);
		Component->SetSourceRadius(6.0f);
		Component->SetSoftSourceRadius(12.0f);
		Component->SetCastShadows(true);
		Component->SetVolumetricScatteringIntensity(bSpot ? 1.5f : 0.6f);
		if (USpotLightComponent* Spot = Cast<USpotLightComponent>(Component))
		{
			// The original's cone: full angle 1.15 * acos((1 - LightCone/256)^2); a softer edge than its own.
			const double Cone = FMath::Min(NumberOf(Light, TEXT("cone")), 255.0);
			const double Full = FMath::RadiansToDegrees(1.15 * FMath::Acos(FMath::Square(1.0 - Cone / 256.0)));
			const float Outer = FMath::Clamp(static_cast<float>(Full * 0.5), 5.0f, 80.0f);
			Spot->SetOuterConeAngle(Outer);
			Spot->SetInnerConeAngle(Outer * 0.45f);
		}
		// LightType 0 is "none": a light that exists to be switched on by the simulation.
		if (!BoolOf(Light, TEXT("initially_on"), true) || NumberOf(Light, TEXT("ltype"), 1.0) == 0.0)
		{
			Component->SetVisibility(false);
		}
		++NumLights;
	}
}
