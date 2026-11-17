#include "Play/ScctMarkersSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Hud/ScctVisionSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	const TCHAR* const MarkerMaterialPath = TEXT("/Game/OpenSCCT/Level/M_ScctMarker.M_ScctMarker");
	const TCHAR* const BeamMaterialPath = TEXT("/Game/OpenSCCT/Level/M_ScctBeam.M_ScctBeam");

	// The state colours. Restrained: they are read against dark rooms, where a little light is a lot.
	const FLinearColor MarkerOpen(0.08f, 0.6f, 1.0f);
	const FLinearColor MarkerTaking(1.0f, 0.58f, 0.1f);
	const FLinearColor MarkerAlarm(1.0f, 0.1f, 0.06f);
	const FLinearColor MarkerDone(0.2f, 1.0f, 0.45f);
	const FLinearColor MarkerIdle(0.3f, 0.42f, 0.6f);
	const FLinearColor MarkerWatching(0.15f, 1.0f, 0.3f);

	// A sign keeps about the same size on screen between these two sizes in the world (cm across).
	constexpr float SignSizePerDistance = 0.02f;
	constexpr float SignSizeMin = 15.0f;
	constexpr float SignSizeMax = 64.0f;

	// The gap between the top of the marked thing and the sign's lower edge (cm).
	constexpr float SignClearance = 7.0f;

	// It fades out as the camera comes right up to it, and far away where it would be a speck.
	constexpr float SignNearStart = 45.0f;
	constexpr float SignNearFull = 130.0f;
	constexpr float SignFarFull = 5200.0f;
	constexpr float SignFarEnd = 7000.0f;

	// A beam: the core is the laser itself, the halo the dust it lights (cm across).
	constexpr float BeamCoreWidth = 0.7f;
	constexpr float BeamHaloWidth = 7.0f;
	constexpr float BeamEndSize = 5.0f;
	constexpr float LampSize = 6.0f;

	// The goggles' beam: never thinner than this across (cm), and wider with distance so it stays about one and a
	// half pixels across in a full HD picture, as the original's beam mesh never vanishes between pixels.
	constexpr float VisionBeamMinWidth = 1.2f;
	constexpr float VisionBeamWidthPerDistance = 0.0022f;

	const FName MarkerColorParameter(TEXT("Color"));
	const FName MarkerIntensityParameter(TEXT("Intensity"));
	const FName MarkerProgressParameter(TEXT("Progress"));
	const FName MarkerPulseParameter(TEXT("Pulse"));
	const FName MarkerShapeParameter(TEXT("Shape"));
	const FName MarkerSharpnessParameter(TEXT("Sharpness"));
	const FName MarkerShimmerParameter(TEXT("Shimmer"));
}

bool UScctMarkersSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctMarkersSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	// The level state first: its frame places (and spawns) the things the signs stand over.
	LevelState = Collection.InitializeDependency<UScctLevelStateSubsystem>();
	if (Session == nullptr)
	{
		return;
	}

	Session->OnSessionChanged.AddUObject(this, &UScctMarkersSubsystem::HandleSessionChanged);
	Session->OnMatchStarted.AddUObject(this, &UScctMarkersSubsystem::HandleMatchStarted);
	Session->OnFrame.AddUObject(this, &UScctMarkersSubsystem::HandleFrame);

	bShowVisionLasers = FParse::Param(FCommandLine::Get(), TEXT("ScctShowVisionLasers"));
	bLog = FParse::Param(FCommandLine::Get(), TEXT("ScctLevelLog"));
	MarkerBuffer.SetNumUninitialized(256);
	LaserBuffer.SetNumUninitialized(128);
}

void UScctMarkersSubsystem::Deinitialize()
{
	if (Session != nullptr)
	{
		Session->OnSessionChanged.RemoveAll(this);
		Session->OnMatchStarted.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------------------------------------------

void UScctMarkersSubsystem::HandleSessionChanged(int32 OldId, int32 NewId)
{
	if (OldId != 0 && bReported)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Markers summary: at most %d objective signs, %d status lamps and %d laser beams drawn at once; %d beams the map shows only in thermal and EMF vision, at most %d beams drawn for the goggles"),
			PeakSigns, PeakLamps, PeakBeams, NumVisionOnlyBeams, PeakGoggleBeams);
	}
	// Name ids and beam ids belong to one match.
	ResetMatch();
}

void UScctMarkersSubsystem::ResetMatch()
{
	for (FPool* Pool : { &Signs, &Lamps, &Cores, &Halos, &VisionBeams })
	{
		for (int32 Index = 0; Index < Pool->Used.Num(); ++Index)
		{
			Pool->Used[Index] = false;
			if (UStaticMeshComponent* Component = Pool->Components[Index].Get())
			{
				Component->SetVisibility(false);
			}
		}
	}
	Drawn.Reset();
	Beams.Reset();
	MatchSeconds = 0.0f;
	bReported = false;
	PeakSigns = PeakLamps = PeakBeams = NumVisionOnlyBeams = PeakGoggleBeams = 0;
	LoggedGoggleBeams = LoggedGoggleMode = 0;
}

void UScctMarkersSubsystem::HandleMatchStarted()
{
	FString Error;
	World = ScctFeature<FScctWorldApi>(&Error);
	if (World == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Markers: none this match, the bridge's world table is missing: %s"), *Error);
		return;
	}
	EnsureScene();
}

bool UScctMarkersSubsystem::EnsureScene()
{
	if (bSceneReady || bSceneFailed)
	{
		return bSceneReady;
	}

	MarkerMaterial = LoadObject<UMaterialInterface>(nullptr, MarkerMaterialPath);
	BeamMaterial = LoadObject<UMaterialInterface>(nullptr, BeamMaterialPath);
	PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (MarkerMaterial == nullptr || BeamMaterial == nullptr || PlaneMesh == nullptr || CylinderMesh == nullptr || SphereMesh == nullptr)
	{
		bSceneFailed = true;
		UE_LOG(LogOpenSCCT, Error, TEXT("Markers: nothing is drawn, the marker materials are missing: run Tools/Level/build_level_assets.py"));
		return false;
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	Host = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	if (Host == nullptr)
	{
		bSceneFailed = true;
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	Host->SetRootComponent(Root);
	Root->RegisterComponent();

	Signs.Mesh = PlaneMesh;
	Signs.Material = MarkerMaterial;
	Lamps.Mesh = SphereMesh;
	Lamps.Material = BeamMaterial;
	Cores.Mesh = CylinderMesh;
	Cores.Material = BeamMaterial;
	Halos.Mesh = CylinderMesh;
	Halos.Material = BeamMaterial;
	// Never drawn in the picture, only into the custom depth stencil, so any opaque material will do: the engine's
	// own one for its shapes.
	VisionBeams.Mesh = CylinderMesh;
	VisionBeams.Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (VisionBeams.Material == nullptr)
	{
		VisionBeams.Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	bSceneReady = true;
	return true;
}

int32 UScctMarkersSubsystem::Acquire(FPool& Pool)
{
	int32 Slot = Pool.Used.Find(false);
	if (Slot == INDEX_NONE)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Host);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetStaticMesh(Pool.Mesh.Get());
		// The component owns its material, so it lives exactly as long as what it colours.
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Pool.Material.Get(), Component);
		Component->SetMaterial(0, Material);
		Component->SetupAttachment(Host->GetRootComponent());
		Component->RegisterComponent();
		Slot = Pool.Used.Add(false);
		Pool.Components.Add(Component);
		Pool.Materials.Add(Material);
	}
	Pool.Used[Slot] = true;
	if (UStaticMeshComponent* Component = Pool.Components[Slot].Get())
	{
		Component->SetVisibility(true);
	}
	return Slot;
}

void UScctMarkersSubsystem::Release(FPool& Pool, int32& Slot)
{
	if (Pool.Used.IsValidIndex(Slot))
	{
		Pool.Used[Slot] = false;
		if (UStaticMeshComponent* Component = Pool.Components[Slot].Get())
		{
			Component->SetVisibility(false);
		}
	}
	Slot = INDEX_NONE;
}

// ---------------------------------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------------------------------

void UScctMarkersSubsystem::HandleFrame(float DeltaSeconds)
{
	if (World == nullptr || !bSceneReady || !IsValid(Host))
	{
		return;
	}

	++Frame;
	MatchSeconds += DeltaSeconds;
	const FScctBridgeView& View = Session->GetView();
	UpdateMarkers(FVector(View.X, View.Y, View.Z));
	UpdateLasers();
}

UScctMarkersSubsystem::FLook UScctMarkersSubsystem::LookOf(const FScctWorldMarker& Marker)
{
	FLook Look;
	const auto Has = [&Marker](int32 Flag) { return (Marker.Flags & Flag) != 0; };

	switch (Marker.Kind)
	{
	case EScctWorldMarkerKind::Computer:
	case EScctWorldMarkerKind::BombTarget:
	case EScctWorldMarkerKind::Flag:
	case EScctWorldMarkerKind::FlagDropZone:
	case EScctWorldMarkerKind::ObjectiveTrigger:
	case EScctWorldMarkerKind::Disk:
		if (Has(EScctWorldMarkerFlags::Hidden))
		{
			// Not part of this game mode, or not shown yet.
			break;
		}
		Look.bSign = true;
		Look.Shape = Marker.Kind == EScctWorldMarkerKind::FlagDropZone ? 1.0f
			: Marker.Kind == EScctWorldMarkerKind::Flag || Marker.Kind == EScctWorldMarkerKind::Disk ? 2.0f : 0.0f;
		Look.Color = MarkerIdle;
		Look.Intensity = 0.6f;
		if (Has(EScctWorldMarkerFlags::Active))
		{
			Look.Color = MarkerOpen;
			Look.Intensity = 1.2f;
		}
		if (Has(EScctWorldMarkerFlags::Taken))
		{
			Look.Color = MarkerDone;
			Look.Intensity = 0.7f;
		}
		if (Has(EScctWorldMarkerFlags::Locked))
		{
			Look.Color = MarkerAlarm;
			Look.Intensity = 1.4f;
		}
		if (Marker.Kind == EScctWorldMarkerKind::Flag && Marker.State != 0)
		{
			// The disk is away from its rack: the sign goes with whoever carries it, over their head.
			Look.Color = MarkerTaking;
			Look.Intensity = 1.5f;
			Look.Pulse = 0.5f;
			Look.Lift = Marker.OwnerPawnId != 0 ? 105.0f : Look.Lift;
		}
		if (Marker.Kind == EScctWorldMarkerKind::BombTarget && Marker.State != 0)
		{
			Look.Color = MarkerAlarm;
			Look.Intensity = 1.7f;
			Look.Pulse = 1.0f;
		}
		if (Has(EScctWorldMarkerFlags::Triggering))
		{
			Look.Color = Marker.Kind == EScctWorldMarkerKind::BombTarget && Marker.State != 0 ? MarkerAlarm : MarkerTaking;
			Look.Intensity = 1.7f;
			Look.Pulse = 1.0f;
		}
		Look.Progress = FMath::Clamp(Marker.Progress, 0.0f, 1.0f);
		if (Marker.Kind == EScctWorldMarkerKind::Disk)
		{
			Look.Size = 0.6f;
			Look.Lift = 24.0f;
		}
		if (Has(EScctWorldMarkerFlags::Gone))
		{
			// Out of sight of the network: this is the last known state.
			Look.Intensity *= 0.6f;
		}
		break;

	case EScctWorldMarkerKind::Camera:
		// LightState: 0 locked, 1 inactive, 2 patrol, 3 alert.
		if (!Has(EScctWorldMarkerFlags::Gone) && !Has(EScctWorldMarkerFlags::Hidden) && Marker.State != 1)
		{
			Look.bLamp = true;
			Look.Color = Marker.State == 3 ? MarkerAlarm : Marker.State == 2 ? MarkerWatching : MarkerTaking;
			Look.Intensity = Marker.State == 3 ? 7.0f : 3.5f;
			Look.Pulse = Marker.State == 3 ? 1.0f : 0.0f;
		}
		break;

	case EScctWorldMarkerKind::VolumetricSensor:
		// bLightState: 0 broken, 1 off, 2 green, 3 red.
		if (!Has(EScctWorldMarkerFlags::Gone) && !Has(EScctWorldMarkerFlags::Hidden) && Marker.State >= 2)
		{
			Look.bLamp = true;
			Look.Color = Marker.State == 3 ? MarkerAlarm : MarkerWatching;
			Look.Intensity = Marker.State == 3 ? 7.0f : 3.5f;
			Look.Pulse = Marker.State == 3 ? 1.0f : 0.0f;
		}
		break;

	default:
		// Alarms are heard, and a tripwire is its beam.
		break;
	}
	return Look;
}

void UScctMarkersSubsystem::UpdateMarkers(const FVector& ViewLocation)
{
	int32 Total = World->GetMarkers(MarkerBuffer.GetData(), MarkerBuffer.Num());
	if (Total > MarkerBuffer.Num())
	{
		MarkerBuffer.SetNumUninitialized(Total + 32);
		Total = World->GetMarkers(MarkerBuffer.GetData(), MarkerBuffer.Num());
	}
	const int32 Count = FMath::Clamp(Total, 0, MarkerBuffer.Num());

	int32 NumSigns = 0;
	int32 NumLamps = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctWorldMarker& Marker = MarkerBuffer[Index];
		const FLook Look = LookOf(Marker);
		FDrawn* Known = Drawn.Find(Marker.NameId);
		if (!Look.bSign && !Look.bLamp)
		{
			if (Known != nullptr)
			{
				Release(Known->bLamp ? Lamps : Signs, Known->Slot);
				Drawn.Remove(Marker.NameId);
			}
			continue;
		}
		if (Known != nullptr && Known->bLamp != Look.bLamp)
		{
			Release(Known->bLamp ? Lamps : Signs, Known->Slot);
		}
		if (Known == nullptr)
		{
			Known = &Drawn.Add(Marker.NameId);
		}
		FPool& Pool = Look.bLamp ? Lamps : Signs;
		if (Known->Slot == INDEX_NONE)
		{
			Known->bLamp = Look.bLamp;
			Known->Slot = Acquire(Pool);
		}
		Known->SeenFrame = Frame;

		UStaticMeshComponent* Component = Pool.Components[Known->Slot].Get();
		UMaterialInstanceDynamic* Material = Pool.Materials[Known->Slot].Get();
		if (Component == nullptr || Material == nullptr)
		{
			continue;
		}

		const FVector At(Marker.X, Marker.Y, Marker.Z);

		// The actor drawn for the marked thing (a terminal, a case, a camera), looked for again now and then: it
		// may be spawned only when the simulation first shows it.
		if (Marker.OwnerPawnId == 0 && !Known->Object.IsValid() && (Frame - Known->LookedFrame > 90 || Known->LookedFrame == 0))
		{
			Known->LookedFrame = Frame;
			Known->Object = LevelState != nullptr ? LevelState->FindLevelActor(LevelState->GetName(Marker.NameId)) : nullptr;
		}
		const AActor* Object = Marker.OwnerPawnId == 0 ? Known->Object.Get() : nullptr;

		if (Look.bLamp)
		{
			++NumLamps;
			const float Throb = 1.0f + Look.Pulse * 0.35f * FMath::Sin(MatchSeconds * 9.0f);
			// The marker's place is the device's origin, inside its body: the lamp sits on top of the body at its
			// front (the original's devices face along their X axis), where it is seen from the room.
			FVector LampAt = At;
			const UStaticMeshComponent* Body = Object != nullptr ? Object->FindComponentByClass<UStaticMeshComponent>() : nullptr;
			if (Body != nullptr && Body->GetStaticMesh() != nullptr)
			{
				const FBox Box = Body->GetStaticMesh()->GetBoundingBox();
				const FTransform& BodyTransform = Body->GetComponentTransform();
				LampAt = BodyTransform.TransformPosition(FVector(FMath::Lerp(Box.Min.X, Box.Max.X, 0.65), Box.GetCenter().Y, Box.Max.Z))
					+ BodyTransform.GetUnitAxis(EAxis::Z) * (LampSize * 0.3);
			}
			Component->SetWorldLocationAndRotation(LampAt, FQuat::Identity);
			Component->SetWorldScale3D(FVector(LampSize / 100.0f));
			Material->SetVectorParameterValue(MarkerColorParameter, Look.Color);
			Material->SetScalarParameterValue(MarkerIntensityParameter, Look.Intensity * Throb);
			Material->SetScalarParameterValue(MarkerSharpnessParameter, 2.5f);
			continue;
		}

		++NumSigns;
		// Over the top of the thing it marks when that is drawn (a terminal, a case), so it never sits in it; a
		// carried disk's sign rides over its carrier's head.
		FVector Place = At + FVector(0.0, 0.0, Look.Lift);
		if (Object != nullptr && !Object->IsHidden())
		{
			const FBox Bounds = Object->GetComponentsBoundingBox();
			if (Bounds.IsValid && Bounds.GetExtent().GetMax() < 200.0)
			{
				Place = FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + SignClearance);
			}
		}
		const float Distance = static_cast<float>(FVector::Dist(ViewLocation, Place));
		const float Fade = FMath::Clamp((Distance - SignNearStart) / (SignNearFull - SignNearStart), 0.0f, 1.0f)
			* FMath::Clamp((SignFarEnd - Distance) / (SignFarEnd - SignFarFull), 0.0f, 1.0f);
		const float Size = FMath::Clamp(Distance * SignSizePerDistance, SignSizeMin, SignSizeMax) * Look.Size;
		Place.Z += Size * 0.5f;
		const FVector ToCamera = ViewLocation - Place;

		// The quad lies in its own XY plane with the picture's top at -Y: its face is turned to the camera and
		// its Y is kept pointing down, so the sign stands upright from wherever it is seen.
		const FVector Facing = ToCamera.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		const FQuat Rotation = FMath::Abs(Facing.Z) < 0.995 ? FRotationMatrix::MakeFromZY(Facing, FVector::DownVector).ToQuat() : FRotationMatrix::MakeFromZ(Facing).ToQuat();
		Component->SetWorldLocationAndRotation(Place, Rotation);
		Component->SetWorldScale3D(FVector(Size / 100.0f));
		Component->SetVisibility(Fade > 0.0f);
		Material->SetVectorParameterValue(MarkerColorParameter, Look.Color);
		Material->SetScalarParameterValue(MarkerIntensityParameter, Look.Intensity * Fade);
		Material->SetScalarParameterValue(MarkerProgressParameter, Look.Progress);
		Material->SetScalarParameterValue(MarkerPulseParameter, Look.Pulse);
		Material->SetScalarParameterValue(MarkerShapeParameter, Look.Shape);
	}

	// A marker the table no longer lists at all.
	for (auto It = Drawn.CreateIterator(); It; ++It)
	{
		if (It.Value().SeenFrame != Frame)
		{
			Release(It.Value().bLamp ? Lamps : Signs, It.Value().Slot);
			It.RemoveCurrent();
		}
	}

	PeakSigns = FMath::Max(PeakSigns, NumSigns);
	PeakLamps = FMath::Max(PeakLamps, NumLamps);
	if (!bReported && Count > 0)
	{
		bReported = true;
		int32 ByKind[11] = {};
		for (int32 Index = 0; Index < Count; ++Index)
		{
			++ByKind[FMath::Clamp(MarkerBuffer[Index].Kind, 0, 10)];
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Markers: the match lists %d (%d terminals, %d bomb spots, %d disks to carry, %d drop zones, %d other objectives, %d alarms, %d cameras, %d room sensors, %d tripwires, %d hunt tags); "
			"%d drawn as signs and %d as status lamps at the first frame"),
			Count, ByKind[1], ByKind[2], ByKind[3], ByKind[4], ByKind[5], ByKind[6], ByKind[7], ByKind[8], ByKind[9], ByKind[10], NumSigns, NumLamps);
		if (bLog)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FScctWorldMarker& Marker = MarkerBuffer[Index];
				UE_LOG(LogOpenSCCT, Log, TEXT("Markers: #%d kind %d state %d flags %d at (%.0f, %.0f, %.0f)"), Marker.NameId, Marker.Kind, Marker.State, Marker.Flags, Marker.X, Marker.Y, Marker.Z);
			}
		}
	}
}

void UScctMarkersSubsystem::UpdateLasers()
{
	int32 Total = World->GetLasers(LaserBuffer.GetData(), LaserBuffer.Num());
	if (Total > LaserBuffer.Num())
	{
		LaserBuffer.SetNumUninitialized(Total + 32);
		Total = World->GetLasers(LaserBuffer.GetData(), LaserBuffer.Num());
	}
	const int32 Count = FMath::Clamp(Total, 0, LaserBuffer.Num());

	// Thermal and EMF draw every beam from its vision look only (the original's FaithfulView hides the normal streak
	// while the vision-only nodes are shown); the other modes draw the beams the map does not hide, as in plain sight.
	const UScctVisionSubsystem* Vision = UScctVisionSubsystem::Find(GetWorld());
	const int32 Mode = Vision != nullptr ? Vision->GetVisionMode() : 0;
	const bool bGoggleLook = Vision != nullptr && Vision->IsHeatOrEmf();
	const FScctBridgeView& View = Session->GetView();
	const FVector Eye(View.X, View.Y, View.Z);

	int32 NumDrawn = 0;
	int32 NumVisionOnly = 0;
	int32 NumGoggle = 0;
	int32 NumGoggleVisionOnly = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctWorldLaser& Laser = LaserBuffer[Index];
		const bool bVisionOnly = (Laser.Flags & EScctWorldLaserFlags::HideInNormalVision) != 0;
		NumVisionOnly += bVisionOnly ? 1 : 0;
		const bool bStreak = !bGoggleLook && (!bVisionOnly || bShowVisionLasers);
		if (!bStreak && !bGoggleLook)
		{
			continue;
		}

		const FVector Start(Laser.StartX, Laser.StartY, Laser.StartZ);
		const FVector End(Laser.EndX, Laser.EndY, Laser.EndZ);
		const double Length = FVector::Dist(Start, End);
		if (Length < 1.0)
		{
			continue;
		}

		FBeam& Beam = Beams.FindOrAdd(Laser.Id);
		if (Beam.Core == INDEX_NONE && Beam.Vision == INDEX_NONE && bLog)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Markers: %.1f s laser beam %d (%s) from (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f), heat %.2f%s"), MatchSeconds, Laser.Id, Laser.Kind == 1 ? TEXT("mine") : TEXT("tripwire"),
				Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z, Laser.Heat, bVisionOnly ? TEXT(", vision modes only") : TEXT(""));
		}
		Beam.SeenFrame = Frame;
		const FQuat Along = FRotationMatrix::MakeFromZ(End - Start).ToQuat();
		const FVector Middle = (Start + End) * 0.5;

		// The goggles' look: a cylinder drawn only into the custom depth stencil, which the mode's material paints
		// (thermal at the beam's heat, EMF as a dark grey streak).
		if (bGoggleLook)
		{
			if (Beam.Vision == INDEX_NONE)
			{
				Beam.Vision = Acquire(VisionBeams);
			}
			if (UStaticMeshComponent* Component = VisionBeams.Components[Beam.Vision].Get())
			{
				const FVector Nearest = FMath::ClosestPointOnSegment(Eye, Start, End);
				const double Width = FMath::Max<double>(VisionBeamMinWidth, FVector::Dist(Eye, Nearest) * VisionBeamWidthPerDistance);
				Component->SetRenderInMainPass(false);
				Component->SetRenderInDepthPass(false);
				Component->SetRenderCustomDepth(true);
				Component->SetCustomDepthStencilValue(Mode == 36 ? UScctVisionSubsystem::HeatStencil(Laser.Heat) : UScctVisionSubsystem::StencilEmfStreak);
				Component->SetWorldLocationAndRotation(Middle, Along);
				Component->SetWorldScale3D(FVector(Width / 100.0, Width / 100.0, Length / 100.0));
			}
			++NumGoggle;
			NumGoggleVisionOnly += bVisionOnly ? 1 : 0;
		}
		else
		{
			Release(VisionBeams, Beam.Vision);
		}

		if (!bStreak)
		{
			Release(Cores, Beam.Core);
			Release(Halos, Beam.Halo);
			Release(Lamps, Beam.Ends[0]);
			Release(Lamps, Beam.Ends[1]);
			continue;
		}
		if (Beam.Core == INDEX_NONE)
		{
			Beam.Core = Acquire(Cores);
			Beam.Halo = Acquire(Halos);
			Beam.Ends[0] = Acquire(Lamps);
			Beam.Ends[1] = Acquire(Lamps);
		}
		++NumDrawn;

		// A mine's beam is fainter than a tripwire's, as its heat is in the original (0.4 against 1).
		const float Strength = (Laser.Kind == 1 ? 0.6f : 1.0f) * (bVisionOnly ? 0.35f : 1.0f);
		const auto Place = [&](FPool& Pool, int32 Slot, const FVector& At, const FVector& Scale, float Intensity, float Sharpness, float Shimmer)
		{
			UStaticMeshComponent* Component = Pool.Components[Slot].Get();
			UMaterialInstanceDynamic* Material = Pool.Materials[Slot].Get();
			if (Component != nullptr && Material != nullptr)
			{
				Component->SetWorldLocationAndRotation(At, Along);
				Component->SetWorldScale3D(Scale);
				Material->SetVectorParameterValue(MarkerColorParameter, MarkerAlarm);
				Material->SetScalarParameterValue(MarkerIntensityParameter, Intensity * Strength);
				Material->SetScalarParameterValue(MarkerSharpnessParameter, Sharpness);
				Material->SetScalarParameterValue(MarkerShimmerParameter, Shimmer);
			}
		};
		// The engine's cylinder is a metre tall along Z and a metre across.
		Place(Cores, Beam.Core, Middle, FVector(BeamCoreWidth / 100.0, BeamCoreWidth / 100.0, Length / 100.0), 26.0f, 1.2f, 0.0f);
		Place(Halos, Beam.Halo, Middle, FVector(BeamHaloWidth / 100.0, BeamHaloWidth / 100.0, Length / 100.0), 0.5f, 3.0f, 1.0f);
		Place(Lamps, Beam.Ends[0], Start, FVector(BeamEndSize / 100.0), 5.0f, 3.0f, 0.0f);
		Place(Lamps, Beam.Ends[1], End, FVector(BeamEndSize / 100.0), 5.0f, 3.0f, 0.0f);
	}

	// A beam that is not listed any more is gone (the tripwire was cut, the mine went off).
	for (auto It = Beams.CreateIterator(); It; ++It)
	{
		FBeam& Beam = It.Value();
		if (Beam.SeenFrame != Frame)
		{
			Release(Cores, Beam.Core);
			Release(Halos, Beam.Halo);
			Release(Lamps, Beam.Ends[0]);
			Release(Lamps, Beam.Ends[1]);
			Release(VisionBeams, Beam.Vision);
			It.RemoveCurrent();
		}
	}

	PeakBeams = FMath::Max(PeakBeams, NumDrawn);
	PeakGoggleBeams = FMath::Max(PeakGoggleBeams, NumGoggle);
	NumVisionOnlyBeams = FMath::Max(NumVisionOnlyBeams, NumVisionOnly);
	const int32 GoggleMode = bGoggleLook ? Mode : 0;
	if (NumGoggleVisionOnly != LoggedGoggleBeams || GoggleMode != LoggedGoggleMode)
	{
		LoggedGoggleBeams = NumGoggleVisionOnly;
		LoggedGoggleMode = GoggleMode;
		UE_LOG(LogOpenSCCT, Log, TEXT("Markers: %d vision-only beams drawn (mode %d), %d beams in all for the goggles, %d listed"), NumGoggleVisionOnly, Mode, NumGoggle, Count);
	}
}
