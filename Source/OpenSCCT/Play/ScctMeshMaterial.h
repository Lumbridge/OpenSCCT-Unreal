#pragma once

#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace ScctMeshMaterial
{
	// Loose maps share a dynamic instance across meshes. Keep the map's textures and
	// parameters while giving this slot its own writable instance.
	inline UMaterialInstanceDynamic* CreateSlotOverride(UMeshComponent& Component, int32 Slot)
	{
		UMaterialInterface* Source = Component.GetMaterial(Slot);
		if (!Source) return nullptr;
		auto* Shared = Cast<UMaterialInstanceDynamic>(Source);
		auto* Local = UMaterialInstanceDynamic::Create(Shared ? Shared->Parent.Get() : Source, &Component);
		if (Local)
		{
			if (Shared) Local->CopyParameterOverrides(Shared);
			Component.SetMaterial(Slot, Local);
		}
		return Local;
	}
}
