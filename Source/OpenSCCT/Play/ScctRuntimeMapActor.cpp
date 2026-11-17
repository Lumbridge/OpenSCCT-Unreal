#include "Play/ScctRuntimeMapActor.h"

#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
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

namespace
{
	// Tools/Import/import_map.py, so a map looks the same built here as imported in the editor.
	constexpr float RadiusUnits = 25.0f;              // world radius = 25 * (LightRadius + 1), as the original engine
	constexpr float LightFalloff = 1.6f;              // light = (1 - (d / R)^2) ^ this
	constexpr float LightPerBrightness = 5.0f / 255.0f;
	constexpr float LightSaturation = 0.7f;
	constexpr float EmissiveUnlit = 0.9f;             // screens, lamps and signs that were unlit in the original
	constexpr float EmissiveAdditive = 0.35f;         // flare and glow sprites, holograms
	constexpr float EmissiveSelfIllum = 2.0f;         // the self-lit part of a lit surface
	constexpr float GlowCardBrightness = 0.2f;        // beam and flare cards modelled into the level
	constexpr float EmissiveCaustics = 0.08f;         // additive sheets drawing light under water
	constexpr float BakedFill = 0.5f;                 // the share of the original's baked light given back

	// Custom primitive data slots of the surface material (Tools/Import/scct_materials.py).
	constexpr int32 LocalDetailSlot = 0;
	constexpr int32 BakedVertexSlot = 1;

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
	if (Package != UScctOnlineSubsystem::RuntimeLevel)
	{
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

// ---------------------------------------------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------------------------------------------

AScctRuntimeMapActor::AScctRuntimeMapActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
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

	// The library meshes by their node names (an asset path, with "#variant" for differently lit copies).
	TMap<FString, int32> MeshOfNode;
	TArray<int32> LevelMeshes;
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
			// The sky zone's own surfaces are not drawn: the level has a real sky.
			if (!BoolOf(NodeRole, TEXT("sky")))
			{
				LevelMeshes.Add(static_cast<int32>(MeshIndex));
			}
		}
		else
		{
			MeshOfNode.Add(StringOf(Node, TEXT("name")), static_cast<int32>(MeshIndex));
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
			FMeshDescription Description;
			FStaticMeshAttributes Attributes(Description);
			Attributes.Register();
			GLTF::FMeshFactory Factory;
			// Metres to centimetres; the reader already turned glTF's Y up into Unreal's Z up.
			Factory.SetUniformScale(100.0f);
			Factory.FillMeshDescription(Asset.Meshes[Index], FTransform::Identity, &Description);
			if (Description.Triangles().Num() > 0)
			{
				Mesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
				const TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
				for (const FPolygonGroupID Group : Description.PolygonGroups().GetElementIDs())
				{
					// The engine's mesh factory names each slot after the glTF material index.
					const FName Slot = SlotNames[Group];
					Mesh->GetStaticMaterials().Add(FStaticMaterial(MaterialFor(FCString::Atoi(*Slot.ToString())), Slot, Slot));
				}
				UStaticMesh::FBuildMeshDescriptionsParams Params;
				Params.bFastBuild = true;
				Params.bMarkPackageDirty = false;
				Params.bCommitMeshDescription = false;
				Params.bBuildSimpleCollision = false;
				Mesh->BuildFromMeshDescriptions({ &Description }, Params);
				Meshes.Add(Mesh);
				NumSections += Description.PolygonGroups().Num();
				NumTriangles += Description.Triangles().Num();
			}
		}
		Built.Add(Index, Mesh);
		return Mesh;
	};

	// ---- the level's own surfaces, at the origin
	for (const int32 Index : LevelMeshes)
	{
		if (UStaticMesh* Mesh = MeshFor(Index))
		{
			PlaceMesh(Mesh, FString::Printf(TEXT("BSP_%s"), *Map), FTransform::Identity, false, false);
		}
	}

	// ---- every placed mesh: world = ((local - PrePivot) * Scale) rotated + Location
	int32 NumWithoutMesh = 0;
	int32 NumSkipped = 0;
	for (const TSharedPtr<FJsonValue>& Value : ArrayOf(ObjectOf(ObjectOf(Gltf, TEXT("extras")), TEXT("svmap")), TEXT("instances")))
	{
		const TSharedPtr<FJsonObject> Instance = Value->AsObject();
		// Hidden actors, the sky zone's dioramas and the copies of surfaces that carry a projected texture (the
		// importer brings those back as decals) are not placed.
		if (!Instance.IsValid() || BoolOf(Instance, TEXT("hidden")) || BoolOf(Instance, TEXT("skyZone")) || Instance->HasField(TEXT("projectorOf")))
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

		const bool bMovable = StringOf(Instance, TEXT("kind")) == TEXT("mover") || BoolOf(Instance, TEXT("gameplay"));
		const bool bVertexLit = BoolOf(Instance, TEXT("vertexLit")) || VertexLit.Contains(MeshName);
		PlaceMesh(Mesh, StringOf(Instance, TEXT("actor")), FTransform(Rotator, Location, Scale), bMovable, bVertexLit);
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
			PlaceMesh(Mesh, StringOf(Record, TEXT("id")), FTransform(Rotator, Location, Scale), bMovable, false);
		}
	}

	PlaceLights(ReadJsonFile(RenderFolder / TEXT("lighting.json")));

	UE_LOG(LogOpenSCCT, Log, TEXT("Runtime map %s: built %d meshes (%d sections, %d triangles) from %s with the engine's glTF reader (%d reader errors), %d materials, %d textures loaded (%d missing), placed %d meshes (%d level surfaces; %d instances skipped as hidden, sky zone or projector copies, %d without a mesh), %d lights; read in %.2f s, built in %.2f s"),
		*Map, Meshes.Num(), NumSections, NumTriangles, *GlbPath, NumReaderErrors, Materials.Num(), Textures.Num(), NumTexturesMissing,
		NumPlaced, LevelMeshes.Num(), NumSkipped, NumWithoutMesh, NumLights, ReadSeconds, FPlatformTime::Seconds() - Start);
	return true;
}

AActor* AScctRuntimeMapActor::PlaceMesh(UStaticMesh* Mesh, const FString& Name, const FTransform& Transform, bool bMovable, bool bVertexLit)
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
	if (bVertexLit)
	{
		Component->SetDefaultCustomPrimitiveDataFloat(BakedVertexSlot, 1.0f);
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
			Instance->SetScalarParameterValue(TEXT("BakedFill"), BakedFill);
			if (const FString* Page = LightmapOf.Find(Key(Name)))
			{
				if (UTexture2D* Lightmap = TextureFor(*Page))
				{
					Instance->SetTextureParameterValue(TEXT("Lightmap"), Lightmap);
				}
			}
		}
		Materials.Add(Instance);
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
	// brightness in the units the post-process exposure was set for, its colour partly given up toward white.
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
		Component->SetLightFalloffExponent(LightFalloff);
		Component->SetIntensity(NumberOf(Light, TEXT("brightness")) * LightPerBrightness);
		FVector Colour = VectorOf(Light, TEXT("color"), FVector::OneVector);
		Colour = FVector(FMath::Pow(Colour.X, 2.2), FMath::Pow(Colour.Y, 2.2), FMath::Pow(Colour.Z, 2.2));
		const double Grey = 0.2126 * Colour.X + 0.7152 * Colour.Y + 0.0722 * Colour.Z;
		Colour = FVector(Grey) + (Colour - FVector(Grey)) * LightSaturation;
		Component->SetLightColor(FLinearColor(Colour.X, Colour.Y, Colour.Z));
		Component->SetAttenuationRadius(RadiusUnits * (NumberOf(Light, TEXT("radius")) + 1.0));
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
