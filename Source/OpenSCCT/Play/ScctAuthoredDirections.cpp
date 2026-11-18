#include "Play/ScctAuthoredDirections.h"

#include "Dom/JsonObject.h"
#include "GLTFAccessor.h"
#include "GLTFMeshFactory.h"
#include "MeshDescription.h"
#include "OpenSCCT.h"
#include "StaticMeshAttributes.h"

bool ScctAuthored::CopyDirections(const GLTF::FMeshFactory& Factory, TConstArrayView<GLTF::FAccessor> Accessors,
	TConstArrayView<TSharedPtr<FJsonValue>> Primitives, FMeshDescription& Description)
{
	auto UVs = FStaticMeshAttributes(Description).GetVertexInstanceUVs();
	const int32 OriginalChannels = UVs.GetNumChannels();
	const auto& Maps = Factory.GetPositionIndexToVertexIdPerPrim();
	bool bCopied = false;
	for (int32 P = 0; P < Primitives.Num() && P < Maps.Num(); ++P)
	{
		const TSharedPtr<FJsonObject>* Attr = nullptr;
		int32 Index = INDEX_NONE;
		const auto Primitive = Primitives[P]->AsObject();
		if (!Primitive || !Primitive->TryGetObjectField(TEXT("attributes"), Attr) ||
			!(*Attr)->TryGetNumberField(TEXT("COLOR_1"), Index))
			continue;
		if (OriginalChannels > 2 || !Accessors.IsValidIndex(Index))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Cannot bind authored COLOR_1 for primitive %d: occupied UV channels or missing accessor"), P);
			continue;
		}
		const auto& Src = Accessors[Index];
		if (!Src.IsValid() || Src.Type != GLTF::FAccessor::EType::Vec4 || Src.Count != uint32(Maps[P].Num()))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Invalid authored COLOR_1 accessor %d on primitive %d"), Index, P);
			continue;
		}
		TArray<FVector4f> Values;
		Src.GetVec4Array(Values); // Raw components, with accessor stride and glTF normalization applied.
		if (!bCopied)
			UVs.SetNumChannels(4); // Absent primitive streams have zero coherence.
		for (const auto& Pair : Maps[P])
		{
			const FVector4f& D = Values[Pair.Key];
			for (FVertexInstanceID Corner : Description.GetVertexVertexInstanceIDs(Pair.Value))
			{
				UVs.Set(Corner, 2, FVector2f(D.X, D.Y));
				UVs.Set(Corner, 3, FVector2f(D.Z, D.W));
			}
		}
		bCopied = true;
	}
	return bCopied;
}
