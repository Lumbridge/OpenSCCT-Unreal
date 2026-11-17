#include "Hud/ScctHudModel.h"

#include "Hud/ScctHudStyle.h"

namespace
{
	struct FLabelSource
	{
		const TCHAR* File;
		const TCHAR* Section;
		const TCHAR* Key;
	};

	// Where each label is in the original's interface strings (the content's shared/ui/strings.json), in the
	// order of EScctHudLabel.
	const FLabelSource LabelSources[] = {
		{ TEXT("GUI"), TEXT("View_GameStatus"), TEXT("Spies.Caption") },
		{ TEXT("GUI"), TEXT("View_GameStatus"), TEXT("Mercenaries.Caption") },
		{ TEXT("GUI"), TEXT("View_GameStatus"), TEXT("TheSpiesList.ColumnHeadings[1]") },
		{ TEXT("GUI"), TEXT("View_GameStatus"), TEXT("TheMercsList.ColumnHeadings[1]") },
		{ TEXT("Engine"), TEXT("PlayerReplicationInfo"), TEXT("sDeath") },
		{ TEXT("GUI"), TEXT("View_GameStatus"), TEXT("TheSpiesList.ColumnHeadings[2]") },
		{ TEXT("GUI"), TEXT("Browser_Servers"), TEXT("ColumnHeadings[1]") },
		{ TEXT("MagmaDynamic"), TEXT("Magma"), TEXT("Inifite_Short") },
		{ TEXT("GUI"), TEXT("View_GameStatus"), TEXT("TitlePage") },
		{ TEXT("GUI"), TEXT("AlternateModeSettings"), TEXT("NightVision.Caption") },
		{ TEXT("GUI"), TEXT("AlternateModeSettings"), TEXT("ThermalVision.Caption") },
		{ TEXT("GUI"), TEXT("AlternateModeSettings"), TEXT("MotionTracking.Caption") },
		{ TEXT("GUI"), TEXT("AlternateModeSettings"), TEXT("EMFVision.Caption") },
		{ TEXT("Magma"), TEXT("A_YouWin"), TEXT("Text1") },
		{ TEXT("Magma"), TEXT("A_YouLose"), TEXT("Text1") },
		{ TEXT("Magma"), TEXT("A_Finish"), TEXT("Text1") },
		{ TEXT("Magma"), TEXT("A_Spectator"), TEXT("textSpectator") },
		{ TEXT("Magma"), TEXT("A_Spectator"), TEXT("textWatching") },
		{ TEXT("Magma"), TEXT("A_Spectator"), TEXT("textRespawnInTime") },
		{ TEXT("Magma"), TEXT("A_Spectator"), TEXT("textNoRespawnLeft") },
	};
	static_assert(UE_ARRAY_COUNT(LabelSources) == static_cast<int32>(EScctHudLabel::Count), "One source per label");

	const FString EmptyText;

	void AddLine(TArray<FScctHudLine>& Lines, FScctHudLine&& Line, int32 MaxLines)
	{
		Lines.Add(MoveTemp(Line));
		while (Lines.Num() > MaxLines)
		{
			Lines.RemoveAt(0);
		}
	}

	void AgeLines(TArray<FScctHudLine>& Lines, float DeltaSeconds)
	{
		for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
		{
			Lines[Index].Age += DeltaSeconds;
			if (Lines[Index].Age >= Lines[Index].Life)
			{
				Lines.RemoveAt(Index);
			}
		}
	}
}

void FScctHudModel::Reset(const FScctHudApi* InApi)
{
	*this = FScctHudModel();
	Api = InApi;
	LoadLabels();
}

void FScctHudModel::LoadLabels()
{
	if (Api == nullptr)
	{
		return;
	}

	TCHAR Buffer[128];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(LabelSources); ++Index)
	{
		const FLabelSource& Source = LabelSources[Index];
		const int32 Length = Api->Localize(Source.File, Source.Section, Source.Key, Buffer, UE_ARRAY_COUNT(Buffer));
		FString Label = Length > 0 ? FString(FMath::Min(Length, static_cast<int32>(UE_ARRAY_COUNT(Buffer)) - 1), Buffer) : FString();
		// Some of the original's labels are written for a "Label: value" line.
		Label.TrimStartAndEndInline();
		Label.RemoveFromEnd(TEXT(":"));
		Labels[Index] = MoveTemp(Label);
	}
}

const FString& FScctHudModel::Text(int32 Id) const
{
	if (Id == 0 || Api == nullptr)
	{
		return EmptyText;
	}
	if (const FString* Known = Texts.Find(Id))
	{
		return *Known;
	}

	// GetText returns the whole length; a text longer than the first buffer is read again into one that fits.
	TCHAR Small[256];
	const int32 Length = Api->GetText(Id, Small, UE_ARRAY_COUNT(Small));
	if (Length < static_cast<int32>(UE_ARRAY_COUNT(Small)))
	{
		return Texts.Add(Id, FString(FMath::Max(Length, 0), Small));
	}

	TArray<TCHAR> Large;
	Large.SetNumZeroed(Length + 1);
	Api->GetText(Id, Large.GetData(), Large.Num());
	return Texts.Add(Id, FString(Length, Large.GetData()));
}

const FString& FScctHudModel::VisionName(int32 Mode) const
{
	switch (Mode)
	{
	case 34: return Label(EScctHudLabel::NightVision);
	case 36: return Label(EScctHudLabel::ThermalVision);
	case 37: return Label(EScctHudLabel::MotionTracking);
	case 38: return Label(EScctHudLabel::EmfVision);
	default: return EmptyText;
	}
}

void FScctHudModel::AddMessage(const FScctHudMessage& Message)
{
	FScctHudLine Line;
	Line.Text = FString(Message.Text).TrimStartAndEnd();
	Line.Category = Message.Category;
	Line.Kind = Message.Kind;
	Line.Cause = Message.Cause;
	Line.Flags = Message.Flags;

	if (Message.Category == EScctHudMessageCategory::Activity)
	{
		if ((Message.Flags & EScctHudMessageFlags::ClearsFeed) != 0)
		{
			Feed.Reset();
		}
		Line.Actor = Text(Message.ActorNameId);
		Line.Victim = Text(Message.VictimNameId);
		Line.ActorTeam = Message.ActorTeam;
		Line.VictimTeam = Message.VictimTeam;
		Line.Life = FMath::Clamp(Message.Duration, 6.0f, 9.0f);
		if (!Line.Text.IsEmpty())
		{
			AddLine(Feed, MoveTemp(Line), 6);
		}
		return;
	}

	// A line about a place that has no name starts with the original's bare separator (" : Objectives locked").
	while (Line.Text.RemoveFromStart(TEXT(":")))
	{
		Line.Text.TrimStartInline();
	}

	// The original's blank spacer lines carry no text.
	if (Line.Text.IsEmpty())
	{
		return;
	}

	switch (Message.Category)
	{
	case EScctHudMessageCategory::Game:
	case EScctHudMessageCategory::System:
		Line.Life = FMath::Clamp(Message.Duration, 4.0f, 7.0f);
		AddLine(Headlines, MoveTemp(Line), 3);
		break;
	case EScctHudMessageCategory::Reticle:
		// Shown under the crosshair from the snapshot (ReticleMessageId) for as long as the original shows it.
		break;
	case EScctHudMessageCategory::Chat:
	case EScctHudMessageCategory::TeamChat:
		// The chat panel (Hud/ScctChatSubsystem) shows players' lines, with their history; the log would repeat them.
		break;
	default:
		Line.Life = FMath::Clamp(Message.Duration, 3.5f, 7.0f);
		AddLine(Log, MoveTemp(Line), 5);
		break;
	}
}

void FScctHudModel::Advance(float DeltaSeconds)
{
	using namespace ScctHudStyle;

	Clock += DeltaSeconds;
	AgeLines(Feed, DeltaSeconds);
	AgeLines(Headlines, DeltaSeconds);
	AgeLines(Log, DeltaSeconds);

	// Health: the bar eases, and a drop leaves a short red trace.
	if (LastHealth >= 0 && State.Health < LastHealth)
	{
		HealthLoss = 1.0f;
	}
	if (LastHealth < 0)
	{
		HealthShown = static_cast<float>(State.Health);
		EnergyShown = State.EnergyLevel;
	}
	LastHealth = State.Health;
	HealthShown = Ease(HealthShown, static_cast<float>(State.Health), DeltaSeconds, 9.0f);
	EnergyShown = Ease(EnergyShown, State.EnergyLevel, DeltaSeconds, 9.0f);
	HealthLoss = FMath::Max(HealthLoss - DeltaSeconds / 0.8f, 0.0f);

	if (State.DamageCount != LastDamageCount)
	{
		LastDamageCount = State.DamageCount;
		DamagePulse = 1.0f;
	}
	DamagePulse = FMath::Max(DamagePulse - DeltaSeconds / 0.7f, 0.0f);

	HitConfirm = (State.Flags & EScctHudFlags::ReticleHit) != 0 ? 1.0f : FMath::Max(HitConfirm - DeltaSeconds / 0.25f, 0.0f);

	const float Range = State.MaxDispersion - State.MinDispersion;
	const float SpreadNow = Range > KINDA_SMALL_NUMBER ? FMath::Clamp((State.Accuracy - State.MinDispersion) / Range, 0.0f, 1.0f) : 0.0f;
	Spread = Ease(Spread, SpreadNow, DeltaSeconds, 18.0f);

	// Ammunition: the fullest clip seen is the clip's size, until another weapon is held.
	if (State.WeaponNameId != LastWeaponNameId)
	{
		LastWeaponNameId = State.WeaponNameId;
		ClipCapacity = 0;
		LastClip = State.ClipAmmo;
	}
	ClipCapacity = FMath::Max(ClipCapacity, State.ClipAmmo);
	if (State.ClipAmmo != LastClip)
	{
		LastClip = State.ClipAmmo;
		AmmoPulse = 1.0f;
	}
	AmmoPulse = FMath::Max(AmmoPulse - DeltaSeconds / 0.25f, 0.0f);

	// The gas mask's gauge is full at the most air it has held.
	GasMaskCapacity = FMath::Max(GasMaskCapacity, State.GasMaskAirSeconds);

	if (State.SelectedGadget != LastSelectedGadget)
	{
		LastSelectedGadget = State.SelectedGadget;
		GadgetPulse = 1.0f;
	}
	GadgetPulse = FMath::Max(GadgetPulse - DeltaSeconds / 0.35f, 0.0f);

	// The prompt keeps its last text while it fades out.
	const int32 PromptId = State.UsePromptId != 0 ? State.UsePromptId : (State.SpeakPrompt != 0 ? State.SpeakPromptId : 0);
	const bool bPrompt = bHasState && PromptId != 0 && !Text(PromptId).IsEmpty();
	if (bPrompt)
	{
		PromptText = Text(PromptId);
	}
	PromptShown = Ease(PromptShown, bPrompt ? 1.0f : 0.0f, DeltaSeconds, 14.0f);

	const bool bVision = bHasState && IsVisionActive(State.VisionMode);
	if (bVision)
	{
		VisionMode = State.VisionMode;
	}
	VisionShown = Ease(VisionShown, bVision ? 1.0f : 0.0f, DeltaSeconds, 10.0f);

	ScoreboardShown = Ease(ScoreboardShown, bScoreboardHeld ? 1.0f : 0.0f, DeltaSeconds, 16.0f);

	if (PreviewWinner >= 0)
	{
		Round.Ended = 1;
		Round.WinningTeam = PreviewWinner;
	}
	const bool bEnded = bHasRound && Round.Ended != 0;
	EndAge = bEnded ? FMath::Max(EndAge, 0.0f) + DeltaSeconds : -1.0f;
}
