#include "Play/ScctPawnLighting.h"

#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Play/ScctLightRules.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	TAutoConsoleVariable<int32> CVarLightPawnShadow(TEXT("scct.Light.PawnShadow"), 1,
		TEXT("Under the lighting plan, walls, furniture and movers block ordinary lamps at the pawn's centre. 0 gives the original unoccluded term."));
	TAutoConsoleVariable<float> CVarViewBounce(TEXT("scct.Light.ViewBounce"), 0.25f,
		TEXT("Diffuse return from visible room lamps onto local first-person equipment, in linear light."));
	const FName HasPawnRig(TEXT("HasPawnRig"));
}

FScctPawnLighting FScctPawnLighting::Sample(const UWorld& World, FVector Centre, float Radius, int32 SpyActorId, bool bSampleViewBounce)
{
	FScctPawnLighting Sample;
	FScctLightRig Rig{};
	Rig.Zone = INDEX_NONE;
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
	const auto* LevelState = World.GetSubsystem<UScctLevelStateSubsystem>();
	FVector3f Ambient(Rig.AmbientR, Rig.AmbientG, Rig.AmbientB);
	const float ViewBounce = bSampleViewBounce ? FMath::Clamp(CVarViewBounce.GetValueOnGameThread(), 0.0f, 1.0f) : 0.0f;
	FVector3f Incoming = FVector3f::ZeroVector;
	for (int32 Index = 0; Index < FMath::Min(Rig.Count, ScctLightRigLights); ++Index)
	{
		const FScctRigLight& Light = Rig.Lights[Index];
		// The visibility meter uses the original light rig. A fixture rendered by Unreal
		// still contributes to that measurement, but its per-pixel light is already on the mesh.
		if (bShadows && (Light.Flags & EScctRigLightFlags::Occluded))
		{
			continue;
		}
		if (ViewBounce > 0.0f && !(Light.Flags & EScctRigLightFlags::PointAmbient))
		{
			// The legacy actor rig can have zero ambient even in an illuminated corridor.
			// Estimate a single diffuse reflection from the same visibility-tested light
			// at the player. Convert its display-space colour to linear energy before mixing.
			Incoming.X += FMath::Pow(FMath::Max(0.0f, Light.R), 2.2f);
			Incoming.Y += FMath::Pow(FMath::Max(0.0f, Light.G), 2.2f);
			Incoming.Z += FMath::Pow(FMath::Max(0.0f, Light.B), 2.2f);
		}
		if (LevelState && LevelState->IsNativeFixture(Light.Id))
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
	if (bSampleViewBounce)
	{
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			const float Return = FMath::Min(ViewBounce * Incoming[Channel], 1.0f);
			Sample.ViewAmbient[Channel] = Return > 0.0f
				? FMath::Pow(FMath::Pow(FMath::Max(Ambient[Channel], 0.0f), 2.2f) + Return, 1.0f / 2.2f)
				: Ambient[Channel];
		}
	}
	return Sample;
}

void FScctPawnLighting::UseViewAmbient()
{
	Data[8] = ViewAmbient.X;
	Data[16] = ViewAmbient.Y;
	Data[24] = ViewAmbient.Z;
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
