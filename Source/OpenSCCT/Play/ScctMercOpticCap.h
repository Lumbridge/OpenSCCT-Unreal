#pragma once

class UStaticMeshComponent;
class UPoseableMeshComponent;

namespace ScctMercOpticCap
{
	void Configure(UStaticMeshComponent& Cap);
	bool HasHousing(const UStaticMeshComponent& Cap);
	void Update(UStaticMeshComponent& Cap, const UPoseableMeshComponent* Rifle);
}
