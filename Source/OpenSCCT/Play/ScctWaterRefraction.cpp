#include "Play/ScctWaterRefraction.h"
#include "Play/ScctWaterRefractionComponent.h"

#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GLTFAsset.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"

UScctWaterRefractionComponent::UScctWaterRefractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.10f;
}

void UScctWaterRefractionComponent::Follow(ULightComponent* Fixture)
{
	SourceFixture = Fixture;
}

void UScctWaterRefractionComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(IsValid(SourceFixture));
}

void UScctWaterRefractionComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTick);
	const bool bOn = IsValid(SourceFixture) && SourceFixture->IsVisible() && SourceFixture->Intensity > 0.0f &&
		!SourceFixture->GetOwner()->IsHidden();
	if (GetVisibleFlag() != bOn) SetVisibility(bOn);
	if (!IsValid(SourceFixture)) SetComponentTickEnabled(false);
}

namespace
{
	const FName ProjectionName(TEXT("TankRefractedLight"));
	const FName WindowApertureTag(TEXT("ScctWaterWindowAperture"));
	const FName GuideTag(TEXT("ScctCausticGuide"));
	const FName TankLampTag(TEXT("ScctTankFixture"));

	struct FPaneTriangle
	{
		FVector A, B, C;
		FLinearColor Water;
	};

	struct FLamp
	{
		FName ActorTag;
		FVector Position;
		FLinearColor Colour;
		double Radius, Brightness;
		bool bSwitchable;
	};

	const TArray<TSharedPtr<FJsonValue>>& Array(const FJsonObject& Object, const TCHAR* Key)
	{
		static const TArray<TSharedPtr<FJsonValue>> Empty;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		return Object.TryGetArrayField(Key, Values) ? *Values : Empty;
	}

	FVector Vector(const FJsonObject& Object, const TCHAR* Key, const FVector Default)
	{
		const auto& V = Array(Object, Key);
		return V.Num() == 3 ? FVector(V[0]->AsNumber(), V[1]->AsNumber(), V[2]->AsNumber()) : Default;
	}

	double Number(const FJsonObject& Object, const TCHAR* Key, double Default)
	{
		Object.TryGetNumberField(Key, Default);
		return Default;
	}

	bool Flag(const FJsonObject& Object, const TCHAR* Key, bool Default = false)
	{
		Object.TryGetBoolField(Key, Default);
		return Default;
	}

	UScctWaterRefractionComponent* FindProjection(const AActor& Actor)
	{
		for (auto* Light : TInlineComponentArray<UScctWaterRefractionComponent*>(&Actor))
			if (Light->GetFName() == ProjectionName) return Light;
		return nullptr;
	}

	bool HasProjection(const AActor& Actor)
	{
		const auto* Light = FindProjection(Actor);
		return Light && Light->ComponentHasTag(WindowApertureTag);
	}

	// The exported tank panes include both flat windows and curved tunnel sections. Use only the actual
	// transmitting triangles, including the overridden glass material of each placed mesh variant.
	TArray<FPaneTriangle> ReadPanes(const FString& Folder, const GLTF::FAsset& Geometry, const FJsonObject& Gltf,
		const TMap<FName, AActor*>& Actors, TArray<UStaticMeshComponent*>& Guides)
	{
		TMap<FString, int32> Nodes;
		for (const auto& V : Array(Gltf, TEXT("nodes")))
		{
			const auto N = V->AsObject();
			int32 Mesh;
			if (N && N->TryGetNumberField(TEXT("mesh"), Mesh)) Nodes.Add(N->GetStringField(TEXT("name")), Mesh);
		}
		TArray<FPaneTriangle> Panes;
		const TSharedPtr<FJsonObject>* Extras = nullptr;
		const TSharedPtr<FJsonObject>* Scene = nullptr;
		if (!Gltf.TryGetObjectField(TEXT("extras"), Extras) || !(*Extras)->TryGetObjectField(TEXT("svmap"), Scene)) return Panes;

		TMap<int32, FLinearColor> Colours;
		for (const auto& V : Array(**Scene, TEXT("instances")))
		{
			const auto Record = V->AsObject();
			if (!Record || Flag(*Record, TEXT("hidden"))) continue;
			AActor* Actor = Actors.FindRef(FName(*(TEXT("Scct:") + Record->GetStringField(TEXT("actor")))));
			auto* Part = Actor ? Actor->FindComponentByClass<UStaticMeshComponent>() : nullptr;
			const FString SourceMesh = Record->GetStringField(TEXT("mesh"));
			const int32* Index = Nodes.Find(SourceMesh);
			if (!Part || !Index || !Geometry.Meshes.IsValidIndex(*Index)) continue;
			if (ScctWaterRefraction::IsGuide(SourceMesh))
			{
				if (!HasProjection(*Actor)) Guides.Add(Part);
				continue;
			}
			for (const auto& Primitive : Geometry.Meshes[*Index].Primitives)
			{
				if (!Primitive.IsValid() || !Geometry.Materials.IsValidIndex(Primitive.MaterialIndex)) continue;
				const auto& Material = Geometry.Materials[Primitive.MaterialIndex];
				if (Material.AlphaMode != GLTF::FMaterial::EAlphaMode::Blend ||
					!(Material.Name.Contains(TEXT("glass")) || Material.Name.Contains(TEXT("vitre")) || Material.Name.Contains(TEXT("water")))) continue;
				const int32 Texture = Material.BaseColor.TextureIndex;
				if (!Geometry.Textures.IsValidIndex(Texture)) continue;
				if (!Colours.Contains(Texture))
				{
					FImage Image;
					const FString ImagePath = Folder / Geometry.Textures[Texture].Source.URI;
					FLinearColor Colour = FLinearColor::Black;
					if (FImageUtils::LoadImage(*ImagePath, Image)) Colour = FImageCore::ComputeImageLinearAverage(Image);
					Colours.Add(Texture, Colour);
				}
				const auto Colour = Colours[Texture];
				const float Peak = FMath::Max(Colour.G, Colour.B);
				// Some tanks use clear, almost transparent glass; there the water colour is entirely
				// in the submerged fixture. Ordinary grey, dense windows do not transmit this light.
				const bool bClear = Colour.A > 0.0f && Colour.A < 0.15f;
				if (!bClear && (Peak < 0.012f || Peak < Colour.R * 1.25f)) continue;
				const FLinearColor Water = bClear ? FLinearColor::White : FLinearColor(Colour.R / Peak, Colour.G / Peak, Colour.B / Peak);
				TArray<FVector3f> Points;
				TArray<uint32> Triangles;
				Primitive.GetPositions(Points);
				Primitive.GetTriangleIndices(Triangles);
				const FTransform Frame = Part->GetComponentTransform();
				for (int32 I = 0; I + 2 < Triangles.Num(); I += 3)
				{
					auto Position = [&](int32 K) { return Frame.TransformPosition(FVector(Points[Triangles[I + K]]) * 100.0); };
					const FVector A = Position(0), B = Position(1), C = Position(2);
					if (FVector::CrossProduct(B-A, C-A).SizeSquared() > 1.e-4) Panes.Add({A, B, C, Water});
				}
			}
		}
		return Panes;
	}

	TArray<FLamp> ReadLamps(const FJsonObject& Lighting)
	{
		TArray<FLamp> Lamps;
		for (const auto& V : Array(Lighting, TEXT("lights")))
		{
			const auto L = V->AsObject();
			if (!L || Flag(*L,TEXT("hidden")) || (!Flag(*L,TEXT("apply_static")) && !Flag(*L,TEXT("apply_ingame")))) continue;
			const double Brightness = Number(*L,TEXT("brightness"),0);
			if (Brightness <= 0) continue;
			const FVector RGB = Vector(*L,TEXT("color"),FVector::OneVector);
			const FLinearColor Linear(FMath::Pow(RGB.X,2.2), FMath::Pow(RGB.Y,2.2), FMath::Pow(RGB.Z,2.2));
			Lamps.Add({FName(*(TEXT("Scct:") + L->GetStringField(TEXT("name")))), Vector(*L,TEXT("location"),FVector::ZeroVector),
				Linear, 25.0 * (Number(*L,TEXT("radius"),0)+1.0), Brightness, Flag(*L,TEXT("triggered"))});
		}
		return Lamps;
	}

	void IlluminateTank(const FLamp& Lamp, AActor* Actor, double ApertureDistance)
	{
		// A coloured fixture seen through this pane is already represented by its original light actor.
		// Switch its native component to a local world receiver, reusing the actual source & its state.
		if (!Actor || Actor->ActorHasTag(TankLampTag) || Actor->ActorHasTag(TEXT("ScctLumenFixture")) ||
			ApertureDistance > 480.0 || FMath::Max(Lamp.Colour.B,Lamp.Colour.G) <= 1.25f * Lamp.Colour.R) return;
		auto* Point = Actor->FindComponentByClass<UPointLightComponent>();
		if (!Point) return;
		if (!Actor->GetWorld()->IsGameWorld()) { Actor->Modify(); Point->Modify(); }
		Actor->Tags.Add(TankLampTag);
		Point->SetMobility(EComponentMobility::Movable);
		Point->SetUseInverseSquaredFalloff(true);
		Point->SetIntensityUnits(ELightUnits::Candelas);
		Point->SetIntensity(FMath::Clamp(20.0*Lamp.Brightness/255.0, 2.0, 54.0));
		Point->SetAttenuationRadius(FMath::Min(Lamp.Radius,FMath::Clamp(ApertureDistance+120.0,170.0,440.0)));
		Point->SetLightColor(Lamp.Colour);
		Point->SetSourceRadius(12.0f);
		Point->SetCastShadows(true);
		Point->SetLightingChannels(true,false,false);
		Point->SetIndirectLightingIntensity(1.0f);
		Point->SetVolumetricScatteringIntensity(0.0f);
		Point->SetMaxDrawDistance(3600.0f);
		Point->SetMaxDistanceFadeRange(650.0f);
		if (!Lamp.bSwitchable) Point->SetVisibility(true);
	}

	// The artist's old patch supplies the destination and footprint only. The emitting aperture, direction,
	// colour and energy all come from the water side of a real, locally illuminated tank pane.
	bool Project(UStaticMeshComponent& Guide, const TArray<FPaneTriangle>& Panes, const TArray<FLamp>& Lamps,
		const TMap<FName, AActor*>& Actors, UMaterialInterface& Function)
	{
		if (!Guide.GetStaticMesh()) return false;
		const FBox LocalGuide = Guide.GetStaticMesh()->GetBoundingBox();
		const FVector Target = Guide.GetComponentTransform().TransformPosition(LocalGuide.GetCenter());
		const FVector Extent = Guide.GetComponentScale().GetAbs() * LocalGuide.GetExtent();
		const double Footprint = FMath::Clamp(Extent.Size(), 65.0, 400.0);
		const double Reach = FMath::Clamp(Footprint * 3.0, 370.0, 1050.0);
		const FPaneTriangle* Pane = nullptr;
		const FLamp* Lamp = nullptr;
		FVector Origin;
		double Best = 0, Energy = 0;
		// Aim at the upper water column on floors and the lower column on ceilings. This keeps
		// the virtual origin on the window, without placing a point lamp directly on the receiver.
		const FVector Normal = Guide.GetComponentTransform().TransformVectorNoScale(FVector::UpVector);
		const FVector Probe = Target + Normal * FMath::Clamp(Footprint * 0.6, 80.0, 180.0);
		for (const auto& P : Panes)
		{
			const FVector At = FMath::ClosestPointOnTriangleToPoint(Probe, P.A, P.B, P.C);
			const double Distance = FVector::Dist(At, Target);
			if (Distance < 35 || Distance > Reach) continue;
			const FVector Out = (Target-At)/Distance;
			FVector N = FVector::CrossProduct(P.B-P.A, P.C-P.A).GetSafeNormal();
			if (FVector::DotProduct(N,Out) < 0) N *= -1.0;
			const double ExitCos = FVector::DotProduct(Out,N);
			if (ExitCos < 0.12) continue;
			for (const auto& L : Lamps)
			{
				const bool bWaterColour = FMath::Max(L.Colour.G,L.Colour.B) > 1.25f*L.Colour.R;
				if (P.Water == FLinearColor::White && !bWaterColour) continue;
				const FVector ToLamp = L.Position-At;
				const double Travel = ToLamp.Size();
				if (Travel > L.Radius || Travel > 720 || FVector::DotProduct(ToLamp,N) > 18) continue;
				const double Transmission = FMath::Square(1.0 - FMath::Square(Travel/L.Radius));
				const double Incident = FMath::Clamp(L.Brightness/255.0,0.0,2.5) * Transmission;
				const double Score = Incident * (0.35 + 0.65*ExitCos) * (bWaterColour ? 1.0 : 0.12)
					/ ((1.0+FMath::Square(Travel/145.0)) * (1.0+FMath::Square(Distance/260.0)));
				if (Score <= Best) continue;
				Best = Score; Pane = &P; Lamp = &L; Origin = At+N*5.0; Energy = Incident;
			}
		}
		if (!Lamp || !Pane) return false;
		AActor* Owner = Guide.GetOwner();
		if (!Owner->GetWorld()->IsGameWorld()) { Owner->Modify(); Guide.Modify(); }
		AActor* SourceActor = Actors.FindRef(Lamp->ActorTag);
		IlluminateTank(*Lamp, SourceActor, FVector::Dist(Origin,Lamp->Position));
		auto* Light = FindProjection(*Owner);
		if (!Light)
		{
			Light = NewObject<UScctWaterRefractionComponent>(Owner, ProjectionName, RF_Transactional);
			Owner->AddInstanceComponent(Light);
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetupAttachment(&Guide);
		}
		else if (!Owner->GetWorld()->IsGameWorld()) Light->Modify();
		Light->SetAbsolute(true,true,true);
		Light->SetWorldLocationAndRotation(Origin,(Target-Origin).Rotation());
		const double Distance = FVector::Dist(Target,Origin);
		// A dyed viewing window transmits light across its face. The clear tunnel gives a
		// direct view into the water and retains the narrower bundle from its local fixture.
		const bool bTintedWindow = Pane->Water != FLinearColor::White;
		const float GuideCone = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Footprint*0.86, Distance)),16.0,58.0);
		const float Cone = bTintedWindow ? FMath::Min(64.0f, GuideCone * 1.20f) : GuideCone;
		Light->SetOuterConeAngle(Cone);
		Light->SetInnerConeAngle(Cone * (bTintedWindow ? 0.74f : 0.55f));
		Light->SetAttenuationRadius(FMath::Min(Distance + Footprint*0.90, 1150.0));
		Light->SetUseInverseSquaredFalloff(true);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(FMath::Clamp(24.0*Energy, 1.0, 60.0) * (bTintedWindow ? 2.7 : 1.0));
		Light->SetSourceRadius(FMath::Clamp(Footprint*0.06, 5.0, 18.0));
		// White light also transmits through the water. The actual pane pigment sets the wavelength bias.
		const FLinearColor Tint = Lamp->Colour * FMath::Lerp(FLinearColor::White, Pane->Water, 0.57f);
		Light->SetLightColor(Tint);
		Light->SetCastShadows(true);
		Light->SetIndirectLightingIntensity(0.0f); // the source fixture supplies the diffuse bounce just once
		Light->SetVolumetricScatteringIntensity(0.0f);
		Light->bAffectTranslucentLighting = false;
		Light->SetLightFunctionMaterial(&Function);
		Light->SetLightFunctionDisabledBrightness(0.0f);
		Light->SetMaxDrawDistance(3200);
		Light->SetMaxDistanceFadeRange(650);
		Light->ComponentTags.AddUnique(Lamp->ActorTag);
		Light->ComponentTags.AddUnique(WindowApertureTag);
		Light->Follow(Lamp->bSwitchable && SourceActor ? SourceActor->FindComponentByClass<ULightComponent>() : nullptr);
		if (!Light->IsRegistered()) Light->RegisterComponent();
		Guide.SetVisibility(false, false);
		return true;
	}
}

bool ScctWaterRefraction::IsGuide(const FString& Name)
{
	return Name.Contains(TEXT("caustic")) || Name.Contains(TEXT("caustiq"));
}

bool ScctWaterRefraction::NeedsPreparation(const ULevel& Level)
{
	for (AActor* A : Level.Actors)
	{
		const AStaticMeshActor* Actor = Cast<AStaticMeshActor>(A);
		if (!IsValid(Actor)) continue;
		const UStaticMesh* Mesh = Actor->GetStaticMeshComponent()->GetStaticMesh();
		if (Mesh && (Actor->ActorHasTag(GuideTag) || IsGuide(Mesh->GetName())) && !HasProjection(*Actor)) return true;
	}
	return false;
}

int32 ScctWaterRefraction::Build(ULevel& Level, const FString& Folder, const FJsonObject& Gltf, const FJsonObject& Lighting, const GLTF::FAsset& Geometry)
{
	if (!Level.GetWorld() || Level.GetWorld()->GetNetMode() == NM_DedicatedServer) return 0;
	TMap<FName,AActor*> Actors;
	for (AActor* Actor : Level.Actors)
	{
		if (!IsValid(Actor)) continue;
		for (FName Tag : Actor->Tags) if (Tag.ToString().StartsWith(TEXT("Scct:"))) Actors.Add(Tag,Actor);
	}
	TArray<UStaticMeshComponent*> Guides;
	const auto Panes = ReadPanes(Folder,Geometry,Gltf,Actors,Guides);
	const auto Lamps = ReadLamps(Lighting);
	if (Guides.IsEmpty() || Panes.IsEmpty()) return 0;
	auto* Function = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/OpenSCCT/Effects/M_ScctWaterRefraction.M_ScctWaterRefraction"));
	if (!Function) return 0;
	int32 Count = 0;
	for (auto* Guide : Guides) if (Project(*Guide,Panes,Lamps,Actors,*Function)) ++Count;
	UE_LOG(LogOpenSCCT,Log,TEXT("Water refraction: projected %d/%d source guides from %d transmitting triangles"),Count,Guides.Num(),Panes.Num());
	return Count;
}
