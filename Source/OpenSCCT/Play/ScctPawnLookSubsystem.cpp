#include "Play/ScctPawnLookSubsystem.h"
#include "Play/ScctCosmeticMaterial.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Play/ScctEquipmentSubsystem.h"
#include "Play/ScctFirstPersonView.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"

namespace
{
	constexpr int32 MaxLooks = 64;

	const FName HideMineSetting(TEXT("Display.HideMyCosmetics"));
	const FName HideOthersSetting(TEXT("Display.HideOthersCosmetics"));
	const FName OwnLookSetting(TEXT("Display.OwnCosmeticLook"));

	const FName BaseColorName(TEXT("BaseColor"));
	const FName LensName(TEXT("Lens"));
	const FName HueTurnName(TEXT("HueTurn"));

	const TCHAR* const HueMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctGoggleGlowHue.M_ScctGoggleGlowHue");

	FString ReadName(const FScctPawnLookApi& Api, int32 Id)
	{
		if (Id <= 0)
		{
			return FString();
		}
		TCHAR Buffer[1024];
		const int32 Length = FMath::Clamp(Api.GetName(Id, Buffer, UE_ARRAY_COUNT(Buffer)), 0, static_cast<int32>(UE_ARRAY_COUNT(Buffer)));
		return FString::ConstructFromPtrSize(Buffer, Length);
	}

	/** A name reduced to its letters and digits, lower case: importers spell punctuation their own way. */
	FString KeyOf(const FString& Name)
	{
		FString Key;
		for (const TCHAR Character : Name)
		{
			if (FChar::IsAlnum(Character))
			{
				Key.AppendChar(FChar::ToLower(Character));
			}
		}
		return Key;
	}

	/** The stock body slot of either team (an outfit's body texture replaces it): Shadw_agent, DEF_01_A_Body. */
	bool IsBodySlot(const FString& Key) { return Key.EndsWith(TEXT("shadwagent")) || Key.EndsWith(TEXT("def01abody")); }

	/** The head slot: Shadw_agent_2, DEF_01_A_Face. */
	bool IsHeadSlot(const FString& Key) { return Key.EndsWith(TEXT("shadwagent2")) || Key.EndsWith(TEXT("def01aface")); }

	/** Suit (the body item's pattern) rather than gun: the bodies and the merc's first-person sleeves (SWeaponTex_1DEF_01). */
	bool IsSuit(const FString& Key) { return IsBodySlot(Key) || IsHeadSlot(Key) || Key.EndsWith(TEXT("sweapontex1def01")); }

	/** A name made safe as an asset name, as Tools/Import/import_outfits.py makes it. */
	FString AssetName(const FString& Name)
	{
		FString Safe;
		for (const TCHAR Character : Name)
		{
			Safe.AppendChar(FChar::IsAlnum(Character) || Character == TEXT('_') ? Character : TEXT('_'));
		}
		return Safe;
	}

	/**
	 * The package folder of an imported texture, as the bridge's camo files are filed: "SPerso" for
	 * /Game/Imported/Chars/SPerso/..., "outfit:<kind>" for a map outfit's model under /Game/Imported/Outfits.
	 */
	FString PackageOf(const UTexture& Texture, int32 Kind)
	{
		TArray<FString> Parts;
		Texture.GetPathName().ParseIntoArray(Parts, TEXT("/"));
		for (int32 Index = 0; Index + 1 < Parts.Num(); ++Index)
		{
			if (Parts[Index] == TEXT("Chars"))
			{
				return Parts[Index + 1];
			}
			if (Parts[Index] == TEXT("Outfits"))
			{
				return FString::Printf(TEXT("outfit:%d"), Kind);
			}
		}
		return FString();
	}

	bool IsCloak(const UMaterialInterface* Material)
	{
		const UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
		return Dynamic != nullptr && Dynamic->Parent != nullptr && Dynamic->Parent->GetFName() == TEXT("M_ScctCloak");
	}

	bool IsGoggleGlow(const UMeshComponent& Mesh)
	{
		const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(&Mesh);
		const USkinnedAsset* Asset = Skinned != nullptr ? Skinned->GetSkinnedAsset() : nullptr;
		return Asset != nullptr && Asset->GetName().EndsWith(TEXT("_beams"));
	}

	/** The colour turned round the hue circle by Radians, its brightness kept. */
	FLinearColor TurnHue(const FLinearColor& Color, float Radians)
	{
		FLinearColor Hsv = Color.LinearRGBToHSV();
		Hsv.R = FMath::Fmod(Hsv.R + FMath::RadiansToDegrees(Radians) + 720.0f, 360.0f);
		FLinearColor Turned = Hsv.HSVToLinearRGB();
		Turned.A = Color.A;
		return Turned;
	}
}

bool UScctPawnLookSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctPawnLookSubsystem::RegisterSettings()
{
	// The two switches are on the Online page (what other players see of the player, and the player of them). Their
	// keys keep the "Display." they were first saved under, so a choice made before the move is still the player's.
	FScctSettingDesc Desc;
	Desc.Page = TEXT("Online");
	Desc.Group = NSLOCTEXT("OpenSCCT", "CosmeticsGroup", "Cosmetics");
	Desc.Type = EScctSettingType::Bool;
	Desc.Default = TEXT("false");

	Desc.Key = HideMineSetting;
	Desc.Label = NSLOCTEXT("OpenSCCT", "HideMyCosmetics", "Hide my cosmetics");
	Desc.Tooltip = NSLOCTEXT("OpenSCCT", "HideMyCosmeticsTip",
		"Your camo, tint and light colour are shown on your own screen only: other players see you in the retail look.");
	Desc.Order = 700;
	FScctSettings::Register(Desc);

	Desc.Key = HideOthersSetting;
	Desc.Label = NSLOCTEXT("OpenSCCT", "HideOthersCosmetics", "Hide others' cosmetics");
	Desc.Tooltip = NSLOCTEXT("OpenSCCT", "HideOthersCosmeticsTip",
		"Every other player is drawn in the retail look, whatever camo, tint or light colour they wear. A map's outfits still show.");
	Desc.Order = 701;
	FScctSettings::Register(Desc);

	// The look shown on the player's own pawn while "Hide my cosmetics" keeps it off the servers (the Armoury's
	// choice, as names "woodland+blue" or "hex:..."): set by code, not shown on the page, and this PC's own (the
	// Display page is not sent to the account; the chosen cosmetics follow it by their own sync).
	Desc.Page = TEXT("Display");
	Desc.Key = OwnLookSetting;
	Desc.Label = NSLOCTEXT("OpenSCCT", "OwnCosmeticLook", "Own cosmetic look");
	Desc.Tooltip = FText::GetEmpty();
	Desc.Type = EScctSettingType::Text;
	Desc.Default = FString();
	Desc.Order = 82;
	Desc.bVisible = false;
	FScctSettings::Register(Desc);
}

void UScctPawnLookSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterSettings();

	UScctSessionSubsystem* Match = Collection.InitializeDependency<UScctSessionSubsystem>();
	// After the equipment feature: its attachments and view model are made and re-skinned in its OnFrame, and the
	// looks go on what it left there.
	Collection.InitializeDependency<UScctEquipmentSubsystem>();
	if (Match == nullptr)
	{
		return;
	}
	Session = Match;
	StartedHandle = Match->OnMatchStarted.AddUObject(this, &UScctPawnLookSubsystem::OnMatchStarted);
	ChangedHandle = Match->OnSessionChanged.AddUObject(this, &UScctPawnLookSubsystem::OnSessionChanged);
	FrameHandle = Match->OnFrame.AddUObject(this, &UScctPawnLookSubsystem::OnFrame);
	SettingHandle = FScctSettings::OnChanged().AddUObject(this, &UScctPawnLookSubsystem::OnSettingChanged);

	FParse::Value(FCommandLine::Get(), TEXT("ScctLook="), ForcedLook);
	bLookOthers = FParse::Param(FCommandLine::Get(), TEXT("ScctLookOthers"));
	bLogWhere = FParse::Param(FCommandLine::Get(), TEXT("ScctLookWhere"));
}

void UScctPawnLookSubsystem::Deinitialize()
{
	if (UScctSessionSubsystem* Match = Session.Get())
	{
		Match->OnMatchStarted.Remove(StartedHandle);
		Match->OnSessionChanged.Remove(ChangedHandle);
		Match->OnFrame.Remove(FrameHandle);
	}
	FScctSettings::OnChanged().Remove(SettingHandle);
	Forget();
	Super::Deinitialize();
}

void UScctPawnLookSubsystem::Forget()
{
	Looks.Reset();
	Logged.Reset();
	LoggedDressed.Reset();
	Looked.Reset();
	Turned.Reset();
	NameCache.Reset();
	OutfitArms.Reset();
	Outfits[0] = {};
	Outfits[1] = {};
	bForced = false;
	LoggedViewDressed = -1;
}

void UScctPawnLookSubsystem::PushOptions() const
{
	if (const FScctPawnLookApi* Api = ScctFeature<FScctPawnLookApi>())
	{
		Api->SetOptions(FScctSettings::GetBool(HideMineSetting) ? 1 : 0, FScctSettings::GetBool(HideOthersSetting) ? 1 : 0);
		const FString Own = ForcedLook.IsEmpty() ? FScctSettings::GetString(OwnLookSetting) : ForcedLook;
		Api->SetOwnLook(*Own, Own.Len());
	}
}

void UScctPawnLookSubsystem::OnSettingChanged(FName Key)
{
	if (Key == HideMineSetting || Key == HideOthersSetting || Key == OwnLookSetting)
	{
		PushOptions();
		UE_LOG(LogOpenSCCT, Log, TEXT("Look: hide my cosmetics %d, hide others' %d"), FScctSettings::GetBool(HideMineSetting) ? 1 : 0,
			FScctSettings::GetBool(HideOthersSetting) ? 1 : 0);
	}
}

void UScctPawnLookSubsystem::OnMatchStarted()
{
	FString Error;
	const FScctPawnLookApi* Api = ScctFeature<FScctPawnLookApi>(&Error);
	if (Api == nullptr)
	{
		if (!bReportedNoTable)
		{
			bReportedNoTable = true;
			UE_LOG(LogOpenSCCT, Error, TEXT("Look: the bridge has no pawn look table (%s); cosmetics and map outfits are not drawn"), *Error);
		}
		return;
	}

	PushOptions();
	if (HueMaterial == nullptr)
	{
		HueMaterial = LoadObject<UMaterialInterface>(nullptr, HueMaterialPath, nullptr, LOAD_NoWarn);
	}
	Looks.SetNumZeroed(MaxLooks);
	for (int32 Kind = 0; Kind < 2; ++Kind)
	{
		Outfits[Kind] = {};
		if (Api->GetOutfit(Kind, &Outfits[Kind]) != 0)
		{
			const FScctKindOutfit& O = Outfits[Kind];
			UE_LOG(LogOpenSCCT, Log, TEXT("Look: the map dresses the %s: model '%s'%s (%d bones, mesh %s), body '%s', head '%s', goggles on '%s' + (%.1f, %.1f, %.1f), cosmetics %s"),
				Kind == 0 ? TEXT("spies") : TEXT("mercs"), *Name(*Api, O.Model), O.MapGlb != 0 ? TEXT(" (the map's glb)") : TEXT(""), O.ModelBones,
				*OutfitModelPath(Kind), *FPaths::GetCleanFilename(Name(*Api, O.BodyTexture)), *FPaths::GetCleanFilename(Name(*Api, O.HeadTexture)),
				*Name(*Api, O.GoggleBone), O.GoggleX, O.GoggleY, O.GoggleZ,
				O.Policy == 1 ? TEXT("player") : O.Policy == 2 ? TEXT("none") : TEXT("map"));
		}
	}
}

void UScctPawnLookSubsystem::OnSessionChanged(int32 OldSessionId, int32 NewSessionId)
{
	// The proxies, attachments and view model of the match before are gone; the name table is the new session's.
	Forget();
}

FString UScctPawnLookSubsystem::Name(const FScctPawnLookApi& Api, int32 Id) const
{
	if (Id <= 0)
	{
		return FString();
	}
	if (const FString* Found = NameCache.Find(Id))
	{
		return *Found;
	}
	return NameCache.Add(Id, ReadName(Api, Id));
}

FString UScctPawnLookSubsystem::OutfitModelPath(int32 Kind)
{
	const FScctPawnLookApi* Api = ScctFeature<FScctPawnLookApi>();
	FScctKindOutfit Outfit = {};
	if (Api == nullptr || Api->GetOutfit(Kind, &Outfit) == 0 || Outfit.Model == 0)
	{
		return FString();
	}

	const FString Model = ReadName(*Api, Outfit.Model);
	if (Outfit.MapGlb != 0)
	{
		const FString Map = AssetName(ReadName(*Api, Outfit.Map));
		const FString Stem = AssetName(FPaths::GetBaseFilename(Model));
		return FString::Printf(TEXT("/Game/Imported/Outfits/%s/%s/SkeletalMeshes/%s.%s"), *Map, *Stem, *Stem, *Stem);
	}

	FString Package, Mesh;
	if (!Model.Split(TEXT("."), &Package, &Mesh) || Package.IsEmpty() || Mesh.IsEmpty())
	{
		return FString();
	}
	return FString::Printf(TEXT("/Game/Imported/Chars/%s/%s/SkeletalMeshes/%s.%s"), *Package, *Mesh, *Mesh, *Mesh);
}

const FScctPawnLook* UScctPawnLookSubsystem::FindLook(int32 ActorId) const
{
	return Looks.FindByPredicate([ActorId](const FScctPawnLook& Look) { return Look.ActorId == ActorId; });
}

UTexture2D* UScctPawnLookSubsystem::LoadTexture(const FString& Path)
{
	if (const TObjectPtr<UTexture2D>* Found = Textures.Find(Path))
	{
		return Found->Get();
	}
	if (FailedTextures.Contains(Path))
	{
		return nullptr;
	}

	FImage Image;
	if (!FImageUtils::LoadImage(*Path, Image) || Image.SizeX <= 0 || Image.SizeY <= 0)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Look: could not read %s"), *Path);
		FailedTextures.Add(Path);
		return nullptr;
	}
	Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);

	// The made textures are loose PNGs: the texture is made here, with its mip chain (box-filtered), so a suit
	// seen from across a room does not shimmer.
	UTexture2D* Texture = UTexture2D::CreateTransient(Image.SizeX, Image.SizeY, PF_B8G8R8A8, *FString::Printf(TEXT("Look_%s"), *AssetName(FPaths::GetBaseFilename(Path))));
	if (Texture == nullptr)
	{
		FailedTextures.Add(Path);
		return nullptr;
	}
	Texture->SRGB = true;
	Texture->NeverStream = true;
	Texture->Filter = TF_Trilinear;
	FTexturePlatformData* Data = Texture->GetPlatformData();

	TArray64<uint8> Level(Image.RawData);
	int32 Width = Image.SizeX;
	int32 Height = Image.SizeY;
	for (int32 MipIndex = 0;; ++MipIndex)
	{
		FTexture2DMipMap* Mip = MipIndex < Data->Mips.Num() ? &Data->Mips[MipIndex] : new FTexture2DMipMap(Width, Height, 1);
		if (MipIndex >= Data->Mips.Num())
		{
			Data->Mips.Add(Mip);
		}
		Mip->SizeX = Width;
		Mip->SizeY = Height;
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Bytes = Mip->BulkData.Realloc(Level.Num());
		FMemory::Memcpy(Bytes, Level.GetData(), Level.Num());
		Mip->BulkData.Unlock();

		if (Width == 1 && Height == 1)
		{
			break;
		}
		const int32 NextWidth = FMath::Max(1, Width / 2);
		const int32 NextHeight = FMath::Max(1, Height / 2);
		TArray64<uint8> Next;
		Next.SetNumUninitialized(static_cast<int64>(NextWidth) * NextHeight * 4);
		for (int32 Y = 0; Y < NextHeight; ++Y)
		{
			for (int32 X = 0; X < NextWidth; ++X)
			{
				const int32 X0 = FMath::Min(X * 2, Width - 1), X1 = FMath::Min(X * 2 + 1, Width - 1);
				const int32 Y0 = FMath::Min(Y * 2, Height - 1), Y1 = FMath::Min(Y * 2 + 1, Height - 1);
				for (int32 Channel = 0; Channel < 4; ++Channel)
				{
					const int32 Sum = Level[(static_cast<int64>(Y0) * Width + X0) * 4 + Channel] + Level[(static_cast<int64>(Y0) * Width + X1) * 4 + Channel] +
						Level[(static_cast<int64>(Y1) * Width + X0) * 4 + Channel] + Level[(static_cast<int64>(Y1) * Width + X1) * 4 + Channel];
					Next[(static_cast<int64>(Y) * NextWidth + X) * 4 + Channel] = static_cast<uint8>((Sum + 2) / 4);
				}
			}
		}
		Level = MoveTemp(Next);
		Width = NextWidth;
		Height = NextHeight;
	}
	Texture->UpdateResource();
	Textures.Add(Path, Texture);
	return Texture;
}

FString UScctPawnLookSubsystem::TextureFor(const FScctPawnLookApi& Api, const FString& Retail, const FString& Package, const FDress& Look, FString& Finish) const
{
	const FString Key = KeyOf(Retail);
	// The arms of the eye view in the map outfit: the merc's sleeves and gloves, and the arms of a spy who wears a
	// model of the map's own, redrawn from the dressed body by the bridge.
	if (Look.bFirstPerson && !Look.ArmsPath.IsEmpty() && Key.EndsWith(Look.Kind == 1 ? TEXT("sweapontex1def01") : TEXT("shadwagent")))
	{
		return Look.ArmsPath;
	}
	// A map outfit's slot textures are laid out on the stock UVs and replace the stock slot's texture. In the eye
	// view only the spy's arms share the suit's layout (ATT_01_fp draws Shadw_agent).
	if (Look.BodySlot != 0 && IsBodySlot(Key) && (!Look.bFirstPerson || Key.EndsWith(TEXT("shadwagent"))))
	{
		return Name(Api, Look.BodySlot);
	}
	if (Look.HeadSlot != 0 && IsHeadSlot(Key) && !Look.bFirstPerson)
	{
		return Name(Api, Look.HeadSlot);
	}

	const int32 Pattern = IsSuit(Key) ? Look.BodyPattern : Look.GunPattern;
	if (Pattern == 0 || Package.IsEmpty())
	{
		return FString();
	}
	TCHAR Buffer[1024];
	const int32 Length = FMath::Clamp(Api.CosmeticTexture(Pattern, *Package, Package.Len(), *Retail, Retail.Len(), Buffer, UE_ARRAY_COUNT(Buffer)),
		0, static_cast<int32>(UE_ARRAY_COUNT(Buffer)));
	if (Length > 0) Finish = Name(Api, Pattern);
	return FString::ConstructFromPtrSize(Buffer, Length);
}

int32 UScctPawnLookSubsystem::Dress(UMeshComponent& Mesh, const FDress& Look)
{
	const FScctPawnLookApi* Api = ScctFeature<FScctPawnLookApi>();
	const int32 Count = Mesh.GetNumMaterials();
	if (Api == nullptr)
	{
		return 0;
	}
	// The camo suit has swapped every slot for its own material and gives back what it took (our instances
	// included) when it goes off: until then the mesh is its.
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		if (IsCloak(Mesh.GetMaterial(Slot)))
		{
			return 0;
		}
	}

	TArray<FLookedSlot>& Slots = Looked.FindOrAdd(&Mesh);
	if (Slots.Num() != Count)
	{
		Slots.SetNum(Count);
	}

	int32 Dressed = 0;
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		UMaterialInterface* Current = Mesh.GetMaterial(Slot);
		FLookedSlot& State = Slots[Slot];
		if (State.Mine.IsValid() ? Current != State.Mine.Get() : Current != State.Source.Get())
		{
			// Another feature put a material here (or this is the first look): that is the slot's own now.
			State = FLookedSlot();
			State.Source = Current;
		}
		UMaterialInterface* Source = State.Source.Get();
		if (Source == nullptr)
		{
			continue;
		}

		const uint32 Key = HashCombine(HashCombine(GetTypeHash(Source), HashCombine(GetTypeHash(Look.BodyPattern), GetTypeHash(Look.GunPattern))),
			HashCombine(HashCombine(GetTypeHash(Look.BodySlot), GetTypeHash(Look.HeadSlot)), HashCombine(GetTypeHash(Look.Kind), HashCombine(GetTypeHash(Look.bFirstPerson), GetTypeHash(Look.ArmsPath)))));
		if (Key != State.Key)
		{
			State.Key = Key;
			UTexture* Retail = nullptr;
			FString Finish;
			const FString Path = Source->GetTextureParameterValue(BaseColorName, Retail) && Retail != nullptr
				? TextureFor(*Api, Retail->GetName(), PackageOf(*Retail, Look.Kind), Look, Finish)
				: FString();
			UTexture2D* Made = Path.IsEmpty() ? nullptr : LoadTexture(Path);
			if (Made == nullptr)
			{
				if (State.Mine.IsValid())
				{
					Mesh.SetMaterial(Slot, Source);
				}
				State.Mine = nullptr;
			}
			else
			{
				UMaterialInstanceDynamic* Instance = ScctCosmeticMaterial::Create(&Mesh, Source, Made, Finish, Path);
				Mesh.SetMaterial(Slot, Instance);
				State.Mine = Instance;
			}
		}
		Dressed += State.Mine.IsValid() ? 1 : 0;
	}
	return Dressed;
}

FString UScctPawnLookSubsystem::OutfitArmsFor(const UMeshComponent& Mesh, int32 Kind)
{
	const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(&Mesh);
	const USkinnedAsset* Asset = Skinned != nullptr ? Skinned->GetSkinnedAsset() : nullptr;
	if (Asset == nullptr || Outfits[Kind].Dressed == 0)
	{
		return FString();
	}

	// /Game/Imported/Chars/<Package>/<Mesh>/SkeletalMeshes/<Mesh>: the bridge reads shared/chars/<Package>/<Mesh>.glb.
	const FString AssetPath = Asset->GetPathName();
	const FString CacheKey = FString::Printf(TEXT("%d|%s"), Kind, *AssetPath);
	if (const FString* Found = OutfitArms.Find(CacheKey))
	{
		return *Found;
	}

	FString Made;
	TArray<FString> Parts;
	AssetPath.ParseIntoArray(Parts, TEXT("/"));
	const int32 Chars = Parts.IndexOfByKey(FString(TEXT("Chars")));
	const FScctOutfitArmsApi* Api = ScctFeature<FScctOutfitArmsApi>();
	if (Api != nullptr && Chars != INDEX_NONE && Chars + 2 < Parts.Num())
	{
		const FString& Package = Parts[Chars + 1];
		const FString& Model = Parts[Chars + 2];
		TCHAR Buffer[1024];
		const int32 Length = FMath::Clamp(Api->ArmsTexture(Kind, *Package, Package.Len(), *Model, Model.Len(), Buffer, UE_ARRAY_COUNT(Buffer)),
			0, static_cast<int32>(UE_ARRAY_COUNT(Buffer)));
		Made = FString::ConstructFromPtrSize(Buffer, Length);
		UE_LOG(LogOpenSCCT, Log, TEXT("Look: first-person arms of %s.%s for the %s in the map outfit: %s"), *Package, *Model,
			Kind == 1 ? TEXT("mercs") : TEXT("spies"), Made.IsEmpty() ? TEXT("kept as they are") : *Made);
	}
	return OutfitArms.Add(CacheKey, Made);
}

void UScctPawnLookSubsystem::TurnGoggles(UMeshComponent& Mesh, float HueTurn, bool bTurn)
{
	static const FName IntensityName(TEXT("Intensity"));
	static const FName FadeName(TEXT("Fade"));

	const int32 Count = Mesh.GetNumMaterials();
	TArray<FLookedSlot>& Slots = Turned.FindOrAdd(&Mesh);
	if (Slots.Num() != Count)
	{
		Slots.SetNum(Count);
	}
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		UMaterialInterface* Current = Mesh.GetMaterial(Slot);
		FLookedSlot& State = Slots[Slot];
		if (State.Mine.IsValid() ? Current != State.Mine.Get() : Current != State.Source.Get())
		{
			// The equipment feature changed the lens (a vision mode's colour): look at the new one.
			State = FLookedSlot();
			State.Source = Current;
		}
		UMaterialInterface* Source = State.Source.Get();
		if (Source == nullptr)
		{
			continue;
		}

		// Only the lenses' own colour turns (the spy's green, the merc's orange), as in the Godot client; the
		// vision modes' lens colours (motion, EMF) stay as they are.
		UTexture* Lens = nullptr;
		const bool bNormal = Source->GetTextureParameterValue(LensName, Lens) && Lens != nullptr &&
			(Lens->GetName().Contains(TEXT("goggle_green")) || Lens->GetName().Contains(TEXT("goggle_orange")));
		if (!bTurn || !bNormal || HueMaterial == nullptr)
		{
			if (State.Mine.IsValid())
			{
				Mesh.SetMaterial(Slot, Source);
				State.Mine = nullptr;
			}
			if (bTurn && bNormal && HueMaterial == nullptr && !bMissingHueReported)
			{
				bMissingHueReported = true;
				UE_LOG(LogOpenSCCT, Warning, TEXT("Look: no %s (Tools/Effects/Build-EffectAssets.ps1 builds it); goggle lights keep their colour"), HueMaterialPath);
			}
			continue;
		}

		if (!State.Mine.IsValid())
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(HueMaterial, &Mesh);
			Instance->SetTextureParameterValue(LensName, Lens);
			float Value = 0.0f;
			if (Source->GetScalarParameterValue(IntensityName, Value))
			{
				Instance->SetScalarParameterValue(IntensityName, Value);
			}
			if (Source->GetScalarParameterValue(FadeName, Value))
			{
				Instance->SetScalarParameterValue(FadeName, Value);
			}
			Mesh.SetMaterial(Slot, Instance);
			State.Mine = Instance;
		}
		State.Mine->SetScalarParameterValue(HueTurnName, HueTurn);
	}
}

void UScctPawnLookSubsystem::LogLook(const FScctPawnLookApi& Api, const FScctPawnLook& Look, int32 Dressed) const
{
	UE_LOG(LogOpenSCCT, Log, TEXT("Look: pawn %d (%s%s%s) body '%s' (%s '%s'%s) light '%s'%s gun '%s' (pattern '%s'); %d slots wear a made texture"),
		Look.ActorId, Look.Side == 1 ? TEXT("merc") : TEXT("spy"), (Look.Flags & EScctPawnLookFlags::Mine) != 0 ? TEXT(", mine") : TEXT(""),
		(Look.Flags & EScctPawnLookFlags::OutfitModel) != 0 ? TEXT(", outfit model") : (Look.Flags & EScctPawnLookFlags::Outfit) != 0 ? TEXT(", outfit") : TEXT(""),
		*Name(Api, Look.BodyItem), Look.BodyKind == 1 ? TEXT("tint") : TEXT("pattern"), *Name(Api, Look.BodyPattern),
		Look.BodyKind == 1 ? *FString::Printf(TEXT(" #%06x"), Look.BodyRgb) : TEXT(""),
		*Name(Api, Look.LightItem),
		(Look.Flags & EScctPawnLookFlags::Hue) != 0 ? *FString::Printf(TEXT(" (hue %.0f, turn %.3f rad)"), Look.LightHue, Look.HueTurn) : TEXT(""),
		*Name(Api, Look.GunItem), *Name(Api, Look.GunPattern), Dressed);
}

void UScctPawnLookSubsystem::OnFrame(float DeltaSeconds)
{
	UScctSessionSubsystem* Match = Session.Get();
	const FScctPawnLookApi* Api = ScctFeature<FScctPawnLookApi>();
	if (Match == nullptr || Api == nullptr || Looks.Num() == 0)
	{
		return;
	}

	if (!bForced && (!ForcedLook.IsEmpty() || bLookOthers) && Match->FindLocalProxy() != nullptr)
	{
		bForced = true;
		if (Match->IsRemoteSession())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Look: -ScctLook and -ScctLookOthers only work in a local match"));
		}
		else
		{
			if (!ForcedLook.IsEmpty())
			{
				const int32 Used = Api->SetOwnLook(*ForcedLook, ForcedLook.Len());
				const int32 Queued = Api->ForceLocalLook(*ForcedLook, ForcedLook.Len());
				UE_LOG(LogOpenSCCT, Log, TEXT("Look: -ScctLook=%s (%d names used, %s)"), *ForcedLook, Used, Queued != 0 ? TEXT("put on the player's info") : TEXT("NOT queued"));
			}
			if (bLookOthers)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Look: -ScctLookOthers %s"), Api->GiveOthersLooks() != 0 ? TEXT("queued") : TEXT("NOT queued"));
			}
		}
	}

	// -ScctLookWhere: where every pawn is once a second, to aim -ScctCams at the bots for a look check.
	if (bLogWhere && (WhereTimer -= DeltaSeconds) <= 0.0f)
	{
		WhereTimer = 1.0f;
		for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Match->GetProxies())
		{
			if (const AScctPawnProxy* Proxy = Pair.Value.Get())
			{
				const FVector At = Proxy->GetActorLocation();
				UE_LOG(LogOpenSCCT, Log, TEXT("Look: where pawn %d (%s) %.0f,%.0f,%.0f yaw %.0f"), Pair.Key, Proxy->GetKind() == 1 ? TEXT("merc") : TEXT("spy"),
					At.X, At.Y, At.Z, Proxy->GetActorRotation().Yaw);
			}
		}
	}

	const int32 Count = FMath::Clamp(Api->GetLooks(Looks.GetData(), MaxLooks), 0, MaxLooks);
	const FScctPawnLook* Mine = nullptr;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctPawnLook& Look = Looks[Index];
		Mine = (Look.Flags & EScctPawnLookFlags::Mine) != 0 ? &Look : Mine;
		AScctPawnProxy* Proxy = Match->FindProxy(Look.ActorId);
		if (Proxy == nullptr || !Proxy->HasModel())
		{
			continue;
		}

		const int32 Kind = FMath::Clamp(Look.Side, 0, 1);
		FDress Suit;
		Suit.Kind = Kind;
		Suit.BodyPattern = Look.BodyPattern;
		Suit.GunPattern = Look.GunPattern;
		Suit.BodySlot = Outfits[Kind].BodyTexture;
		Suit.HeadSlot = Outfits[Kind].HeadTexture;

		const bool bTurn = (Look.Flags & EScctPawnLookFlags::Hue) != 0;
		int32 Dressed = Dress(*Proxy->GetBodyMesh(), Suit);
		TArray<USceneComponent*> Attached;
		Proxy->GetBodyMesh()->GetChildrenComponents(true, Attached);
		for (USceneComponent* Child : Attached)
		{
			UMeshComponent* Mesh = Cast<UMeshComponent>(Child);
			if (Mesh == nullptr || Mesh->IsA<UStaticMeshComponent>())
			{
				continue;
			}
			if (IsGoggleGlow(*Mesh))
			{
				TurnGoggles(*Mesh, Look.HueTurn, bTurn);
				continue;
			}
			// What hangs on the body (the gun in the hands, a holstered one) takes the gun's pattern.
			FDress Gear = Suit;
			Gear.BodySlot = 0;
			Gear.HeadSlot = 0;
			Dressed += Dress(*Mesh, Gear);
		}

		// The lamp's beam takes the light item's colour too (the bridge sets the retail colour every frame).
		if (bTurn)
		{
			TArray<USpotLightComponent*> Lamps;
			Proxy->GetComponents<USpotLightComponent>(Lamps);
			for (USpotLightComponent* Lamp : Lamps)
			{
				Lamp->SetLightColor(TurnHue(Lamp->GetLightColor(), Look.HueTurn));
			}
		}

		const FScctPawnLook* Before = Logged.Find(Look.ActorId);
		const int32* DressedBefore = LoggedDressed.Find(Look.ActorId);
		if (Before == nullptr || FMemory::Memcmp(Before, &Look, sizeof(Look)) != 0 || DressedBefore == nullptr || *DressedBefore != Dressed)
		{
			LogLook(*Api, Look, Dressed);
			Logged.Add(Look.ActorId, Look);
			LoggedDressed.Add(Look.ActorId, Dressed);
		}
	}

	// The player's own arms and gun in his eye view: the suit's pattern on the sleeves, the gun's on the gun.
	const UScctEquipmentSubsystem* Equipment = GetWorld()->GetSubsystem<UScctEquipmentSubsystem>();
	AScctFirstPersonView* View = Equipment != nullptr ? Equipment->GetFirstPersonView() : nullptr;
	if (Mine != nullptr && IsValid(View) && View->IsShown())
	{
		const int32 Kind = FMath::Clamp(Mine->Side, 0, 1);
		FDress Arms;
		Arms.Kind = Kind;
		Arms.BodyPattern = Mine->BodyPattern;
		Arms.GunPattern = Mine->GunPattern;
		Arms.bFirstPerson = true;
		// The spy's eye-view arms are laid out as the suit: an outfit's body texture fits them, but not an outfit
		// model's, which the bridge moves onto the arms surface by surface (OutfitArmsFor), as it does the merc's.
		Arms.BodySlot = Outfits[Kind].Model == 0 ? Outfits[Kind].BodyTexture : 0;
		int32 Dressed = 0;
		TArray<UMeshComponent*> Meshes;
		View->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (!Mesh->IsA<UStaticMeshComponent>())
			{
				Arms.ArmsPath = OutfitArmsFor(*Mesh, Kind);
				Dressed += Dress(*Mesh, Arms);
			}
		}
		if (Dressed != LoggedViewDressed)
		{
			LoggedViewDressed = Dressed;
			UE_LOG(LogOpenSCCT, Log, TEXT("Look: first-person view of pawn %d: %d slots wear a made texture (sleeves '%s', gun '%s')"),
				Mine->ActorId, Dressed, *Name(*Api, Mine->BodyPattern), *Name(*Api, Mine->GunPattern));
		}
	}

	// Meshes that are gone.
	for (auto It = Looked.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = Turned.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
