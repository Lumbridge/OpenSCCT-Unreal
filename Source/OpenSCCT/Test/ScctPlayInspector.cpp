#include "Test/ScctPlayInspector.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/World.h"
#include "Hud/ScctHudSubsystem.h"
#include "Play/ScctAudioSubsystem.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Test/ScctInspectApi.h"
#include "Test/ScctTest.h"
#include "Test/ScctTestApi.h"

namespace ScctPlayInspectorLocal
{
	constexpr float NoValue = TNumericLimits<float>::Lowest();

	/** A move this fast between two windows' ends is a placement (spawn, respawn), not walking. */
	constexpr float PlacedSpeed = 2000.0f;

	/** Seconds a walking pawn may go without the floor under it before that is an issue (an edge, a step). */
	constexpr double UnsupportedGrace = 0.25;

	FVector DrawnFeetOrigin(const AScctPawnProxy& Proxy)
	{
		const FScctBridgePawn& Pawn = Proxy.GetPawn();
		return Proxy.GetActorLocation() - FVector(Pawn.PrePivotX, Pawn.PrePivotY, Pawn.PrePivotZ);
	}
}

FString FScctInspectIssue::ToString() const
{
	const FString WorstPart = Worst != ScctPlayInspectorLocal::NoValue && WorstMessage != Message ? FString::Printf(TEXT(" | worst: %s"), *WorstMessage) : FString();
	return FString::Printf(TEXT("[%s] %s (seen %dx, first in step '%s' at %.1f s)%s"),
		FScctPlayInspector::AreaName(Area), *Message, Count, *FirstStep, FirstSeconds, *WorstPart);
}

const TCHAR* FScctPlayInspector::AreaName(EScctInspectArea Area)
{
	switch (Area)
	{
	case EScctInspectArea::Animation: return TEXT("Animation");
	case EScctInspectArea::Feet: return TEXT("Feet");
	case EScctInspectArea::Camera: return TEXT("Camera");
	case EScctInspectArea::Audio: return TEXT("Audio");
	case EScctInspectArea::Net: return TEXT("Net");
	case EScctInspectArea::Hud: return TEXT("Hud");
	case EScctInspectArea::PauseMenu: return TEXT("PauseMenu");
	case EScctInspectArea::Input: return TEXT("Input");
	default: return TEXT("?");
	}
}

FScctPlayInspector::~FScctPlayInspector()
{
	Detach();
}

bool FScctPlayInspector::Attach(UWorld* InWorld, int32 LocalSide, FString* OutError)
{
	Detach();
	UScctSessionSubsystem* InSession = InWorld != nullptr ? InWorld->GetSubsystem<UScctSessionSubsystem>() : nullptr;
	if (InSession == nullptr || !InSession->IsPlaying())
	{
		if (OutError != nullptr)
		{
			*OutError = TEXT("no match plays in the world");
		}
		return false;
	}

	FString Error;
	Api = ScctFeature<FScctInspectApi>(&Error);
	if (Api == nullptr)
	{
		if (OutError != nullptr)
		{
			*OutError = FString::Printf(TEXT("the bridge's inspect table is missing: %s"), *Error);
		}
		return false;
	}

	World = InWorld;
	Session = InSession;
	Side = LocalSide;
	SessionId = InSession->GetSessionId();
	FrameHandle = InSession->OnFrame.AddRaw(this, &FScctPlayInspector::HandleFrame);
	PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FScctPlayInspector::HandlePostActorTick);
	UE_LOG(LogScctTest, Log, TEXT("SCCT_INSPECT attached to session %d (local side %s)"), SessionId, Side == 1 ? TEXT("merc") : TEXT("spy"));
	return true;
}

void FScctPlayInspector::Detach()
{
	if (UScctSessionSubsystem* Watched = Session.Get())
	{
		Watched->OnFrame.Remove(FrameHandle);
	}
	if (PostTickHandle.IsValid())
	{
		FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
	}
	FrameHandle.Reset();
	PostTickHandle.Reset();
	Session.Reset();
	World.Reset();
}

void FScctPlayInspector::Note(EScctInspectArea Area, const FString& Subject, const FString& Message, float Value)
{
	const FString Key = FString::Printf(TEXT("%s|%s"), AreaName(Area), *Subject);
	if (const int32* Found = IssueIndex.Find(Key))
	{
		FScctInspectIssue& Issue = Issues[*Found];
		++Issue.Count;
		if (Value != ScctPlayInspectorLocal::NoValue && (Issue.Worst == ScctPlayInspectorLocal::NoValue || Value > Issue.Worst))
		{
			Issue.Worst = Value;
			Issue.WorstMessage = Message;
		}
		return;
	}

	FScctInspectIssue& Issue = Issues.AddDefaulted_GetRef();
	Issue.Area = Area;
	Issue.Subject = Subject;
	Issue.Message = Message;
	Issue.FirstStep = CurrentStep.IsEmpty() ? FString(TEXT("(none)")) : CurrentStep;
	Issue.FirstSeconds = Seconds;
	Issue.Count = 1;
	Issue.Worst = Value;
	Issue.WorstMessage = Message;
	IssueIndex.Add(Key, Issues.Num() - 1);
	UE_LOG(LogScctTest, Warning, TEXT("SCCT_INSPECT new %s"), *Issue.ToString());
}

bool FScctPlayInspector::GetLocal(FScctInspectPawn& Out) const
{
	for (const FScctInspectPawn& Pawn : SimPawns)
	{
		if ((Pawn.Flags & EScctInspectPawnFlags::Local) != 0)
		{
			Out = Pawn;
			return true;
		}
	}
	return false;
}

bool FScctPlayInspector::TraceWorld(const FVector& Start, const FVector& End, const FVector& Extent, FVector* OutHit, float* OutNormalZ) const
{
	if (Api == nullptr)
	{
		return false;
	}
	const float From[3] = { static_cast<float>(Start.X), static_cast<float>(Start.Y), static_cast<float>(Start.Z) };
	const float To[3] = { static_cast<float>(End.X), static_cast<float>(End.Y), static_cast<float>(End.Z) };
	const float Size[3] = { static_cast<float>(Extent.X), static_cast<float>(Extent.Y), static_cast<float>(Extent.Z) };
	float Hit[4] = {};
	if (Api->Trace(From, To, Size, Hit) != 1)
	{
		return false;
	}
	if (OutHit != nullptr)
	{
		*OutHit = FVector(Hit[0], Hit[1], Hit[2]);
	}
	if (OutNormalZ != nullptr)
	{
		*OutNormalZ = Hit[3];
	}
	return true;
}

int32 FScctPlayInspector::GetVoicesStarted() const
{
	const UWorld* Watched = World.Get();
	const UScctAudioSubsystem* Audio = Watched != nullptr ? Watched->GetSubsystem<UScctAudioSubsystem>() : nullptr;
	return Audio != nullptr ? Audio->GetNumStarted() : 0;
}

FString FScctPlayInspector::NameOf(int32 Id) const
{
	if (Api == nullptr || Id == 0)
	{
		return TEXT("None");
	}
	TCHAR Buffer[128];
	Api->GetName(Id, Buffer, UE_ARRAY_COUNT(Buffer));
	return FString(Buffer);
}

FString FScctPlayInspector::Stance(const FScctInspectPawn& Sim)
{
	const bool bMoving = FMath::Square(Sim.VelocityX) + FMath::Square(Sim.VelocityY) > FMath::Square(MovingSpeed);
	return FString::Printf(TEXT("%s %s"), (Sim.Flags & EScctInspectPawnFlags::Crouched) != 0 ? TEXT("crouched") : TEXT("standing"), bMoving ? TEXT("moving") : TEXT("still"));
}

void FScctPlayInspector::HandleFrame(float DeltaSeconds)
{
	UScctSessionSubsystem* Watched = Session.Get();
	if (Watched == nullptr || Api == nullptr || Watched->GetSessionId() != SessionId)
	{
		return;
	}
	Seconds += DeltaSeconds;
	++Frames;

	SimPawns.SetNumUninitialized(64);
	SimPawns.SetNum(FMath::Clamp(Api->GetPawns(SimPawns.GetData(), SimPawns.Num()), 0, SimPawns.Num()));

	FScctInspectCounters Counters = {};
	if (Api->GetCounters(&Counters) != 0)
	{
		SoundEvents = Counters.SoundEvents;
		SoundsStarted = Counters.SoundsStarted;
		SoundsUnknown = Counters.SoundsUnknown;
		Snaps = Counters.Snaps;
		WorstSnap = Counters.WorstSnap;
		ClientTicks = Counters.ClientTicks;
		MovingTicks = Counters.LocalMovingTicks;
	}
	if (const FScctTestApi* Test = ScctFeature<FScctTestApi>())
	{
		FScctTestCounters Net = {};
		if (Test->GetCounters(&Net) != 0)
		{
			CorrectionsApplied = Net.CorrectionsApplied;
			CorrectionsReceived = Net.CorrectionsReceived;
		}
	}
	if (VoicesAtStart < 0)
	{
		VoicesAtStart = GetVoicesStarted();
	}

	for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Watched->GetProxies())
	{
		const AScctPawnProxy* Proxy = Pair.Value.Get();
		FPoseWatch* Existing = Poses.Find(Pair.Key);
		if (Proxy == nullptr || !Proxy->HasModel() || !Proxy->IsShown())
		{
			if (Existing != nullptr)
			{
				Existing->bWindowOpen = false;
			}
			continue;
		}

		FPoseWatch& Watch = Existing != nullptr ? *Existing : Poses.Add(Pair.Key);
		if (Watch.Label.IsEmpty())
		{
			Watch.Label = FString::Printf(TEXT("%s %d (%s)"), Proxy->IsLocal() ? TEXT("you") : TEXT("bot"), Pair.Key, Proxy->GetKind() == 1 ? TEXT("merc") : TEXT("spy"));
		}
		const FScctInspectPawn* Sim = SimPawns.FindByPredicate([&](const FScctInspectPawn& Candidate) { return Candidate.ActorId == Pair.Key; });
		CheckPose(Watch, *Proxy, Sim, DeltaSeconds);
	}

	CheckHud();
}

void FScctPlayInspector::CheckPose(FPoseWatch& Watch, const AScctPawnProxy& Proxy, const FScctInspectPawn* Sim, float DeltaSeconds)
{
	const UPoseableMeshComponent* Body = Proxy.GetBodyMesh();
	static const TArray<FTransform> NoBones;
	const TArray<FTransform>& Bones = Body != nullptr ? Body->GetComponentSpaceTransforms() : NoBones;
	const int32 Count = Bones.Num();
	if (Count == 0)
	{
		Watch.bWindowOpen = false;
		return;
	}
	Watch.bSeen = true;

	if (!Watch.bFootBonesFound)
	{
		Watch.bFootBonesFound = true;
		for (int32 Index = 0; Index < Body->GetNumBones(); ++Index)
		{
			// The biped's foot and toe bones ("B L Foot", "B R Toe0"); helper bones the animations do not pose would read as under the floor.
			const FString Name = Body->GetBoneName(Index).ToString();
			if (Name.Contains(TEXT("Foot")) || Name.Contains(TEXT("Toe")))
			{
				Watch.FootBones.Add(Index);
			}
		}
	}

	if (Sim != nullptr && (Sim->MissingSimClips != 0 || Sim->MissingMeshClips != 0))
	{
		const FString Sequence = NameOf(Sim->MissingName);
		if (Sim->MissingSimClips != 0)
		{
			Note(EScctInspectArea::Animation, FString::Printf(TEXT("no sim clip %s"), *Sequence),
				FString::Printf(TEXT("%s: channels 0x%x play %s but the simulation has no clip for it (nothing is drawn for that channel)"), *Watch.Label, Sim->MissingSimClips, *Sequence));
		}
		else
		{
			Note(EScctInspectArea::Animation, FString::Printf(TEXT("no mesh clip %s"), *Sequence),
				FString::Printf(TEXT("%s: channels 0x%x play %s but the drawn character file has no animation of that name"), *Watch.Label, Sim->MissingMeshClips, *Sequence));
		}
	}

	const FVector Location = ScctPlayInspectorLocal::DrawnFeetOrigin(Proxy);
	if (!Watch.bWindowOpen || Watch.Start.Num() != Count)
	{
		Watch.Start.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Watch.Start[Index] = Bones[Index].GetLocation();
		}
		Watch.StartLocation = Location;
		Watch.StartSeq = Sim != nullptr ? Sim->Channel0Name : 0;
		Watch.StartFrame = Sim != nullptr ? Sim->Channel0Frame : 0.0f;
		Watch.WindowTime = 0.0;
		Watch.WindowBoneMotion = 0.0f;
		Watch.bChannelAdvanced = false;
		Watch.bWindowOpen = true;
	}
	else
	{
		Watch.WindowTime += DeltaSeconds;
		// Every drawn sample, not only the window's ends: a looping walk can be back on its first pose at the end.
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Watch.WindowBoneMotion = FMath::Max(Watch.WindowBoneMotion, static_cast<float>(FVector::Dist(Bones[Index].GetLocation(), Watch.Start[Index])));
		}
		if (Sim != nullptr)
		{
			Watch.bChannelAdvanced |= Sim->Channel0Name != Watch.StartSeq || Sim->Channel0Frame != Watch.StartFrame;
		}

		if (Watch.WindowTime >= WindowSeconds)
		{
			Watch.bWindowOpen = false;
			const float Speed = static_cast<float>(FVector::Dist2D(Location, Watch.StartLocation) / Watch.WindowTime);
			const float Motion = Watch.WindowBoneMotion;
			Watch.MaxBoneMotion = FMath::Max(Watch.MaxBoneMotion, Motion);
			if (Speed >= MovingSpeed && Speed < ScctPlayInspectorLocal::PlacedSpeed)
			{
				++Watch.MovingWindows;
				const FString StanceText = Sim != nullptr ? Stance(*Sim) : FString(TEXT("no simulation record"));
				const FString Channel = Sim != nullptr ? FString::Printf(TEXT("channel 0 %s frame %.2f"), *NameOf(Sim->Channel0Name), Sim->Channel0Frame) : FString(TEXT("channel 0 unknown"));
				if (Motion >= BoneMotion)
				{
					++Watch.AnimatedWindows;
				}
				else
				{
					Note(EScctInspectArea::Animation, FString::Printf(TEXT("frozen %s"), *Watch.Label),
						FString::Printf(TEXT("%s moves at %.0f uu/s (%s) but no drawn bone moved more than %.2f uu in %.1f s (%s)"),
							*Watch.Label, Speed, *StanceText, Motion, Watch.WindowTime, *Channel), BoneMotion - Motion);
				}
				if (Sim != nullptr && Sim->Channel0Name == 0)
				{
					Note(EScctInspectArea::Animation, FString::Printf(TEXT("no channel 0 %s"), *Watch.Label),
						FString::Printf(TEXT("%s moves at %.0f uu/s (%s) with nothing on channel 0"), *Watch.Label, Speed, *StanceText));
				}
				else if (Sim != nullptr && !Watch.bChannelAdvanced && Sim->Channel0Frames > 1)
				{
					Note(EScctInspectArea::Animation, FString::Printf(TEXT("stuck channel 0 %s"), *Watch.Label),
						FString::Printf(TEXT("%s moves at %.0f uu/s (%s) while channel 0 stays on %s frame %.3f (rate %.3f)"),
							*Watch.Label, Speed, *StanceText, *NameOf(Sim->Channel0Name), Sim->Channel0Frame, Sim->Channel0Rate));
				}
			}
		}
	}

	if (Sim == nullptr || (Sim->Flags & EScctInspectPawnFlags::Walking) == 0 || Watch.FootBones.Num() == 0)
	{
		Watch.UnsupportedSeconds = 0.0;
		return;
	}
	if ((Sim->Flags & EScctInspectPawnFlags::JustLanded) != 0)
	{
		// The drawn body still blends out of the landing: not a grounded sample.
		++Watch.LandingSamples;
		return;
	}

	const FTransform ToWorld = Body->GetComponentTransform();
	float Lowest = TNumericLimits<float>::Max();
	FName LowestBone;
	for (const int32 Bone : Watch.FootBones)
	{
		if (Bones.IsValidIndex(Bone))
		{
			const float Z = static_cast<float>(ToWorld.TransformPosition(Bones[Bone].GetLocation()).Z);
			if (Z < Lowest)
			{
				Lowest = Z;
				LowestBone = Body->GetBoneName(Bone);
			}
		}
	}
	if (!LowestBone.IsNone())
	{
		CheckFeet(Watch, Proxy, *Sim, Lowest, LowestBone, DeltaSeconds);
	}
}

void FScctPlayInspector::CheckFeet(FPoseWatch& Watch, const AScctPawnProxy& Proxy, const FScctInspectPawn& Sim, float Lowest, FName Bone, float DeltaSeconds)
{
	const FVector Start = ScctPlayInspectorLocal::DrawnFeetOrigin(Proxy);
	const float Height = Sim.CollisionHeight;
	FVector Hit;
	if (!TraceWorld(Start, Start - FVector(0.0, 0.0, Height + FloorSearch), FVector::ZeroVector, &Hit))
	{
		// A cylinder can stand with its centre over an edge: the movement's own short support sweep says whether it stands.
		float NormalZ = 0.0f;
		if (TraceWorld(Start, Start - FVector(0.0, 0.0, 8.0), FVector(Sim.CollisionRadius, Sim.CollisionRadius, Height), nullptr, &NormalZ) && NormalZ >= 0.7f)
		{
			++Watch.CylinderSupportSamples;
			Watch.UnsupportedSeconds = 0.0;
			return;
		}
		Watch.UnsupportedSeconds += DeltaSeconds;
		if (Watch.UnsupportedSeconds > ScctPlayInspectorLocal::UnsupportedGrace)
		{
			Note(EScctInspectArea::Feet, FString::Printf(TEXT("no support %s"), *Watch.Label),
				FString::Printf(TEXT("walking %s has no floor under its collision shape (at %s)"), *Watch.Label, *Start.ToCompactString()));
		}
		return;
	}

	const float Floor = static_cast<float>(Hit.Z);
	const float Bottom = static_cast<float>(Start.Z) - Height;
	if (FMath::Abs(Bottom - Floor) > FloorSearch * 0.5f)
	{
		// On a stair the centre ray finds the step below while the cylinder rests on the step's edge: the support
		// sweep says whether it stands, as above. A foot is not measured against either step then.
		float NormalZ = 0.0f;
		if (TraceWorld(Start, Start - FVector(0.0, 0.0, 8.0), FVector(Sim.CollisionRadius, Sim.CollisionRadius, Height), nullptr, &NormalZ) && NormalZ >= 0.7f)
		{
			++Watch.CylinderSupportSamples;
			Watch.UnsupportedSeconds = 0.0;
			return;
		}
		Watch.UnsupportedSeconds += DeltaSeconds;
		if (Watch.UnsupportedSeconds > ScctPlayInspectorLocal::UnsupportedGrace)
		{
			Note(EScctInspectArea::Feet, FString::Printf(TEXT("collision off floor %s"), *Watch.Label),
				FString::Printf(TEXT("walking %s: its collision bottom is %.1f uu from the floor under it"), *Watch.Label, Bottom - Floor), FMath::Abs(Bottom - Floor));
		}
		return;
	}

	Watch.UnsupportedSeconds = 0.0;
	const float Offset = Lowest - Floor;
	++Watch.FootSamples;
	Watch.MinFoot = FMath::Min(Watch.MinFoot, Offset);
	Watch.MaxFoot = FMath::Max(Watch.MaxFoot, Offset);
	const FString StanceText = Stance(Sim);
	if (Offset < -FeetBelow)
	{
		Note(EScctInspectArea::Feet, FString::Printf(TEXT("under floor %s %s"), *Watch.Label, *StanceText),
			FString::Printf(TEXT("%s (%s, channel 0 %s frame %.2f): the lowest drawn foot bone (%s) is %.1f uu under the floor (pawn at %.1f, collision height %.1f, body origin %.1f with pre-pivot %.1f, floor %.1f)"),
				*Watch.Label, *StanceText, *NameOf(Sim.Channel0Name), Sim.Channel0Frame, *Bone.ToString(), -Offset, Start.Z, Height, Proxy.GetActorLocation().Z, Proxy.GetPawn().PrePivotZ, Floor), -Offset);
	}
	else if (Offset > FeetAbove)
	{
		Note(EScctInspectArea::Feet, FString::Printf(TEXT("over floor %s %s"), *Watch.Label, *StanceText),
			FString::Printf(TEXT("%s (%s, channel 0 %s): the lowest drawn foot bone (%s) floats %.1f uu over the floor (pawn at %.1f, collision height %.1f, floor %.1f)"),
				*Watch.Label, *StanceText, *NameOf(Sim.Channel0Name), *Bone.ToString(), Offset, Start.Z, Height, Floor), Offset);
	}
}

void FScctPlayInspector::HandlePostActorTick(UWorld* TickedWorld, ELevelTick TickType, float DeltaSeconds)
{
	// After the world's actors and its camera managers have updated, before the frame is drawn: the camera the player
	// is about to see, with the cut the controller raised for it still set.
	if (TickedWorld == nullptr || TickedWorld != World.Get() || TickType == LEVELTICK_PauseTick)
	{
		return;
	}
	const UScctSessionSubsystem* Watched = Session.Get();
	if (Watched == nullptr || Watched->GetSessionId() != SessionId)
	{
		return;
	}
	CheckCamera(DeltaSeconds);
}

void FScctPlayInspector::CheckCamera(float DeltaSeconds)
{
	UWorld* Watched = World.Get();
	const AScctPlayerController* Player = Watched != nullptr ? Cast<AScctPlayerController>(Watched->GetFirstPlayerController()) : nullptr;
	const APlayerCameraManager* Camera = Player != nullptr ? Player->PlayerCameraManager.Get() : nullptr;
	if (Camera == nullptr || Player->GetLookView().Valid == 0)
	{
		bHadCamera = false;
		return;
	}

	const FScctLookView& Look = Player->GetLookView();
	const FVector Location = Camera->GetCameraCacheView().Location;
	const bool bCut = Camera->bGameCameraCutThisFrame;
	++CameraFrames;
	CutFrames += bCut ? 1 : 0;
	if (bHadCamera)
	{
		const float Move = static_cast<float>(FVector::Dist(Location, LastCamera));
		const float Allowed = FMath::Max(CutDistance, CutSpeed * FMath::Min(DeltaSeconds, CutFrameSeconds));
		if (!bCut && Move > Allowed)
		{
			Note(EScctInspectArea::Camera, TEXT("jump without cut"),
				FString::Printf(TEXT("the camera jumped %.0f uu in one frame (%.1f ms; %.0f allowed) from %s to %s and no camera cut was raised: motion blur and anti-aliasing smear across it"),
					Move, DeltaSeconds * 1000.0f, Allowed, *LastCamera.ToCompactString(), *Location.ToCompactString()), Move);
		}
		else if (!bCut)
		{
			LargestCameraMove = FMath::Max(LargestCameraMove, Move);
		}
	}
	LastCamera = Location;
	bHadCamera = true;

	if (Look.Occluded != 0 || Api == nullptr)
	{
		// No clear place for the eye: the original covers the picture that frame, as the controller does.
		return;
	}
	const TCHAR* Mode = Look.SpyFirstPerson != 0 ? TEXT("spy first person") : Side == 1 ? TEXT("merc first person") : TEXT("third person");
	const float At[3] = { static_cast<float>(Location.X), static_cast<float>(Location.Y), static_cast<float>(Location.Z) };
	if (Api->PointInSolid(At, 4.0f) == 1)
	{
		Note(EScctInspectArea::Camera, FString::Printf(TEXT("in solid %s"), Mode),
			FString::Printf(TEXT("the %s camera at %s is inside the level's geometry"), Mode, *Location.ToCompactString()));
		return;
	}
	if (!TraceWorld(Location, Location - FVector(0.0, 0.0, 20000.0), FVector::ZeroVector))
	{
		Note(EScctInspectArea::Camera, FString::Printf(TEXT("outside %s"), Mode),
			FString::Printf(TEXT("the %s camera at %s has no floor under it: it is outside the level"), Mode, *Location.ToCompactString()));
	}
}

void FScctPlayInspector::CheckHud()
{
	const UWorld* Watched = World.Get();
	const UScctHudSubsystem* Hud = Watched != nullptr ? Watched->GetSubsystem<UScctHudSubsystem>() : nullptr;
	++HudFrames;
	if (Hud == nullptr || !Hud->GetModel().bHasState)
	{
		LastHudState = Hud == nullptr ? TEXT("no HUD subsystem") : TEXT("the HUD has no state");
		return;
	}
	const FScctHudModel& Model = Hud->GetModel();
	const FScctHudState& State = Model.State;
	const bool bFilled = Side == 1
		? State.Side == 1 && State.Health > 0 && (State.ClipAmmo > 0 || State.Ammo > 0)
		: State.Side == 0 && State.Health > 0 && Model.Gadgets.Num() > 0;
	HudFilledFrames += bFilled ? 1 : 0;
	LastHudState = FString::Printf(TEXT("side %d health %d clip %d ammo %d gadgets %d"), State.Side, State.Health, State.ClipAmmo, State.Ammo, Model.Gadgets.Num());
}

void FScctPlayInspector::Finish()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;

	const int32 Voices = GetVoicesStarted() - FMath::Max(VoicesAtStart, 0);
	if (SoundEvents == 0)
	{
		Note(EScctInspectArea::Audio, TEXT("no sound events"),
			FString::Printf(TEXT("gameplay raised no sound event on the client in %lld ticks (footsteps, gear)"), ClientTicks));
	}
	else if (SoundsStarted == 0)
	{
		Note(EScctInspectArea::Audio, TEXT("no sound handed over"),
			FString::Printf(TEXT("none of the %lld sound events resolved to a file for Unreal (%lld unknown)"), SoundEvents, SoundsUnknown));
	}
	if (Voices <= 0)
	{
		Note(EScctInspectArea::Audio, TEXT("no voice"),
			FString::Printf(TEXT("the audio subsystem started no voice in %.0f s (%lld sound events, %lld handed over)"), Seconds, SoundEvents, SoundsStarted));
	}

	const double Allowed = FMath::Max(static_cast<double>(CorrectionsPer10s), CorrectionsPer10s * Seconds / 10.0);
	if (CorrectionsApplied > Allowed)
	{
		Note(EScctInspectArea::Net, TEXT("corrections"),
			FString::Printf(TEXT("the client applied %lld server corrections to the local player in %.0f s (%lld received, %.0f allowed, %.1f s moving)"),
				CorrectionsApplied, Seconds, CorrectionsReceived, Allowed, MovingTicks / 60.0));
	}
	if (Snaps > 0)
	{
		Note(EScctInspectArea::Net, TEXT("snap"),
			FString::Printf(TEXT("the local pawn jumped beyond what its velocity explains in %d client ticks (worst %.0f uu over)"), Snaps, WorstSnap), WorstSnap);
	}

	if (HudFilledFrames == 0)
	{
		Note(EScctInspectArea::Hud, TEXT("not filled"),
			FString::Printf(TEXT("in %d frames the HUD never showed the %s's figures (%s); last: %s"), HudFrames, Side == 1 ? TEXT("merc") : TEXT("spy"),
				Side == 1 ? TEXT("health and ammunition") : TEXT("health and gadgets"), *LastHudState));
	}

	int32 Bodies = 0;
	int32 FootSamples = 0;
	for (const TPair<int32, FPoseWatch>& Pair : Poses)
	{
		const FPoseWatch& Watch = Pair.Value;
		Bodies += Watch.bSeen ? 1 : 0;
		FootSamples += Watch.FootSamples;
		if (Watch.MovingWindows > 0 && Watch.AnimatedWindows == 0)
		{
			Note(EScctInspectArea::Animation, FString::Printf(TEXT("never animated %s"), *Watch.Label),
				FString::Printf(TEXT("%s moved in %d windows and was never drawn animating (largest bone motion %.2f uu)"), *Watch.Label, Watch.MovingWindows, Watch.MaxBoneMotion));
		}
	}
	if (Bodies == 0)
	{
		Note(EScctInspectArea::Animation, TEXT("no bodies"), TEXT("no pawn was drawn with a posed character model"));
	}
	if (FootSamples == 0)
	{
		Note(EScctInspectArea::Feet, TEXT("never measured"), TEXT("no walking pawn's feet could be measured against the floor"));
	}
	if (CameraFrames == 0)
	{
		Note(EScctInspectArea::Camera, TEXT("no camera"), TEXT("the player never had a view to check"));
	}
}

TArray<FString> FScctPlayInspector::AreaSummaries() const
{
	int32 Bodies = 0, MovingWindows = 0, AnimatedWindows = 0, FootSamples = 0, Landing = 0, Edge = 0;
	float BoneMax = 0.0f, MinFoot = TNumericLimits<float>::Max(), MaxFoot = TNumericLimits<float>::Lowest();
	for (const TPair<int32, FPoseWatch>& Pair : Poses)
	{
		const FPoseWatch& Watch = Pair.Value;
		Bodies += Watch.bSeen ? 1 : 0;
		MovingWindows += Watch.MovingWindows;
		AnimatedWindows += Watch.AnimatedWindows;
		BoneMax = FMath::Max(BoneMax, Watch.MaxBoneMotion);
		FootSamples += Watch.FootSamples;
		Landing += Watch.LandingSamples;
		Edge += Watch.CylinderSupportSamples;
		if (Watch.FootSamples > 0)
		{
			MinFoot = FMath::Min(MinFoot, Watch.MinFoot);
			MaxFoot = FMath::Max(MaxFoot, Watch.MaxFoot);
		}
	}

	TArray<FString> Figures;
	Figures.SetNum(static_cast<int32>(EScctInspectArea::Count));
	Figures[static_cast<int32>(EScctInspectArea::Animation)] = FString::Printf(TEXT("bodies %d, moving windows %d, animated %d, largest bone motion %.1f uu"), Bodies, MovingWindows, AnimatedWindows, BoneMax);
	Figures[static_cast<int32>(EScctInspectArea::Feet)] = FootSamples > 0
		? FString::Printf(TEXT("foot samples %d (landing %d and edge-supported %d left out), lowest foot bone %.1f..%.1f uu over the floor"), FootSamples, Landing, Edge, MinFoot, MaxFoot)
		: FString(TEXT("no foot samples"));
	Figures[static_cast<int32>(EScctInspectArea::Camera)] = FString::Printf(TEXT("frames %d, cut frames %d, largest uncut move %.1f uu"), CameraFrames, CutFrames, LargestCameraMove);
	Figures[static_cast<int32>(EScctInspectArea::Audio)] = FString::Printf(TEXT("sound events %lld, handed over %lld, unknown %lld, voices started %d"), SoundEvents, SoundsStarted, SoundsUnknown, GetVoicesStarted() - FMath::Max(VoicesAtStart, 0));
	Figures[static_cast<int32>(EScctInspectArea::Net)] = FString::Printf(TEXT("client ticks %lld, corrections applied %lld received %lld, snaps %d (worst %.1f uu)"), ClientTicks, CorrectionsApplied, CorrectionsReceived, Snaps, WorstSnap);
	Figures[static_cast<int32>(EScctInspectArea::Hud)] = FString::Printf(TEXT("frames with the side's figures %d of %d (last: %s)"), HudFilledFrames, HudFrames, *LastHudState);

	TArray<FString> Lines;
	for (int32 AreaIndex = 0; AreaIndex < static_cast<int32>(EScctInspectArea::Count); ++AreaIndex)
	{
		const EScctInspectArea Area = static_cast<EScctInspectArea>(AreaIndex);
		int32 Found = 0;
		for (const FScctInspectIssue& Issue : Issues)
		{
			Found += Issue.Area == Area ? 1 : 0;
		}
		const FString& Extra = AreaNotes[AreaIndex].IsEmpty() ? Figures[AreaIndex] : AreaNotes[AreaIndex];
		Lines.Add(FString::Printf(TEXT("%s: %s%s%s"), AreaName(Area), Found == 0 ? TEXT("OK") : *FString::Printf(TEXT("%d issue%s"), Found, Found == 1 ? TEXT("") : TEXT("s")),
			Extra.IsEmpty() ? TEXT("") : TEXT(" ("), Extra.IsEmpty() ? TEXT("") : *(Extra + TEXT(")"))));
	}
	return Lines;
}
