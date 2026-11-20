#include "Hud/ScctVisionObjects.h"

#include "Animation/SkeletalMeshActor.h"
#include "Components/MeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Hud/ScctVisionData.h"
#include "Hud/ScctVisionSubsystem.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "OpenSCCT.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Play/ScctPawnProxy.h"

namespace
{
	const TCHAR* const NameTagPrefix = TEXT("Scct:");
	const TCHAR* const HeatTag = TEXT("ScctHeat:");
	const TCHAR* const HeatOpacityTag = TEXT("ScctHeatOpacity:");
	const TCHAR* const EmfTag = TEXT("ScctEmf:");
	const TCHAR* const EmfOpacityTag = TEXT("ScctEmfOpacity:");
	const FName EmfOnTag(TEXT("ScctEmfOn"));
	const FName TintParameter(TEXT("Tint"));

	/** The heat a mesh must have before thermal marks it: below this the room's own heat (from brightness) is as warm. */
	constexpr float ThermalMarkHeat = 0.2f;

	/** The importer's tag value after Prefix ("ScctHeat:0.500"), or false when the tag is not that one. */
	bool ReadTag(const FString& Tag, const TCHAR* Prefix, float& Value)
	{
		if (!Tag.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			return false;
		}
		Value = FCString::Atof(*Tag + FCString::Strlen(Prefix));
		return true;
	}

	/** Draws a primitive as wanted; the engine calls do nothing when nothing changes. */
	void ApplyPrimitive(UPrimitiveComponent* Primitive, bool bMainPass, bool bDepthPass, bool bCustomDepth, int32 Stencil)
	{
		if (Primitive->bRenderInMainPass != bMainPass)
		{
			Primitive->SetRenderInMainPass(bMainPass);
		}
		if (Primitive->bRenderInDepthPass != bDepthPass)
		{
			Primitive->SetRenderInDepthPass(bDepthPass);
		}
		if (Primitive->bRenderCustomDepth != bCustomDepth)
		{
			Primitive->SetRenderCustomDepth(bCustomDepth);
		}
		if (Primitive->CustomDepthStencilValue != Stencil)
		{
			Primitive->SetCustomDepthStencilValue(Stencil);
		}
	}
}

void FScctVisionObjects::IndexLevel(UWorld* World)
{
	IndexedWorld = World;
	Tagged.Reset();
	Emitters.Reset();
	int32 NumEmitterActors = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (ANiagaraActor* Particles = Cast<ANiagaraActor>(Actor))
		{
			// The sprite emitters of one map Emitter share its name tag.
			for (const FName& Tag : Actor->Tags)
			{
				const FString Text = Tag.ToString();
				UNiagaraComponent* Component = Particles->GetNiagaraComponent();
				if (Component != nullptr && Text.StartsWith(NameTagPrefix, ESearchCase::CaseSensitive))
				{
					Emitters.Add(Text.RightChop(FCString::Strlen(NameTagPrefix)), Component);
					++NumEmitterActors;
					break;
				}
			}
			continue;
		}
		if (!Actor->IsA<AStaticMeshActor>() && !Actor->IsA<ASkeletalMeshActor>())
		{
			continue;
		}
		FTagged Entry;
		bool bHasVision = false;
		for (const FName& Tag : Actor->Tags)
		{
			const FString Text = Tag.ToString();
			if (ReadTag(Text, HeatTag, Entry.Heat) || ReadTag(Text, HeatOpacityTag, Entry.HeatOpacity)
				|| ReadTag(Text, EmfTag, Entry.Emf) || ReadTag(Text, EmfOpacityTag, Entry.EmfOpacity))
			{
				bHasVision = true;
			}
			else if (Tag == EmfOnTag)
			{
				Entry.bEmfOn = true;
			}
		}
		if (bHasVision)
		{
			Entry.Actor = Actor;
			Tagged.Add(Entry);
		}
	}

	int32 NumHot = 0;
	int32 NumElectric = 0;
	int32 NumThin = 0;
	for (const FTagged& Entry : Tagged)
	{
		NumHot += Entry.Heat >= ThermalMarkHeat ? 1 : 0;
		NumElectric += Entry.bEmfOn && Entry.Emf > 0.0f ? 1 : 0;
		NumThin += Entry.HeatOpacity < 0.99f || Entry.EmfOpacity < 0.99f ? 1 : 0;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Vision: %d level meshes carry the importer's vision tags (%d hot enough for thermal, %d running electronics, %d see-through or hidden in a mode), %d map emitter actors"),
		Tagged.Num(), NumHot, NumElectric, NumThin, NumEmitterActors);
	// With -ScctVisionDump, each of them with where it stands, so a camera can be put in front of one.
	if (FParse::Param(FCommandLine::Get(), TEXT("ScctVisionDump")) || FCString::Strifind(FCommandLine::Get(), TEXT("ScctVisionDump=")) != nullptr)
	{
		for (const FTagged& Entry : Tagged)
		{
			const AActor* Actor = Entry.Actor.Get();
			const FVector At = Actor->GetActorLocation();
			UE_LOG(LogOpenSCCT, Log, TEXT("Vision:   tagged %s at (%.0f, %.0f, %.0f): heat %.2f/%.2f emf %.2f/%.2f%s"), Actor->Tags.Num() > 0 ? *Actor->Tags[0].ToString() : *Actor->GetName(),
				At.X, At.Y, At.Z, Entry.Heat, Entry.HeatOpacity, Entry.Emf, Entry.EmfOpacity, Entry.bEmfOn ? TEXT(" on") : TEXT(""));
		}
	}
}

bool FScctVisionObjects::Want(AActor* Actor, UPrimitiveComponent* Single, bool bThermal, float Intensity, float Opacity, bool bMine)
{
	FWanted Mark;
	if (Opacity <= 0.01f)
	{
		// Not drawn at all in this mode, as the original discards it.
		Mark.bMainPass = false;
		Mark.bCustomDepth = false;
		Mark.Stencil = 0;
	}
	else if (Opacity < 0.99f)
	{
		// Only in the custom depth: the material lays it over what is behind it by its opacity.
		Mark.bMainPass = false;
		Mark.bCustomDepth = true;
		Mark.Stencil = UScctVisionSubsystem::PawnStencil(Intensity, Opacity);
	}
	else if (bThermal && bMine && Intensity > 0.0f)
	{
		// A mine is a small heat source. Use its own stencil band so a sharp,
		// visible casing can be resolved independently of the large soft pawn silhouettes.
		Mark.bCustomDepth = true;
		Mark.Stencil = 32 + FMath::RoundToInt(FMath::Clamp(Intensity, 0.0f, 1.0f) * 15.0f);
	}
	else if (bThermal ? Intensity >= ThermalMarkHeat : Intensity > 0.01f)
	{
		Mark.bMainPass = true;
		Mark.bCustomDepth = true;
		Mark.Stencil = UScctVisionSubsystem::PawnStencil(Intensity, Opacity);
	}
	else
	{
		return false;
	}

	int32 Added = 0;
	auto Add = [this, &Mark, &Added](UPrimitiveComponent* Primitive)
	{
		// What hangs on a pawn is the pawn marking's (the body and what it carries go together).
		if (Primitive == nullptr || Cast<AScctPawnProxy>(Primitive->GetAttachmentRootActor()) != nullptr)
		{
			return;
		}
		Wanted.Add(Primitive, Mark);
		++Added;
	};
	if (Single != nullptr)
	{
		Add(Single);
	}
	else if (Actor != nullptr)
	{
		Actor->ForEachComponent<UMeshComponent>(false, [&Add](UMeshComponent* Mesh) { Add(Mesh); });
	}
	if (Added == 0)
	{
		return false;
	}
	NumHidden += Opacity <= 0.01f ? 1 : 0;
	NumSeeThrough += Opacity > 0.01f && Opacity < 0.99f ? 1 : 0;
	return true;
}

void FScctVisionObjects::Update(UWorld* World, int32 Mode, const FScctVisionData& Data)
{
	const bool bThermal = Mode == 36;
	if (World == nullptr || (Mode != 36 && Mode != 38))
	{
		RestoreAll();
		return;
	}
	if (IndexedWorld.Get() != World)
	{
		RestoreAll();
		IndexLevel(World);
	}

	UScctLevelStateSubsystem* Level = World->GetSubsystem<UScctLevelStateSubsystem>();
	const UScctEffectsSubsystem* Effects = World->GetSubsystem<UScctEffectsSubsystem>();

	Wanted.Reset();
	LiveActors.Reset();
	NumHidden = 0;
	NumSeeThrough = 0;
	TMap<UNiagaraComponent*, float> WantedEmitters;
	int32 NumLive = 0;
	int32 NumMissing = 0;

	// The simulation's live values first: they win over the importer's tags for the same actor.
	for (const FScctVisionActor& Row : Data.GetActors())
	{
		if ((Row.Flags & EScctVisionActorFlags::Hidden) != 0)
		{
			continue;
		}
		const float Intensity = bThermal ? Row.HeatIntensity : Row.EmfGlow;
		const float Opacity = bThermal ? Row.HeatOpacity : Row.EMFOpacity;

		AActor* Actor = nullptr;
		UPrimitiveComponent* Single = nullptr;
		bool bEmitter = false;
		// A copy: the level state's name list can grow (and move) on the next GetName, the class name's in the log.
		FString Name;
		if (Row.NameId != 0 && Level != nullptr)
		{
			Name = Level->GetName(Row.NameId);
			Actor = Level->FindLevelActor(Name);
			// A map Emitter's particles keep HeatOpacity of their strength in thermal.
			TArray<TWeakObjectPtr<UNiagaraComponent>, TInlineAllocator<4>> Found;
			Emitters.MultiFind(Name, Found);
			bEmitter = Found.Num() > 0;
			if (bThermal && Opacity < 0.99f)
			{
				for (const TWeakObjectPtr<UNiagaraComponent>& Component : Found)
				{
					if (Component.IsValid())
					{
						WantedEmitters.Add(Component.Get(), Opacity);
					}
				}
			}
		}
		if (Actor == nullptr && Effects != nullptr)
		{
			// Spawned in play: a mine, a sticky cam, a grenade, drawn among the effect actors.
			Single = Effects->FindActorMesh(Row.ActorId);
		}

		if (Actor == nullptr && Single == nullptr)
		{
			// The spy's pistol is the pawn marking's and an emitter has no mesh; what is spawned in play without a mesh
			// of the effects (a beam the markers draw, a laser trigger, a shocker dart in flight) is only counted. A
			// placed actor with nothing drawn is said once.
			if (!bEmitter && Row.OwnerPawnId == 0)
			{
				++NumMissing;
				if (Row.NameId != 0 && !ReportedMissing.Contains(Row.ActorId))
				{
					ReportedMissing.Add(Row.ActorId);
					UE_LOG(LogOpenSCCT, Log, TEXT("Vision:   actor #%d %s '%s' has vision values but nothing drawn for it"),
						Row.ActorId, Level != nullptr ? *Level->GetName(Row.ClassId) : TEXT(""), *Name);
				}
			}
			continue;
		}
		if (Actor != nullptr)
		{
			LiveActors.Add(Actor);
		}
		if (Want(Actor, Single, bThermal, Intensity, Opacity, (Row.Flags & EScctVisionActorFlags::Mine) != 0))
		{
			++NumLive;
			if (!Listed.Contains(Row.ActorId))
			{
				Listed.Add(Row.ActorId);
				UE_LOG(LogOpenSCCT, Log, TEXT("Vision:   %s marks #%d %s '%s' at (%.0f, %.0f, %.0f): level %.2f, opacity %.2f"),
					bThermal ? TEXT("thermal") : TEXT("EMF"), Row.ActorId, Level != nullptr ? *Level->GetName(Row.ClassId) : TEXT(""),
					*Name, Row.X, Row.Y, Row.Z, Intensity, Opacity);
			}
		}
	}

	// The importer's values for the level meshes the simulation does not list.
	int32 NumByTags = 0;
	for (const FTagged& Entry : Tagged)
	{
		AActor* Actor = Entry.Actor.Get();
		if (Actor == nullptr || LiveActors.Contains(Actor) || Actor->IsHidden())
		{
			continue;
		}
		const float Intensity = bThermal ? Entry.Heat : (Entry.bEmfOn ? Entry.Emf : 0.0f);
		const float Opacity = bThermal ? Entry.HeatOpacity : Entry.EmfOpacity;
		NumByTags += Want(Actor, nullptr, bThermal, Intensity, Opacity) ? 1 : 0;
	}

	// Apply, keeping how each was drawn before; give back what is no longer marked.
	for (const TPair<UPrimitiveComponent*, FWanted>& Pair : Wanted)
	{
		UPrimitiveComponent* Primitive = Pair.Key;
		const TWeakObjectPtr<UPrimitiveComponent> Key(Primitive);
		if (!Marked.Contains(Key))
		{
			FOriginal Original;
			Original.bMainPass = Primitive->bRenderInMainPass;
			Original.bDepthPass = Primitive->bRenderInDepthPass;
			Original.bCustomDepth = Primitive->bRenderCustomDepth;
			Original.Stencil = Primitive->CustomDepthStencilValue;
			Marked.Add(Key, Original);
		}
		const FWanted& Mark = Pair.Value;
		// Out of the depth pass too, or a hidden mesh still stands in the depth as a black cut-out.
		ApplyPrimitive(Primitive, Mark.bMainPass, Mark.bMainPass, Mark.bCustomDepth, Mark.Stencil);
	}
	for (auto It = Marked.CreateIterator(); It; ++It)
	{
		UPrimitiveComponent* Primitive = It.Key().Get();
		if (Primitive == nullptr)
		{
			It.RemoveCurrent();
			continue;
		}
		if (!Wanted.Contains(Primitive))
		{
			const FOriginal& Original = It.Value();
			ApplyPrimitive(Primitive, Original.bMainPass, Original.bDepthPass, Original.bCustomDepth, Original.Stencil);
			It.RemoveCurrent();
		}
	}

	for (const TPair<UNiagaraComponent*, float>& Pair : WantedEmitters)
	{
		UNiagaraComponent* Component = Pair.Key;
		const TWeakObjectPtr<UNiagaraComponent> Key(Component);
		FEmitterOriginal* Original = Thinned.Find(Key);
		if (Original == nullptr)
		{
			FEmitterOriginal Entry;
			const FNiagaraVariable Tint(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Tint"));
			Entry.Tint = Component->GetOverrideParameters().GetParameterValueOrDefault<FLinearColor>(Tint, FLinearColor::White);
			Entry.bVisible = Component->IsVisible();
			Original = &Thinned.Add(Key, Entry);
		}
		if (Original->Applied != Pair.Value)
		{
			// Every channel, alpha included: the sprite system multiplies the whole particle colour by Tint, so an
			// alpha-blended sprite thins and an additive one dims. Again whenever the simulation's opacity moves.
			Original->Applied = Pair.Value;
			Component->SetVariableLinearColor(TintParameter, Original->Tint * Pair.Value);
		}
		const bool bVisible = Original->bVisible && Pair.Value > 0.01f;
		if (Component->IsVisible() != bVisible)
		{
			Component->SetVisibility(bVisible);
		}
	}
	for (auto It = Thinned.CreateIterator(); It; ++It)
	{
		UNiagaraComponent* Component = It.Key().Get();
		if (Component == nullptr)
		{
			It.RemoveCurrent();
			continue;
		}
		if (!WantedEmitters.Contains(Component))
		{
			Component->SetVariableLinearColor(TintParameter, It.Value().Tint);
			Component->SetVisibility(It.Value().bVisible);
			It.RemoveCurrent();
		}
	}

	// What the goggles do with the level and the gadgets, in the log when it changes and every few seconds.
	uint32 Summary = HashCombine(GetTypeHash(Mode), GetTypeHash(Tagged.Num()));
	Summary = HashCombine(Summary, GetTypeHash(NumLive));
	Summary = HashCombine(Summary, GetTypeHash(NumByTags));
	Summary = HashCombine(Summary, GetTypeHash(NumHidden * 1000 + NumSeeThrough));
	Summary = HashCombine(Summary, GetTypeHash(Thinned.Num()));
	const double Now = FPlatformTime::Seconds();
	if (static_cast<int32>(Summary) != LastSummary || Now - LastLog > 3.0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: %s: %d tagged level actors, %d live objects marked (%d by tags, %d hidden, %d see-through, %d emitters thinned, %d rows with nothing drawn, %d primitives)"),
			bThermal ? TEXT("thermal") : TEXT("EMF"), Tagged.Num(), NumLive, NumByTags, NumHidden, NumSeeThrough, Thinned.Num(), NumMissing, Marked.Num());
		LastSummary = static_cast<int32>(Summary);
		LastLog = Now;
	}
}

void FScctVisionObjects::RestoreAll()
{
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FOriginal>& Pair : Marked)
	{
		if (UPrimitiveComponent* Primitive = Pair.Key.Get())
		{
			ApplyPrimitive(Primitive, Pair.Value.bMainPass, Pair.Value.bDepthPass, Pair.Value.bCustomDepth, Pair.Value.Stencil);
		}
	}
	Marked.Reset();
	for (const TPair<TWeakObjectPtr<UNiagaraComponent>, FEmitterOriginal>& Pair : Thinned)
	{
		if (UNiagaraComponent* Component = Pair.Key.Get())
		{
			Component->SetVariableLinearColor(TintParameter, Pair.Value.Tint);
			Component->SetVisibility(Pair.Value.bVisible);
		}
	}
	Thinned.Reset();
	LastSummary = -1;
}

void FScctVisionObjects::Reset()
{
	RestoreAll();
	IndexedWorld.Reset();
	Tagged.Reset();
	Emitters.Reset();
	Listed.Reset();
	ReportedMissing.Reset();
}
