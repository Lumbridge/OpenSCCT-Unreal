#include "Play/ScctEffectsSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

bool UScctEffectsSubsystem::FindFlareContact(const FVector& Origin, FVector& Contact, FVector& Normal)
{
	// The flare emitter's rotation describes the hit location relative to the world origin.
	// Its burn projector, created by the same impact, carries the actual surface normal.
	double Best = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < ProjectionCount; ++Index)
	{
		const FScctProjection& Mark = ProjectionBuffer[Index];
		const FVector Point(Mark.X, Mark.Y, Mark.Z);
		if (FVector::DistSquared(Point, Origin) > 45.0 * 45.0 || !GetName(Mark.ClassId).EndsWith(TEXT("SFlareBurnProjector")))
		{
			continue;
		}
		const FVector Face(Mark.NormalX, Mark.NormalY, Mark.NormalZ);
		const FVector Offset = Origin - Point;
		const double StandOff = FVector::DotProduct(Offset, Face);
		const double Across = (Offset - Face * StandOff).SizeSquared();
		if (StandOff >= 0.0 && Across < 4.0 * 4.0 && Across < Best)
		{
			Contact = Point;
			Normal = Face;
			Best = Across;
		}
	}
	if (Best < TNumericLimits<double>::Max())
	{
		return true;
	}

	// A joining client can receive the burning emitter after the projectile has gone.
	// Only on attachment, find the nearest surface in the simulation's own collision.
	if (!FxTrace) FxTrace = ScctFeature<FScctFxTraceApi>();
	if (!FxTrace) return false;
	const FVector Directions[] = { FVector::ForwardVector, FVector::BackwardVector, FVector::RightVector,
		FVector::LeftVector, FVector::UpVector, FVector::DownVector };
	for (const FVector& Direction : Directions)
	{
		const FVector End = Origin + Direction * 55.0;
		FScctFxHit Hit{};
		if (!FxTrace->Trace(Origin.X, Origin.Y, Origin.Z, End.X, End.Y, End.Z, &Hit)) continue;
		const FVector Face = FVector(Hit.NormalX, Hit.NormalY, Hit.NormalZ).GetSafeNormal();
		const double Distance = FVector::DotProduct(Origin - FVector(Hit.X, Hit.Y, Hit.Z), Face);
		if (!Face.IsNearlyZero() && Distance > 0.0 && Distance < 42.0 && Distance < Best)
		{
			Best = Distance;
			Contact = Origin - Face * Distance;
			Normal = Face;
		}
	}
	return Best < TNumericLimits<double>::Max();
}

FVector UScctEffectsSubsystem::UpdateFlare(FActorFx& Fx, const FScctEffectActor& Record, const FTransform& Place,
	const FVector& PreviousLocation, float DeltaSeconds, bool bHidden, bool bRestoring, bool bHistorical)
{
	if (!Fx.Flare)
	{
		Fx.Flare = MakeUnique<FFlareVisual>();
		FFlareVisual& Burn = *Fx.Flare;
		Burn.bFlying = GetName(Record.ClassId).EndsWith(TEXT("SFlareCourseEmitter"));
		Burn.FlightDirection = Place.GetRotation().GetForwardVector();

		// The live projectile already owns its visible cartridge. After impact its replacement
		// owns one here, only when it has an actual mounting surface.
		FVector Contact, Normal;
		if (!Burn.bFlying && FindFlareContact(Fx.Location, Contact, Normal))
		{
			Burn.ContactOffset = Place.InverseTransformPosition(Contact);
			Burn.ContactNormal = Place.InverseTransformVectorNoScale(Normal);
		}
		auto AddPart = [this](UStaticMesh* Mesh, UMaterialInterface* Material)
		{
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Host, NAME_None, RF_Transient);
			Part->SetMobility(EComponentMobility::Movable);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCanEverAffectNavigation(false);
			// A point source at the cartridge lip projects an oversized hard wedge from
			// its own shell. Other objects in the scene still cast into the flare's light.
			Part->SetCastShadow(false);
			Part->bAffectDistanceFieldLighting = false;
			Part->bAffectDynamicIndirectLighting = false;
			Part->bReceivesDecals = false;
			Part->SetStaticMesh(Mesh);
			if (Material) Part->SetMaterial(0, Material);
			Part->SetupAttachment(Host->GetRootComponent());
			Part->RegisterComponent();
			return Part;
		};
		if (!bHistorical && !Burn.ContactNormal.IsZero() && FlareCartridge)
		{
			Burn.Cartridge = AddPart(FlareCartridge, nullptr);
		}
		if (!bHistorical && FlareCard && FlareMaterial)
		{
			Burn.Flame = AddPart(FlareCard, FlareMaterial);
			Burn.Flame->SetCustomPrimitiveDataFloat(0, (Record.ActorId % 89) * 0.371f);
		}
	}

	FFlareVisual& Burn = *Fx.Flare;
	const FVector Travel = Fx.Location - PreviousLocation;
	FVector Tip = Fx.Location;
	FVector Jet = FVector::UpVector;
	if (Burn.bFlying)
	{
		if (!Travel.IsNearlyZero() && !(Record.Flags & EScctEffectFlags::Snapped))
		{
			Burn.FlightDirection = Travel.GetSafeNormal();
		}
		Jet = -Burn.FlightDirection;
		Tip += Jet * 6.0;
	}
	else if (!Burn.ContactNormal.IsZero())
	{
		const FVector Normal = Place.TransformVectorNoScale(Burn.ContactNormal);
		const FVector Contact = Place.TransformPosition(Burn.ContactOffset);
		// A cartridge lies on a floor and stands proud of a wall. Keep the burning
		// mouth clear of the surface, with the plume bending upwards as it burns.
		const FVector UpOnSurface = FVector::VectorPlaneProject(FVector::UpVector, Normal);
		const FVector Tangent = UpOnSurface.GetSafeNormal(0.02, FVector::ForwardVector);
		const FVector Axis = (Tangent + Normal * 0.27).GetSafeNormal();
		double HalfLength = 7.4, Radius = 3.4;
		if (UStaticMeshComponent* Case = Burn.Cartridge.Get())
		{
			const FBoxSphereBounds Bounds = FlareCartridge->GetBounds();
			const FVector Extent = Bounds.BoxExtent * 2.0; // SProjFlare.DrawScale3D
			const int32 LongAxis = Extent.X >= Extent.Y && Extent.X >= Extent.Z ? 0 : (Extent.Y >= Extent.Z ? 1 : 2);
			FVector MeshAxis = FVector::ZeroVector;
			MeshAxis[LongAxis] = 1.0;
			HalfLength = Extent[LongAxis];
			Radius = Extent.GetMin();
			const FQuat Turn = FQuat::FindBetweenNormals(MeshAxis, Axis);
			const FVector Centre = Contact + Normal * Radius + Axis * HalfLength;
			Case->SetWorldTransform(FTransform(Turn, Centre - Turn.RotateVector(Bounds.Origin * 2.0), FVector(2.0)));
			Case->SetVisibility(!bHidden);
		}
		Tip = Contact + Normal * Radius + Axis * (2.0 * HalfLength);
		Jet = (Axis * 0.75 + FVector::UpVector + Normal * 0.22).GetSafeNormal(UE_SMALL_NUMBER, Normal);
	}

	// The settled light's brightness falls from ~100 to zero in the last five seconds.
	const float Strength = Burn.bFlying ? 1.0f : FMath::Clamp(Record.LightBrightness * 0.01f, 0.0f, 1.0f);
	if (UStaticMeshComponent* Flame = Burn.Flame.Get())
	{
		Flame->SetVisibility(!bHidden && Strength > 0.0f);
		if (!bHidden)
		{
			const FScctBridgeView& View = Session->GetView();
			const FVector Eye(View.X, View.Y, View.Z);
			const FRotationMatrix Camera(FRotator(View.Pitch, View.Yaw, View.Roll));
			const FVector ToEye = (Eye - Tip).GetSafeNormal(UE_SMALL_NUMBER, -Camera.GetUnitAxis(EAxis::X));
			const FVector InPlane = FVector::VectorPlaneProject(Jet, ToEye);
			const FVector Along = InPlane.GetSafeNormal(UE_SMALL_NUMBER, Camera.GetUnitAxis(EAxis::Z));
			const FVector Across = FVector::CrossProduct(Along, ToEye).GetSafeNormal(UE_SMALL_NUMBER, Camera.GetUnitAxis(EAxis::Y));
			const double Height = 21.0 * FMath::Lerp(0.42, 1.0, InPlane.Size()) * (0.65 + 0.35 * Strength);
			const FQuat Turn = FRotationMatrix::MakeFromXY(Across, Along).ToQuat();
			Flame->SetWorldTransform(FTransform(Turn, Tip + Along * (0.28 * Height), FVector(0.18, Height / 100.0, 1.0)));
			Flame->SetCustomPrimitiveDataFloat(1, Strength);
		}
	}
	// Cast the light from the flame, above the lip. A light at the old floating
	// emitter position projects the cartridge's shadow across its own burning tip.
	const FVector LightAt = Tip + Jet * 2.5;
	if (bHidden || Strength <= 0.0f) return LightAt;

	// Birth times are independent of the render rate. Space the trail along the flight segment,
	// and on demo seeks reconstruct only births whose smoke or sparks can still be alive.
	const float Rate = (Burn.bFlying ? 30.0f : 23.0f) * Strength;
	const float BeforeEnd = bHistorical ? FMath::Max(0.0f, Record.Age - Record.LifeSpan) : 0.0f;
	const float Step = bRestoring ? FMath::Min(Fx.Age, 1.2f) - BeforeEnd : DeltaSeconds;
	if (Step <= 0.0f || (bHistorical && !bRestoring)) return LightAt;
	Fx.EmitDebt += Rate * Step;
	for (; Fx.EmitDebt >= 1.0f; Fx.EmitDebt -= 1.0f)
	{
		const float SinceBirth = (Fx.EmitDebt - 1.0f) / Rate;
		const FVector Source = Burn.bFlying && !bRestoring && DeltaSeconds > 0.0f
			? Tip - Travel * FMath::Clamp(SinceBirth / DeltaSeconds, 0.0f, 1.0f) : Tip;
		const bool bSmoke = (++Fx.EmittedSprites % 3) == 0;
		FScctFxParticle& P = Sprites.Spawn(bSmoke ? EScctFxLayer::Cloud : EScctFxLayer::Glow);
		P.SeekAge = SinceBirth + BeforeEnd;
		P.Position = Source;
		if (bSmoke)
		{
			P.Cell = Random.RandHelper(4);
			P.Velocity = Jet * 22.0 + FVector::UpVector * 34.0 + Random.VRand() * 10.0;
			P.Life = Random.FRandRange(0.8f, 1.2f);
			P.SizeStart = 3.0f;
			P.SizeEnd = Random.FRandRange(18.0f, 26.0f);
			P.Color = FLinearColor(0.53f, 0.49f, 0.45f, 0.30f * Strength);
			P.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
			P.Spin = Random.FRandRange(-1.3f, 1.3f);
			P.FadeIn = 0.12f;
		}
		else
		{
			P.Velocity = RandomCone(Jet, 78.0f) * Random.FRandRange(52.0f, 170.0f);
			P.Color = FLinearColor(8.0f, Random.FRandRange(1.5f, 3.5f), 0.32f, Strength);
			P.Life = Random.FRandRange(0.21f, 0.64f);
			P.SizeStart = Random.FRandRange(0.7f, 1.15f);
			P.SizeEnd = 0.25f;
			P.Stretch = 0.023f;
			P.Drag = 0.8f;
			P.Gravity = 360.0f;
		}
	}
	return LightAt;
}
