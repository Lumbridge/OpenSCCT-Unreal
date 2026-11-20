#include "Play/ScctViewPawn.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Hud/ScctVisionSubsystem.h"
#include "Play/ScctMercCameraOptics.h"
#include "Play/ScctControlSettings.h"
#include "Play/ScctLightRules.h"
#include "Play/ScctTone.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	bool IsLocalMercView(const AScctPlayerController& Player, const UScctSessionSubsystem& Match,
		const FScctBridgeView& View, FScctHudState& Hud)
	{
		if (View.Valid == 0 || View.SpyFirstPerson || !View.HideLocalBody
			|| !Match.GetSessionId() || Match.IsTourView() || Match.IsBackdrop()
			|| Player.GetViewTarget() != Player.GetPawn() || !(Match.GetSessionInfo().Flags & EScctSessionFlags::Merc))
		{
			return false;
		}
		const FScctLookView& Look = Player.GetLookView();
		if (!Look.Valid || Look.Cut || Look.Occluded)
		{
			return false;
		}
		// The bridge caches this POD snapshot when the simulation ticks. Read the current tick: the HUD
		// widget only consumes it later in the frame, after this camera has already been placed.
		const FScctHudApi* Api = ScctFeature<FScctHudApi>();
		constexpr int32 Inactive = EScctHudFlags::Spectating | EScctHudFlags::Sleeping
			| EScctHudFlags::LyingOnGround | EScctHudFlags::GameEnded;
		// Goggles change the picture from this camera. The merc still carries the same rifle.
		return Api && Api->GetState(&Hud) && Hud.Valid && Hud.Side == 1 && Hud.Health > 0
			&& (Hud.Flags & EScctHudFlags::HasPawn) && !(Hud.Flags & Inactive) && !Hud.GadgetView && !Hud.BinocularMode;
	}
}

AScctViewPawn::AScctViewPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	SetRootComponent(Camera);
	Camera->bUsePawnControlRotation = false;

	// The simulation's field of view is the horizontal angle of a 4:3 picture, and wider screens keep its
	// vertical angle (they see more at the sides). Unreal keeps the vertical angle it derives from this ratio,
	// as long as the vertical axis is the one it maintains: said here, so a project setting cannot change it.
	Camera->SetAspectRatio(4.0f / 3.0f);
	Camera->SetConstraintAspectRatio(false);
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainYFOV);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	SetCanBeDamaged(false);
}

void AScctViewPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	if (OutResult.bUseFirstPersonParameters && Camera->bEnableFirstPersonScale)
	{
		// UE pulls first-person vertices toward the eye before clipping them. Keep the
		// receiver, eyepiece and hands down to one centimetre in the model's own frame.
		constexpr float NearInModelCm = 1.0f;
		OutResult.PerspectiveNearClipPlane = FMath::Min(OutResult.GetFinalPerspectiveNearClipPlane(),
			NearInModelCm * OutResult.FirstPersonScale);
	}
}

void AScctViewPawn::SetMercCameraActive(bool bEnabled)
{
	if (bEnabled != bMercCameraActive)
	{
		bMercCameraActive = bEnabled;
		if (UWorld* World = GetWorld(); World && !World->bIsTearingDown)
		{
			FScctLightRules::SetMercCameraSurfaces(*World, bEnabled);
		}
	}
}

void AScctViewPawn::EndViewTarget(APlayerController* PC)
{
	SetMercCameraActive(false);
	Super::EndViewTarget(PC);
}

void AScctViewPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetMercCameraActive(false);
	Super::EndPlay(EndPlayReason);
}

void AScctViewPawn::ApplyView(const FScctBridgeView& View)
{
	const AScctPlayerController* Player = Cast<AScctPlayerController>(GetController());
	const UScctSessionSubsystem* Match = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	FScctHudState Hud{};
	const bool bPreviouslyActive = bMercCameraActive;
	SetMercCameraActive(Player && Match && IsLocalMercView(*Player, *Match, View, Hud));
	const UScctVisionSubsystem* Vision = UScctVisionSubsystem::Find(GetWorld());
	const bool bPlainSight = (Hud.VisionMode == 0 || Hud.VisionMode == 5)
		&& (!Vision || Vision->GetShownAmount() <= 0.0f);
	FScctTone::ApplyMercCamera(Camera->PostProcessSettings, bMercCameraActive && bPlainSight);
	bMercLensActive = ScctMercCameraOptics::Apply(Camera->PostProcessSettings, bMercCameraActive);
	if (View.Valid == 0)
	{
		return;
	}

	const FVector Eye(View.X, View.Y, View.Z);
	const FRotator Aim(View.Pitch, View.Yaw, View.Roll);
	FQuat Rotation = Aim.Quaternion();
	if (bMercCameraActive)
	{
		const int32 Session = Match->GetSessionId();
		const FRotator Motion = MercCameraMotion.Update(Eye, Aim, Player->GetMercAimTurn(), Hud.Speed,
			(Hud.Flags & EScctHudFlags::Crouched) != 0, Player->GetLookView().RifleAimBlend,
			GetWorld()->GetDeltaSeconds(), !bPreviouslyActive || Session != MercCameraSession);
		MercCameraSession = Session;
		Rotation = MercCameraMotion.CameraAnchor(Aim).Quaternion() * Player->GetMercFreeLookOffset().Quaternion() * Motion.Quaternion();
	}
	// Keep the shot origin, torch, laser and weapon sockets in the simulation's frame. The lens rotates
	// about the same eye, so a nearby target and a distant one both keep their reticle correspondence.
	SetActorLocationAndRotation(Eye, Rotation);
	if (View.Fov > 1.0f)
	{
		float Fov = View.Fov;
		if (bMercLensActive)
		{
			const UScctControlSettings* Settings = UScctControlSettings::Get();
			const FScctLookView& Look = Player->GetLookView();
			float Cap = Settings->bWidescreen ? Settings->MercFovCap : Look.HorizontalFov;
			if (Settings->bWidescreen && Look.OpticMagnification > 1.0f)
			{
				// Apply the same distortion correction as the unzoomed scene, even when
				// the player's horizontal cap limited that correction at hip height.
				Cap = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Cap * .5f)) / Look.OpticMagnification));
			}
			Fov = ScctMercCameraOptics::CompensateFov(Fov, Look.HorizontalFov, Cap);
		}
		Camera->SetFieldOfView(Fov);
	}
}

FVector2f AScctViewPawn::ProjectMercAim(FVector2f AimPoint, FVector2f CanvasSize) const
{
	const AScctPlayerController* Player = Cast<AScctPlayerController>(GetController());
	if (!bMercCameraActive || !Player || CanvasSize.X <= 0.0f || CanvasSize.Y <= 0.0f)
	{
		return AimPoint;
	}
	const FScctLookView& Look = Player->GetLookView();
	const float TanHalf = FMath::Tan(FMath::DegreesToRadians(Look.VerticalFov * 0.5f));
	const FVector Ray(1.0, (2.0f * AimPoint.X - CanvasSize.X) / CanvasSize.Y * TanHalf,
		(1.0f - 2.0f * AimPoint.Y / CanvasSize.Y) * TanHalf);
	const FVector InWorld = FRotator(Look.Pitch, Look.Yaw, Look.Roll).RotateVector(Ray);
	FVector2D Pixel;
	int32 Width, Height;
	Player->GetViewportSize(Width, Height);
	if (Width > 0 && Height > 0 && Player->ProjectWorldLocationToScreen(GetActorLocation() + InWorld * 1000.0, Pixel, true))
	{
		return FVector2f(Pixel.X * CanvasSize.X / Width, Pixel.Y * CanvasSize.Y / Height);
	}
	return AimPoint;
}
