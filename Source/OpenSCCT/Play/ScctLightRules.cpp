#include "Play/ScctLightRules.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/Light.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "OpenSCCT.h"
#include "Play/ScctLightPlan.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctLightPlanApi.h"
#include "Runtime/ScctWorldApi.h"

namespace
{
	const TCHAR* const LightRulesCollection = TEXT("/Game/OpenSCCT/Materials/MPC_ScctLight.MPC_ScctLight");
	/** Put on a light actor once it is dressed, so a second match in the same world (the menu's backdrop) leaves it. */
	const FName LightRulesDressedTag(TEXT("ScctLightPlan"));
	const FName LightRulesSurfaceMaster(TEXT("M_ScctSurface"));
	const FName LightRulesLightmap(TEXT("Lightmap"));
	/** The surface material's custom primitive data slot that says "these vertex colours are baked light". */
	constexpr int32 LightRulesBakedVertexSlot = 1;

	// The engine's law for a light without the inverse square law, measured by -ScctLightEngineChecks (0.304 to
	// 0.323 of the intensity on a white surface): light = albedo x intensity x (1 - (d/R)^2)^exponent / pi.
	constexpr float LightRulesPerIntensity = 1.0f / UE_PI;
	// The display values of the original's term whose distances the solve keeps: the edge of "lit" and of "dark".
	constexpr float LightRulesContourLit = 0.5f;
	constexpr float LightRulesContourDark = 0.15f;
	// A cone light's inner angle as a share of its outer one (the original's; linear in the cosine between, which
	// the engine squares: close to the display ramp's power of 2.2).
	constexpr float LightRulesInnerCone = 0.05f;

	TAutoConsoleVariable<float> CVarLightLightmapScale(TEXT("scct.Light.LightmapScale"), 2.0f,
		TEXT("Under scct.Light.Plan 1: what the stored baked light (lightmaps, vertex colours) is multiplied by. The original stored it halved: 2."));
	TAutoConsoleVariable<float> CVarLightBakedGain(TEXT("scct.Light.BakedGain"), 1.0f,
		TEXT("Under scct.Light.Plan 1: a factor on the baked light term, in display units (1: as the original)."));
	TAutoConsoleVariable<float> CVarLightBakedSaturation(TEXT("scct.Light.BakedSaturation"), 1.0f,
		TEXT("Under scct.Light.Plan 1: 1 the baked light keeps its own colour, 0 it is grey."));
	TAutoConsoleVariable<float> CVarLightBounceGate(TEXT("scct.Light.BounceGate"), 0.35f,
		TEXT("Under scct.Light.Plan 1: the most bounced and sky light a surface the original lit takes (0 none, 1 all); a surface the original has dark takes none."));
	TAutoConsoleVariable<float> CVarLightAmbientFloor(TEXT("scct.Light.AmbientFloor"), 0.0f,
		TEXT("Under scct.Light.Plan 1: the share of the released picture's ambient floor that stays (0: the original's dark is black)."));
	TAutoConsoleVariable<float> CVarLightLampGain(TEXT("scct.Light.LampGain"), 1.0f,
		TEXT("Under scct.Light.Plan 1: a factor on the strength of every map light (read when a level is dressed)."));
	TAutoConsoleVariable<float> CVarLightLampSaturation(TEXT("scct.Light.LampSaturation"), 1.0f,
		TEXT("Under scct.Light.Plan 1: 1 a map light keeps its own colour, 0 it is grey (read when a level is dressed)."));
	TAutoConsoleVariable<float> CVarLightPeakCap(TEXT("scct.Light.PeakCap"), 2.5f,
		TEXT("Under scct.Light.Plan 1: the most a map light's own term shows at the light, in the original's display units (it capped at 2). A brighter light keeps the distance of its lit edge."));
	TAutoConsoleVariable<int32> CVarLightCharacterLamps(TEXT("scct.Light.CharacterLamps"), 1,
		TEXT("Under scct.Light.Plan 1: ordinary actor lamps light pawns through the material rig and other actor-lit props on channel 1. 0 disables those lamps."));
	TAutoConsoleVariable<int32> CVarLightBodyAids(TEXT("scct.Light.BodyAids"), 0,
		TEXT("Under scct.Light.Plan 1: 1 keeps the fill and rim lights on the bodies of the local player's own side (scct.BodyFill, scct.BodyRim); 0 removes them, as the original had none."));
	TAutoConsoleVariable<float> CVarLightMoon(TEXT("scct.Light.Moon"), 0.0f,
		TEXT("Under scct.Light.Plan 1: the share of the level's moon that stays on a map whose own lights have no sun (0: none; read when a level is dressed)."));
	TAutoConsoleVariable<float> CVarLightSkyLight(TEXT("scct.Light.SkyLight"), 1.0f,
		TEXT("Under scct.Light.Plan 1: the share of the level's sky light that stays (it reaches a surface only as far as scct.Light.BounceGate lets it; read when a level is dressed)."));
	TAutoConsoleVariable<float> CVarLightFogInscatter(TEXT("scct.Light.FogInscatter"), 0.0f,
		TEXT("Under scct.Light.Plan 1: the share of the height fog's own glow that stays (0: the air glows only where a light shines through it; read when a level is dressed)."));
	TAutoConsoleVariable<int32> CVarLightProjectorShadows(TEXT("scct.Light.ProjectorShadows"), 1,
		TEXT("Under scct.Light.Plan 1: 1 a projector (LightEffect 22) is shadowed by the level's drawn meshes; 0 it is not (to tell what of a missing patch is shadow; read when a level is dressed)."));
	TAutoConsoleVariable<int32> CVarLightFlareShadows(TEXT("scct.Light.FlareShadows"), 1,
		TEXT("Under the lighting plan, geometry and characters cast shadows from burning and airborne flares."));
	TAutoConsoleVariable<int32> CVarLightRulesLog(TEXT("scct.Light.RulesLog"), 0,
		TEXT("1: log every map light as the lighting plan dresses it."));

	/** What was last written to the parameter collection, so a frame without a change writes nothing. */
	struct FLightRulesShown
	{
		TWeakObjectPtr<UWorld> World;
		float Values[5] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
	};
	FLightRulesShown LightRulesShown;

	float LightRulesFalloff(int32 Kind, float X)
	{
		X = FMath::Clamp(X, 0.0f, 1.0f);
		switch (Kind)
		{
		case FScctLightRules::EFalloff::Linear: return 1.0f - X;
		case FScctLightRules::EFalloff::Flat: return 1.0f;
		case FScctLightRules::EFalloff::Late: return X <= 0.9f ? 1.0f : (1.0f - X) / 0.1f;
		default: return 1.0f - 3.0f * X * X + 2.0f * X * X * X;
		}
	}

	/** The share of the reach at which the falloff has come down to Level. */
	float LightRulesReach(int32 Kind, float Level)
	{
		float Near = 0.0f;
		float Far = 1.0f;
		for (int32 Step = 0; Step < 32; ++Step)
		{
			const float Middle = 0.5f * (Near + Far);
			(LightRulesFalloff(Kind, Middle) > Level ? Near : Far) = Middle;
		}
		return 0.5f * (Near + Far);
	}

	void LightRulesPush(UWorld* World, bool bForce)
	{
		const float Wanted[5] =
		{
			FMath::Max(CVarLightLightmapScale.GetValueOnGameThread(), 0.0f),
			FMath::Max(CVarLightBakedGain.GetValueOnGameThread(), 0.0f),
			FMath::Clamp(CVarLightBakedSaturation.GetValueOnGameThread(), 0.0f, 2.0f),
			FMath::Clamp(CVarLightBounceGate.GetValueOnGameThread(), 0.0f, 1.0f),
			FMath::Max(CVarLightAmbientFloor.GetValueOnGameThread(), 0.0f),
		};
		if (!bForce && LightRulesShown.World.Get() == World && FMemory::Memcmp(Wanted, LightRulesShown.Values, sizeof(Wanted)) == 0)
		{
			return;
		}
		UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, LightRulesCollection);
		UMaterialParameterCollectionInstance* Instance = Collection != nullptr ? World->GetParameterCollectionInstance(Collection) : nullptr;
		if (Instance == nullptr)
		{
			// Said once for a world: the values are noted as shown, so the next frame does not ask again.
			UE_LOG(LogOpenSCCT, Error, TEXT("LightRules: %s is missing (Tools/Materials/Build-SurfaceMaterial.ps1 makes it); the surfaces keep the released light."), LightRulesCollection);
			LightRulesShown.World = World;
			FMemory::Memcpy(LightRulesShown.Values, Wanted, sizeof(Wanted));
			return;
		}
		Instance->SetScalarParameterValue(TEXT("Plan"), 1.0f);
		Instance->SetScalarParameterValue(TEXT("BakedScale"), Wanted[0]);
		Instance->SetScalarParameterValue(TEXT("BakedGain"), Wanted[1]);
		Instance->SetScalarParameterValue(TEXT("BakedSaturation"), Wanted[2]);
		Instance->SetScalarParameterValue(TEXT("BounceGate"), Wanted[3]);
		Instance->SetScalarParameterValue(TEXT("Floor"), Wanted[4]);
		LightRulesShown.World = World;
		FMemory::Memcpy(LightRulesShown.Values, Wanted, sizeof(Wanted));
		UE_LOG(LogOpenSCCT, Log, TEXT("LightRules: surfaces show the baked light whole: scale %.2f, gain %.2f, saturation %.2f, bounce gate %.2f, ambient floor share %.2f."),
			Wanted[0], Wanted[1], Wanted[2], Wanted[3], Wanted[4]);
	}

	/** A light that lights nothing and is not drawn. */
	void LightRulesOff(ULightComponent* Light)
	{
		Light->SetIntensity(0.0f);
		Light->SetVisibility(false);
	}

	void LightRulesMegaLights(ULightComponent* Light, bool bAllow)
	{
		if (static_cast<bool>(Light->bAllowMegaLights) != bAllow)
		{
			Light->bAllowMegaLights = bAllow;
			Light->MarkRenderStateDirty();
		}
	}

	/** True when a mesh is drawn with the surface material and carries none of the original's baked light. */
	bool LightRulesActorLit(const UMeshComponent* Component)
	{
		const TArray<float>& Data = Component->GetCustomPrimitiveData().Data.Num() > LightRulesBakedVertexSlot
			? Component->GetCustomPrimitiveData().Data : Component->GetDefaultCustomPrimitiveData().Data;
		if (Data.Num() > LightRulesBakedVertexSlot && Data[LightRulesBakedVertexSlot] >= 0.5f)
		{
			return false;
		}
		bool bSurface = false;
		for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = Component->GetMaterial(Slot);
			const UMaterial* Base = Material != nullptr ? Material->GetBaseMaterial() : nullptr;
			if (Base == nullptr || Base->GetFName() != LightRulesSurfaceMaster)
			{
				continue;
			}
			bSurface = true;
			UTexture* Page = nullptr;
			if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(LightRulesLightmap), Page) && Page != nullptr
				&& !Page->GetPathName().StartsWith(TEXT("/Engine/")))
			{
				return false;
			}
		}
		return bSurface;
	}
}

FScctLightRules::FFit FScctLightRules::Fit(float Peak, int32 Falloff, float Cap, float Gain)
{
	FFit Out;
	if (Peak <= 1.0e-4f || Gain <= 0.0f)
	{
		return Out;
	}
	const float Most = FMath::Pow(FMath::Max(Cap, 0.1f), 2.2f);
	// A dim light never reaches the two display values: it keeps their ratio under its own peak.
	const float Lit = FMath::Min(LightRulesContourLit, 0.6f * Peak);
	const float Dark = Lit * (LightRulesContourDark / LightRulesContourLit);
	float AtLight = 0.0f;
	if (Falloff == EFalloff::Flat)
	{
		AtLight = FMath::Pow(Peak, 2.2f);
		Out.Exponent = 0.1f;
	}
	else
	{
		const float LitAt = LightRulesReach(Falloff, Lit / Peak);
		const float DarkAt = LightRulesReach(Falloff, Dark / Peak);
		const float LitLeft = FMath::Max(1.0f - LitAt * LitAt, 1.0e-4f);
		const float DarkLeft = FMath::Clamp(1.0f - DarkAt * DarkAt, 1.0e-5f, LitLeft * 0.999f);
		Out.Exponent = 2.2f * FMath::Loge(Lit / Dark) / FMath::Loge(LitLeft / DarkLeft);
		AtLight = FMath::Pow(Lit, 2.2f) / FMath::Pow(LitLeft, Out.Exponent);
		if (AtLight > Most && LitLeft < 0.999f)
		{
			// The original's term is cut at 2 over the middle of such a light. Its lit edge stays where it was.
			Out.Exponent = FMath::Loge(FMath::Pow(Lit, 2.2f) / Most) / FMath::Loge(LitLeft);
			Out.bCapped = true;
		}
	}
	if (AtLight > Most)
	{
		AtLight = Most;
		Out.bCapped = true;
	}
	Out.Exponent = FMath::Clamp(Out.Exponent, 0.1f, 16.0f);
	Out.Intensity = AtLight / LightRulesPerIntensity * Gain;
	return Out;
}

FLinearColor FScctLightRules::LightColour(const FLinearColor& Display)
{
	const float Most = FMath::Max3(Display.R, Display.G, Display.B);
	if (Most <= 1.0e-5f)
	{
		return FLinearColor::Black;
	}
	const FLinearColor Linear(FMath::Pow(FMath::Max(Display.R, 0.0f) / Most, 2.2f), FMath::Pow(FMath::Max(Display.G, 0.0f) / Most, 2.2f), FMath::Pow(FMath::Max(Display.B, 0.0f) / Most, 2.2f));
	const float Keep = FMath::Clamp(CVarLightLampSaturation.GetValueOnGameThread(), 0.0f, 1.0f);
	const float Grey = 0.2126f * Linear.R + 0.7152f * Linear.G + 0.0722f * Linear.B;
	return FLinearColor(Grey + (Linear.R - Grey) * Keep, Grey + (Linear.G - Grey) * Keep, Grey + (Linear.B - Grey) * Keep);
}

void FScctLightRules::SetFlareLight(UPointLightComponent& Light, float Brightness, const FLinearColor& Display)
{
	// ActorLightRules.Colour at full value, multiplied by the reference point-light gain.
	// The effect bridge already supplies the live flicker/fade and converts the reach to centimetres.
	const float Peak = (2.0f * 0.8213f / 255.0f) * Brightness * FMath::Max3(Display.R, Display.G, Display.B);
	const FFit Solved = Fit(Peak, EFalloff::Smooth, CVarLightPeakCap.GetValueOnGameThread(), 1.0f);
	Light.SetUseInverseSquaredFalloff(false);
	Light.SetLightFalloffExponent(Solved.Exponent);
	Light.SetIntensity(Solved.Intensity);
	Light.SetLightColor(LightColour(Display));
	Light.SetCastShadows(CVarLightFlareShadows.GetValueOnGameThread() != 0);
}

bool FScctLightRules::BodiesOnLampChannel()
{
	return FScctLightPlan::IsOn() && CVarLightCharacterLamps.GetValueOnGameThread() > 0;
}

bool FScctLightRules::BodyAids()
{
	return !FScctLightPlan::IsOn() || CVarLightBodyAids.GetValueOnGameThread() > 0;
}

void FScctLightRules::Tick(UWorld* World)
{
	if (World != nullptr && FScctLightPlan::IsOn())
	{
		LightRulesPush(World, false);
	}
}

bool FScctLightRules::DressLevel(UWorld* World, const TMap<FName, TWeakObjectPtr<AActor>>& TaggedLights, TFunctionRef<const FString&(int32)> NameOf)
{
	if (World == nullptr || !FScctLightPlan::IsOn())
	{
		return true;
	}
	LightRulesPush(World, false);

	FString Error;
	const FScctLightPlanApi* Api = ScctFeature<FScctLightPlanApi>(&Error);
	FScctLightLevel Level;
	FMemory::Memzero(Level);
	if (Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("LightRules: the bridge has no light table (%s); the level's lights stay as they were made."), *Error);
		return true;
	}
	if (Api->GetLevel(&Level) == 0)
	{
		// The client has no map yet: the first frames of a match.
		return false;
	}
	if (Level.HasData == 0)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("LightRules: the map has no light data; the level's lights stay as they were made."));
		return true;
	}
	TArray<FScctLightInfo> Lights;
	Lights.SetNumZeroed(FMath::Max(Api->GetLights(nullptr, 0), 0));
	Lights.SetNum(FMath::Clamp(Api->GetLights(Lights.GetData(), Lights.Num()), 0, Lights.Num()));

	const float Cap = CVarLightPeakCap.GetValueOnGameThread();
	const float Gain = FMath::Max(CVarLightLampGain.GetValueOnGameThread(), 0.0f);
	const bool bCharacterLamps = CVarLightCharacterLamps.GetValueOnGameThread() > 0;
	const bool bLog = CVarLightRulesLog.GetValueOnGameThread() > 0;

	int32 ByClass[EScctLightClass::Count] = {};
	int32 NumWithoutActor = 0, NumAlready = 0, NumCapped = 0, NumOnBodies = 0, NumHaze = 0, NumOff = 0, NumLive = 0;
	bool bSun = false;
	float MostIntensity = 0.0f;
	for (const FScctLightInfo& Info : Lights)
	{
		bSun |= (Info.Flags & EScctLightInfoFlags::Sun) != 0 && (Info.Flags & EScctLightInfoFlags::Hidden) == 0;
		if ((Info.Flags & EScctLightInfoFlags::Hidden) != 0 || Info.NameId == 0)
		{
			continue;
		}
		const FString& Name = NameOf(Info.NameId);
		ALight* Actor = Cast<ALight>(TaggedLights.FindRef(FScctWorld::LevelTag(*Name)).Get());
		ULightComponent* Light = Actor != nullptr ? Actor->GetLightComponent() : nullptr;
		if (Light == nullptr)
		{
			++NumWithoutActor;
			continue;
		}
		if (Actor->Tags.Contains(LightRulesDressedTag))
		{
			++NumAlready;
			continue;
		}
		Actor->Tags.Add(LightRulesDressedTag);
		const int32 Class = FMath::Clamp(Info.Class, 0, EScctLightClass::Count - 1);
		++ByClass[Class];

		const bool bLightsActors = (Info.Flags & EScctLightInfoFlags::LightsActors) != 0;
		// What the light is under the plan: 0 nothing, 1 live on the world, 2 the bodies' lamp (channel 1), 3 haze only.
		int32 Role = 0;
		switch (Class)
		{
		case EScctLightClass::Projector:
		case EScctLightClass::Dynamic: Role = 1; break;
		case EScctLightClass::Baked: Role = bLightsActors && bCharacterLamps ? 2 : 3; break;
		case EScctLightClass::CharactersOnly: Role = bLightsActors && bCharacterLamps ? 2 : 0; break;
		default: Role = 0; break;
		}

		const float Peak = Info.Gain * FMath::Max3(Info.R, Info.G, Info.B);
		int32 Falloff = EFalloff::Smooth;
		if (Class == EScctLightClass::Projector)
		{
			Falloff = Info.Attenuation == 0 ? EFalloff::Flat : Info.Attenuation == 2 ? EFalloff::Late : EFalloff::Linear;
		}
		const FFit Solved = Fit(Peak, Falloff, Cap, Gain);
		if (Role == 0 || Solved.Intensity <= 0.0f)
		{
			LightRulesOff(Light);
			++NumOff;
			if (bLog)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_LIGHT_RULE %s class=%d role=off peak=%.3f"), *Name, Class, Peak);
			}
			continue;
		}

		UPointLightComponent* Point = Cast<UPointLightComponent>(Light);
		USpotLightComponent* Spot = Cast<USpotLightComponent>(Light);
		const float Before = Light->Intensity;
		if (Point != nullptr)
		{
			Point->SetUseInverseSquaredFalloff(false);
			Point->SetLightFalloffExponent(Solved.Exponent);
			if (Info.WorldRadius > 1.0f)
			{
				Point->SetAttenuationRadius(Info.WorldRadius);
			}
		}
		Light->SetIntensity(Solved.Intensity);
		Light->SetLightColor(LightColour(FLinearColor(Info.R, Info.G, Info.B)));
		NumCapped += Solved.bCapped ? 1 : 0;
		MostIntensity = FMath::Max(MostIntensity, Solved.Intensity);

		if (Spot != nullptr)
		{
			if (Class == EScctLightClass::Projector)
			{
				// A level built at run time has no projected rectangle yet (the plan's projector step): the cone
				// that holds the patch. The importer's dress (its light function) already has the rectangle.
				if (Light->LightFunctionMaterial == nullptr && Info.EchelonRange > 1.0f)
				{
					const float Half = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(FMath::Square(Info.SpotWidth) + FMath::Square(Info.SpotHeight)) / Info.EchelonRange));
					Spot->SetOuterConeAngle(FMath::Clamp(Half, 5.0f, 80.0f));
					Spot->SetInnerConeAngle(FMath::Clamp(Half, 5.0f, 80.0f));
				}
			}
			else if ((Info.Flags & EScctLightInfoFlags::Spot) != 0 && Info.ConeDegrees > 0.0f)
			{
				const float Outer = FMath::Clamp(Info.ConeDegrees * 0.5f, 1.0f, 80.0f);
				Spot->SetOuterConeAngle(Outer);
				Spot->SetInnerConeAngle(Outer * LightRulesInnerCone);
			}
		}

		switch (Role)
		{
		case 1:
			// Live: the world's channel, with shadows. Bodies are on it too until the body term.
			Light->SetLightingChannels(true, false, false);
			Light->SetCastShadows(Class != EScctLightClass::Projector || CVarLightProjectorShadows.GetValueOnGameThread() > 0);
			Light->SetDiffuseScale(1.0f);
			++NumLive;
			break;
		case 2:
			// The bodies' lamp: no surface of the world is on its channel, and the engine does not shadow such a
			// light by the world, as the original lit a pawn through a wall. No haze and no bounce from it.
			Light->SetLightingChannels(false, true, false);
			Light->SetCastShadows(false);
			Light->SetVolumetricScatteringIntensity(0.0f);
			Light->SetIndirectLightingIntensity(0.0f);
			++NumOnBodies;
			break;
		default:
			// In the lightmaps already: it lights no surface and bounces nothing, and the air still glows in it.
			// A light with no diffuse and no highlight keeps its haze only outside MegaLights (engine checks).
			Light->SetLightingChannels(true, false, false);
			Light->SetDiffuseScale(0.0f);
			Light->SetSpecularScale(0.0f);
			Light->SetIndirectLightingIntensity(0.0f);
			LightRulesMegaLights(Light, false);
			++NumHaze;
			break;
		}
		if (bLog)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_LIGHT_RULE %s class=%d role=%s peak=%.3f reach=%.0f intensity=%.3f (was %.3f) exponent=%.2f%s"),
				*Name, Class, Role == 1 ? TEXT("live") : Role == 2 ? TEXT("bodies") : TEXT("haze"), Peak, Info.WorldRadius, Solved.Intensity, Before, Solved.Exponent,
				Solved.bCapped ? TEXT(" capped") : TEXT(""));
		}
	}

	// ---- the sky. The original has no sun or moon on a map without a sun light, and its dark is not lit by a sky:
	// the moon goes, the height fog's own glow goes, and the sky light stays for what the gate lets through.
	const float MoonShare = FMath::Max(CVarLightMoon.GetValueOnGameThread(), 0.0f);
	const float SkyShare = FMath::Max(CVarLightSkyLight.GetValueOnGameThread(), 0.0f);
	const float FogShare = FMath::Max(CVarLightFogInscatter.GetValueOnGameThread(), 0.0f);
	int32 NumMoons = 0, NumSkies = 0, NumFogs = 0;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ULightComponent* Light = It->GetLightComponent();
		if (Light == nullptr || It->Tags.Contains(LightRulesDressedTag) || bSun)
		{
			continue;
		}
		It->Tags.Add(LightRulesDressedTag);
		Light->SetIntensity(Light->Intensity * MoonShare);
		if (MoonShare <= 0.0f)
		{
			Light->SetVisibility(false);
		}
		++NumMoons;
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		USkyLightComponent* Light = It->GetLightComponent();
		if (Light == nullptr || It->Tags.Contains(LightRulesDressedTag))
		{
			continue;
		}
		It->Tags.Add(LightRulesDressedTag);
		Light->SetIntensity(Light->Intensity * SkyShare);
		Light->SetVolumetricScatteringIntensity(Light->VolumetricScatteringIntensity * FogShare);
		++NumSkies;
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		UExponentialHeightFogComponent* Fog = It->GetComponent();
		if (Fog == nullptr || It->Tags.Contains(LightRulesDressedTag))
		{
			continue;
		}
		It->Tags.Add(LightRulesDressedTag);
		Fog->SetFogInscatteringColor(Fog->FogInscatteringLuminance * FogShare);
		Fog->SetDirectionalInscatteringColor(Fog->DirectionalInscatteringLuminance * FogShare);
		++NumFogs;
	}

	// ---- what the original lit as an actor: meshes drawn with the surface material that carry no baked light
	// (movers, props). Until the body term they take the bodies' lamps.
	int32 NumActorLit = 0, NumMeshes = 0;
	if (bCharacterLamps)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			UMeshComponent* Mesh = nullptr;
			if (const AStaticMeshActor* Static = Cast<AStaticMeshActor>(*It))
			{
				Mesh = Static->GetStaticMeshComponent();
			}
			else if (const ASkeletalMeshActor* Skeletal = Cast<ASkeletalMeshActor>(*It))
			{
				Mesh = Skeletal->GetSkeletalMeshComponent();
			}
			if (Mesh == nullptr)
			{
				continue;
			}
			++NumMeshes;
			if (!Mesh->LightingChannels.bChannel1 && LightRulesActorLit(Mesh))
			{
				Mesh->SetLightingChannels(Mesh->LightingChannels.bChannel0, true, Mesh->LightingChannels.bChannel2);
				++NumActorLit;
			}
		}
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("LightRules: %d lights of the map dressed for scct.Light.Plan 1 (projectors %d, dynamic %d, baked %d, characters-only %d, special-lit %d, unused %d): %d live on the world, %d light bodies from channel 1, %d keep only their haze, %d light nothing; %d held at the peak cap %.2f, strongest intensity %.1f; %d have no level light, %d were dressed before."),
		NumLive + NumOnBodies + NumHaze + NumOff, ByClass[EScctLightClass::Projector], ByClass[EScctLightClass::Dynamic], ByClass[EScctLightClass::Baked],
		ByClass[EScctLightClass::CharactersOnly], ByClass[EScctLightClass::SpecialLit], ByClass[EScctLightClass::None],
		NumLive, NumOnBodies, NumHaze, NumOff, NumCapped, Cap, MostIntensity, NumWithoutActor, NumAlready);
	UE_LOG(LogOpenSCCT, Log, TEXT("LightRules: the map %s; %d moons at share %.2f, %d sky lights at share %.2f, %d height fogs with %.2f of their glow; %d of %d level meshes carry no baked light and take the bodies' lamps."),
		bSun ? TEXT("has a sun light (the level's directional light stays)") : TEXT("has no sun light"), NumMoons, MoonShare, NumSkies, SkyShare, NumFogs, FogShare, NumActorLit, NumMeshes);
	return true;
}
