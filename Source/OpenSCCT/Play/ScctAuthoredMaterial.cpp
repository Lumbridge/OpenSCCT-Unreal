#include "Play/ScctAuthoredMaterial.h"

#include "Algo/AnyOf.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "OpenSCCT.h"
#include "Play/ScctAuthoredTexture.h"
#if WITH_EDITOR
#include "Materials/MaterialInstanceConstant.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#endif

namespace
{
	const TCHAR* const AuthoredSurfaceSlots[] = { TEXT("baseColor"), TEXT("normal"), TEXT("height"), TEXT("roughness"),
		TEXT("metallic"), TEXT("specular"), TEXT("emission"), TEXT("ao"), TEXT("opacity"), TEXT("detailColor"),
		TEXT("detailNormal"), TEXT("environment") };

	TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject>& Parent, const FString& Name)
	{
		const TSharedPtr<FJsonObject>* Child;
		return Parent && Parent->TryGetObjectField(Name, Child) ? *Child : nullptr;
	}

	// An omitted field takes the editor's v1 default. A present field of the wrong type is an error.
	struct FFields
	{
		const TSharedPtr<FJsonObject> Json;
		FString& Error;

		void Invalid(const TCHAR* Key) const
		{
			if (Error.IsEmpty())
				Error = FString::Printf(TEXT("Invalid authored field '%s'"), Key);
		}

		float Number(const TCHAR* Key, float Default, float Min = -MAX_flt, float Max = MAX_flt, bool bInteger = false) const
		{
			if (!Json || !Json->HasField(Key))
				return Default;
			double Value;
			if (!Json->HasTypedField<EJson::Number>(Key) || !Json->TryGetNumberField(Key, Value) || !FMath::IsFinite(Value)
				|| Value < Min || Value > Max || (bInteger && Value != FMath::FloorToDouble(Value)))
			{
				Invalid(Key);
				return Default;
			}
			return float(Value);
		}

		bool Flag(const TCHAR* Key, bool Default = false) const
		{
			if (!Json || !Json->HasField(Key))
				return Default;
			bool Value;
			if (Json->HasTypedField<EJson::Boolean>(Key) && Json->TryGetBoolField(Key, Value))
				return Value;
			Invalid(Key);
			return Default;
		}

		FString String(const TCHAR* Key, const TCHAR* Default) const
		{
			FString Value;
			if (!Json || !Json->HasField(Key))
				return Default;
			if (Json->HasTypedField<EJson::String>(Key) && Json->TryGetStringField(Key, Value))
				return Value;
			Invalid(Key);
			return Default;
		}

		FLinearColor Color(const TCHAR* Key) const
		{
			FLinearColor Value = FLinearColor::White;
			if (!Json->HasField(Key))
				return Value;
			const TArray<TSharedPtr<FJsonValue>>* RGB;
			if (Json->TryGetArrayField(Key, RGB) && RGB->Num() == 3)
			{
				for (int32 I = 0; I < 3; ++I)
				{
					double V;
					if ((*RGB)[I]->Type != EJson::Number || !(*RGB)[I]->TryGetNumber(V) || !FMath::IsFinite(V) || V < 0 || V > MAX_flt)
					{
						Invalid(Key);
						break;
					}
					Value.Component(I) = float(V);
				}
			}
			else
				Invalid(Key);
			return Value;
		}

		FFields Child(const TCHAR* Key) const
		{
			const auto ChildJson = Object(Json, Key);
			if (Json && Json->HasField(Key) && !ChildJson)
				Invalid(Key);
			return {ChildJson, Error};
		}
	};

	struct FDefinition
	{
		FString ParentName;
		TMap<FName, float> Scalars;
		TMap<FName, FLinearColor> Vectors;
		TMap<FName, ScctAuthored::FTextureBinding> Images;
	};

	bool Read(const TSharedPtr<FJsonObject>& Json, FDefinition& Out, FString& Error)
	{
		if (!Json)
		{
			Error = TEXT("Expected an authored material object");
			return false;
		}
		const FFields D{Json, Error};
		D.Number(TEXT("version"), 1, 1, 1, true);
		const FString Surface = D.String(TEXT("surface"), TEXT("opaque"));
		if (!Surface.Equals(TEXT("opaque"), ESearchCase::CaseSensitive) && !Surface.Equals(TEXT("masked"), ESearchCase::CaseSensitive)
			&& !Surface.Equals(TEXT("translucent"), ESearchCase::CaseSensitive))
			D.Invalid(TEXT("surface"));
		Out.ParentName = FString::Printf(TEXT("MI_Authored_%s_%s%s"), *Surface,
			D.Flag(TEXT("unlit")) ? TEXT("Unlit") : TEXT("Lit"), D.Flag(TEXT("twoSided")) ? TEXT("_TwoSided") : TEXT(""));

		for (const auto& Field : { TPair<const TCHAR*, float>(TEXT("opacity"), 1), {TEXT("cutoff"), 0.5f},
			{TEXT("roughness"), 0.85f}, {TEXT("metallic"), 0}, {TEXT("specular"), 0.5f}, {TEXT("aoStrength"), 1},
			{TEXT("clearcoat"), 0}, {TEXT("clearcoatGloss"), 0.5f}, {TEXT("anisotropy"), 0} })
			Out.Scalars.Add(Field.Key, D.Number(Field.Key, Field.Value, 0, 1));
		for (const auto& Field : { TPair<const TCHAR*, float>(TEXT("normalStrength"), 1), {TEXT("emissionEnergy"), 0},
			{TEXT("refraction"), 0}, {TEXT("detailStrength"), 0}, {TEXT("parallaxDepth"), 0} })
			Out.Scalars.Add(Field.Key, D.Number(Field.Key, Field.Value, 0));
		Out.Scalars.Add(TEXT("ParallaxSteps"), D.Number(TEXT("parallaxSteps"), 16, 4, 64, true));
		Out.Scalars.Add(TEXT("Rotation"), D.Number(TEXT("rotation"), 0));
		Out.Scalars.Add(TEXT("NormalDirectX"), D.Flag(TEXT("normalDirectX")));
		const bool bTriplanar = D.Flag(TEXT("triplanar")), bBump = D.Flag(TEXT("bumpFromHeight"));
		Out.Scalars.Add(TEXT("Triplanar"), bTriplanar);
		Out.Scalars.Add(TEXT("BumpFromHeight"), bBump);
		Out.Vectors.Add(TEXT("BaseFactor"), D.Color(TEXT("baseColor")));
		Out.Vectors.Add(TEXT("EmissionColor"), D.Color(TEXT("emissionColor")));
		Out.Vectors.Add(TEXT("SpecularColor"), D.Color(TEXT("specularColor")));
		const float TileU = D.Number(TEXT("tileU"), 1), TileV = D.Number(TEXT("tileV"), 1);
		if (FMath::Abs(TileU) < 1e-6f || FMath::Abs(TileV) < 1e-6f)
			D.Invalid(TEXT("tileU/tileV"));
		Out.Vectors.Add(TEXT("Mapping"), FLinearColor(TileU, TileV, D.Number(TEXT("offsetU"), 0), D.Number(TEXT("offsetV"), 0)));

		const FFields A = D.Child(TEXT("animation"));
		const int32 Cols = int32(A.Number(TEXT("columns"), 1, 1, 128, true)), Rows = int32(A.Number(TEXT("rows"), 1, 1, 128, true));
		const float Frames = A.Number(TEXT("frames"), 1, 1, Cols * Rows, true);
		const FLinearColor Motion(A.Number(TEXT("scrollU"), 0), A.Number(TEXT("scrollV"), 0), A.Number(TEXT("rotate"), 0), A.Number(TEXT("phase"), 0));
		Out.Vectors.Add(TEXT("Atlas"), FLinearColor(Cols, Rows, Frames, A.Number(TEXT("fps"), 12, 0, 240)));
		Out.Vectors.Add(TEXT("Motion"), Motion);
		Out.Scalars.Add(TEXT("Looping"), A.Flag(TEXT("loop"), true));
		Out.Scalars.Add(TEXT("PingPong"), A.Flag(TEXT("pingPong")));

		const FFields Textures = D.Child(TEXT("textures"));
		if (Textures.Json)
		{
			for (const auto& Entry : Textures.Json->Values)
			{
				const FString Slot(*Entry.Key);
				if (!Algo::AnyOf(AuthoredSurfaceSlots, [&](const TCHAR* Known) { return Slot.Equals(Known, ESearchCase::CaseSensitive); }))
				{
					D.Invalid(*Slot);
					continue;
				}
				const FFields B = Textures.Child(*Slot);
				ScctAuthored::FTextureBinding Image;
				Image.File = B.String(TEXT("file"), TEXT(""));
				const FString Channel = B.String(TEXT("channel"), TEXT("r"));
				static const TArray<FString> Channels = {TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a"), TEXT("luminance")};
				const int32 Index = Channels.IndexOfByPredicate([&](const FString& C) { return C.Equals(Channel, ESearchCase::CaseSensitive); });
				if (Index == INDEX_NONE)
					B.Invalid(TEXT("channel"));
				Image.bNormal = Slot == TEXT("normal") || Slot == TEXT("detailNormal");
				Image.bSRGB = B.Flag(TEXT("srgb"));
				if (Slot != TEXT("environment"))
				{
					Image.Columns = Cols;
					Image.Rows = Rows;
				}
				Out.Images.Add(FName(*(TEXT("T_") + Slot)), MoveTemp(Image));
				Out.Vectors.Add(FName(*(TEXT("B_") + Slot)), FLinearColor(1, Index, B.Flag(TEXT("invert")), 0));
			}
		}
		// These authoring controls have already affected the exported mesh, collision and lighting bake.
		D.Number(TEXT("subdivision"), 0, 0, 6, true);
		D.Number(TEXT("heightMidpoint"), 0.5f, 0, 1);
		D.Flag(TEXT("bakeEmission"));
		D.Flag(TEXT("displacementCollision"));
		const float Displacement = D.Number(TEXT("displacement"), 0);
		const bool bParallax = Out.Scalars.FindChecked(TEXT("parallaxDepth")) > 0;
		if ((bBump || bParallax || Displacement != 0) && !Out.Images.Contains(TEXT("T_height")))
			D.Invalid(TEXT("height (required for relief)"));
		if (bTriplanar && (Displacement != 0 || bParallax))
			D.Invalid(TEXT("triplanar (incompatible with displacement and parallax)"));
		if (Displacement != 0 && (Frames > 1 || Motion.R != 0 || Motion.G != 0 || Motion.B != 0))
			D.Invalid(TEXT("displacement (requires static mapping)"));
		return Error.IsEmpty();
	}

	struct FResolved
	{
		FDefinition Definition;
		UMaterialInterface* Parent = nullptr;
		TMap<FName, UTexture2D*> Textures;

		bool Prepare(const TSharedPtr<FJsonObject>& Flags, const FString& Lightmap, const FString& Folder, const FString& AssetFolder,
			UObject* Outer, ScctAuthored::FTextureCache& Cache, FString& Error)
		{
			if (!Read(Object(Flags, TEXT("authored")), Definition, Error))
				return false;
			FString Direction;
			Flags->TryGetStringField(TEXT("authored_direction"), Direction);
			if (!Lightmap.IsEmpty())
			{
				Definition.Images.Add(TEXT("Lightmap"), {Lightmap});
				Definition.Scalars.Add(TEXT("HasLightmap"), 1);
			}
			if (!Direction.IsEmpty())
			{
				Definition.Images.Add(TEXT("BakedDirection"), {Direction});
				Definition.Scalars.Add(TEXT("HasBakedDirection"), 1);
			}
			const FString Path = TEXT("/Game/OpenSCCT/Materials/Authored/") + Definition.ParentName + TEXT(".") + Definition.ParentName;
			Parent = LoadObject<UMaterialInterface>(nullptr, *Path);
			if (!Parent)
			{
				Error = TEXT("Missing authored material parent ") + Path;
				return false;
			}
			for (const auto& Entry : Definition.Images)
			{
				auto* Texture = ScctAuthored::LoadTexture(Entry.Value, Folder, AssetFolder, Outer, Cache, Error);
				if (!Texture)
					return false;
				Textures.Add(Entry.Key, Texture);
			}
			return true;
		}
	};
}

UMaterialInstanceDynamic* ScctAuthored::MaterialFor(UObject* Outer, const TSharedPtr<FJsonObject>& Flags, const FString& Lightmap,
	const FString& RenderFolder, FTextureCache& Cache, const FString& Name, UMaterialInstanceDynamic* Legacy)
{
	FResolved R;
	FString Error;
	if (!R.Prepare(Flags, Lightmap, RenderFolder, FString(), Outer, Cache, Error))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Runtime map %s: rejected authored material %s (%s); using legacy surface"), *RenderFolder, *Name, *Error);
		return Legacy;
	}
	auto* Instance = UMaterialInstanceDynamic::Create(R.Parent, Outer);
	Instance->SetScalarParameterValue(TEXT("BakedFill"), Legacy->K2_GetScalarParameterValue(TEXT("BakedFill")));
	for (const auto& Entry : R.Definition.Scalars)
		Instance->SetScalarParameterValue(Entry.Key, Entry.Value);
	for (const auto& Entry : R.Definition.Vectors)
		Instance->SetVectorParameterValue(Entry.Key, Entry.Value);
	for (const auto& Entry : R.Textures)
		Instance->SetTextureParameterValue(Entry.Key, Entry.Value);
	return Instance;
}

#if WITH_EDITOR
FString ScctAuthored::BindImported(UMaterialInstanceConstant* Instance, const FString& Json, const FString& Lightmap, const FString& RenderFolder,
	const FString& TextureFolder, FTextureCache& Cache)
{
	TSharedPtr<FJsonObject> ObjectValue;
	if (!Instance || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), ObjectValue))
		return TEXT("Expected an authored material object and a material instance");
	FString Error;
	FResolved R;
	if (!R.Prepare(ObjectValue, Lightmap, RenderFolder, TextureFolder, Instance, Cache, Error))
		return Error;
	Instance->SetParentEditorOnly(R.Parent);
	Instance->BasePropertyOverrides = {}; // Reimport may be replacing a legacy masked, two-sided or unlit instance.
	for (const auto& Entry : R.Definition.Scalars)
		Instance->SetScalarParameterValueEditorOnly(Entry.Key, Entry.Value);
	for (const auto& Entry : R.Definition.Vectors)
		Instance->SetVectorParameterValueEditorOnly(Entry.Key, Entry.Value);
	for (const auto& Entry : R.Textures)
		Instance->SetTextureParameterValueEditorOnly(Entry.Key, Entry.Value);
	Instance->PostEditChange();
	Instance->MarkPackageDirty();
	return FString();
}
#endif
