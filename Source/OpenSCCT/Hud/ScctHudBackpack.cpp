#include "Hud/ScctHudSubsystem.h"
#include "Hud/ScctHudTextures.h"
#include "OpenSCCT.h"
#include "Runtime/ScctHudBackpackApi.h"

// The HUD subsystem's part for the merc backpack panels: the bridge's backpack table (the own share and both mates'
// while the merc holds the backpack, the share a mate carries while the refill entry is focused) into the model, a
// log line whenever the panels change and with each snapshot line, and the timed commands an unattended check uses to
// give and select the backpack and to stand a mate in front of the player. The painting is in
// ScctHudElementsBackpack.cpp.

namespace
{
	/** Three panels of four rows at most. */
	constexpr int32 MaxBackpackRows = 12;

	FString DescribeRows(const FScctHudModel& M)
	{
		FString Rows;
		for (const FScctHudBackpackRow& Row : M.BackpackRows)
		{
			Rows += FString::Printf(TEXT(" [p%d r%d id%d %d/%d %s.%s (%d,%d)]"), Row.Panel, Row.Row, Row.GadgetId, Row.Charges, Row.Max,
				Row.Package, Row.Texture, Row.U0, Row.V0);
		}
		return Rows;
	}
}

void UScctHudSubsystem::InitBackpack()
{
	// Nothing for the player to set: the panels follow the backpack and the refill entry, as in the original.
}

void UScctHudSubsystem::ReadBackpack()
{
	FScctHudModel& M = *Model;
	M.Backpack = FScctHudBackpackState{};
	M.BackpackRows.Reset();
	const FScctHudBackpackApi* Backpack = ScctFeature<FScctHudBackpackApi>();
	if (Backpack == nullptr)
	{
		if (!bBackpackTableMissingLogged && M.bHasState)
		{
			bBackpackTableMissingLogged = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHud: the bridge has no backpack table; the merc backpack panels are not shown"));
		}
		return;
	}

	M.BackpackRows.SetNumUninitialized(MaxBackpackRows);
	const int32 Count = Backpack->GetBackpack(&M.Backpack, M.BackpackRows.GetData(), MaxBackpackRows);
	M.BackpackRows.SetNum(FMath::Clamp(Count, 0, MaxBackpackRows));

	// A line each time what the panels show changes: which panels, then every row's gadget and charges.
	uint32 Signature = HashCombine(GetTypeHash(M.Backpack.Mode), GetTypeHash(M.Backpack.Panels));
	Signature = HashCombine(Signature, HashCombine(GetTypeHash(M.Backpack.MateANameId), GetTypeHash(M.Backpack.MateBNameId)));
	for (const FScctHudBackpackRow& Row : M.BackpackRows)
	{
		Signature = HashCombine(Signature, HashCombine(GetTypeHash(Row.GadgetId), GetTypeHash(Row.Charges)));
	}
	if (Signature != LastBackpackSignature)
	{
		LastBackpackSignature = Signature;
		static const TCHAR* const Modes[] = { TEXT("hidden"), TEXT("own backpack"), TEXT("refill share") };
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: backpack %s (mode %d, panels %d, %d rows a panel, mates '%s' '%s')%s"),
			Modes[FMath::Clamp(M.Backpack.Mode, 0, 2)], M.Backpack.Mode, M.Backpack.Panels, M.Backpack.Rows,
			*M.Text(M.Backpack.MateANameId), *M.Text(M.Backpack.MateBNameId), *DescribeRows(M));

		// Once the panels first show, say which icon textures the content lacks (those rows show the charges alone).
		for (const FScctHudBackpackRow& Row : M.BackpackRows)
		{
			if (Row.Texture[0] != 0 && UScctHudTextures::Find(Row.Package, Row.Texture) == nullptr)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHud: the content has no %s.%s; the backpack row of gadget %d shows no icon"), Row.Package, Row.Texture, Row.GadgetId);
			}
		}
	}
}

bool UScctHudSubsystem::RunBackpackCommand(const FString& Name, int32 Argument)
{
	const FScctHudBackpackApi* Backpack = ScctFeature<FScctHudBackpackApi>();
	if (Name.Equals(TEXT("GiveBackpack"), ESearchCase::IgnoreCase))
	{
		const bool bQueued = Backpack != nullptr && Backpack->GiveBackpacks(Argument >= 2 ? 1 : 0) != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: GiveBackpack %d %s"), Argument, bQueued ? TEXT("queued on the server") : TEXT("refused (remote round, no pawn or no backpack table)"));
		return true;
	}
	if (Name.Equals(TEXT("Backpack"), ESearchCase::IgnoreCase))
	{
		const bool bSelected = Argument != 0 && Backpack != nullptr && Backpack->SelectBackpack() != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: Backpack %s"), bSelected ? TEXT("selected") : TEXT("not selected (no backpack, no pawn or no backpack table)"));
		return true;
	}
	if (Name.Equals(TEXT("Refill"), ESearchCase::IgnoreCase))
	{
		const bool bQueued = Backpack != nullptr && Backpack->ForceRefill(static_cast<float>(FMath::Max(Argument, 1))) != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: Refill %d s %s"), Argument, bQueued ? TEXT("queued on the server") : TEXT("refused (remote round, no pawn or no backpack table)"));
		return true;
	}
	return false;
}

void UScctHudSubsystem::LogBackpack()
{
	const FScctHudModel& M = *Model;
	if (M.Backpack.Mode == 0)
	{
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("HUD backpack: mode %d panels %d rows %d labels '%s'/'%s' mates '%s' '%s' |%s"),
		M.Backpack.Mode, M.Backpack.Panels, M.Backpack.Rows, *M.Text(M.Backpack.SelfLabelId), *M.Text(M.Backpack.MateLabelId),
		*M.Text(M.Backpack.MateANameId), *M.Text(M.Backpack.MateBNameId), *DescribeRows(M));
}
