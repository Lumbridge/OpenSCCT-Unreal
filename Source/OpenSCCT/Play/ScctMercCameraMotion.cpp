#include "Play/ScctMercCameraMotion.h"

FRotator FScctMercCameraMotion::CameraAnchor(const FRotator& Aim) const
{
	return FRotator(FMath::Clamp(Aim.Pitch - GunOffset.Y, -89.9, 89.9), FRotator::NormalizeAxis(Aim.Yaw - GunOffset.X), Aim.Roll);
}

FRotator FScctMercCameraMotion::Update(const FVector& Eye, const FRotator& Aim, FVector2D AimTurn, float PawnSpeed,
	bool bCrouched, float AimBlend, float DeltaSeconds, bool bCut)
{
	const FVector Travel = Eye - LastEye;
	const float YawStep = FMath::FindDeltaAngleDegrees(LastAim.Yaw, Aim.Yaw);
	const float PitchStep = FMath::FindDeltaAngleDegrees(LastAim.Pitch, Aim.Pitch);
	if (!bSampled || bCut || Travel.SizeSquared() > FMath::Square(FMath::Max(100.0f, 1000.0f * DeltaSeconds))
		|| FMath::Abs(FRotator::NormalizeAxis(YawStep - AimTurn.X)) > 70.0
		|| FMath::Abs(FRotator::NormalizeAxis(PitchStep - AimTurn.Y)) > 50.0)
	{
		*this = {};
		bSampled = true;
		LastAim = Aim;
		LastEye = Eye;
		return FRotator::ZeroRotator;
	}
	if (DeltaSeconds <= UE_SMALL_NUMBER)
	{
		return Offset;
	}
	LastAim = Aim;
	LastEye = Eye;

	// The reference first moves the common control pivot at 25/s. Its camera boom follows
	// that pivot at 2..16/s, depending on turn velocity. Keep both offsets in gameplay aim space.
	const double LookSpeed = (FMath::Abs(AimTurn.X) + FMath::Abs(AimTurn.Y)) / DeltaSeconds;
	const double TargetSpeed = FMath::Lerp(2.0, 16.0, FMath::Clamp((LookSpeed - 50.0) / 50.0, 0.0, 1.0));
	const double RateChange = (TargetSpeed - CameraFollowSpeed) * (1.0 - FMath::Exp(-5.0 * DeltaSeconds));
	CameraFollowSpeed += RateChange;
	const double CameraFollow = TargetSpeed * DeltaSeconds - RateChange / 5.0;
	const double PivotFollow = 25.0 * DeltaSeconds;

	// Angular travel adds damping in proportion to the hip/sight window. This bounds a hard
	// flick without clipping at a rim or accumulating excess rotation to unwind on reversal.
	const FVector2D Limits(FMath::Lerp(16.0, 1.25, AimBlend), FMath::Lerp(10.0, 0.85, AimBlend));
	const double TravelInWindow = (AimTurn / Limits).Size();
	const double PivotStep = PivotFollow + TravelInWindow;
	const double CameraStep = CameraFollow + TravelInWindow;
	const FVector2D PivotTarget = AimTurn / PivotStep;
	const FVector2D CameraTarget = (AimTurn + PivotTarget * CameraFollow) / CameraStep;
	const FVector2D PreviousPivot = (PivotOffset / Limits).GetClampedToMaxSize(1.0) * Limits;
	const FVector2D PreviousGun = (GunOffset / Limits).GetClampedToMaxSize(1.0) * Limits;

	// Integrate both lag stages over the frame, including the pivot's motion during this
	// sample. On release the same equations converge on the held aim with zero input.
	const double PivotRetained = FMath::Exp(-PivotStep);
	const double CameraRetained = FMath::Exp(-CameraStep);
	const double PivotInfluence = CameraFollow * (CameraRetained - PivotRetained) / (PivotFollow - CameraFollow);
	PivotOffset = FMath::Lerp(PivotTarget, PreviousPivot, PivotRetained);
	GunOffset = FMath::Lerp(CameraTarget, PreviousGun, CameraRetained) + (PreviousPivot - PivotTarget) * PivotInfluence;

	const float Distance = static_cast<float>(Travel.Size2D());
	const float HorizontalSpeed = FMath::Min(PawnSpeed, Distance / DeltaSeconds);
	const float Pace = FMath::Clamp(HorizontalSpeed / 280.0f, 0.0f, 1.0f);
	const float StepLength = bCrouched ? 52.0f : FMath::Lerp(62.0f, 85.0f, Pace);
	if (HorizontalSpeed > 3.0f)
	{
		WalkPhase = FMath::Fmod(WalkPhase + UE_PI * Distance / StepLength, 2.0f * UE_PI);
	}
	const float Ease = 1.0f - FMath::Exp(-10.0f * DeltaSeconds);
	WalkWeight = FMath::Lerp(WalkWeight, Pace * (bCrouched ? 0.48f : 1.0f), Ease);
	const FVector Right = FRotationMatrix(FRotator(0.0, Aim.Yaw, 0.0)).GetUnitAxis(EAxis::Y);
	const float Lateral = FMath::Clamp(static_cast<float>(FVector::DotProduct(Travel, Right)) / (DeltaSeconds * 240.0f), -1.0f, 1.0f);
	Bank = FMath::Lerp(Bank, Lateral, Ease);

	// A rapid descent ending at floor height compresses the mount once. Ordinary stride bob is below this speed.
	const float Fall = static_cast<float>(-Travel.Z) / DeltaSeconds;
	LandingDip *= FMath::Exp(-12.0f * DeltaSeconds);
	if (DownwardSpeed > 180.0f && Fall < 60.0f)
	{
		LandingDip = FMath::Max(LandingDip, FMath::Clamp(DownwardSpeed / 330.0f, 0.0f, 1.5f));
	}
	DownwardSpeed = Fall;

	const float Step = FMath::Sin(WalkPhase);
	const float Weight = FMath::Lerp(1.0f, 0.18f, AimBlend);
	// SPlayerCam already places the camera at the moving pawn's eye. The mount adds no
	// translation; small angular travel keeps the walking horizon stable even at full speed.
	Offset = FRotator(
		Weight * (0.075f * FMath::Sin(2.0f * WalkPhase) * WalkWeight - 0.25f * LandingDip),
		Weight * 0.05f * Step * WalkWeight,
		Weight * (0.16f * Step * WalkWeight + 0.20f * Bank));
	return Offset;
}

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScctMercCameraMotionTest, "OpenSCCT.Presentation.MercCamera.Motion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FScctMercCameraMotionTest::RunTest(const FString& Parameters)
{
	auto WalkAndTurn = [](int32 Hz)
	{
		const float Dt = 1.0f / Hz;
		FScctMercCameraMotion Motion;
		FRotator Result;
		for (int32 Frame = 0; Frame <= Hz; ++Frame)
		{
			// Cross the yaw seam while walking in a straight line.
			const float Time = Frame * Dt;
			const FRotator Aim(0.0, FRotator::NormalizeAxis(170.0 + 56.0 * Time), 0.0);
			const FRotator Shake = Motion.Update(FVector(150.0 * Time, 0.0, 0.0), Aim, FVector2D(56.0 * Dt, 0.0),
				150.0f, false, 0, Dt, false);
			Result = (Motion.CameraAnchor(Aim).Quaternion() * Shake.Quaternion()).Rotator();
		}
		return Result;
	};
	TestTrue(TEXT("Walking turns at 30 and 240 FPS produce the same camera within a tenth of a degree"),
		WalkAndTurn(30).Equals(WalkAndTurn(240), 0.1));
	FScctMercCameraMotion Motion;
	constexpr float Step = 1.0f / 60.0f;
	const FRotator Straight = FRotator::ZeroRotator;
	const FVector2D Still = FVector2D::ZeroVector;
	auto Turn = [&](FRotator Gun, FVector2D Input, float AimBlend = 0.0f, bool bCut = false)
	{
		Motion.Update(FVector::ZeroVector, Gun, Input, 0, false, AimBlend, Step, bCut);
		return Motion.CameraAnchor(Gun);
	};
	Turn(Straight, Still);
	const FRotator Small(0.5, 2, 0);
	const FRotator SmallCamera = Turn(Small, FVector2D(2, 0.5));
	TestTrue(TEXT("The rifle leads a small turn while the camera also starts turning"),
		SmallCamera.Yaw > 0.001 && SmallCamera.Yaw < Small.Yaw * 0.5);
	const FRotator Wide(12, 22, 0);
	const FRotator AtShoulder = Turn(Wide, FVector2D(20, 11.5));
	const FVector2D Radius((Wide.Yaw - AtShoulder.Yaw) / 16.0, (Wide.Pitch - AtShoulder.Pitch) / 10.0);
	TestTrue(TEXT("Diagonal aim remains inside the hip ellipse"), Radius.Size() < 1.0 && Radius.Size() > 0.5);
	auto SettleAfterTurn = [&](int32 Hz)
	{
		FScctMercCameraMotion Copy = Motion;
		for (int32 Frame = 0; Frame < Hz / 2; ++Frame)
		{
			Copy.Update(FVector::ZeroVector, Wide, Still, 0, false, 0, 1.0f / Hz, false);
		}
		return Copy.CameraAnchor(Wide);
	};
	TestTrue(TEXT("Camera catch-up after release is consistent at 30 and 240 FPS"),
		SettleAfterTurn(30).Equals(SettleAfterTurn(240), 0.005));
	FScctMercCameraMotion WithoutKick = Motion;
	WithoutKick.Update(FVector::ZeroVector, Wide, Still, 0, false, 0, Step, false);
	const FRotator Kick(2, 0, 2);
	TestTrue(TEXT("A camera kick passes through during hip recentering without banking the weapon"),
		Turn(Wide + Kick, Still).Equals(WithoutKick.CameraAnchor(Wide) + Kick));
	const FRotator Ads = Turn(Wide, Still, 1);
	TestTrue(TEXT("ADS pulls the sight into the smaller ellipse"),
		FVector2D((Wide.Yaw - Ads.Yaw) / 1.25, (Wide.Pitch - Ads.Pitch) / 0.85).Size() <= 1.000001);
	const FRotator Lowered = Turn(Wide, Still);
	TestTrue(TEXT("Lowering the sights continues the return toward the held aim"),
		FMath::Abs(Wide.Yaw - Lowered.Yaw) < FMath::Abs(Wide.Yaw - Ads.Yaw));
	TestTrue(TEXT("Gadget exit or a cut recentres the weapon"), Turn(Small, Still, 0, true).Equals(Small));
	const FRotator FlickCamera = Turn(FRotator(0.5, 92, 0), FVector2D(90, 0));
	TestTrue(TEXT("A fast mouse flick carries the camera while leaving independent rifle travel"),
		FlickCamera.Yaw > 75.9 && FlickCamera.Yaw < 82.0);

	for (float SightBlend : {0.0f, 0.5f, 1.0f})
	{
		for (double Direction : {-1.0, 1.0})
		{
			const FRotator Held(3.0, 8.0 * Direction, 0);
			Turn(Straight, Still, SightBlend, true);
			const FRotator EndOfTurn = Turn(Held, FVector2D(Held.Yaw, Held.Pitch), SightBlend);
			const double InitialError = (Held - EndOfTurn).GetNormalized().Yaw * Direction;
			const FRotator FirstIdle = Turn(Held, Still, SightBlend);
			const double IdleError = (Held - FirstIdle).GetNormalized().Yaw * Direction;
			TestTrue(TEXT("The mount settles progressively from either side when input stops"),
				InitialError > 0.5 && IdleError > InitialError * 0.5 && IdleError < InitialError);
			for (int32 Frame = 0; Frame < 240; ++Frame)
			{
				Turn(Held, Still, SightBlend);
			}
			TestTrue(TEXT("Idle hip, transition and ADS converge on the held pitch and yaw"),
				Motion.CameraAnchor(Held).Equals(Held, 0.01));
		}
	}

	for (int32 Hz : {30, 60, 144, 240})
	{
		const float Dt = 1.0f / Hz;
		FScctMercCameraMotion Pan;
		FRotator Heading = Straight;
		Pan.Update(FVector::ZeroVector, Heading, Still, 0, false, 0, Dt, false);
		for (int32 Frame = 0; Frame < Hz; ++Frame)
		{
			const FVector2D Input(12.0 * Dt, 0.0);
			Heading.Yaw += Input.X;
			Pan.Update(FVector::ZeroVector, Heading, Input, 0, false, 0, Dt, false);
		}
		const double PanLag = Heading.Yaw - Pan.CameraAnchor(Heading).Yaw;
		TestTrue(TEXT("A continuing slow pan turns the camera and keeps the rifle inside the window"),
			PanLag > 0.1 && PanLag < Heading.Yaw * 0.75);
		FScctMercCameraMotion Stopped = Pan;
		FScctMercCameraMotion Reversed = Pan;
		const FRotator StartOfRelease = Heading;
		for (int32 Frame = 0; Frame < Hz; ++Frame)
		{
			const FVector2D Drift(0.001 * Dt, 0.0);
			Heading.Yaw += Drift.X;
			Pan.Update(FVector::ZeroVector, Heading, Drift, 0, false, 0, Dt, false);
			Stopped.Update(FVector::ZeroVector, StartOfRelease, Still, 0, false, 0, Dt, false);
		}
		const double DriftLag = Heading.Yaw - Pan.CameraAnchor(Heading).Yaw;
		const double StoppedLag = StartOfRelease.Yaw - Stopped.CameraAnchor(StartOfRelease).Yaw;
		TestTrue(TEXT("The rifle returns after an abrupt stop even with residual stick input"),
			StoppedLag > 0 && StoppedLag < PanLag * 0.2 && FMath::Abs(DriftLag - StoppedLag) < 0.002);
		const FRotator Back(0, StartOfRelease.Yaw - 1, 0);
		Reversed.Update(FVector::ZeroVector, Back, FVector2D(-1, 0), 0, false, 0, Dt, false);
		TestTrue(TEXT("Countersteering immediately reduces the existing rifle lead"),
			FMath::Abs(Back.Yaw - Reversed.CameraAnchor(Back).Yaw) < PanLag);
	}

	Motion.Update(FVector(0, 0, 300), Straight, Still, 0, false, 0, Step, true);
	Motion.Update(FVector(0, 0, 295), Straight, Still, 300, false, 0, Step, false);
	const FRotator Land = Motion.Update(FVector(0, 0, 295), Straight, Still, 0, false, 0, Step, false);
	TestTrue(TEXT("Landing pitches the mount down"), Land.Pitch < 0);
	TestTrue(TEXT("Pause holds the mount still"), Motion.Update(FVector(0, 0, 295), Straight, Still, 0, false, 0, 0, false).Equals(Land));
	TestTrue(TEXT("A new origin has no artificial step or landing"), Motion.Update(FVector::ZeroVector, Straight, Still, 300, false, 0, Step, false).IsZero());
	double PeakWalkRoll = 0.0;
	for (int32 Frame = 1; Frame <= 180; ++Frame)
	{
		const FRotator Shake = Motion.Update(FVector(280.0 * Step * Frame, 0, 0), Straight, Still, 280, false, 0, Step, false);
		PeakWalkRoll = FMath::Max(PeakWalkRoll, FMath::Abs(Shake.Roll));
		if (FMath::Abs(Shake.Pitch) > 0.0751 || FMath::Abs(Shake.Yaw) > 0.0501)
		{
			AddError(TEXT("Ordinary walking exceeded pitch or yaw amplitude"));
			break;
		}
	}
	TestTrue(TEXT("Walk roll is present and restrained at full pace"), PeakWalkRoll > 0.10 && PeakWalkRoll <= 0.1601);
	return true;
}

#endif
