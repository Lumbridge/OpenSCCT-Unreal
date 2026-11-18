#include "Play/ScctMapLightSprites.h"

#include "Components/LightComponent.h"
#include "Engine/Light.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Hud/ScctVisionSubsystem.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "OpenSCCT.h"
#include "Online/ScctMapFolders.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Play/ScctSessionSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonSerializer.h"

namespace ScctMapSprites
{
	using FObject = TSharedPtr<FJsonObject>;

	FObject Document(const FString& Path)
	{
		FString Text;
		FObject Object;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		}
		return Object;
	}

	FObject Child(const FObject& Object, const TCHAR* Key)
	{
		const FObject* Result;
		return Object && Object->TryGetObjectField(Key, Result) ? *Result : nullptr;
	}

	FString String(const FObject& Object, const TCHAR* Key)
	{
		FString Value;
		if (Object) Object->TryGetStringField(Key, Value);
		return Value;
	}

	double Number(const FObject& Object, const TCHAR* Key, double Fallback)
	{
		double Value;
		return Object && Object->TryGetNumberField(Key, Value) ? Value : Fallback;
	}

	bool Flag(const FObject& Object, const TCHAR* Key, bool Fallback = false)
	{
		bool Value;
		return Object && Object->TryGetBoolField(Key, Value) ? Value : Fallback;
	}

	FVector Location(const FObject& Object)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values;
		if (!Object || !Object->TryGetArrayField(TEXT("location"), Values) || Values->Num() != 3) return FVector::ZeroVector;
		return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
	}

	FString Blend(const FObject& Props, const FObject& Sprite)
	{
		switch (static_cast<int32>(Number(Props, TEXT("Style"), 1)))
		{
		case 2: return TEXT("Masked");
		case 3: case 6: return TEXT("Add");
		case 4: return TEXT("Modulate");
		case 5: return TEXT("Alpha");
		}
		const FString Mode = String(Sprite, TEXT("blend"));
		if (Mode == TEXT("masked")) return TEXT("Masked");
		if (Mode == TEXT("translucent")) return TEXT("Alpha");
		if (Mode == TEXT("additive")) return TEXT("Add");
		if (Mode == TEXT("modulate") || Mode == TEXT("darken")) return TEXT("Modulate");
		return TEXT("Opaque");
	}

	struct FBatch
	{
		UMaterialInstanceDynamic* Material = nullptr;
		USceneComponent* Parent = nullptr;
		bool bSky = false;
		FProcMeshSection Section;

		void Add(const FVector& Centre, const FVector2D& Size)
		{
			const uint32 Base = Section.ProcVertexBuffer.Num();
			const FVector2D Corners[] = {{0,0}, {1,0}, {1,1}, {0,1}};
			for (const FVector2D& UV : Corners)
			{
				FProcMeshVertex& Vertex = Section.ProcVertexBuffer.AddDefaulted_GetRef();
				Vertex.UV0 = UV;
				Vertex.UV1 = FVector2D((UV.X - 0.5) * Size.X, (0.5 - UV.Y) * Size.Y);
				Vertex.Position = Centre + FVector(0, Vertex.UV1.X, Vertex.UV1.Y);
			}
			Section.ProcIndexBuffer.Append({Base, Base+1, Base+2, Base, Base+2, Base+3});
			// Shader billboarding can rotate the quad outside its CPU plane.
			const FVector Extent(Size.Size() * 0.5);
			Section.SectionLocalBox += Centre - Extent;
			Section.SectionLocalBox += Centre + Extent;
		}
	};
}

bool UScctMapLightSprites::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UScctMapLightSprites::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	auto* Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	Collection.InitializeDependency<UScctLevelStateSubsystem>();
	if (!Session) return;
	bPendingLoad = true;
	Session->OnMatchStopped.AddUObject(this, &UScctMapLightSprites::Clear);
	Session->OnFrame.AddUObject(this, &UScctMapLightSprites::Frame);
}

void UScctMapLightSprites::Deinitialize()
{
	if (auto* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->OnMatchStopped.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	Clear();
	Super::Deinitialize();
}

void UScctMapLightSprites::Clear()
{
	for (auto Weak : ReplacedCoronas)
		if (auto* Component = Weak.Get()) Component->SetRenderInMainPass(true);
	for (const auto& Pair : OriginalScatter)
		if (auto* Light = Pair.Key.Get()) Light->SetVolumetricScatteringIntensity(Pair.Value);
	if (auto* Host = SpriteHost.Get()) Host->Destroy();
	SpriteHost.Reset();
	ReplacedCoronas.Reset();
	OriginalScatter.Reset();
	SkyGroups.Reset();
	bPendingLoad = true;
}

void UScctMapLightSprites::Frame(float)
{
	if (bPendingLoad)
	{
		bPendingLoad = false;
		Load();
	}
	if (!SpriteHost.IsValid()) return;
	const auto* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>();
	SpriteHost->SetActorHiddenInGame(Vision && Vision->ShouldHideCoronasAndSky());
	const auto& View = GetWorld()->GetSubsystem<UScctSessionSubsystem>()->GetView();
	if (View.Valid)
		for (auto Weak : SkyGroups)
			if (auto* Group = Weak.Get()) Group->SetWorldLocation(FVector(View.X, View.Y, View.Z));
}

void UScctMapLightSprites::Load()
{
	using namespace ScctMapSprites;
	UWorld* World = GetWorld();
	const FString Map = World->GetSubsystem<UScctSessionSubsystem>()->GetSessionInfo().Map;
	const FString Folder = ScctMapFolders::FolderOf(Map);
	if (Folder.IsEmpty()) return;
	const FString Render = FPaths::ConvertRelativePathToFull(Folder / TEXT("render"));
	const FObject Lighting = Document(Render / TEXT("lighting.json"));
	const TArray<TSharedPtr<FJsonValue>>* Lights;
	if (!Lighting || !Lighting->TryGetArrayField(TEXT("lights"), Lights)) return;

	const FObject Beams = Document(Render / TEXT("lightbeams.json"));
	const TArray<TSharedPtr<FJsonValue>>* Rows;
	TMap<FString, float> Scatter;
	if (Beams && Beams->TryGetArrayField(TEXT("beams"), Rows))
	{
		for (const auto& Value : *Rows)
		{
			const FObject Beam = Value->AsObject();
			if (!Flag(Beam, TEXT("lightOn"), true)) continue;
			const double Strength = 4 * FMath::Sqrt(FMath::Max(Number(Beam, TEXT("alpha"), 1), 1.0));
			Scatter.Add(String(Beam, TEXT("name")), Strength * Number(Beam, TEXT("falloffScale"), 1));
		}
	}

	const auto* Level = World->GetSubsystem<UScctLevelStateSubsystem>();
	const FVector SkyEye = Location(Child(Lighting, TEXT("sky")));
	const FString SharedRender = FPaths::ConvertRelativePathToFull(FOpenSCCTModule::FindContentRoot() / TEXT("shared/render"));
	TOptional<TMap<FName, FString>> SharedTextures;
	TMap<FString, UTexture2D*> Textures;
	TMap<FString, UMaterialInstanceDynamic*> Materials;
	TMap<FString, FBatch> Batches;
	TSet<FName> LegacyCards;
	for (const auto& Value : *Lights)
	{
		const FObject Light = Value->AsObject();
		if (!Light) continue;
		const FString Name = String(Light, TEXT("name"));
		if (Name.IsEmpty()) continue;
		AActor* Placed = Level ? Level->FindLevelActor(Name) : nullptr;
		if (const float* Strength = Scatter.Find(Name))
		{
			if (auto* Lamp = Cast<ALight>(Placed))
			{
				ULightComponent* Component = Lamp->GetLightComponent();
				OriginalScatter.Add(Component, Component->VolumetricScatteringIntensity);
				const float Gain = String(Light, TEXT("echelon_texture_uri")).IsEmpty() ? 1.f : 0.3f;
				Component->SetVolumetricScatteringIntensity(Number(Light, TEXT("effect"), 0) == 22
					? FMath::Clamp(*Strength * Gain, 0.f, 16.f) : 0.f);
			}
		}

		const FObject Props = Child(Light, TEXT("props"));
		const FString Source = String(Child(Props, TEXT("Texture")), TEXT("$asset"));
		if (String(Light, TEXT("class")) != TEXT("Light") || Light->HasField(TEXT("corona")) ||
			Flag(Props, TEXT("bCorona")) || Flag(Props, TEXT("bHidden"), true) ||
			Number(Props, TEXT("DrawType"), 1) != 1 || Source.IsEmpty()) continue;
		const FObject Sprite = Child(Light, TEXT("sprite"));
		FString Uri = String(Sprite, TEXT("texture_uri"));
		const FString* TextureRoot = &Render;
		if (Uri.IsEmpty())
		{
			// Older maps reference the shared texture by package and object name.
			if (!SharedTextures.IsSet())
			{
				SharedTextures.Emplace();
				const FObject Table = Child(Document(SharedRender / TEXT("render.json")), TEXT("textures"));
				if (Table)
					for (const auto& Entry : Table->Values)
						if (Entry.Value->Type == EJson::String) SharedTextures->Add(FName(*FString(Entry.Key)), Entry.Value->AsString());
			}
			Uri = SharedTextures->FindRef(FName(*Source));
			int32 FirstDot, LastDot;
			if (Uri.IsEmpty() && Source.FindChar('.', FirstDot) && Source.FindLastChar('.', LastDot) && LastDot > FirstDot)
				Uri = SharedTextures->FindRef(FName(*(Source.Left(FirstDot) + Source.Mid(LastDot))));
			TextureRoot = &SharedRender;
		}
		if (Uri.IsEmpty() || !FPaths::IsRelative(Uri)) continue;
		FString File = FPaths::ConvertRelativePathToFull(*TextureRoot / Uri);
		if (!FPaths::CollapseRelativeDirectories(File) || !FPaths::IsUnderDirectory(File, *TextureRoot)) continue;
		UTexture2D** Found = Textures.Find(File);
		UTexture2D* Texture = Found ? *Found : Textures.Add(File, FImageUtils::ImportFileAsTexture2D(File));
		if (!Texture) continue;

		const FString Suffix = Blend(Props, Sprite);
		const FString Key = Suffix + TEXT(":") + File;
		auto*& Material = Materials.FindOrAdd(Key);
		if (!Material)
		{
			const FString Asset = TEXT("/Game/OpenSCCT/Effects/M_ScctLightSprite") + Suffix;
			auto* Parent = LoadObject<UMaterialInterface>(nullptr, *Asset);
			if (!Parent) continue;
			Material = UMaterialInstanceDynamic::Create(Parent, World);
			Material->SetTextureParameterValue(TEXT("Texture"), Texture);
		}

		const bool bSky = Flag(Light, TEXT("sky_zone"));
		FVector Centre = Location(Light);
		const FObject Scale3D = Child(Props, TEXT("DrawScale3D"));
		const double Width = Number(Sprite, TEXT("width"), Texture->GetSizeX()) * Number(Props, TEXT("DrawScale"), 1);
		FVector2D Size = Width * FVector2D(Number(Scale3D, TEXT("X"), 1), Number(Scale3D, TEXT("Y"), 1));
		USceneComponent* Anchor = !bSky && Placed ? Placed->GetRootComponent() : nullptr;
		if (bSky)
		{
			Centre -= SkyEye;
			if (Centre.IsNearlyZero()) continue;
			const double Projection = 250000.0 / Centre.Size();
			Centre *= Projection;
			Size *= Projection;
		}
		else if (Anchor)
		{
			Centre -= Anchor->GetComponentLocation();
		}
		FBatch& Batch = Batches.FindOrAdd((bSky ? TEXT("sky:") : TEXT("world:")) + Key + (Anchor ? TEXT(":") + Name : FString()));
		Batch.Material = Material;
		Batch.Parent = Anchor;
		Batch.bSky = bSky;
		Batch.Add(Centre, Size);
		LegacyCards.Add(FName(*(TEXT("ScctCoronaOf:") + Name)));
	}

	if (Batches.IsEmpty()) return;
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	AActor* Host = World->SpawnActor<AActor>(Params);
	if (!Host) return;
	SpriteHost = Host;
	for (auto& Pair : Batches)
	{
		FBatch& Batch = Pair.Value;
		auto* Group = NewObject<UProceduralMeshComponent>(Host);
		Host->AddInstanceComponent(Group);
		Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Group->SetCastShadow(false);
		Group->SetCanEverAffectNavigation(false);
		Group->SetAbsolute(false, true, true);
		if (Batch.Parent) Group->SetupAttachment(Batch.Parent);
		Group->SetProcMeshSection(0, Batch.Section);
		Group->SetMaterial(0, Batch.Material);
		Group->RegisterComponent();
		if (Batch.bSky) SkyGroups.Add(Group);
	}
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		auto* Component = It->GetStaticMeshComponent();
		if (Component && Component->bRenderInMainPass && It->Tags.ContainsByPredicate([&](FName Tag){return LegacyCards.Contains(Tag);}))
		{
			Component->SetRenderInMainPass(false);
			ReplacedCoronas.Add(Component);
		}
	}
}
