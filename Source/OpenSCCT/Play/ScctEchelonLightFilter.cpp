#include "Play/ScctEchelonLightFilter.h"

#include "Algo/Includes.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Light.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Online/ScctMapFolders.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Play/ScctLightRules.h"
#include "Runtime/ScctWorldApi.h"
#include "SceneViewExtension.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace ScctEchelon
{
	using FObject = TSharedPtr<FJsonObject>;
	constexpr int32 SpotShadowEffect = 22;
	const TCHAR* const FilterMaterial = TEXT("/Game/OpenSCCT/Effects/M_ScctEchelonReceiverFilter.M_ScctEchelonReceiverFilter");

	class FReceiverView final : public FWorldSceneViewExtension
	{
	public:
		FReceiverView(const FAutoRegister& AutoRegister, UScctEchelonLightFilter* Filter)
			: FWorldSceneViewExtension(AutoRegister, Filter->GetWorld()), Owner(Filter) {}
		void SetupViewFamily(FSceneViewFamily&) override {}
		void BeginRenderViewFamily(FSceneViewFamily&) override {}
		void SetupView(FSceneViewFamily& Family, FSceneView& View) override
		{
			constexpr int32 ReceiverViewFlag = 1 << 30; // build_echelon_light_filter.py
			View.FinalPostProcessSettings.UserFlags &= ~ReceiverViewFlag;
			auto* Filter = Owner.Get();
			auto* Viewport = Filter ? Filter->GetWorld()->GetGameViewport() : nullptr;
			if (Viewport && Family.RenderTarget == Viewport->Viewport && !View.bIsSceneCapture &&
				!View.bIsReflectionCapture && !Family.Views.IsEmpty() && Family.Views[0] == &View)
			{
				View.FinalPostProcessSettings.UserFlags |= ReceiverViewFlag;
				Filter->CaptureVisibleProjectors(View);
			}
		}
	private:
		TWeakObjectPtr<UScctEchelonLightFilter> Owner;
	};

	FObject Document(const FString& Path)
	{
		FString Text;
		FObject Object;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		}
		return Object;
	}

	FObject WorldExtras(const FString& Folder)
	{
		// Older cooked level actors retain the original actor tag. Join that tag to the JSON
		// chunk only; world.glb's vertex/index buffers can be hundreds of megabytes.
		TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*(Folder / TEXT("render/world.glb"))));
		uint32 Header[5];
		if (!File || File->TotalSize() < sizeof(Header)) return nullptr;
		File->Serialize(Header, sizeof(Header));
		if (Header[0] != 0x46546C67 || Header[1] != 2 || Header[4] != 0x4E4F534A ||
			Header[3] == 0 || Header[3] > MAX_int32 || Header[3] > File->TotalSize() - int64(sizeof(Header))) return nullptr;
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Header[3]);
		File->Serialize(Bytes.GetData(), Bytes.Num());
		if (File->IsError()) return nullptr;
		FUTF8ToTCHAR Text(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num());
		FObject Root;
		const FObject* Extras;
		const FObject* Svmap;
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Root)
			&& Root && Root->TryGetObjectField(TEXT("extras"), Extras) && (*Extras)->TryGetObjectField(TEXT("svmap"), Svmap)
			? *Svmap : nullptr;
	}

	template<typename F>
	void Each(const FObject& Object, const TCHAR* Field, F Visit)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows;
		if (Object && Object->TryGetArrayField(Field, Rows))
		{
			for (const auto& Entry : *Rows)
			{
				if (Entry && Entry->Type == EJson::Object) Visit(Entry->AsObject());
			}
		}
	}

	bool Flag(const FObject& Object, const TCHAR* Field)
	{
		bool Value = false;
		return Object->TryGetBoolField(Field, Value) && Value;
	}

	struct FRule
	{
		int32 Flags = 0;
		TSet<FName> Named;

		void AddName(FString Path)
		{
			int32 Dot;
			if (Path.FindLastChar('.', Dot)) Path.RightChopInline(Dot + 1);
			if (!Path.IsEmpty()) Named.Add(FName(*Path));
		}
	};

	TMap<FName, FRule> ReadRules(const FString& Folder)
	{
		TMap<FName, FRule> Rules;
		Each(WorldExtras(Folder), TEXT("instances"), [&](const FObject& Instance)
		{
			FString Actor;
			if (!Instance->TryGetStringField(TEXT("actor"), Actor) || Actor.IsEmpty()) return;
			int32 Flags = 0;
			Instance->TryGetNumberField(TEXT("echelonIgnore"), Flags);
			TArray<FString> Names;
			Instance->TryGetStringArrayField(TEXT("ignoredEchelonLights"), Names);
			if ((Flags & 3) == 0 && Names.IsEmpty()) return;
			FRule& Rule = Rules.FindOrAdd(FScctWorld::LevelTag(*Actor));
			Rule.Flags |= Flags & 3;
			for (const FString& Name : Names) Rule.AddName(Name);
		});
		Each(Document(Folder / TEXT("sim/gameplay.json")), TEXT("actors"), [&](const FObject& Actor)
		{
			const FObject* Props;
			FString Name;
			if (!Actor->TryGetObjectField(TEXT("props"), Props) || !Actor->TryGetStringField(TEXT("id"), Name) || Name.IsEmpty()) return;
			int32 Glass = 0;
			(*Props)->TryGetNumberField(TEXT("GlassEffect"), Glass);
			const int32 Flags = Glass != 0 ? 3 : (int32(Flag(*Props, TEXT("bIgnoreEchelonLighting"))) |
				(int32(Flag(*Props, TEXT("bIgnoreDynamicEchelonLighting"))) << 1));
			if (Flags) Rules.FindOrAdd(FScctWorld::LevelTag(*Name)).Flags |= Flags;
			Each(*Props, TEXT("IgnoredEchelonLights"), [&](const FObject& Ref)
			{
				FString Path;
				if (Ref->TryGetStringField(TEXT("$actor"), Path) || Ref->TryGetStringField(TEXT("$asset"), Path))
					Rules.FindOrAdd(FScctWorld::LevelTag(*Name)).AddName(Path);
			});
		});
		return Rules;
	}

	bool IsReceiver(const AActor* Actor)
	{
		return Actor && (Actor->IsA<AStaticMeshActor>() || Actor->IsA<ASkeletalMeshActor>());
	}

	FName ActorTag(const AActor& Actor)
	{
		for (FName Tag : Actor.Tags)
			if (Tag.ToString().StartsWith(TEXT("Scct:"))) return Tag;
		return NAME_None;
	}

	void ReadImportedTags(const AActor& Actor, TMap<FName, FRule>& Rules)
	{
		const FName Key = ActorTag(Actor);
		if (Key.IsNone()) return;
		for (FName Tag : Actor.Tags)
		{
			if (Tag == TEXT("ScctSkipStaticSpot")) Rules.FindOrAdd(Key).Flags |= 1;
			else if (Tag == TEXT("ScctSkipDynamicSpot")) Rules.FindOrAdd(Key).Flags |= 2;
			else
			{
				FString Text = Tag.ToString();
				if (Text.RemoveFromStart(TEXT("ScctSkipSpot:"))) Rules.FindOrAdd(Key).AddName(Text);
			}
		}
	}
}

bool UScctEchelonLightFilter::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UScctEchelonLightFilter::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (auto* Session = Collection.InitializeDependency<UScctSessionSubsystem>())
		Session->OnMatchStopped.AddUObject(this, &UScctEchelonLightFilter::Clear);
}

void UScctEchelonLightFilter::Deinitialize()
{
	if (auto* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>()) Session->OnMatchStopped.RemoveAll(this);
	Clear();
	Super::Deinitialize();
}

void UScctEchelonLightFilter::Clear()
{
	bLightPlanReady = false;
	Uninstall();
}

void UScctEchelonLightFilter::Uninstall()
{
	RestoreRouting();
	ViewExtension.Reset();
	if (SpawnHandle.IsValid()) GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
	SpawnHandle.Reset();
	for (const FLightState& State : Lights)
	{
		if (ULightComponent* Light = State.Light.Get())
		{
			// A triggered light may already have had its pre-match function restored by level state.
			if (Light->LightFunctionMaterial && Light->LightFunctionMaterial->GetBaseMaterial()->GetFName() == TEXT("M_ScctEchelonReceiverFilter"))
				Light->SetLightFunctionMaterial(State.Original.Get());
			Light->bAllowMegaLights = State.bMegaLights;
			Light->MarkRenderStateDirty();
		}
	}
	for (const auto& Capture : Captures)
		if (Capture.IsValid()) Capture->DestroyComponent();
	Lights.Empty();
	Captures.Empty();
	CapturesByReceiver.Empty();
	Pending.Empty();
	bInstalled = false;
}

bool UScctEchelonLightFilter::AddedToUnshapedLight(const ULightComponent* Light) const
{
	return Lights.ContainsByPredicate([Light](const FLightState& State) { return State.Light.Get() == Light && !State.Original; });
}

void UScctEchelonLightFilter::CaptureVisibleProjectors(const FSceneView& View)
{
	TBitArray<> Requested(false, Captures.Num());
	for (const FLightState& State : Lights)
	{
		auto* Spot = Cast<USpotLightComponent>(State.Light.Get());
		if (!Spot || Requested[State.Capture] || !Spot->IsVisible() || Spot->Intensity <= 0 ||
			!View.ViewFrustum.IntersectSphere(Spot->GetComponentLocation(), Spot->AttenuationRadius)) continue;
		if (auto* Capture = Captures[State.Capture].Get())
		{
			Capture->CaptureSceneDeferred();
			Requested[State.Capture] = true;
		}
	}
}

void UScctEchelonLightFilter::QueueActor(AActor* Actor)
{
	// Stand-ins acquire their Scct tag after SpawnActor returns.
	if (!Actor) return;
	if (!NativeReceivers.IsEmpty())
		for (auto* Light : TInlineComponentArray<ULightComponent*>(Actor)) RouteLight(*Light);
	if (ScctEchelon::IsReceiver(Actor)) Pending.Add(Actor);
}

void UScctEchelonLightFilter::RouteLight(ULightComponent& Light)
{
	// Duplicate the ordinary world bit on ordinary lights. Projectors that reject the
	// complete native group keep only their original world bit. Actor lamps keep bit 1.
	if (NativeReceivers.IsEmpty() || !Light.LightingChannels.bChannel0 || Light.LightingChannels.bChannel2 ||
		NativeProjectors.Contains(&Light)) return;
	Light.SetLightingChannels(true, Light.LightingChannels.bChannel1, true);
	MirroredLights.Add(&Light);
}

void UScctEchelonLightFilter::RouteNewWorldLight(ULightComponent& Light)
{
	if (auto* World = Light.GetWorld())
		if (auto* Filter = World->GetSubsystem<UScctEchelonLightFilter>()) Filter->RouteLight(Light);
}

void UScctEchelonLightFilter::RouteReceiverComponent(UPrimitiveComponent& Mesh)
{
	AActor* Owner = Mesh.GetOwner();
	if (!Mesh.LightingChannels.bChannel0 || Mesh.LightingChannels.bChannel2 || !ScctEchelon::IsReceiver(Owner) ||
		!NativeReceivers.Contains(ScctEchelon::ActorTag(*Owner))) return;
	Mesh.SetLightingChannels(false, Mesh.LightingChannels.bChannel1, true);
	RoutedMeshes.Add(&Mesh);
}

void UScctEchelonLightFilter::RestoreRouting()
{
	for (const auto& Weak : RoutedMeshes)
		if (auto* Mesh = Weak.Get(); Mesh && !Mesh->LightingChannels.bChannel0 && Mesh->LightingChannels.bChannel2)
			Mesh->SetLightingChannels(true, Mesh->LightingChannels.bChannel1, false);
	for (const auto& Weak : MirroredLights)
		if (auto* Light = Weak.Get(); Light && Light->LightingChannels.bChannel0 && Light->LightingChannels.bChannel2)
			Light->SetLightingChannels(true, Light->LightingChannels.bChannel1, false);
	RoutedMeshes.Reset();
	MirroredLights.Reset();
	NativeProjectors.Reset();
	NativeReceivers.Reset();
}

UMaterialInterface* UScctEchelonLightFilter::HoldSteady(ULightComponent& Light, FName Parameter, float Value)
{
	FLightState* State = Lights.FindByPredicate([&](const FLightState& Entry) { return Entry.Light.Get() == &Light; });
	UMaterialInterface* Original = State ? State->Original.Get() : Light.LightFunctionMaterial.Get();
	if (!Original) return nullptr;
	auto* Steady = UMaterialInstanceDynamic::Create(Original, &Light);
	Steady->SetScalarParameterValue(Parameter, Value);
	if (State)
	{
		State->Original.Reset(Steady);
		CastChecked<UMaterialInstanceDynamic>(Light.LightFunctionMaterial)->SetScalarParameterValue(Parameter, Value);
	}
	else Light.SetLightFunctionMaterial(Steady);
	return Original;
}

void UScctEchelonLightFilter::AddReceiver(AActor& Actor)
{
	if (!NativeReceivers.IsEmpty())
	{
		for (auto* Light : TInlineComponentArray<ULightComponent*>(&Actor)) RouteLight(*Light);
		if (ScctEchelon::IsReceiver(&Actor) && NativeReceivers.Contains(ScctEchelon::ActorTag(Actor)))
			for (auto* Mesh : TInlineComponentArray<UPrimitiveComponent*>(&Actor)) RouteReceiverComponent(*Mesh);
	}
	for (const FName Tag : Actor.Tags)
	{
		for (auto It = CapturesByReceiver.CreateConstKeyIterator(Tag); It; ++It)
		{
			if (auto* Capture = Captures[It.Value()].Get(); Capture && (Capture->GetOwner()->GetLevel() == Actor.GetLevel() || Actor.HasAnyFlags(RF_Transient)))
				Capture->ShowOnlyActors.AddUnique(&Actor);
		}
	}
}

void UScctEchelonLightFilter::Apply()
{
	static const IConsoleVariable* MegaChannels = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.LightingChannels"));
	const bool bCanSplit = bLightPlanReady && !FScctLightRules::BodyAids() && (!MegaChannels || MegaChannels->GetInt() != 0);
	if (bInstalled && bNativeAllowed != bCanSplit) Uninstall();
	if (!bInstalled)
	{
		const auto* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
		if (!Session || !Session->IsPlaying() || !Session->GetSessionInfo().Map[0] || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
		const FString Folder = ScctMapFolders::FolderOf(Session->GetSessionInfo().Map);
		bNativeAllowed = bCanSplit;
		RegisterMap(Folder);
		bInstalled = true;
	}
	for (const auto& Actor : Pending)
		if (Actor.IsValid()) AddReceiver(*Actor.Get());
	Pending.Reset();
}

void UScctEchelonLightFilter::RegisterMap(const FString& Folder)
{
	using namespace ScctEchelon;
	TMap<FName, bool> Projectors;
	Each(Folder.IsEmpty() ? nullptr : Document(Folder / TEXT("render/lighting.json")), TEXT("lights"), [&](const FObject& Row)
	{
		int32 Effect = 0;
		FString Name;
		if (Row->TryGetNumberField(TEXT("effect"), Effect) && Effect == SpotShadowEffect && !Flag(Row, TEXT("hidden")) &&
			Row->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
			Projectors.Add(FName(*Name), Flag(Row, TEXT("dynamic")));
	});
	TMap<FName, ALight*> Placed;
	for (TActorIterator<ALight> It(GetWorld()); It; ++It)
	{
		const FName Tag = ActorTag(**It);
		if (Tag.IsNone()) continue;
		Placed.Add(Tag, *It);
		if (It->ActorHasTag(TEXT("ScctStaticSpot")) || It->ActorHasTag(TEXT("ScctDynamicSpot")))
			Projectors.Add(FName(*Tag.ToString().RightChop(5)), It->ActorHasTag(TEXT("ScctDynamicSpot")));
	}
	if (Projectors.IsEmpty()) return;
	TMap<FName, FRule> Rules;
	if (!Folder.IsEmpty()) Rules = ReadRules(Folder);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (IsReceiver(*It)) ReadImportedTags(**It, Rules);
	if (Rules.IsEmpty()) return;

	struct FGroup
	{
		TArray<FName> Excluded;
		TArray<USpotLightComponent*> Spots;
	};
	TArray<FGroup> Groups;
	for (const auto& Projector : Projectors)
	{
		ALight* Actor = Placed.FindRef(FScctWorld::LevelTag(*Projector.Key.ToString()));
		auto* Light = Actor ? Cast<USpotLightComponent>(Actor->GetLightComponent()) : nullptr;
		if (!Light) continue;
		TArray<FName> Excluded;
		for (const auto& Rule : Rules)
		{
			if ((Rule.Value.Flags & (Projector.Value ? 2 : 1)) || Rule.Value.Named.Contains(Projector.Key))
				Excluded.Add(Rule.Key);
		}
		if (Excluded.IsEmpty()) continue;
		Excluded.Sort(FNameLexicalLess());
		FGroup* Group = Groups.FindByPredicate([&](const FGroup& G) { return G.Excluded == Excluded; });
		if (!Group)
		{
			Group = &Groups.AddDefaulted_GetRef();
			Group->Excluded = MoveTemp(Excluded);
		}
		Group->Spots.Add(Light);
	}
	if (Groups.IsEmpty()) return;

	// A stable choice on cooked and loose maps. Prefer the group that eliminates the
	// most projector light functions, resolving ties by the sorted source actor names.
	Groups.Sort([](const FGroup& A, const FGroup& B)
	{
		if (A.Spots.Num() != B.Spots.Num()) return A.Spots.Num() > B.Spots.Num();
		const int32 Common = FMath::Min(A.Excluded.Num(), B.Excluded.Num());
		for (int32 I = 0; I < Common; ++I)
			if (A.Excluded[I] != B.Excluded[I]) return A.Excluded[I].LexicalLess(B.Excluded[I]);
		return A.Excluded.Num() < B.Excluded.Num();
	});
	if (bNativeAllowed)
	{
		for (FName Tag : Groups[0].Excluded) NativeReceivers.Add(Tag);
	}

	UMaterial* Master = nullptr;
	for (const FGroup& Group : Groups)
	{
		TArray<FName> Residual = Group.Excluded;
		if (!NativeReceivers.IsEmpty() && Algo::Includes(Group.Excluded, Groups[0].Excluded, FNameLexicalLess()))
		{
			for (auto* Spot : Group.Spots) NativeProjectors.Add(Spot);
			Residual.RemoveAll([&](FName Tag) { return NativeReceivers.Contains(Tag); });
		}
		if (Residual.IsEmpty()) continue;
		if (!Master) Master = LoadObject<UMaterial>(nullptr, FilterMaterial);
		if (!Master)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Missing Echelon receiver light function %s"), FilterMaterial);
			continue;
		}
		AActor* Actor = Group.Spots[0]->GetOwner();
		auto* Capture = NewObject<USceneCaptureComponent2D>(Actor, NAME_None, RF_Transient);
		auto* Depth = NewObject<UTextureRenderTarget2D>(Capture, NAME_None, RF_Transient);
		Depth->Filter = TF_Nearest;
		Depth->InitCustomFormat(1, 1, PF_R32_FLOAT, true);
		Capture->TextureTarget = Depth;
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Capture->CaptureSource = SCS_SceneDepth;
		Capture->bRenderInMainRenderer = true;
		Capture->bMainViewResolution = true;
		Capture->bMainViewCamera = true;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Actor->AddInstanceComponent(Capture);
		Capture->RegisterComponent();
		const int32 Index = Captures.Add(Capture);
		for (FName Tag : Residual) CapturesByReceiver.Add(Tag, Index);
		for (auto* Light : Group.Spots)
		{
			auto* Function = UMaterialInstanceDynamic::Create(Master, Light);
			UMaterialInterface* Original = Light->LightFunctionMaterial;
			if (Original) Function->CopyMaterialUniformParameters(Original);
			Function->SetTextureParameterValue(TEXT("ExcludedDepth"), Captures[Index]->TextureTarget);
			Lights.Add({Light, TStrongObjectPtr<UMaterialInterface>(Original), bool(Light->bAllowMegaLights), Index});
			// A camera-space receiver lookup cannot be baked into a projector's 2D cookie atlas.
			Light->bAllowMegaLights = false;
			Light->SetLightFunctionMaterial(Function);
			Light->MarkRenderStateDirty();
		}
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		AddReceiver(**It);
	SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UScctEchelonLightFilter::QueueActor));
	if (!Captures.IsEmpty()) ViewExtension = FSceneViewExtensions::NewExtension<FReceiverView>(this);
}
