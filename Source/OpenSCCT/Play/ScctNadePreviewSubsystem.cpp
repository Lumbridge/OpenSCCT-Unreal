#include "Play/ScctNadePreviewSubsystem.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Play/ScctEquipmentSubsystem.h"
#include "Play/ScctFirstPersonView.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctNadePreviewApi.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	/** The player's choice, kept across maps until the game is closed. */
	bool GNadePreviewWanted = false;

	/** Just below the HUD: UMG puts a widget added at z-order N at N + 10, and the HUD is added at 10. */
	constexpr int32 NadePreviewZOrder = 19;

	constexpr double NadePreviewNoticeSeconds = 4.0;

	/** The path's points asked of the bridge: it thins a longer flight to fit (every second tick of a 3 s fuse). */
	constexpr int32 NadePreviewMaxPoints = 96;

	/** The line is this share of its distance from the camera wide (about 2.5 pixels at 1080p), within these cm. */
	constexpr double NadePreviewWidthShare = 0.0024;
	constexpr double NadePreviewMinWidth = 0.6;
	constexpr double NadePreviewMaxWidth = 8.0;

	/** The ring on the surface where the projectile ends (cm), and the widest effect ring drawn. */
	constexpr double NadePreviewLandRadius = 16.0;
	constexpr double NadePreviewMaxEffectRadius = 650.0;
	constexpr int32 NadePreviewLandSegments = 20;
	constexpr int32 NadePreviewEffectSegments = 40;

	/** The picture: its size, how often it is drawn again, and its field of view. */
	constexpr int32 NadePreviewPictureWidth = 384;
	constexpr int32 NadePreviewPictureHeight = 288;
	constexpr float NadePreviewPictureInterval = 1.0f / 15.0f;
	constexpr float NadePreviewPictureFov = 75.0f;

	const FLinearColor NadePreviewReady(0.15f, 1.0f, 0.35f);
	const FLinearColor NadePreviewCharging(1.0f, 0.62f, 0.05f);
	const FLinearColor NadePreviewBlocked(1.0f, 0.08f, 0.05f);
	const FLinearColor NadePreviewMarker(1.0f, 0.3f, 0.05f);
	constexpr float NadePreviewGlow = 5.0f;

	const TCHAR* const NadePreviewRefusal = TEXT("nadepreview works offline and in practice games only.");

	const TCHAR* NadePreviewKindName(int32 Kind)
	{
		switch (Kind)
		{
		case EScctNadePreviewKind::Frag: return TEXT("frag");
		case EScctNadePreviewKind::Flare: return TEXT("flare");
		case EScctNadePreviewKind::Smoke: return TEXT("smoke");
		case EScctNadePreviewKind::Chaff: return TEXT("chaff");
		case EScctNadePreviewKind::FlashBang: return TEXT("flash-bang");
		case EScctNadePreviewKind::Phosphorus: return TEXT("phosphorus");
		default: return TEXT("grenade");
		}
	}

	/** The world a console command without one means: the first game world that plays. */
	UWorld* NadePreviewGameWorld(UWorld* World)
	{
		if (World != nullptr && World->IsGameWorld())
		{
			return World;
		}
		if (GEngine != nullptr)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() != nullptr && Context.World()->IsGameWorld())
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}

	void NadePreviewCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		int32 Mode = -1;
		if (Args.Num() > 0)
		{
			const FString& Arg = Args[0];
			if (Arg == TEXT("1") || Arg.Equals(TEXT("on"), ESearchCase::IgnoreCase))
			{
				Mode = 1;
			}
			else if (Arg == TEXT("0") || Arg.Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				Mode = 0;
			}
			else
			{
				Output.Log(TEXT("usage: NadePreview [0|1]"));
				return;
			}
		}

		UWorld* Game = NadePreviewGameWorld(World);
		UScctNadePreviewSubsystem* Subsystem = Game != nullptr ? Game->GetSubsystem<UScctNadePreviewSubsystem>() : nullptr;
		if (Subsystem == nullptr)
		{
			// No game world yet (the menus of a world without the subsystem): the choice is kept for the next match.
			GNadePreviewWanted = Mode < 0 ? !GNadePreviewWanted : Mode != 0;
			Output.Log(GNadePreviewWanted ? TEXT("nadepreview: on, from the next match") : TEXT("nadepreview: off"));
			return;
		}
		Output.Log(Subsystem->Request(Mode));
	}

	const TCHAR* const NadePreviewHelp =
		TEXT("NadePreview [0|1] - offline and practice only: draw the path the selected grenade would fly if thrown now, ")
		TEXT("its landing point and a small picture of it. Without an argument it toggles.");

	FAutoConsoleCommandWithWorldArgsAndOutputDevice NadePreviewConsoleCommand(
		TEXT("NadePreview"), NadePreviewHelp, FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NadePreviewCommand));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice NadePreviewScctConsoleCommand(
		TEXT("scct.NadePreview"), NadePreviewHelp, FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NadePreviewCommand));
}

UScctNadePreviewSubsystem::UScctNadePreviewSubsystem()
	: Model(MakeShared<FScctNadePreviewPipModel>())
{
}

bool UScctNadePreviewSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UScctNadePreviewSubsystem::IsWanted()
{
	return GNadePreviewWanted;
}

void UScctNadePreviewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UScctSessionSubsystem* Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	if (Session == nullptr)
	{
		return;
	}
	Session->OnSessionChanged.AddUObject(this, &UScctNadePreviewSubsystem::HandleSessionChanged);
	Session->OnMatchStopped.AddUObject(this, &UScctNadePreviewSubsystem::HandleMatchStopped);
	if (FParse::Param(FCommandLine::Get(), TEXT("ScctNadePreview")))
	{
		GNadePreviewWanted = true;
	}
	FParse::Value(FCommandLine::Get(), TEXT("ScctNadePreviewHold="), HoldAt);
	FParse::Value(FCommandLine::Get(), TEXT("ScctNadePreviewOff="), OffAt);
	if (GNadePreviewWanted)
	{
		Bind();
	}
}

void UScctNadePreviewSubsystem::Deinitialize()
{
	if (UScctSessionSubsystem* Session = GetWorld() != nullptr ? GetWorld()->GetSubsystem<UScctSessionSubsystem>() : nullptr)
	{
		Session->OnSessionChanged.RemoveAll(this);
		Session->OnMatchStopped.RemoveAll(this);
	}
	Unbind();
	HideWidget();
	DestroyStage();
	Super::Deinitialize();
}

void UScctNadePreviewSubsystem::Bind()
{
	if (!TickHandle.IsValid())
	{
		// After the world's ticks: the session subsystem has read this frame's view and moved the camera, and the
		// equipment has placed the gun whose muzzle the line leaves. Nothing is drawn before this in the frame.
		TickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &UScctNadePreviewSubsystem::HandlePostActorTick);
	}
}

void UScctNadePreviewSubsystem::Unbind()
{
	if (TickHandle.IsValid())
	{
		FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
		TickHandle.Reset();
	}
}

FString UScctNadePreviewSubsystem::Request(int32 Mode)
{
	const bool bOn = Mode < 0 ? !GNadePreviewWanted : Mode != 0;
	if (!bOn)
	{
		if (GNadePreviewWanted)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("NadePreview: off (it worked on %d frames, %d with a path, %d pictures)"), FramesWorked, FramesWithPath, CapturesMade);
		}
		GNadePreviewWanted = false;
		Unbind();
		HideDrawn();
		DestroyStage();
		Model->bPicture = false;
		const bool bNotice = !Model->Notice.IsEmpty() && FPlatformTime::Seconds() < Model->NoticeUntil;
		if (!bNotice)
		{
			HideWidget();
		}
		return TEXT("nadepreview: off");
	}

	const UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	if (Session != nullptr && Session->IsPlaying())
	{
		Api = ScctFeature<FScctNadePreviewApi>();
		if (Api == nullptr)
		{
			UE_LOG(LogOpenSCCT, Error, TEXT("NadePreview: the bridge has no grenade preview table (its reason is in the log above)"));
			return TEXT("nadepreview: the bridge has no grenade preview table");
		}
		if (Api->IsAllowed() == 0)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("NadePreview: refused, the match is on another server and is not a practice round"));
			Notify(NadePreviewRefusal);
			return NadePreviewRefusal;
		}
	}

	if (!GNadePreviewWanted)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("NadePreview: on"));
	}
	GNadePreviewWanted = true;
	bRefused = false;
	LogTimer = 0.0f;
	Bind();
	return Session != nullptr && Session->IsPlaying()
		? TEXT("nadepreview: on (select a grenade and have the gun out)")
		: TEXT("nadepreview: on, from the next match");
}

void UScctNadePreviewSubsystem::Notify(const FString& Text)
{
	Model->Notice = Text;
	Model->NoticeUntil = FPlatformTime::Seconds() + NadePreviewNoticeSeconds;
	ShowWidget();
}

void UScctNadePreviewSubsystem::HandleSessionChanged(int32 OldSessionId, int32 NewSessionId)
{
	// What is drawn belongs to one match; the player's choice stays.
	HideDrawn();
	DestroyStage();
	Model->bPicture = false;
	Model->Notice.Reset();
	HideWidget();
	Api = nullptr;
	bRefused = false;
	ShownFor = 0.0f;
	HoldStage = 0;
	bOffDone = false;
	if (GNadePreviewWanted)
	{
		Bind();
	}
}

void UScctNadePreviewSubsystem::HandleMatchStopped()
{
	HideDrawn();
	Model->bPicture = false;
}

void UScctNadePreviewSubsystem::HandlePostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld())
	{
		return;
	}
	if (!GNadePreviewWanted)
	{
		// Turned off through another world's subsystem.
		Unbind();
		HideDrawn();
		return;
	}
	UpdateFrame(DeltaSeconds);
}

void UScctNadePreviewSubsystem::UpdateFrame(float DeltaSeconds)
{
	UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	if (Session == nullptr || !Session->IsPlaying())
	{
		if (bDrawn)
		{
			HideDrawn();
			Model->bPicture = false;
		}
		return;
	}
	if (Api == nullptr)
	{
		Api = ScctFeature<FScctNadePreviewApi>();
		if (Api == nullptr)
		{
			UE_LOG(LogOpenSCCT, Error, TEXT("NadePreview: the bridge has no grenade preview table; the preview is off"));
			GNadePreviewWanted = false;
			Unbind();
			return;
		}
	}
	if (bRefused)
	{
		return;
	}
	if (Api->IsAllowed() == 0)
	{
		// A match on another server that is not a practice round: said once, asked again in the next match.
		bRefused = true;
		HideDrawn();
		Model->bPicture = false;
		UE_LOG(LogOpenSCCT, Warning, TEXT("NadePreview: not shown, the match is on another server and is not a practice round"));
		Notify(NadePreviewRefusal);
		return;
	}

	const double Began = FPlatformTime::Seconds();
	++FramesWorked;
	FScctNadePreview Head;
	FScctNadePoint Raw[NadePreviewMaxPoints];
	const int32 Count = FMath::Clamp(Api->GetPreview(&Head, Raw, NadePreviewMaxPoints), 0, NadePreviewMaxPoints);
	const double Read = FPlatformTime::Seconds();
	if (Head.State == EScctNadePreviewState::None || Count < 2)
	{
		if (bDrawn)
		{
			HideDrawn();
		}
		Model->bPicture = false;
		bPictureCamera = false;
		LogTimer -= DeltaSeconds;
		if (LogTimer <= 0.0f)
		{
			LogTimer = 2.0f;
			UE_LOG(LogOpenSCCT, Log, TEXT("NadePreview: nothing to draw (no grenade gadget ready in the local player's hands)"));
		}
		TickChecks(0.0f);
		return;
	}
	if (!EnsureStage())
	{
		return;
	}

	FVector Points[NadePreviewMaxPoints];
	int32 PointFlags[NadePreviewMaxPoints];
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Points[Index] = FVector(Raw[Index].X, Raw[Index].Y, Raw[Index].Z);
		PointFlags[Index] = Raw[Index].Flags;
	}

	// The line leaves the muzzle that is drawn: the view model's, else the gun in the pawn's hand. The simulation's
	// own launch point is a little ahead of where it holds the gun, which is not exactly where the gun is drawn.
	bool bMuzzle = false;
	if (const UScctEquipmentSubsystem* Equipment = GetWorld()->GetSubsystem<UScctEquipmentSubsystem>())
	{
		FTransform Muzzle;
		if (Equipment->FindMuzzle(Head.ActorId, Muzzle))
		{
			Points[0] = Muzzle.GetLocation();
			bMuzzle = true;
		}
		if (Capture != nullptr && Capture->HiddenActors.Num() == 0 && Equipment->GetFirstPersonView() != nullptr)
		{
			// The view model belongs to the player's own eyes, not to a camera across the room.
			Capture->HiddenActors.Add(Equipment->GetFirstPersonView());
		}
	}

	const FScctBridgeView& View = Session->GetView();
	const FVector Camera(View.X, View.Y, View.Z);
	const int32 State = (Head.Flags & EScctNadePreviewFlags::Blocked) != 0 ? 2 : (Head.Flags & EScctNadePreviewFlags::Charging) != 0 ? 1 : 0;
	const FLinearColor& Colour = State == 2 ? NadePreviewBlocked : State == 1 ? NadePreviewCharging : NadePreviewReady;
	if (State != LineState && LineMaterial != nullptr)
	{
		LineState = State;
		LineMaterial->SetVectorParameterValue(TEXT("Color"), Colour);
	}

	DrawPath(Head, TArrayView<const FVector>(Points, Count), TArrayView<const int32>(PointFlags, Count), Camera);
	UpdatePicture(Head, DeltaSeconds);

	Model->Frame = Colour;
	Model->Caption = FString::Printf(TEXT("%s   %d%%   %.1f s"), *FString(NadePreviewKindName(Head.Kind)).ToUpper(),
		FMath::RoundToInt32(Head.Charge * 100.0f), Head.FlightSeconds);
	if ((Head.Flags & EScctNadePreviewFlags::Blocked) != 0)
	{
		Model->Caption += TEXT("   NOT READY");
	}
	ShowWidget();

	++FramesWithPath;
	const double Done = FPlatformTime::Seconds();
	ReadSeconds += Read - Began;
	WorkSeconds += Done - Began;
	++TimedFrames;
	LogTimer -= DeltaSeconds;
	if (LogTimer <= 0.0f || ((Head.Flags & EScctNadePreviewFlags::Charging) != 0 && LogTimer <= 0.75f))
	{
		LogTimer = 1.0f;
		const FVector Land(Head.LandX, Head.LandY, Head.LandZ);
		UE_LOG(LogOpenSCCT, Log, TEXT("NadePreview: %s v %.2f%s%s, %d points, lands (%.0f %.0f %.0f) %.0f cm away after %.1f s, %d bounces%s%s, line from %s (%.0f %.0f %.0f), picture %s (%d drawn); %d frames: bridge %.3f ms, all %.3f ms a frame"),
			NadePreviewKindName(Head.Kind), Head.Charge,
			(Head.Flags & EScctNadePreviewFlags::Charging) != 0 ? TEXT(" charging") : TEXT(""),
			(Head.Flags & EScctNadePreviewFlags::Blocked) != 0 ? TEXT(" blocked") : TEXT(""),
			Count, Land.X, Land.Y, Land.Z, FVector::Dist(Land, FVector(Head.StartX, Head.StartY, Head.StartZ)), Head.FlightSeconds, Head.Bounces,
			(Head.Flags & EScctNadePreviewFlags::Stuck) != 0 ? TEXT(", catches there") : TEXT(""),
			(Head.Flags & EScctNadePreviewFlags::Airborne) != 0 ? TEXT(", in the air") : TEXT(""),
			bMuzzle ? TEXT("the drawn muzzle") : TEXT("the simulation's launch point"), Points[0].X, Points[0].Y, Points[0].Z,
			Model->bPicture ? TEXT("shown") : TEXT("hidden"), CapturesMade,
			TimedFrames, ReadSeconds / TimedFrames * 1000.0, WorkSeconds / TimedFrames * 1000.0);
		ReadSeconds = 0.0;
		WorkSeconds = 0.0;
		TimedFrames = 0;
	}
	TickChecks(DeltaSeconds);
}

void UScctNadePreviewSubsystem::Segment(TArray<TObjectPtr<UStaticMeshComponent>>& Pool, int32& Used, UMaterialInstanceDynamic* Material, const FVector& From, const FVector& To, double Width)
{
	const FVector Along = To - From;
	const double Length = Along.Size();
	if (Length < 0.5)
	{
		return;
	}
	// The engine's cylinder is 100 across and 100 long, along Z.
	Place(Pool, Used++, CylinderMesh, Material, FTransform(FRotationMatrix::MakeFromZ(Along / Length).ToQuat(), From + Along * 0.5,
		FVector(Width / 100.0, Width / 100.0, Length / 100.0)));
}

void UScctNadePreviewSubsystem::Ring(int32& Used, const FVector& Centre, const FVector& Normal, double Radius, int32 Segments, const FVector& Camera)
{
	const FMatrix Basis = FRotationMatrix::MakeFromZ(Normal);
	const FVector AxisX = Basis.GetUnitAxis(EAxis::X);
	const FVector AxisY = Basis.GetUnitAxis(EAxis::Y);
	const double Width = FMath::Clamp(FVector::Dist(Centre, Camera) * NadePreviewWidthShare, NadePreviewMinWidth, NadePreviewMaxWidth);
	FVector Previous = Centre + AxisX * Radius;
	for (int32 Index = 1; Index <= Segments; ++Index)
	{
		const double Angle = UE_TWO_PI * Index / Segments;
		const FVector Next = Centre + (AxisX * FMath::Cos(Angle) + AxisY * FMath::Sin(Angle)) * Radius;
		Segment(MarkerParts, Used, MarkerMaterial, Previous, Next, Width);
		Previous = Next;
	}
}

void UScctNadePreviewSubsystem::DrawPath(const FScctNadePreview& Head, const TArrayView<const FVector>& Points, const TArrayView<const int32>& PointFlags, const FVector& Camera)
{
	const auto WidthAt = [&Camera](const FVector& Point)
	{
		return FMath::Clamp(FVector::Dist(Point, Camera) * NadePreviewWidthShare, NadePreviewMinWidth, NadePreviewMaxWidth);
	};

	int32 Lines = 0;
	int32 Bounces = 0;
	for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
	{
		Segment(LineParts, Lines, LineMaterial, Points[Index], Points[Index + 1], WidthAt((Points[Index] + Points[Index + 1]) * 0.5));
		if (PointFlags[Index + 1] != 0)
		{
			const double Size = WidthAt(Points[Index + 1]) * 3.0;
			Place(BounceParts, Bounces++, SphereMesh, LineMaterial, FTransform(FQuat::Identity, Points[Index + 1], FVector(Size / 100.0)));
		}
	}
	Trim(LineParts, Lines);
	Trim(BounceParts, Bounces);

	// The marker: a ring on the surface, the reach of the effect around it, and an upright ring when the
	// projectile goes off in the air (there is no surface to lie the ring on).
	int32 Marks = 0;
	const FVector Normal = FVector(Head.NormalX, Head.NormalY, Head.NormalZ).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const FVector Land = FVector(Head.LandX, Head.LandY, Head.LandZ) + Normal * 1.5;
	Ring(Marks, Land, Normal, NadePreviewLandRadius, NadePreviewLandSegments, Camera);
	Ring(Marks, Land, Normal, NadePreviewLandRadius * 0.35, 8, Camera);
	if ((Head.Flags & EScctNadePreviewFlags::Airborne) != 0)
	{
		const FVector Side = FVector::CrossProduct(Normal, (Camera - Land).GetSafeNormal()).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
		Ring(Marks, Land, FVector::CrossProduct(Side, Normal), NadePreviewLandRadius, NadePreviewLandSegments, Camera);
	}
	if (Head.Radius > NadePreviewLandRadius && Head.Radius <= NadePreviewMaxEffectRadius)
	{
		Ring(Marks, Land, Normal, Head.Radius, NadePreviewEffectSegments, Camera);
	}
	Trim(MarkerParts, Marks);
	bDrawn = true;
}

void UScctNadePreviewSubsystem::UpdatePicture(const FScctNadePreview& Head, float DeltaSeconds)
{
	const bool bValid = (Head.Flags & EScctNadePreviewFlags::PipValid) != 0 && Capture != nullptr && Target != nullptr;
	Model->bPicture = bValid;
	if (!bValid)
	{
		bPictureCamera = false;
		return;
	}

	// The camera glides to its new place rather than jumping with every step of the landing point; a far move cuts.
	const FVector Wanted(Head.PipX, Head.PipY, Head.PipZ);
	if (!bPictureCamera || FVector::DistSquared(Wanted, PictureCamera) > FMath::Square(350.0))
	{
		PictureCamera = Wanted;
		bPictureCamera = true;
		CaptureTimer = 0.0f;
	}
	else
	{
		PictureCamera = FMath::VInterpTo(PictureCamera, Wanted, DeltaSeconds, 10.0f);
	}

	CaptureTimer -= DeltaSeconds;
	if (CaptureTimer > 0.0f)
	{
		return;
	}
	CaptureTimer = NadePreviewPictureInterval;
	const FVector Normal = FVector(Head.NormalX, Head.NormalY, Head.NormalZ).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const FVector LookAt = FVector(Head.LandX, Head.LandY, Head.LandZ) + Normal * 8.0;
	Capture->SetWorldLocationAndRotation(PictureCamera, (LookAt - PictureCamera).Rotation());
	// Drawn with the frame's own scene update, so it shows the line and the marker as placed above.
	Capture->CaptureSceneDeferred();
	++CapturesMade;
}

bool UScctNadePreviewSubsystem::EnsureStage()
{
	if (IsValid(Stage))
	{
		return true;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	if (CylinderMesh == nullptr)
	{
		CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		GlowMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Weapons/Materials/M_ScctGlow.M_ScctGlow"), nullptr, LOAD_NoWarn);
		UE_CLOG(GlowMaterial == nullptr, LogOpenSCCT, Warning, TEXT("NadePreview: M_ScctGlow is missing; the line is drawn in the engine's default material"));
	}
	if (CylinderMesh == nullptr || SphereMesh == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("NadePreview: the engine's basic shapes are missing; the preview is off"));
		GNadePreviewWanted = false;
		Unbind();
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Stage = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Stage == nullptr)
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Stage, TEXT("Root"), RF_Transient);
	Root->SetMobility(EComponentMobility::Movable);
	Stage->SetRootComponent(Root);
	Root->RegisterComponent();

	const auto MakeMaterial = [this](const FLinearColor& Colour) -> UMaterialInstanceDynamic*
	{
		if (GlowMaterial == nullptr)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(GlowMaterial, Stage);
		Material->SetVectorParameterValue(TEXT("Color"), Colour);
		Material->SetScalarParameterValue(TEXT("Intensity"), NadePreviewGlow);
		Material->SetScalarParameterValue(TEXT("Sharpness"), 1.0f);
		return Material;
	};
	LineMaterial = MakeMaterial(NadePreviewReady);
	MarkerMaterial = MakeMaterial(NadePreviewMarker);
	LineState = -1;

	Target = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
	Target->RenderTargetFormat = RTF_RGBA8_SRGB;
	Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	Target->InitAutoFormat(NadePreviewPictureWidth, NadePreviewPictureHeight);
	Target->UpdateResourceImmediate(true);

	Capture = NewObject<USceneCaptureComponent2D>(Stage, TEXT("Picture"), RF_Transient);
	Capture->SetupAttachment(Root);
	Capture->SetUsingAbsoluteLocation(true);
	Capture->SetUsingAbsoluteRotation(true);
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	// Drawn only when asked (UpdatePicture), a few times a second; its exposure carries on between the pictures.
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->FOVAngle = NadePreviewPictureFov;
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetBloom(false);
	Capture->ShowFlags.SetLensFlares(false);
	Capture->ShowFlags.SetDepthOfField(false);
	Capture->ShowFlags.SetVignette(false);
	Capture->ShowFlags.SetGrain(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetScreenSpaceReflections(false);
	Capture->ShowFlags.SetTemporalAA(false);
	Capture->TextureTarget = Target;
	Capture->RegisterComponent();
	Stage->AddInstanceComponent(Capture);
	CaptureTimer = 0.0f;
	bPictureCamera = false;
	if (Widget.IsValid())
	{
		Widget->SetTarget(Target);
	}
	return true;
}

UStaticMeshComponent* UScctNadePreviewSubsystem::Place(TArray<TObjectPtr<UStaticMeshComponent>>& Pool, int32 Index, UStaticMesh* Mesh, UMaterialInstanceDynamic* Material, const FTransform& Transform)
{
	while (Pool.Num() <= Index)
	{
		UStaticMeshComponent* Made = NewObject<UStaticMeshComponent>(Stage, NAME_None, RF_Transient);
		Made->SetStaticMesh(Mesh);
		Made->SetMobility(EComponentMobility::Movable);
		Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Made->SetCanEverAffectNavigation(false);
		Made->SetCastShadow(false);
		Made->bAffectDistanceFieldLighting = false;
		Made->bAffectDynamicIndirectLighting = false;
		Made->SetupAttachment(Stage->GetRootComponent());
		Made->SetUsingAbsoluteLocation(true);
		Made->SetUsingAbsoluteRotation(true);
		Made->SetUsingAbsoluteScale(true);
		if (Material != nullptr)
		{
			Made->SetMaterial(0, Material);
		}
		Made->RegisterComponent();
		Stage->AddInstanceComponent(Made);
		Pool.Add(Made);
	}
	UStaticMeshComponent* Part = Pool[Index];
	Part->SetWorldTransform(Transform);
	if (!Part->IsVisible())
	{
		Part->SetVisibility(true);
	}
	return Part;
}

void UScctNadePreviewSubsystem::Trim(TArray<TObjectPtr<UStaticMeshComponent>>& Pool, int32 Used)
{
	for (int32 Index = Used; Index < Pool.Num(); ++Index)
	{
		if (Pool[Index] != nullptr && Pool[Index]->IsVisible())
		{
			Pool[Index]->SetVisibility(false);
		}
	}
}

void UScctNadePreviewSubsystem::HideDrawn()
{
	Trim(LineParts, 0);
	Trim(BounceParts, 0);
	Trim(MarkerParts, 0);
	bDrawn = false;
}

void UScctNadePreviewSubsystem::DestroyStage()
{
	if (Widget.IsValid())
	{
		Widget->SetTarget(nullptr);
	}
	LineParts.Reset();
	BounceParts.Reset();
	MarkerParts.Reset();
	LineMaterial = nullptr;
	MarkerMaterial = nullptr;
	Capture = nullptr;
	Target = nullptr;
	if (IsValid(Stage))
	{
		Stage->Destroy();
	}
	Stage = nullptr;
	bDrawn = false;
	bPictureCamera = false;
}

void UScctNadePreviewSubsystem::ShowWidget()
{
	if (Widget.IsValid())
	{
		return;
	}
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (Viewport == nullptr)
	{
		return;
	}
	Widget = SNew(SScctNadePreviewPip).Model(Model);
	Widget->SetTarget(Target);
	ViewportContent = Widget;
	Viewport->AddViewportWidgetContent(ViewportContent.ToSharedRef(), NadePreviewZOrder);
}

void UScctNadePreviewSubsystem::HideWidget()
{
	if (ViewportContent.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() != nullptr ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(ViewportContent.ToSharedRef());
		}
	}
	ViewportContent.Reset();
	Widget.Reset();
}

void UScctNadePreviewSubsystem::TickChecks(float DeltaSeconds)
{
	if (HoldAt < 0.0f && OffAt < 0.0f)
	{
		return;
	}
	ShownFor += DeltaSeconds;
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	const auto Shot = [this](const TCHAR* Name)
	{
		FString Directory = FPaths::ProjectSavedDir() / TEXT("ScctShots");
		FParse::Value(FCommandLine::Get(), TEXT("ScctShotDir="), Directory);
		const FString File = Directory / Name;
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogOpenSCCT, Log, TEXT("NadePreview: -ScctNadePreviewHold: screenshot %s at %.2f s (caption \"%s\")"), *File, ShownFor, *Model->Caption);
		LogTimer = 0.0f;
	};
	if (HoldAt >= 0.0f && Player != nullptr)
	{
		const float Held = ShownFor - HoldAt;
		if (HoldStage == 0 && Held >= 0.0f)
		{
			HoldStage = 1;
			Player->HoldScriptedAction(TEXT("altfire"), true);
			UE_LOG(LogOpenSCCT, Log, TEXT("NadePreview: -ScctNadePreviewHold: the throw button is held from %.2f s"), ShownFor);
		}
		else if (HoldStage == 1 && Held >= 0.8f)
		{
			HoldStage = 2;
			Shot(TEXT("NadePreviewCharge.png"));
		}
		else if (HoldStage == 2 && Held >= 1.6f)
		{
			HoldStage = 3;
			Shot(TEXT("NadePreviewFull.png"));
		}
		else if (HoldStage == 3 && Held >= 2.0f)
		{
			HoldStage = 4;
			Player->HoldScriptedAction(TEXT("altfire"), false);
			UE_LOG(LogOpenSCCT, Log, TEXT("NadePreview: -ScctNadePreviewHold: the throw button is let go at %.2f s (the grenade is thrown)"), ShownFor);
		}
	}
	if (OffAt >= 0.0f && !bOffDone && ShownFor >= OffAt)
	{
		bOffDone = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("NadePreview: -ScctNadePreviewOff: %s"), *Request(0));
		const int32 FramesAtOff = FramesWorked;
		GetWorld()->GetTimerManager().SetTimer(OffReport, FTimerDelegate::CreateWeakLambda(this, [this, FramesAtOff]
		{
			int32 Visible = 0;
			for (const TArray<TObjectPtr<UStaticMeshComponent>>* Pool : { &LineParts, &BounceParts, &MarkerParts })
			{
				for (const UStaticMeshComponent* Part : *Pool)
				{
					Visible += Part != nullptr && Part->IsVisible() ? 1 : 0;
				}
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("NadePreview: -ScctNadePreviewOff: 2 s after off: %d frames worked on since, per-frame hook %s, %d parts visible, stage %s, picture widget %s"),
				FramesWorked - FramesAtOff, TickHandle.IsValid() ? TEXT("bound") : TEXT("not bound"), Visible,
				IsValid(Stage) ? TEXT("there") : TEXT("gone"), Widget.IsValid() ? TEXT("there") : TEXT("gone"));
		}), 2.0f, false);
	}
}
