#include "Play/ScctWindowLighting.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GLTFAsset.h"
#include "GLTFMeshFactory.h"
#include "GLTFReader.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "Online/ScctMapFolders.h"
#include "Play/ScctRuntimeMapActor.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

struct FScctWindowBakeApi
{
	int32 Version;
	int32 (__cdecl* Poll)();
	int32 (__cdecl* Copy)(FScctWindowEdit*, int32);
};
static_assert(sizeof(FScctWindowEdit) == 56);
template<> struct TScctFeatureInfo<FScctWindowBakeApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.WindowBakeExports"); }
	static constexpr int32 Version = 1;
};

namespace
{
	FColor AdjustBake(FColor Original, const FScctWindowEdit& Edit)
	{
		auto Byte = [](float Value) { return static_cast<uint8>(FMath::Clamp(FMath::RoundHalfToEven(Value), 0.0f, 255.0f)); };
		return FColor(Byte(Original.R + Edit.R), Byte(Original.G + Edit.G), Byte(Original.B + Edit.B), Original.A);
	}

	UTexture2D* MakePage(const FString& Path, const TArray<FScctWindowEdit>& Edits)
	{
		FImage Image;
		if (!FImageUtils::LoadImage(*Path, Image)) return nullptr;
		Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		auto Pixels = Image.AsBGRA8();
		for (const auto& E : Edits)
			if (Pixels.IsValidIndex(E.Element)) Pixels[E.Element] = AdjustBake(Pixels[E.Element], E);
		return UTexture2D::CreateTransientFromImage(&Image);
	}

	void SwapWindowMesh(UStaticMeshComponent& C, UStaticMesh* Mesh)
	{
		const bool Registered = C.IsRegistered();
		if (Registered) C.UnregisterComponent();
		C.SetStaticMesh(Mesh);
		if (Registered) C.RegisterComponent();
	}

	FString BakeKey(const FString& S)
	{
		FString Key;
		for (TCHAR C : S) if (FChar::IsAlnum(C)) Key.AppendChar(FChar::ToLower(C));
		return Key;
	}

	// Rebuild just the affected placement from its glTF primitive. Cooked Nanite meshes need no CPU copy, and
	// float positions/normals and the author's section identities are still available before UE welds vertices.
	UStaticMesh* MakeWindowMesh(UStaticMesh& Original, const GLTF::FAsset& Asset, const TArray<FScctWindowEdit>& Edits)
	{
		const int32 MeshIndex = Edits[0].Mesh;
		if (!Asset.Meshes.IsValidIndex(MeshIndex)) return nullptr;
		const GLTF::FMesh& Source = Asset.Meshes[MeshIndex];
		FMeshDescription Desc;
		FStaticMeshAttributes Attr(Desc);
		Attr.Register();
		GLTF::FMeshFactory Factory;
		Factory.SetUniformScale(100);
		TMultiMap<FIntVector, const FScctWindowEdit*> ByPoint;
		auto PointKey = [](FVector3f P) { return FIntVector(FMath::RoundToInt(P.X*1000), FMath::RoundToInt(P.Y*1000), FMath::RoundToInt(P.Z*1000)); };
		int32 Applied = 0;
		TMap<int32,TArray<const FScctWindowEdit*>> Sections;
		TMap<int32, int32> SourceSections;
		for (const auto& E : Edits)
		{
			if (E.Element < 0) SourceSections.Add(E.Primitive, E.HasPoint);
			else Sections.FindOrAdd(E.Primitive).Add(&E);
		}
		FStaticMeshOperations::FAppendSettings Append;
		for (bool& Channel : Append.bMergeUVChannels) Channel = true;
		for (int32 Primitive = 0; Primitive < Source.Primitives.Num(); ++Primitive)
		{
			GLTF::FMesh Part;
			Part.Primitives.Add(Source.Primitives[Primitive]);
			FMeshDescription Section;
			FStaticMeshAttributes SectionAttrs(Section);
			SectionAttrs.Register();
			Factory.FillMeshDescription(Part, FTransform::Identity, &Section);
			if (!Section.Triangles().Num()) continue;
			auto Colors = SectionAttrs.GetVertexInstanceColors();
			const auto Positions = SectionAttrs.GetVertexPositions();
			const auto Normals = SectionAttrs.GetVertexInstanceNormals();
			const auto& Vertices = Factory.GetPositionIndexToVertexIdPerPrim()[0];
			ByPoint.Reset();
			TMap<int32,const FScctWindowEdit*> ByIndex;
			for (const auto* E : Sections.FindOrAdd(Primitive))
			{
				if (E->HasPoint) ByPoint.Add(PointKey(FVector3f(E->X,E->Z,E->Y)*100), E);
				else ByIndex.Add(E->Element,E);
			}
			for (const auto& Vertex : Vertices)
			{
				const auto Key = PointKey(Positions[Vertex.Value]);
				for (const FVertexInstanceID Corner : Section.GetVertexVertexInstanceIDs(Vertex.Value))
				{
					const FScctWindowEdit* Edit = ByIndex.FindRef(Vertex.Key);
					for (auto It=ByPoint.CreateConstKeyIterator(Key); It; ++It)
					{
						const auto* E=It.Value();
						if (Normals[Corner].Equals(FVector3f(E->NX,E->NZ,E->NY), 0.00015f)) { Edit=E; break; }
					}
					if (Edit)
					{
						const FColor Bytes = FLinearColor(Colors[Corner]).ToFColorSRGB();
						Colors[Corner] = FVector4f(FLinearColor(AdjustBake(Bytes,*Edit)));
						++Applied;
					}
				}
			}
			const int32 GltfId = Source.Primitives[Primitive].MaterialIndex;
			const FName SectionSlot(*FString::Printf(TEXT("%d:ScctSection:%d"), GltfId, SourceSections.FindRef(Primitive)));
			int32 Index = Original.GetMaterialIndex(SectionSlot);
			if (Index == INDEX_NONE) Index = Original.GetMaterialIndex(FName(*FString::FromInt(GltfId)));
			if (Index == INDEX_NONE)
			{
				if (!Asset.Materials.IsValidIndex(GltfId)) return nullptr;
				const FString Wanted = BakeKey(Asset.Materials[GltfId].Name);
				Index = Original.GetStaticMaterials().IndexOfByPredicate([&](const FStaticMaterial& M)
				{
					if (BakeKey(M.MaterialSlotName.ToString()) == Wanted) return true;
					if (!M.MaterialInterface) return false;
					FString Name = M.MaterialInterface->GetName();
					Name.RemoveFromStart(TEXT("MI_"));
					return BakeKey(Name) == Wanted;
				});
			}
			if (Index == INDEX_NONE && Original.GetStaticMaterials().Num() == 1) Index = 0;
			if (Index == INDEX_NONE) return nullptr;
			for (auto Group : Section.PolygonGroups().GetElementIDs())
				SectionAttrs.GetPolygonGroupMaterialSlotNames()[Group] = Original.GetStaticMaterials()[Index].MaterialSlotName;
			FStaticMeshOperations::AppendMeshDescription(Section, Desc, Append);
		}
		if (!Applied) return nullptr;
		UStaticMesh* Built = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		Built->GetStaticMaterials() = Original.GetStaticMaterials();
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;
		Params.bMarkPackageDirty = Params.bCommitMeshDescription = false;
		if (!Built->BuildFromMeshDescriptions({ &Desc }, Params)) return nullptr;
		Built->SetBodySetup(Original.GetBodySetup());
		return Built;
	}
}

bool UScctWindowLighting::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UScctWindowLighting::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (auto* Session = Collection.InitializeDependency<UScctSessionSubsystem>())
	{
		Session->OnFrame.AddUObject(this, &UScctWindowLighting::Update);
		Session->OnMatchStopped.AddUObject(this, &UScctWindowLighting::EndSession);
	}
}

void UScctWindowLighting::Restore()
{
	for (const auto& Entry : Materials)
		if (auto* C = Entry.Component.Get(); C && C->GetMaterial(Entry.Slot) == Entry.Replacement) C->SetMaterial(Entry.Slot, Entry.Original);
	for (const auto& Entry : Meshes)
		if (auto* C = Entry.Component.Get(); C && C->GetStaticMesh() == Entry.Replacement) SwapWindowMesh(*C, Entry.Original);
	Materials.Reset();
	Meshes.Reset();
}

void UScctWindowLighting::EndSession()
{
	Restore();
	Geometry.Reset();
}

void UScctWindowLighting::Deinitialize()
{
	if (auto* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->OnFrame.RemoveAll(this);
		Session->OnMatchStopped.RemoveAll(this);
	}
	EndSession();
	Super::Deinitialize();
}

void UScctWindowLighting::Update(float)
{
	const auto* Api = ScctFeature<FScctWindowBakeApi>();
	if (!Api) return;
	int32 Count = Api->Poll();
	if (Count < 0) return;
	TArray<FScctWindowEdit> Rows;
	Rows.SetNumUninitialized(Count);
	if (Count && Api->Copy(Rows.GetData(), Count) != Count) return;
	Restore();
	if (Count) Apply(Rows);
}

void UScctWindowLighting::Apply(const TArray<FScctWindowEdit>& Rows)
{
	const auto* Events = ScctFeature<FScctEventsApi>();
	if (!Events) return;
	const FString Map = GetWorld()->GetSubsystem<UScctSessionSubsystem>()->GetSessionInfo().Map;
	const FString Render = ScctMapFolders::FolderOf(Map) / TEXT("render");
	TMap<FName,TArray<FScctWindowEdit>> Actors;
	TMap<int32,TArray<FScctWindowEdit>> Pages;
	TMap<int32,int32> RuntimeSlots;
	TMap<FString,int32> ImportedPages;
	TMap<int32,FString> Names;
	for (const auto& E : Rows)
	{
		FString* Name = Names.Find(E.Name);
		if (!Name)
		{
			TArray<TCHAR> Buffer;
			Buffer.SetNumZeroed(Events->GetName(E.Name, nullptr, 0)+1);
			Events->GetName(E.Name, Buffer.GetData(), Buffer.Num());
			Name = &Names.Add(E.Name, Buffer.GetData());
		}
		if (E.Mesh >= 0) Actors.FindOrAdd(FName(*(TEXT("Scct:") + *Name))).Add(E);
		else if (E.Mesh == -2) RuntimeSlots.Add(E.Element,E.Name);
		else Pages.FindOrAdd(E.Name).Add(E);
	}
	TMap<int32,UTexture2D*> Textures;
	for (const auto& Page : Pages)
	{
		const FString& Uri = Names[Page.Key];
		if (auto* Texture = MakePage(Render/Uri,Page.Value))
		{
			Textures.Add(Page.Key,Texture);
			ImportedPages.Add(BakeKey(FPaths::GetBaseFilename(Uri)),Page.Key);
		}
	}
	if (!Actors.IsEmpty() && !Geometry.IsValid())
	{
		Geometry = MakeShared<GLTF::FAsset>();
		GLTF::FFileReader Reader;
		Reader.ReadFile(Render/TEXT("world.glb"), false, false, *Geometry);
	}
	const FString Prefix = TEXT("/Game/Imported/Maps/") + Map + TEXT("/");
	TMap<UMaterialInterface*,UMaterialInstanceDynamic*> PageInstances;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const bool Runtime = It->GetOwner() && It->GetOwner()->IsA<AScctRuntimeMapActor>();
		for (auto* C : TInlineComponentArray<UStaticMeshComponent*>(*It))
		{
			UStaticMesh* Original = C->GetStaticMesh();
			if (!Original || (!Runtime && !Original->GetOutermost()->GetName().StartsWith(Prefix))) continue;
			for (FName Tag : It->Tags)
			{
				if (auto* Edits = Actors.Find(Tag))
				{
					if (UStaticMesh* Built = MakeWindowMesh(*Original, *Geometry, *Edits))
					{
						auto& Entry = Meshes.AddDefaulted_GetRef();
						Entry.Component = C; Entry.Original = Original; Entry.Replacement = Built;
						SwapWindowMesh(*C,Built);
					}
					break;
				}
			}
			for (int32 I=0; I<C->GetNumMaterials(); ++I)
			{
				UMaterialInterface* M=C->GetMaterial(I);
				UTexture* Page=nullptr;
				if (!M || !M->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Lightmap")),Page) || !Page) continue;
				int32 Id = Runtime && Original->GetStaticMaterials().IsValidIndex(I)
					? RuntimeSlots.FindRef(FCString::Atoi(*Original->GetStaticMaterials()[I].MaterialSlotName.ToString())) : ImportedPages.FindRef(BakeKey(Page->GetName()));
				UTexture2D* Texture = Textures.FindRef(Id);
				if (!Texture) continue;
				auto*& MID = PageInstances.FindOrAdd(M);
				if (!MID)
				{
					if (auto* Dynamic = Cast<UMaterialInstanceDynamic>(M))
					{
						MID = UMaterialInstanceDynamic::Create(Dynamic->Parent, this);
						if (MID) MID->CopyParameterOverrides(Dynamic);
					}
					else MID = UMaterialInstanceDynamic::Create(M, this);
					if (!MID) continue;
					MID->SetTextureParameterValue(TEXT("Lightmap"),Texture);
				}
				auto& Binding=Materials.AddDefaulted_GetRef();
				Binding.Component = C;
				Binding.Slot = I;
				Binding.Original = C->OverrideMaterials.IsValidIndex(I) ? C->OverrideMaterials[I].Get() : nullptr;
				Binding.Replacement = MID;
				C->SetMaterial(I,MID);
			}
		}
	}
}
