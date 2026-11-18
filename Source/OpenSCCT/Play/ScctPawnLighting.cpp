#include "Play/ScctPawnLighting.h"

#include "Components/MeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Play/ScctLightRules.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	TAutoConsoleVariable<int32> CVarLightPawnShadow(TEXT("scct.Light.PawnShadow"), 1,
		TEXT("Under the lighting plan, walls, furniture and movers block ordinary lamps at the pawn's centre. 0 gives the original unoccluded term."));
	const FName HasPawnRig(TEXT("HasPawnRig"));
}

FScctPawnLighting FScctPawnLighting::Sample(FVector Centre, float Radius, int32 SpyActorId)
{
	FScctPawnLighting Sample;
	Sample.bLamps = FScctLightRules::BodiesOnLampChannel();
	if (!Sample.bLamps)
	{
		return Sample;
	}
	const FScctLightPlanApi* Api = ScctFeature<FScctLightPlanApi>();
	if (!Api)
	{
		return Sample;
	}
	const bool bShadows = CVarLightPawnShadow.GetValueOnGameThread() != 0;
	const int32 Options = EScctLightRigOptions::MaterialLamps | (bShadows ? 0 : EScctLightRigOptions::NoOcclusion);
	FScctLightRig Rig{};
	// Viewer is only used by the audit's facing estimate. Per-pixel normals determine the drawn term.
	if (SpyActorId != INDEX_NONE && Api->GetActorRig(SpyActorId, Centre.X, Centre.Y, Centre.Z,
		Options | EScctLightRigOptions::SpySurface, &Rig))
	{
		Sample.SpySurfaceLight = 1.0f + FMath::Clamp(Rig.Level, 0.0f, 1.0f);
	}
	else if (!Api->GetPointRig(Centre.X, Centre.Y, Centre.Z, Radius, Centre.X, Centre.Y, Centre.Z, Options, &Rig))
	{
		return Sample;
	}
	Sample.Data[0] = 1.0f;
	FVector3f Ambient(Rig.AmbientR, Rig.AmbientG, Rig.AmbientB);
	for (int32 Index = 0; Index < FMath::Min(Rig.Count, ScctLightRigLights); ++Index)
	{
		const FScctRigLight& Light = Rig.Lights[Index];
		if (bShadows && (Light.Flags & EScctRigLightFlags::Occluded))
		{
			continue;
		}
		float* Place = Sample.Data + 1 + 8 * Index;
		Place[0] = Light.X;
		Place[1] = Light.Y;
		Place[2] = Light.Z;
		Place[3] = (Light.Flags & EScctRigLightFlags::Sun) ? 0.0f : 1.0f;
		Place[4] = Light.R;
		Place[5] = Light.G;
		Place[6] = Light.B;
		if (Light.Flags & EScctRigLightFlags::PointAmbient)
		{
			Ambient += FVector3f(Light.R, Light.G, Light.B);
		}
	}
	// The first three colour vectors carry ambient RGB in their spare alpha components.
	Sample.Data[8] = Ambient.X;
	Sample.Data[16] = Ambient.Y;
	Sample.Data[24] = Ambient.Z;
	return Sample;
}

void FScctPawnLighting::Apply(UMeshComponent& Mesh, bool bReceiveBodyAid, bool bBody) const
{
	const bool bSupportsMaterialRig = Mesh.GetCustomPrimitiveDataIndexForScalarParameter(HasPawnRig) == 3;
	// A custom gun or overlay with its own material still receives shadowed world lights. Only fall back
	// to the level's actor-light channel while waiting for the bridge to provide a rig.
	const bool bLampChannel = bLamps && Data[0] == 0.0f;
	if (!Mesh.LightingChannels.bChannel0 || Mesh.LightingChannels.bChannel1 != bLampChannel || Mesh.LightingChannels.bChannel2 != bReceiveBodyAid)
	{
		Mesh.SetLightingChannels(true, bLampChannel, bReceiveBodyAid);
	}
	if (bSupportsMaterialRig && Data[0] > 0.0f)
	{
		Mesh.SetCustomPrimitiveDataFloatArray(3, MakeArrayView(Data));
	}
	else if (bSupportsMaterialRig && Mesh.GetCustomPrimitiveData().Data.IsValidIndex(3) && Mesh.GetCustomPrimitiveData().Data[3] != 0.0f)
	{
		Mesh.SetCustomPrimitiveDataFloat(3, 0.0f);
	}
	if (bBody && (SpySurfaceLight != 0.0f || Mesh.GetCustomPrimitiveData().Data.IsValidIndex(35)))
	{
		Mesh.SetCustomPrimitiveDataFloat(35, SpySurfaceLight);
	}
}
