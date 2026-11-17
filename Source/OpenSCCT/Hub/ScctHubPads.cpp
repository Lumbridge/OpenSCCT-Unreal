#include "Hub/ScctHubPads.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Hub/ScctHubSubsystem.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Menu/SScctHubLaptopPage.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	const TCHAR* const PadMaterialPath = TEXT("/Game/OpenSCCT/Hub/M_ScctHubPad.M_ScctHubPad");
	const TCHAR* const PictureMaterialPath = TEXT("/Game/OpenSCCT/Hub/M_ScctHubPicture.M_ScctHubPicture");
	const TCHAR* const PlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");

	/** Where Import-HubMap.ps1 puts the hub map's meshes, the theme kits among them (Hub_HubTheme_<pad>_<theme>_*). */
	const TCHAR* const KitMeshFolder = TEXT("/Game/Imported/Maps/Hub/world/StaticMeshes");

	/** The actors' tag prefix for a kit's part (Tools/Import/import_map.py tags every placed mesh "Scct:<label>"). */
	const TCHAR* const KitTagPrefix = TEXT("Scct:HubTheme_");

	/** HubPadView: the overlay lies 3 cm over the floor, its border is 18 cm wide, the picture is 3.4 m wide. */
	constexpr float OverlayLift = 3.0f;
	constexpr float BorderWidth = 18.0f;
	constexpr float PictureWidth = 340.0f;

	/** The engine's plane is 100 units square. */
	constexpr float PlaneSize = 100.0f;

	/** Above the hub HUD (10) and under the in-match menu: the laptop takes the mouse while it is open. */
	constexpr int32 LaptopZOrder = 40;

	FString FixedText(const TCHAR* Text, int32 Capacity)
	{
		int32 Length = 0;
		while (Length < Capacity && Text[Length] != 0)
		{
			++Length;
		}
		return FString::ConstructFromPtrSize(Text, Length);
	}

	const TCHAR* StateName(int32 State)
	{
		static const TCHAR* const Names[] = { TEXT("off"), TEXT("idle"), TEXT("waiting"), TEXT("countdown"), TEXT("starting") };
		return State >= 0 && State < UE_ARRAY_COUNT(Names) ? Names[State] : TEXT("?");
	}
}

FScctHubPads::FScctHubPads(UScctHubSubsystem* InOwner)
	: Owner(InOwner)
{
}

FScctHubPads::~FScctHubPads()
{
	Stop();
}

const FScctHubPadsApi* FScctHubPads::GetApi()
{
	if (Api == nullptr && !bReportedNoApi)
	{
		FString Error;
		Api = ScctFeature<FScctHubPadsApi>(&Error);
		if (Api == nullptr)
		{
			bReportedNoApi = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHubPads: no pads table, the pads are not drawn: %s"), *Error);
		}
	}
	return Api;
}

FLinearColor FScctHubPads::ColourOf(uint32 Rgba)
{
	return FLinearColor(FColor((Rgba >> 24) & 0xFF, (Rgba >> 16) & 0xFF, (Rgba >> 8) & 0xFF, Rgba & 0xFF));
}

FString FScctHubPads::ReadText(EScctHubPadsText Kind) const
{
	if (Api == nullptr)
	{
		return FString();
	}
	TCHAR Buffer[512];
	Api->GetText(static_cast<int32>(Kind), Buffer, UE_ARRAY_COUNT(Buffer));
	return Buffer;
}

FLinearColor FScctHubPads::GetQueueColour() const
{
	for (const FScctHubPadRow& Row : Pads)
	{
		if ((Row.Flags & EScctHubPadsFlags::Queued) != 0)
		{
			return ColourOf(Row.Rgba);
		}
	}
	return FLinearColor::White;
}

bool FScctHubPads::QueueOnPad(int32 Number, int32 Side, const FString& Options)
{
	if (GetApi() == nullptr)
	{
		return false;
	}
	return Api->QueueOnPad(Number, Side, *Options) != 0;
}

// ---- the frame ----------------------------------------------------------------------------------------------------

void FScctHubPads::Tick(UWorld* World, float DeltaSeconds, bool bInVerbose)
{
	bVerbose = bInVerbose;
	if (World == nullptr || GetApi() == nullptr)
	{
		return;
	}

	Api->GetState(&State);
	Pads.SetNum(FMath::Clamp(State.PadCount, 0, 16));
	const int32 Count = Pads.Num() > 0 ? Api->GetPads(Pads.GetData(), Pads.Num()) : 0;
	Pads.SetNum(FMath::Min(Count, Pads.Num()));

	if ((State.Flags & EScctHubPadsFlags::InHub) != 0 && EnsureScene(World))
	{
		if (!bKitsGathered)
		{
			GatherKits(World);
		}

		FVector Eye = FVector::ZeroVector;
		if (const APlayerController* Player = World->GetFirstPlayerController(); Player != nullptr && Player->PlayerCameraManager != nullptr)
		{
			Eye = Player->PlayerCameraManager->GetCameraLocation();
		}
		for (const FScctHubPadRow& Row : Pads)
		{
			const FString Id = FixedText(Row.Id, UE_ARRAY_COUNT(Row.Id));
			UpdatePad(Row, Views.FindOrAdd(Id), Eye, State.Now);
		}
	}

	FollowLaptop(World);
	LogLine(DeltaSeconds);
}

void FScctHubPads::Stop()
{
	CloseLaptop();
	if (Laptop.IsValid() && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Laptop.ToSharedRef());
	}
	Laptop.Reset();
}

bool FScctHubPads::EnsureScene(UWorld* World)
{
	if (SceneWorld.Get() != World)
	{
		// A new hub level: its actors are new, so are the overlays and the kits.
		SceneWorld = World;
		Host.Reset();
		Views.Reset();
		Kits.Reset();
		bKitsGathered = false;
		bSceneFailed = false;
	}
	if (Host.IsValid())
	{
		return true;
	}
	if (bSceneFailed)
	{
		return false;
	}

	if (LoadObject<UMaterialInterface>(nullptr, PadMaterialPath) == nullptr || LoadObject<UMaterialInterface>(nullptr, PictureMaterialPath) == nullptr)
	{
		bSceneFailed = true;
		UE_LOG(LogOpenSCCT, Error, TEXT("ScctHubPads: the pad materials are missing (run Tools/Hub/build_hub_assets.py): no overlays or pictures"));
		return false;
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	if (Actor == nullptr)
	{
		bSceneFailed = true;
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Host = Actor;
	return true;
}

void FScctHubPads::UpdatePad(const FScctHubPadRow& Row, FPadView& View, const FVector& Eye, float Now)
{
	AActor* Actor = Host.Get();
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, PlaneMeshPath);
	if (Actor == nullptr || Plane == nullptr)
	{
		return;
	}

	auto MakePlane = [&](const TCHAR* MaterialPath, TWeakObjectPtr<UStaticMeshComponent>& OutComponent, TWeakObjectPtr<UMaterialInstanceDynamic>& OutMaterial)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetStaticMesh(Plane);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr, MaterialPath), Component);
		Component->SetMaterial(0, Material);
		Component->SetupAttachment(Actor->GetRootComponent());
		Component->RegisterComponent();
		OutComponent = Component;
		OutMaterial = Material;
	};

	// The floor overlay: the pad's rectangle, its state's colour (HubPadView.Update).
	if (!View.Overlay.IsValid())
	{
		MakePlane(PadMaterialPath, View.Overlay, View.OverlayMaterial);
	}
	const float Width = FMath::Max(Row.HiX - Row.LoX, 1.0f);
	const float Depth = FMath::Max(Row.HiY - Row.LoY, 1.0f);
	const FVector Centre((Row.LoX + Row.HiX) * 0.5f, (Row.LoY + Row.HiY) * 0.5f, Row.FloorZ + OverlayLift);
	if (UStaticMeshComponent* Overlay = View.Overlay.Get())
	{
		Overlay->SetWorldLocationAndRotation(Centre, FRotator::ZeroRotator);
		Overlay->SetWorldScale3D(FVector(Width / PlaneSize, Depth / PlaneSize, 1.0f));
	}
	if (UMaterialInstanceDynamic* Material = View.OverlayMaterial.Get())
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Now * 6.0f);
		const EScctHubPadState PadState = static_cast<EScctHubPadState>(Row.State);
		const float Fill = PadState == EScctHubPadState::Countdown ? 0.22f + 0.2f * Pulse
			: PadState == EScctHubPadState::Starting ? 0.4f
			: PadState == EScctHubPadState::Waiting ? 0.2f
			: PadState == EScctHubPadState::Off ? 0.08f
			: 0.1f;
		const float Border = PadState == EScctHubPadState::Countdown ? 0.6f + 0.4f * Pulse : 0.85f;
		Material->SetVectorParameterValue(TEXT("Color"), ColourOf(Row.Rgba));
		Material->SetScalarParameterValue(TEXT("Fill"), Fill);
		Material->SetScalarParameterValue(TEXT("Border"), Border);
		Material->SetScalarParameterValue(TEXT("BorderU"), BorderWidth / Width);
		Material->SetScalarParameterValue(TEXT("BorderV"), BorderWidth / Depth);
	}
	if (View.State != Row.State)
	{
		if (View.State >= 0 || bVerbose)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubPads: %s is %s%s (overlay %s)"), Row.Id, StateName(Row.State),
				Row.State == static_cast<int32>(EScctHubPadState::Countdown) ? *FString::Printf(TEXT(", %d s"), Row.Seconds) : TEXT(""),
				*ColourOf(Row.Rgba).ToFColor(true).ToHex());
		}
		View.State = Row.State;
	}

	// The theme kit of the pad's map (HubPadThemes).
	const FString Theme = FixedText(Row.Theme, UE_ARRAY_COUNT(Row.Theme));
	if (View.Theme != Theme)
	{
		View.Theme = Theme;
		ShowTheme(FixedText(Row.Id, UE_ARRAY_COUNT(Row.Id)), Theme);
	}

	// The map's picture over the sign (HubPadView: on a board's top edge, else over the sign facing the camera).
	const FString Path = (Row.Flags & EScctHubPadsFlags::HasPicture) != 0 ? FixedText(Row.Picture, UE_ARRAY_COUNT(Row.Picture)) : FString();
	UTexture2D* Texture = Path.IsEmpty() ? nullptr : GetPicture(Path);
	if (Texture == nullptr)
	{
		if (UStaticMeshComponent* Picture = View.Picture.Get())
		{
			Picture->SetVisibility(false);
		}
		return;
	}
	if (!View.Picture.IsValid())
	{
		MakePlane(PictureMaterialPath, View.Picture, View.PictureMaterial);
	}
	UStaticMeshComponent* Picture = View.Picture.Get();
	if (Picture == nullptr)
	{
		return;
	}
	if (View.PicturePath != Path)
	{
		View.PicturePath = Path;
		if (UMaterialInstanceDynamic* Material = View.PictureMaterial.Get())
		{
			Material->SetTextureParameterValue(TEXT("Picture"), Texture);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubPads: %s shows the picture of %s (%dx%d, %s)"), Row.Id, Row.Map, Texture->GetSizeX(), Texture->GetSizeY(), *Path);
	}
	const float Height = PictureWidth * Texture->GetSizeY() / FMath::Max(1, Texture->GetSizeX());

	FVector At;
	FVector Facing;
	const bool bSign = (Row.Flags & EScctHubPadsFlags::HasSign) != 0;
	if (bSign && (Row.Flags & EScctHubPadsFlags::SignMounted) != 0)
	{
		const float Angle = Row.SignYaw * UE_TWO_PI / 65536.0f;
		Facing = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		At = FVector(Row.SignX, Row.SignY, Row.SignZ) + FVector(0.0f, 0.0f, Row.SignHeight * 0.5f + Height * 0.5f + 10.0f) + Facing * 2.0f;
	}
	else
	{
		At = bSign ? FVector(Row.SignX, Row.SignY, Row.SignZ + 150.0f + Height * 0.5f) : FVector(Centre.X, Centre.Y, Row.FloorZ + 380.0f + Height * 0.5f);
		Facing = (Eye - At).GetSafeNormal2D();
		if (Facing.IsNearlyZero())
		{
			Facing = FVector::ForwardVector;
		}
	}
	// The plane's X runs along the picture's width (left to right as seen from in front), its Y down the picture, its
	// normal towards the viewer.
	const FVector Right(Facing.Y, -Facing.X, 0.0f);
	const FMatrix Basis(Right, -FVector::UpVector, Facing, FVector::ZeroVector);
	Picture->SetWorldLocationAndRotation(At, Basis.Rotator());
	Picture->SetWorldScale3D(FVector(PictureWidth / PlaneSize, Height / PlaneSize, 1.0f));
	Picture->SetVisibility(true);
}

UTexture2D* FScctHubPads::GetPicture(const FString& Path)
{
	if (const TStrongObjectPtr<UTexture2D>* Found = Pictures.Find(Path))
	{
		return Found->Get();
	}
	if (BadPictures.Contains(Path))
	{
		return nullptr;
	}
	UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Path);
	if (Texture == nullptr)
	{
		BadPictures.Add(Path);
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHubPads: the map picture %s could not be read"), *Path);
		return nullptr;
	}
	Pictures.Add(Path, TStrongObjectPtr<UTexture2D>(Texture));
	return Texture;
}

// ---- the theme kits -----------------------------------------------------------------------------------------------

void FScctHubPads::GatherKits(UWorld* World)
{
	bKitsGathered = true;
	TSet<FString> InLevel;
	TMap<FString, TArray<float>> CustomData;
	int32 Parts = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			const FString Text = Tag.ToString();
			if (!Text.StartsWith(KitTagPrefix))
			{
				continue;
			}
			TArray<FString> Fields;
			Text.Mid(FCString::Strlen(KitTagPrefix)).ParseIntoArray(Fields, TEXT("_"));
			if (Fields.Num() < 2)
			{
				continue;
			}
			TArray<UPrimitiveComponent*> Components;
			It->GetComponents(Components);
			for (UPrimitiveComponent* Component : Components)
			{
				Kits.Add({ Fields[0], Fields[1], Component });
				CustomData.FindOrAdd(Fields[0]) = Component->GetCustomPrimitiveData().Data;
				++Parts;
			}
			InLevel.Add(Fields[0] + TEXT("_") + Fields[1]);
		}
	}

	// The level holds each pad's default kit only (the importer leaves out the hidden ones): the others are added from
	// the imported meshes, hidden until their theme is wanted. Their vertices are in hub space, so they sit at the origin.
	int32 Added = 0;
	AActor* Actor = Host.Get();
	const FString Folder = FPaths::ProjectContentDir() / TEXT("Imported/Maps/Hub/world/StaticMeshes");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("Hub_HubTheme_*.uasset")), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		const FString Name = FPaths::GetBaseFilename(File);
		TArray<FString> Fields;
		Name.Mid(FCString::Strlen(TEXT("Hub_HubTheme_"))).ParseIntoArray(Fields, TEXT("_"));
		if (Actor == nullptr || Fields.Num() < 2 || InLevel.Contains(Fields[0] + TEXT("_") + Fields[1]))
		{
			continue;
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), KitMeshFolder, *Name, *Name));
		if (Mesh == nullptr)
		{
			continue;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetStaticMesh(Mesh);
		if (const TArray<float>* Data = CustomData.Find(Fields[0]))
		{
			for (int32 Index = 0; Index < Data->Num(); ++Index)
			{
				Component->SetDefaultCustomPrimitiveDataFloat(Index, (*Data)[Index]);
			}
		}
		Component->SetVisibility(false);
		Component->SetupAttachment(Actor->GetRootComponent());
		Component->RegisterComponent();
		Kits.Add({ Fields[0], Fields[1], Component });
		++Added;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubPads: theme kits: %d parts in the level (%d kits), %d added from %s"), Parts, InLevel.Num(), Added, KitMeshFolder);
}

void FScctHubPads::ShowTheme(const FString& Pad, const FString& Theme)
{
	bool bHas = false;
	for (const FKitPart& Part : Kits)
	{
		bHas |= Part.Pad == Pad && Part.Theme == Theme && Part.Component.IsValid();
	}
	const FString Shown = bHas ? Theme : FString(TEXT("hub"));
	int32 Visible = 0;
	int32 Hidden = 0;
	for (const FKitPart& Part : Kits)
	{
		UPrimitiveComponent* Component = Part.Component.Get();
		if (Component == nullptr || Part.Pad != Pad)
		{
			continue;
		}
		const bool bVisible = Part.Theme == Shown;
		Component->SetVisibility(bVisible);
		bVisible ? ++Visible : ++Hidden;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubPads: %s shows the %s theme kit (%d parts shown, %d hidden)"), *Pad, *Shown, Visible, Hidden);
}

// ---- the laptop ---------------------------------------------------------------------------------------------------

void FScctHubPads::FollowLaptop(UWorld* World)
{
	const bool bOpen = (State.Flags & EScctHubPadsFlags::Laptop) != 0 && (State.Flags & EScctHubPadsFlags::InHub) != 0;
	if (bOpen && (!Laptop.IsValid() || State.LaptopSerial != SeenLaptopSerial) && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		SeenLaptopSerial = State.LaptopSerial;
		if (!Laptop.IsValid())
		{
			Laptop = SNew(SScctHubLaptopPage).Pads(SharedThis(this));
			GEngine->GameViewport->AddViewportWidgetContent(Laptop.ToSharedRef(), LaptopZOrder);
			if (AScctPlayerController* Player = Cast<AScctPlayerController>(World->GetFirstPlayerController()))
			{
				Player->SetMenuOpen(true);
				Player->SetShowMouseCursor(true);
				if (!FApp::IsUnattended())
				{
					FInputModeGameAndUI Mode;
					Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
					Mode.SetHideCursorDuringCapture(false);
					Mode.SetWidgetToFocus(Laptop);
					Player->SetInputMode(Mode);
				}
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubPads: the laptop of %s is open: %s"), *ReadText(EScctHubPadsText::LaptopPad), *ReadText(EScctHubPadsText::LaptopSummary));
		}
		Laptop->Refresh(true);
	}
	else if (!bOpen && Laptop.IsValid())
	{
		if (GEngine != nullptr && GEngine->GameViewport != nullptr)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(Laptop.ToSharedRef());
		}
		Laptop.Reset();
		if (AScctPlayerController* Player = Cast<AScctPlayerController>(World->GetFirstPlayerController()))
		{
			Player->SetMenuOpen(false);
			if (!FApp::IsUnattended())
			{
				Player->SetInputMode(FInputModeGameOnly());
			}
			Player->SetShowMouseCursor(false);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubPads: the laptop is closed"));
	}
	else if (Laptop.IsValid())
	{
		Laptop->Refresh(false);
	}
}

void FScctHubPads::CloseLaptop()
{
	if (Api != nullptr && (State.Flags & EScctHubPadsFlags::Laptop) != 0)
	{
		Api->CloseLaptop();
	}
}

void FScctHubPads::LogLine(float DeltaSeconds)
{
	if (!bVerbose || Pads.Num() == 0)
	{
		return;
	}
	FString States;
	for (const FScctHubPadRow& Row : Pads)
	{
		States += FString::Printf(TEXT(" %s %s %s %d/%d+%d/%d%s;"), Row.Id, StateName(Row.State), Row.Map, Row.Spies, Row.SpySlots, Row.Mercs, Row.MercSlots,
			(Row.Flags & EScctHubPadsFlags::Queued) != 0 ? TEXT(" QUEUED") : TEXT(""));
	}
	LogTimer -= DeltaSeconds;
	if (LogTimer > 0.0f && States == LastLoggedStates)
	{
		return;
	}
	LogTimer = 4.0f;
	LastLoggedStates = States;
	const FString Queue = ReadText(EScctHubPadsText::Queue);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubPads:%s queue '%s'"), *States, *Queue);
}
