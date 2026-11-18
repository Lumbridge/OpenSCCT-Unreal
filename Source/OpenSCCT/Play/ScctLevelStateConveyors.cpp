#include "Play/ScctLevelStateSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "OpenSCCT.h"
#include "ProceduralMeshComponent.h"

namespace
{
	uint64 GeometryKey(int32 Owner, int32 Slot)
	{
		return (static_cast<uint64>(static_cast<uint32>(Owner)) << 32) | static_cast<uint32>(Slot);
	}

	FString MaterialKey(FString Name)
	{
		Name.RemoveFromStart(TEXT("MI_"));
		FString Base, Variant;
		if (Name.Split(TEXT("__"), &Base, &Variant)) Name = Base;
		Name.ReplaceInline(TEXT("."), TEXT("_"));
		return Name;
	}
}

void UScctLevelStateSubsystem::ScrollConveyor(const FScctGeometryRow& Row)
{
	const uint64 Key = GeometryKey(Row.Owner, Row.Material);
	auto* Slots = ConveyorScrolls.Find(Key);
	if (!Slots)
	{
		AActor* Owner = FindLevelActor(GetName(Row.Owner));
		auto* Mesh = Owner ? Owner->FindComponentByClass<UStaticMeshComponent>() : nullptr;
		if (!Mesh) return;
		Slots = &ConveyorScrolls.Add(Key);
		const FString Filter = GetName(Row.Material);
		const auto* FilterMaterial = Row.Material ? FindMaterial(Filter) : nullptr;
		FString Package, Object, Discard;
		Filter.Split(TEXT("."), &Package, &Discard);
		Filter.Split(TEXT("."), &Discard, &Object, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		for (int32 S = 0; S < Mesh->GetNumMaterials(); ++S)
		{
			auto* Base = Mesh->GetMaterial(S);
			if (!Base) continue;
			const FString SlotName = MaterialKey(Base->GetName());
			if (Row.Material && Base != FilterMaterial && !SlotName.Equals(MaterialKey(Filter), ESearchCase::IgnoreCase)
				&& !SlotName.Equals(Package + TEXT("_") + Object, ESearchCase::IgnoreCase)) continue;
			auto* Original = Mesh->OverrideMaterials.IsValidIndex(S) ? Mesh->OverrideMaterials[S].Get() : nullptr;
			auto* Mid = Mesh->CreateDynamicMaterialInstance(S, Base);
			if (!Mid) continue;
			ConveyorOriginals.AddUnique(Original);
			Slots->Add({Mesh, S, Original, Mid});
		}
	}
	for (const FScrollSlot& Slot : *Slots)
	{
		if (auto* Mid = Slot.Material.Get())
		{
			// An oscillator with a fixed quarter-turn phase is a constant UV offset. The existing
			// cooked surface shader supports this even in older map packs. PanU/V remain independent.
			Mid->SetVectorParameterValue(TEXT("AnimA"), FLinearColor(Row.Part == 0 ? Row.Phase : 0, 0, 0, .25f));
			Mid->SetVectorParameterValue(TEXT("AnimB"), FLinearColor(Row.Part == 0 ? 0 : Row.Phase, 0, 0, 2));
			Mid->SetVectorParameterValue(TEXT("AnimC"), FLinearColor(.25f, 0, 0, 0));
		}
	}
}

void UScctLevelStateSubsystem::DrawWorldGeometry()
{
	if (!bGeometryApiTried)
	{
		bGeometryApiTried = true;
		FString Error;
		GeometryApi = static_cast<const FScctWorldGeometryApi*>(FOpenSCCTModule::Get().FindFeatureTable(
			TEXT("OpenScct.UnrealBridge.WorldGeometryExports"), sizeof(FScctWorldGeometryApi), 1, Error));
		if (!GeometryApi) UE_LOG(LogOpenSCCT, Warning, TEXT("Map device geometry unavailable: %s"), *Error);
	}
	if (!GeometryApi) return;
	const int32 Count = GeometryApi->Prepare();
	if (!Count && !GeometryOwner.IsValid()) return;
	GeometryRows.SetNumUninitialized(Count, EAllowShrinking::No);
	if (Count > 0 && GeometryApi->Rows(GeometryRows.GetData(), Count) != Count) return;
	AActor* Owner = GeometryOwner.Get();
	if (!Owner && Count)
	{
		Owner = GetWorld()->SpawnActor<AActor>();
		if (!Owner) return;
		GeometryOwner = Owner;
		auto* Root = NewObject<USceneComponent>(Owner);
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
	}

	int32 Cargo = 0;
	TSet<uint64> Visible;
	for (const auto& Row : GeometryRows)
	{
		constexpr double Degrees = 360.0 / 65536;
		const FQuat Turn = FRotator(Row.Pitch * Degrees, Row.Yaw * Degrees, Row.Roll * Degrees).Quaternion();
		const FTransform Frame(Turn, FVector(Row.X, Row.Y, Row.Z), FVector(Row.ScaleX, Row.ScaleY, Row.ScaleZ));
		if (Row.Kind == 1)
		{
			ScrollConveyor(Row);
			continue;
		}
		if (Row.Kind == 3)
		{
			UStaticMesh* Mesh = FindMesh(GetName(Row.Asset));
			if (!Mesh) continue;
			if (!ConveyorCargo.IsValidIndex(Cargo))
			{
				auto* C = NewObject<UStaticMeshComponent>(Owner);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetupAttachment(Owner->GetRootComponent());
				C->RegisterComponent();
				ConveyorCargo.Add(C);
			}
			auto* C = ConveyorCargo[Cargo++].Get();
			C->SetStaticMesh(Mesh);
			FTransform Placement = Frame;
			Placement.SetRotation(Turn * FRotator(0, Row.Spin * Degrees, 0).Quaternion());
			Placement.AddToTranslation(Placement.TransformVector(FVector(0, 0, -Mesh->GetBoundingBox().Min.Z)));
			C->SetWorldTransform(Placement);
			C->SetVisibility(true);
			continue;
		}
		const uint64 Key = GeometryKey(Row.Owner, Row.Part);
		if (Row.Count < 3) continue;
		auto& Section = DynamicSections.FindOrAdd(Key);
		auto* C = Section.Mesh.Get();
		if (!C)
		{
			C = NewObject<UProceduralMeshComponent>(Owner);
			C->SetupAttachment(Owner->GetRootComponent());
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->RegisterComponent();
			Section.Mesh = C;
		}
		AActor* Replaced = FindLevelActor(GetName(Row.Owner));
		if (Row.Kind == 2 && Replaced)
		{
			if (auto* Baked = Replaced->FindComponentByClass<UStaticMeshComponent>())
				C->SetMaterial(0, Baked->GetMaterial(0));
		}
		else if (Row.Kind == 4 && (Section.Material != Row.Material || Section.Picture != Row.Picture))
		{
			UMaterialInterface* Mat = Row.Material ? FindMaterial(GetName(Row.Material)) : nullptr;
			if (!Mat && Row.Picture)
			{
				auto& Default = ConsolePictures.FindOrAdd(Row.Picture);
				if (!Default)
				{
					auto* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/RuntimeMap/MI_ScctRuntime_Opaque_Lit"));
					if (auto* Tex = FImageUtils::ImportFileAsTexture2D(GetName(Row.Picture)); Parent && Tex)
					{
						auto* Mid = UMaterialInstanceDynamic::Create(Parent, this);
						Mid->SetTextureParameterValue(TEXT("BaseColor"), Tex);
						Default = Mid;
					}
				}
				Mat = Default.Get();
			}
			C->SetMaterial(0, Mat);
			Section.Material = Row.Material;
			Section.Picture = Row.Picture;
		}
		C->SetWorldTransform(Frame);
		if (Section.Phase != Row.Phase)
		{
			GeometryVertices.SetNumUninitialized(Row.Count, EAllowShrinking::No);
			if (GeometryApi->Vertices(Row.First, GeometryVertices.GetData(), Row.Count) != Row.Count) continue;
			TArray<FVector> P, N;
			TArray<FVector2D> UV;
			P.Reserve(Row.Count); N.Reserve(Row.Count); UV.Reserve(Row.Count);
			for (const auto& V : GeometryVertices)
			{
				P.Emplace(V.X, V.Y, V.Z); N.Emplace(V.Nx, V.Ny, V.Nz); UV.Emplace(V.U, V.V);
			}
			const FProcMeshSection* Previous = C->GetProcMeshSection(0);
			if (Row.Kind == 4 && Previous && Previous->ProcVertexBuffer.Num() == Row.Count)
			{
				C->UpdateMeshSection(0, P, N, UV, {}, {});
			}
			else
			{
				TArray<int32> Indices;
				Indices.Reserve(Row.Count);
				for (int32 I = 0; I + 2 < Row.Count; I += 3)
				{
					// A UE front face winds clockwise looking along its normal.
					const bool bReverse = FVector::DotProduct(FVector::CrossProduct(P[I+1]-P[I], P[I+2]-P[I]), N[I]) > 0;
					Indices.Add(I); Indices.Add(I + (bReverse ? 2 : 1)); Indices.Add(I + (bReverse ? 1 : 2));
				}
				C->CreateMeshSection(0, P, Indices, N, UV, {}, {}, false);
			}
			Section.Phase = Row.Phase;
		}
		if (Replaced && !GeometryHidden.Contains(Replaced))
		{
			GeometryHidden.Add(Replaced, Replaced->IsHidden());
			Replaced->SetActorHiddenInGame(true);
		}
		Visible.Add(Key);
		C->SetVisibility(true);
	}
	for (auto& Pair : DynamicSections)
		if (!Visible.Contains(Pair.Key) && Pair.Value.Mesh.IsValid()) Pair.Value.Mesh->SetVisibility(false);
	for (; Cargo < ConveyorCargo.Num(); ++Cargo)
		if (ConveyorCargo[Cargo].IsValid()) ConveyorCargo[Cargo]->SetVisibility(false);
}

void UScctLevelStateSubsystem::RestoreWorldGeometry()
{
	for (auto& Pair : ConveyorScrolls)
		for (const auto& Slot : Pair.Value)
			if (auto* C = Slot.Mesh.Get(); C && C->GetMaterial(Slot.Slot) == Slot.Material.Get()) C->SetMaterial(Slot.Slot, Slot.Original.Get());
	for (auto& Pair : GeometryHidden)
		if (AActor* A = Pair.Key.Get()) A->SetActorHiddenInGame(Pair.Value);
	if (GeometryOwner.IsValid()) GeometryOwner->Destroy();
	GeometryOwner.Reset();
	DynamicSections.Reset();
	ConveyorScrolls.Reset();
	ConveyorCargo.Reset();
	ConsolePictures.Reset();
	ConveyorOriginals.Reset();
	GeometryHidden.Reset();
}
