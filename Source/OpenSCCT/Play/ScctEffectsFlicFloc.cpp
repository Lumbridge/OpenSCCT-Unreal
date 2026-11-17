// Rain splashing on a pawn (SFlicFlocEmitter, spec/equipment/rain.md rule 4), as the Godot client's
// game/Scripts/Fx/EffectViews.FlicFloc.cs draws it. The simulation's emitter sits on the pawn's head and keeps what its
// script set into its three sub-emitters; this applies that to the drawn copy: sub-emitter 0 splashes over the drawn
// body while the pawn is in the rain (a random one of the original's 11 bone segments, a random point between its two
// raised bones, spread across the pawn's forward and right axes), sub-emitter 1 drips from the head for ten seconds
// after it leaves the rain with the pawn's horizontal speed, and sub-emitter 2 is a puff the script triggers every
// 2.5 s in the rain. Each restart and trigger is applied once.

#include "Components/PoseableMeshComponent.h"
#include "OpenSCCT.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/**
	 * One body segment of the rain-mode spawn table: two bones by their index in the original's skeleton, each raised
	 * by a height, and the spread across the pawn's forward and right axes. UFlicFlocEmitter::SpawnParticle's table
	 * (EffectViews.FlicFloc s_splashSegments).
	 */
	struct FFlicSplashSegment
	{
		int32 BoneA;
		float RiseA;
		int32 BoneB;
		float RiseB;
		float SpreadForward;
		float SpreadRight;
	};

	const FFlicSplashSegment FlicSplashSegments[] = {
		{ 14, 2.0f, 11, 3.0f, 2.0f, 5.0f }, { 11, 5.0f, 10, 5.0f, 2.0f, 5.0f }, { 10, 5.0f, 9, 5.0f, 2.0f, 5.0f }, { 9, 8.0f, 5, 11.0f, 10.0f, 2.0f },
		{ 26, 2.0f, 23, 3.0f, 2.0f, 5.0f }, { 23, 5.0f, 22, 5.0f, 2.0f, 5.0f }, { 22, 5.0f, 21, 5.0f, 2.0f, 5.0f }, { 21, 8.0f, 5, 11.0f, 10.0f, 2.0f },
		{ 6, 20.0f, 6, 20.0f, 9.0f, 9.0f }, { 4, 20.0f, 5, 9.0f, 5.0f, 20.0f }, { 4, 25.0f, 3, 25.0f, 0.0f, 20.0f },
	};

	/**
	 * The bones of those indices in the converted meshes, by the bridge's names (the spy's ATT_01 and the merc's
	 * DEF_01 differ from index 7 on, so the spy's left arm entries fall a bone further out).
	 */
	const TCHAR* FlicBoneName(int32 Kind, int32 Index)
	{
		switch (Index)
		{
		case 3: return TEXT("B Spine1");
		case 4: return TEXT("B Spine2");
		case 5: return TEXT("B Neck");
		case 6: return TEXT("B Head");
		case 9: return Kind == 0 ? TEXT("B L UpperArm") : TEXT("B L Clavicle");
		case 10: return Kind == 0 ? TEXT("B L ForeArm") : TEXT("B L UpperArm");
		case 11: return Kind == 0 ? TEXT("B L Hand") : TEXT("B L ForeArm");
		case 14: return Kind == 0 ? TEXT("B L Finger1") : TEXT("B L Finger01");
		case 21: return TEXT("B R Clavicle");
		case 22: return TEXT("B R UpperArm");
		case 23: return TEXT("B R ForeArm");
		case 26: return TEXT("B R Finger01");
		default: return TEXT("");
		}
	}

	/** The drawn body's bone for a table index, looked up once per pawn kind (only found bones are kept). */
	FName FindFlicBone(const AScctPawnProxy& Proxy, int32 Index)
	{
		static TMap<int32, FName> Known;
		const int32 Key = Proxy.GetKind() * 64 + Index;
		if (const FName* Found = Known.Find(Key))
		{
			return *Found;
		}
		const FName Bone = Proxy.FindMeshBoneName(FlicBoneName(Proxy.GetKind(), Index));
		if (!Bone.IsNone())
		{
			Known.Add(Key, Bone);
		}
		return Bone;
	}

	/**
	 * Spawns owed this frame by a sub-emitter running at Rate a second (ParticleEmitterSim with a rate override), with
	 * FlicFlocEmitter.VariableMaxParticles: while the budget is not 0 it caps the spawns and each spawn uses one; once
	 * used up the rate spawns freely again (UFlicFlocEmitter::SpawnParticles).
	 */
	int32 TakeFlicSpawns(float Rate, bool bRespawn, float DeltaSeconds, float& Debt, int32& Budget)
	{
		if (Rate <= 0.0f || !bRespawn)
		{
			Debt = 0.0f;
			return 0;
		}
		Debt += Rate * DeltaSeconds;
		int32 Count = FMath::FloorToInt(Debt);
		Debt -= Count;
		if (Budget != 0)
		{
			Count = FMath::Clamp(Count, 0, Budget);
			Budget -= Count;
		}
		return FMath::Min(Count, 16);
	}
}

bool UScctEffectsSubsystem::UpdateFlicFloc(FActorFx& Fx, const FScctEffectActor& Record, const FVector& Head, bool bHidden, float DeltaSeconds)
{
	if (FlicFlocApi == nullptr)
	{
		if (bFlicFlocApiTried)
		{
			return false;
		}
		bFlicFlocApiTried = true;
		FString Error;
		FlicFlocApi = ScctFeature<FScctFlicFlocApi>(&Error);
		if (FlicFlocApi == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Effects: rain on a pawn is drawn as a plain splash at the head, the bridge's rain-on-pawn table is missing: %s"), *Error);
			return false;
		}
	}

	FScctFlicFloc State;
	if (FlicFlocApi->GetFlicFloc(Record.ActorId, &State) == 0)
	{
		return false;
	}

	// The script's restarts and triggers, each once (EffectViews.ApplyFlicFloc). Puffs fired before this copy was
	// first seen are not played all at once.
	if (!Fx.bFlicSeen)
	{
		Fx.bFlicSeen = true;
		Fx.FlicPuffTriggers = State.PuffTriggers;
	}
	if (Fx.FlicSplashStarts != State.SplashStarts)
	{
		Fx.FlicSplashStarts = State.SplashStarts;
		Fx.FlicSplashBudget = State.SplashBudget;
		Fx.FlicSplashDebt = 0.0f;
		if (bLog || NumFlicSplashes == 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Effects: pawn #%d is in the rain: splashes over the body at %.1f a second (budget %d), puff every 2.5 s"), State.PawnActorId, State.SplashRate, State.SplashBudget);
		}
	}
	if (Fx.FlicDripStarts != State.DripStarts)
	{
		Fx.FlicDripStarts = State.DripStarts;
		Fx.FlicDripBudget = State.DripBudget;
		Fx.FlicDripDebt = 0.0f;
		if (bLog || NumFlicDrips == 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Effects: pawn #%d left the rain: drips from the head at %.1f a second for 10 s"), State.PawnActorId, State.DripRate);
		}
	}
	const int32 Puffs = FMath::Clamp(State.PuffTriggers - Fx.FlicPuffTriggers, 0, 4);
	Fx.FlicPuffTriggers = State.PuffTriggers;

	int32 Splashes = TakeFlicSpawns(State.SplashRate, State.SplashRespawn != 0, DeltaSeconds, Fx.FlicSplashDebt, Fx.FlicSplashBudget);
	const int32 Drips = TakeFlicSpawns(State.DripRate, State.DripRespawn != 0, DeltaSeconds, Fx.FlicDripDebt, Fx.FlicDripBudget);
	if (bHidden)
	{
		return true;
	}

	// Sub-emitter 0: on the drawn body (UFlicFlocEmitter::SpawnParticle in rain mode).
	const AScctPawnProxy* Proxy = Session != nullptr ? Session->FindProxy(State.PawnActorId) : nullptr;
	const UPoseableMeshComponent* Body = Proxy != nullptr && Proxy->HasModel() ? Proxy->GetBodyMesh() : nullptr;
	if (Body != nullptr && Splashes > 0)
	{
		FVector Forward, Right, Up;
		FRotationMatrix(FRotator(0.0f, Proxy->GetPawn().Yaw, 0.0f)).GetScaledAxes(Forward, Right, Up);
		for (; Splashes > 0; --Splashes)
		{
			const FFlicSplashSegment& Segment = FlicSplashSegments[Random.RandHelper(UE_ARRAY_COUNT(FlicSplashSegments))];
			const FName BoneA = FindFlicBone(*Proxy, Segment.BoneA);
			const FName BoneB = FindFlicBone(*Proxy, Segment.BoneB);
			if (BoneA.IsNone() || BoneB.IsNone())
			{
				continue;
			}
			const FVector From = Body->GetSocketLocation(BoneA) + FVector(0.0, 0.0, Segment.RiseA);
			const FVector To = Body->GetSocketLocation(BoneB) + FVector(0.0, 0.0, Segment.RiseB);
			const float F = Random.FRand() * Segment.SpreadForward - Segment.SpreadForward * 0.5f;
			const float R = Random.FRand() * Segment.SpreadRight - Segment.SpreadRight * 0.5f;
			const FVector At = From + (To - From) * Random.FRand() + Forward * F + Right * R;

			// The preset's splash (sub-emitter 0 of SFlicFlocEmitter): 0.2 to 0.3 s, a start size of 0.5 to 1.5
			// growing to four times that, thrown 5 to 15 up and 5 either way across, pulled down at 60.
			FScctFxParticle& Splash = Sprites.Spawn(EScctFxLayer::Smoke);
			Splash.Position = At;
			Splash.Velocity = FVector(Random.FRandRange(-5.0f, 5.0f), Random.FRandRange(-5.0f, 5.0f), Random.FRandRange(5.0f, 15.0f));
			Splash.Color = FLinearColor(0.82f, 0.88f, 0.97f, 0.6f);
			Splash.Life = Random.FRandRange(0.2f, 0.3f);
			Splash.SizeStart = 2.0f * Random.FRandRange(0.5f, 1.5f);
			Splash.SizeEnd = Splash.SizeStart * 4.0f;
			Splash.Gravity = 60.0f;
			Splash.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
			if (NumFlicSplashes < 6)
			{
				const FScctBridgePawn& Pawn = Proxy->GetPawn();
				UE_LOG(LogOpenSCCT, Log, TEXT("Effects: rain splash on pawn #%d between bones '%s' and '%s' at (%.0f, %.0f, %.0f), %.0f cm above the middle of the body"),
					State.PawnActorId, *BoneA.ToString(), *BoneB.ToString(), At.X, At.Y, At.Z, At.Z - Pawn.Z);
			}
			++NumFlicSplashes;
		}
	}

	// Sub-emitter 1: from the emitter on the head, with the pawn's horizontal speed.
	for (int32 Index = 0; Index < Drips; ++Index)
	{
		FScctFxParticle& Drip = Sprites.Spawn(EScctFxLayer::Smoke);
		Drip.Position = Head + Random.VRand() * 3.0;
		Drip.Velocity = FVector(State.PawnVelX, State.PawnVelY, 0.0);
		Drip.Color = FLinearColor(0.78f, 0.85f, 0.95f, 0.55f);
		Drip.Life = Random.FRandRange(1.0f, 2.0f);
		Drip.SizeStart = Drip.SizeEnd = 2.0f * Random.FRandRange(0.7f, 0.9f);
		Drip.Gravity = 200.0f;
		Drip.Stretch = 0.025f;
		++NumFlicDrips;
	}

	// Sub-emitter 2: a puff of spray off the head on each trigger (the preset: 0.5 to 0.6 s, size 5 swelling to three
	// times, thrown 50 to 80 along the emitter's -Y, taken here as the pawn's left).
	const FVector PuffLeft = -FRotationMatrix(FRotator(0.0f, Proxy != nullptr ? Proxy->GetPawn().Yaw : 0.0f, 0.0f)).GetScaledAxis(EAxis::Y);
	for (int32 Puff = 0; Puff < Puffs; ++Puff)
	{
		for (int32 Index = 0; Index < 6; ++Index)
		{
			FScctFxParticle& Mist = Sprites.Spawn(EScctFxLayer::Smoke);
			Mist.Position = Head + Random.VRand() * 4.0;
			Mist.Velocity = PuffLeft * Random.FRandRange(50.0f, 80.0f) + Random.VRand() * 8.0;
			Mist.Color = FLinearColor(0.8f, 0.84f, 0.9f, 0.18f);
			Mist.Life = Random.FRandRange(0.5f, 0.6f);
			Mist.SizeStart = 10.0f;
			Mist.SizeEnd = 30.0f;
			Mist.Rotation = Random.FRandRange(0.0f, UE_TWO_PI);
			Mist.FadeIn = 0.1f;
		}
		++NumFlicPuffs;
	}
	return true;
}
