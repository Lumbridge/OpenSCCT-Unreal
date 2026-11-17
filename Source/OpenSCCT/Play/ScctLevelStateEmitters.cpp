// The map's placed Emitters that ride another actor (the Godot client's Views/AttachedEffects.cs for emitters): a
// mover's rider, or one hung on a bone of a skeletal prop (ActorsToAttach: the Club's falling fuse boxes, the Missile
// base's doors, the Orphanage's generator) or of a pawn. The importer placed each Emitter's particle actors in the
// Emitter's placed frame; here they keep that place in the Emitter's frame as it moves, so new particles come from the
// moving part (and a particle system simulated in its own space carries its live particles with it).

#include "Animation/SkeletalMeshActor.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraComponent.h"
#include "OpenSCCT.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** How often -ScctLevelLog says where a moving riding emitter is. */
	constexpr float RidingEmitterLogSeconds = 2.0f;
}

void UScctLevelStateSubsystem::UpdateAttachedEmitters()
{
	if (EmitterApi == nullptr)
	{
		if (bEmitterApiTried)
		{
			return;
		}
		bEmitterApiTried = true;
		FString Error;
		EmitterApi = ScctFeature<FScctWorldEmitterApi>(&Error);
		if (EmitterApi == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Level state: map emitters on movers and prop bones stay where they were imported, the bridge's attached-emitter table is missing: %s"), *Error);
			return;
		}
	}

	if (EmitterBuffer.Num() == 0)
	{
		EmitterBuffer.SetNumZeroed(16);
	}
	int32 Total = EmitterApi->GetAttachedEmitters(EmitterBuffer.GetData(), EmitterBuffer.Num());
	if (Total > EmitterBuffer.Num())
	{
		EmitterBuffer.SetNumZeroed(Total + 8);
		Total = EmitterApi->GetAttachedEmitters(EmitterBuffer.GetData(), EmitterBuffer.Num());
	}
	const int32 Count = FMath::Clamp(Total, 0, EmitterBuffer.Num());

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctWorldEmitter& Record = EmitterBuffer[Index];
		if (Record.NameId <= 0)
		{
			continue;
		}
		const FString& Name = GetName(Record.NameId);
		FRidingEmitter& State = RidingEmitters.FindOrAdd(Record.NameId);
		const FTransform Placed(FScctWorld::ToRotator(Record.PlacedPitch, Record.PlacedYaw, Record.PlacedRoll), FVector(Record.PlacedX, Record.PlacedY, Record.PlacedZ));
		if (!State.bBound)
		{
			// Its particle actors (one per sub-emitter the importer drew), and each one's place in the Emitter's frame.
			State.bBound = true;
			const FName Tag = FScctWorld::LevelTag(*Name);
			for (auto It = TaggedEmitters.CreateConstKeyIterator(Tag); It; ++It)
			{
				const UNiagaraComponent* Component = It.Value().Component.Get();
				AActor* Owner = Component != nullptr ? Component->GetOwner() : nullptr;
				if (Owner == nullptr || State.Parts.ContainsByPredicate([Owner](const FRidingEmitter::FPart& Part) { return Part.Actor.Get() == Owner; }))
				{
					continue;
				}
				FRidingEmitter::FPart& Part = State.Parts.AddDefaulted_GetRef();
				Part.Actor = Owner;
				Part.Original = Owner->GetActorTransform();
				FTransform Unscaled = Part.Original;
				Unscaled.SetScale3D(FVector::OneVector);
				Part.InEmitter = Unscaled.GetRelativeTransform(Placed);
				if (USceneComponent* Root = Owner->GetRootComponent(); Root != nullptr && Root->Mobility != EComponentMobility::Movable)
				{
					Root->SetMobility(EComponentMobility::Movable);
				}
			}
			const TCHAR* Kind = (Record.Flags & EScctWorldEmitterFlags::OnBone) != 0 ? TEXT("bone")
				: (Record.Flags & EScctWorldEmitterFlags::OnPawn) != 0 ? TEXT("pawn bone") : TEXT("mover");
			const FString On = (Record.Flags & (EScctWorldEmitterFlags::OnBone | EScctWorldEmitterFlags::OnPawn)) != 0
				? FString::Printf(TEXT("'%s' of %s"), *GetName(Record.AttachBoneId),
					(Record.Flags & EScctWorldEmitterFlags::OnPawn) != 0 ? *FString::Printf(TEXT("pawn #%d"), Record.AttachPawnId) : *FString::Printf(TEXT("'%s'"), *GetName(Record.AttachNameId)))
				: FString::Printf(TEXT("'%s'"), Record.AttachNameId != 0 ? *GetName(Record.AttachNameId) : TEXT("(an actor the table does not follow)"));
			UE_LOG(LogOpenSCCT, Log, TEXT("Level state: emitter '%s' rides %s %s with %d particle actors%s"), *Name, Kind, *On, State.Parts.Num(),
				State.Parts.Num() == 0 ? TEXT(" (the level has none for it: mesh emitters are not drawn)") : TEXT(""));
		}
		if (State.Parts.Num() == 0)
		{
			continue;
		}

		// The Emitter's frame now.
		FTransform Frame;
		const FTransform Relative(FScctWorld::ToRotator(Record.Pitch, Record.Yaw, Record.Roll), FVector(Record.X, Record.Y, Record.Z));
		if ((Record.Flags & (EScctWorldEmitterFlags::OnBone | EScctWorldEmitterFlags::OnPawn)) != 0)
		{
			FTransform BoneNow;
			if ((Record.Flags & EScctWorldEmitterFlags::OnPawn) != 0)
			{
				const AScctPawnProxy* Proxy = Session != nullptr ? Session->FindProxy(Record.AttachPawnId) : nullptr;
				const FName Bone = Proxy != nullptr && Proxy->HasModel() ? Proxy->FindMeshBoneName(GetName(Record.AttachBoneId)) : NAME_None;
				if (Bone.IsNone())
				{
					continue; // its pawn is not drawn (yet): it stays where it was
				}
				BoneNow = Proxy->GetBodyMesh()->GetSocketTransform(Bone);
			}
			else
			{
				const FPropState* Prop = PropStates.Find(Record.AttachNameId);
				const ASkeletalMeshActor* PropActor = Prop != nullptr ? Prop->Actor.Get() : nullptr;
				const USkeletalMeshComponent* Component = PropActor != nullptr ? PropActor->GetSkeletalMeshComponent() : nullptr;
				const int32 Bone = Component != nullptr ? Component->GetBoneIndex(FName(*GetName(Record.AttachBoneId))) : INDEX_NONE;
				if (Bone == INDEX_NONE)
				{
					const FString Key = Name + TEXT("@") + GetName(Record.AttachBoneId);
					if (!PropsReported.Contains(Key))
					{
						PropsReported.Add(Key);
						UE_LOG(LogOpenSCCT, Warning, TEXT("Level state: emitter '%s' hangs on bone '%s' of '%s', which the level %s; it stays where it was imported"),
							*Name, *GetName(Record.AttachBoneId), *GetName(Record.AttachNameId), Component == nullptr ? TEXT("has no skeletal prop for") : TEXT("prop's imported mesh does not have"));
					}
					continue;
				}
				BoneNow = Component->GetBoneTransform(Bone);
			}
			BoneNow.SetScale3D(FVector::OneVector);
			if (!State.bBoneSeen)
			{
				State.bBoneSeen = true;
				State.FirstBoneQuat = BoneNow.GetRotation();
			}
			// AttachedEffects.BonePlace: on the bone, moved by RelativeLocation in its frame. The imported bone's axes
			// need not be the original's, so the Emitter keeps its placed turn and turns as far as the bone has
			// turned since it was first seen (both are the same when the map's attachment has no relative turn).
			Frame.SetLocation(BoneNow.TransformPosition(Relative.GetLocation()));
			Frame.SetRotation(BoneNow.GetRotation() * State.FirstBoneQuat.Inverse() * Placed.GetRotation() * Relative.GetRotation());
		}
		else
		{
			Frame = Relative; // a mover's rider: where the simulation carried it
		}

		bool bApplied = false;
		for (FRidingEmitter::FPart& Part : State.Parts)
		{
			AActor* Actor = Part.Actor.Get();
			if (Actor == nullptr)
			{
				continue;
			}
			FTransform Wanted = Part.InEmitter * Frame;
			Wanted.SetScale3D(Part.Original.GetScale3D());
			if (!Wanted.Equals(Actor->GetActorTransform(), 0.01))
			{
				Actor->SetActorTransform(Wanted);
				bApplied = true;
			}
		}

		const FVector Location = Frame.GetLocation();
		if (!State.bMoved)
		{
			State.bMoved = true;
			State.FirstLocation = State.LastLogged = Location;
			State.LastLogSeconds = MatchSeconds;
		}
		State.MaxTravel = FMath::Max(State.MaxTravel, static_cast<float>(FVector::Dist(Location, State.FirstLocation)));
		if (bLog && bApplied && MatchSeconds - State.LastLogSeconds >= RidingEmitterLogSeconds && !Location.Equals(State.LastLogged, 1.0))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %.1f s emitter '%s' is carried to (%.0f, %.0f, %.0f), %.0f cm from where it was %.1f s ago"), MatchSeconds, *Name,
				Location.X, Location.Y, Location.Z, FVector::Dist(Location, State.LastLogged), MatchSeconds - State.LastLogSeconds);
			State.LastLogged = Location;
			State.LastLogSeconds = MatchSeconds;
		}
	}
}

void UScctLevelStateSubsystem::RestoreAttachedEmitters(bool bTearingDown)
{
	if (bTearingDown)
	{
		return;
	}
	for (const TPair<int32, FRidingEmitter>& Pair : RidingEmitters)
	{
		for (const FRidingEmitter::FPart& Part : Pair.Value.Parts)
		{
			if (AActor* Actor = Part.Actor.Get())
			{
				Actor->SetActorTransform(Part.Original);
			}
		}
	}
}

void UScctLevelStateSubsystem::LogAttachedEmitters()
{
	if (RidingEmitters.Num() == 0)
	{
		return;
	}
	TArray<FString> Lines;
	for (const TPair<int32, FRidingEmitter>& Pair : RidingEmitters)
	{
		Lines.Add(FString::Printf(TEXT("%s %.0f cm (%d actors)"), *GetName(Pair.Key), Pair.Value.MaxTravel, Pair.Value.Parts.Num()));
	}
	Lines.Sort();
	UE_LOG(LogOpenSCCT, Log, TEXT("Level state: %d map emitters rode a mover or a bone, carried up to: %s"), RidingEmitters.Num(), *FString::Join(Lines, TEXT(", ")));
}
