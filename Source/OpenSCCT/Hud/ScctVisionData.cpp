#include "Hud/ScctVisionData.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Runtime/ScctFeatures.h"

CSV_DEFINE_CATEGORY(ScctVision, false);

namespace
{
	/** Reads a table's array: asks for the count with the buffer as it is, grows it when the count is larger and asks again. */
	template <typename TRow, typename TRead>
	void ReadRows(TArray<TRow>& Rows, TRead Read)
	{
		if (Rows.Num() < 64)
		{
			Rows.SetNumUninitialized(64);
		}
		int32 Count = Read(Rows.GetData(), Rows.Num());
		if (Count > Rows.Num())
		{
			Rows.SetNumUninitialized(Count + 32);
			Count = Read(Rows.GetData(), Rows.Num());
		}
		Rows.SetNum(FMath::Clamp(Count, 0, Rows.Num()), EAllowShrinking::No);
	}

	FString WorldName(int32 Id)
	{
		const FScctWorldApi* World = ScctFeature<FScctWorldApi>();
		if (Id == 0 || World == nullptr)
		{
			return FString();
		}
		TCHAR Buffer[256];
		World->GetName(Id, Buffer, UE_ARRAY_COUNT(Buffer));
		return Buffer;
	}

	FString PawnFlagText(int32 Flags)
	{
		FString Text;
		auto Add = [&Text, Flags](int32 Bit, const TCHAR* Name)
		{
			if ((Flags & Bit) != 0)
			{
				Text += Text.IsEmpty() ? Name : *(FString(TEXT(" ")) + Name);
			}
		};
		Add(EScctVisionPawnFlags::Merc, TEXT("merc"));
		Add(EScctVisionPawnFlags::EmfOn, TEXT("emfOn"));
		Add(EScctVisionPawnFlags::EmfShows, TEXT("emfShows"));
		Add(EScctVisionPawnFlags::WeaponDrawn, TEXT("drawn"));
		Add(EScctVisionPawnFlags::Moving, TEXT("moving"));
		Add(EScctVisionPawnFlags::Tracked, TEXT("tracked"));
		Add(EScctVisionPawnFlags::ShockImmune, TEXT("shockImmune"));
		Add(EScctVisionPawnFlags::ShockStateProtected, TEXT("shockByState"));
		Add(EScctVisionPawnFlags::StickySkinned, TEXT("sticky"));
		Add(EScctVisionPawnFlags::Wet, TEXT("wet"));
		Add(EScctVisionPawnFlags::Hidden, TEXT("hidden"));
		return Text;
	}
}

void FScctVisionData::Bind()
{
	if (Api != nullptr)
	{
		return;
	}

	// A failure is asked again each frame (the module keeps its answer, so that is cheap) but said once.
	FString Error;
	Api = ScctFeature<FScctVisionApi>(&Error);
	if (Api == nullptr)
	{
		if (!bTriedBind)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Vision: no per-actor vision values, the bridge's vision table is missing: %s"), *Error);
		}
		bTriedBind = true;
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Vision: api v%d bound (pawn %d, actor %d, lock %d bytes)"), Api->Version,
		static_cast<int32>(sizeof(FScctVisionPawn)), static_cast<int32>(sizeof(FScctVisionActor)), static_cast<int32>(sizeof(FScctVisionLock)));

	float Seconds = 6.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctVisionDump="), Seconds) || FParse::Param(FCommandLine::Get(), TEXT("ScctVisionDump")))
	{
		DumpAt = FMath::Max(0.0f, Seconds);
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: every vision row is logged %.1f s into the match (-ScctVisionDump)"), DumpAt);
	}
}

bool FScctVisionData::Refresh(float DeltaSeconds, bool bReadActors)
{
	CSV_SCOPED_TIMING_STAT(ScctVision, Refresh);
	Bind();
	if (Api == nullptr)
	{
		return false;
	}

	ReadRows(Pawns, [this](FScctVisionPawn* Rows, int32 Capacity) { return Api->GetPawns(Rows, Capacity); });
	bHasLevel = Api->GetLevel(&Level) != 0;

	PawnIndex.Reset();
	for (int32 Index = 0; Index < Pawns.Num(); ++Index)
	{
		PawnIndex.Add(Pawns[Index].ActorId, Index);
	}
	if (!Pawns.IsEmpty())
	{
		MatchSeconds += DeltaSeconds;
	}
	const bool bDumpNow = DumpAt >= 0.0f && !bDumped && !Pawns.IsEmpty() && MatchSeconds >= DumpAt;

	if (bReadActors || bDumpNow)
	{
		CSV_SCOPED_TIMING_STAT(ScctVision, ActorRows);
		ReadRows(Actors, [this](FScctVisionActor* Rows, int32 Capacity) { return Api->GetActors(Rows, Capacity); });
		ActorIndex.Reset();
		MapActorIndex.Reset();
		for (int32 Index = 0; Index < Actors.Num(); ++Index)
		{
			ActorIndex.Add(Actors[Index].ActorId, Index);
			if (Actors[Index].NameId != 0)
			{
				MapActorIndex.Add(Actors[Index].NameId, Index);
			}
		}
	}
	else if (!Actors.IsEmpty())
	{
		Actors.Reset();
		ActorIndex.Reset();
		MapActorIndex.Reset();
	}

	if (bDumpNow)
	{
		bDumped = true;
		Dump();
	}
	return Pawns.Num() > 0 || bHasLevel;
}

void FScctVisionData::Reset()
{
	Pawns.Reset();
	Actors.Reset();
	Locks.Reset();
	PawnIndex.Reset();
	ActorIndex.Reset();
	MapActorIndex.Reset();
	Level = {};
	bHasLevel = false;
	MatchSeconds = 0.0f;
	bDumped = false;
}

const FScctVisionPawn* FScctVisionData::FindPawn(int32 ActorId) const
{
	const int32* Index = PawnIndex.Find(ActorId);
	return Index != nullptr ? &Pawns[*Index] : nullptr;
}

const FScctVisionActor* FScctVisionData::FindActor(int32 ActorId) const
{
	const int32* Index = ActorIndex.Find(ActorId);
	return Index != nullptr ? &Actors[*Index] : nullptr;
}

const FScctVisionActor* FScctVisionData::FindMapActor(int32 NameId) const
{
	const int32* Index = MapActorIndex.Find(NameId);
	return Index != nullptr ? &Actors[*Index] : nullptr;
}

TConstArrayView<FScctVisionLock> FScctVisionData::ReadMotionLocks(const FScctVisionCamera& Camera)
{
	if (Api == nullptr)
	{
		Locks.Reset();
		return Locks;
	}
	ReadRows(Locks, [this, &Camera](FScctVisionLock* Rows, int32 Capacity) { return Api->GetMotionLocks(&Camera, Rows, Capacity); });
	return Locks;
}

void FScctVisionData::Dump() const
{
	UE_LOG(LogOpenSCCT, Log, TEXT("Vision: %d pawns, %d actors with vision values; MTVSlope %d, MTVOffset %d, level flags %d, %d locks"),
		Pawns.Num(), Actors.Num(), Level.MtvSlope, Level.MtvOffset, Level.Flags, Level.LockCount);
	for (const FScctVisionPawn& Pawn : Pawns)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision:   pawn #%d heat %.2f/%.2f emf %.2f/%.2f glow %.2f shock %.2f (%.1f s) cloak %.2f speed %.0f [%s]"),
			Pawn.ActorId, Pawn.HeatIntensity, Pawn.HeatOpacity, Pawn.EMFIntensity, Pawn.EMFOpacity, Pawn.EmfGlow,
			Pawn.ShockFraction, Pawn.ShockSeconds, Pawn.Cloak, Pawn.Speed, *PawnFlagText(Pawn.Flags));
	}
	for (const FScctVisionActor& Actor : Actors)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision:   actor #%d %s '%s' owner #%d heat %.2f/%.2f emf %.2f/%.2f glow %.2f flags %d"),
			Actor.ActorId, *WorldName(Actor.ClassId), *WorldName(Actor.NameId), Actor.OwnerPawnId, Actor.HeatIntensity,
			Actor.HeatOpacity, Actor.EMFIntensity, Actor.EMFOpacity, Actor.EmfGlow, Actor.Flags);
	}
}
