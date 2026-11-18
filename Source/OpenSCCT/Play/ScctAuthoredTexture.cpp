#include "Play/ScctAuthoredTexture.h"

#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#endif

namespace
{
	float Linear(float Value)
	{
		return Value <= 0.04045f ? Value / 12.92f : FMath::Pow((Value + 0.055f) / 1.055f, 2.4f);
	}

	void Reduce(const FImage& Src, FImage& Dst, bool bNormal)
	{
		Dst.Init(FMath::Max(1, Src.SizeX / 2), FMath::Max(1, Src.SizeY / 2), ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		const auto Pixels = Src.AsRGBA32F();
		auto Out = Dst.AsRGBA32F();
		for (int32 Y = 0; Y < Dst.SizeY; ++Y)
		{
			const int32 Y0 = int64(Y) * Src.SizeY / Dst.SizeY, Y1 = int64(Y + 1) * Src.SizeY / Dst.SizeY;
			for (int32 X = 0; X < Dst.SizeX; ++X)
			{
				const int32 X0 = int64(X) * Src.SizeX / Dst.SizeX, X1 = int64(X + 1) * Src.SizeX / Dst.SizeX;
				FLinearColor Sum(0, 0, 0, 0);
				for (int32 J = Y0; J < Y1; ++J)
					for (int32 I = X0; I < X1; ++I)
						Sum += Pixels[int64(J) * Src.SizeX + I];
				FLinearColor Mean = Sum / float((Y1 - Y0) * (X1 - X0));
				if (bNormal)
				{
					const FVector3f N = FVector3f(2*Mean.R-1, 2*Mean.G-1, 2*Mean.B-1).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
					Mean = FLinearColor(N.X*0.5f+0.5f, N.Y*0.5f+0.5f, N.Z*0.5f+0.5f, Mean.A);
				}
				Out[int64(Y) * Dst.SizeX + X] = Mean;
			}
		}
	}
}

UTexture2D* ScctAuthored::LoadTexture(const FTextureBinding& Binding, const FString& RenderFolder,
	const FString& AssetFolder, UObject* Outer, TMap<FString, TObjectPtr<UTexture2D>>& Cache, FString& Error)
{
	TArray<FString> Parts;
	Binding.File.ParseIntoArray(Parts, TEXT("/"), false);
	if (Binding.File.IsEmpty() || Binding.File.Contains(TEXT("\\")) || Binding.File.Contains(TEXT(":")) ||
		Parts.Contains(TEXT(".")) || Parts.Contains(TEXT("..")) || Parts.Contains(FString()))
	{
		Error = TEXT("Expected a relative project texture: ") + Binding.File;
		return nullptr;
	}
	const FString Key = FString::Printf(TEXT("#authored:%d:%d:%d:%d:%s"), Binding.bSRGB, Binding.bNormal,
		Binding.Columns, Binding.Rows, *Binding.File);
	if (const auto* Found = Cache.Find(Key))
		return Found->Get();

	FImage Source;
	TArray64<uint8> BytesOnDisk;
	if (!FFileHelper::LoadFileToArray(BytesOnDisk, *(RenderFolder / Binding.File)) ||
		!FImageUtils::DecompressImage(BytesOnDisk.GetData(), BytesOnDisk.Num(), Source) || Source.NumSlices != 1 ||
		Source.SizeX < Binding.Columns || Source.SizeY < Binding.Rows ||
		Source.SizeX % Binding.Columns != 0 || Source.SizeY % Binding.Rows != 0)
	{
		Error = TEXT("Unreadable image or atlas cells do not fit: ") + Binding.File;
		return nullptr;
	}
	BytesOnDisk.Empty();

	// Interpret the file per binding instead of using the PNG's gamma metadata. Apply the transfer before
	// filtering, also for 16-bit maps. Alpha always remains linear. The GPU samples straight linear texels.
	const bool bEightBit = Source.Format == ERawImageFormat::BGRA8 || Source.Format == ERawImageFormat::G8;
	const auto Format = !bEightBit ? ERawImageFormat::RGBA32F : Binding.bSRGB ? ERawImageFormat::RGBA16F : ERawImageFormat::BGRA8;
	Source.GammaSpace = EGammaSpace::Linear;
	FImage Level;
	Source.CopyTo(Level, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
	Source.RawData.Empty();
	if (Binding.bSRGB)
	{
		for (FLinearColor& Pixel : Level.AsRGBA32F())
		{
			Pixel.R = Linear(Pixel.R);
			Pixel.G = Linear(Pixel.G);
			Pixel.B = Linear(Pixel.B);
		}
	}
	const int32 Width = Level.SizeX, Height = Level.SizeY;
	int32 FrameWidth = Width / Binding.Columns, FrameHeight = Height / Binding.Rows;
	const bool bAtlas = Binding.Columns != 1 || Binding.Rows != 1;
	const bool bFrameMips = FMath::IsPowerOfTwo(FrameWidth) && FMath::IsPowerOfTwo(FrameHeight);
	TArray<FImage> Mips;
	for (;;)
	{
		Level.CopyTo(Mips.AddDefaulted_GetRef(), Format, EGammaSpace::Linear);
		if ((Level.SizeX == 1 && Level.SizeY == 1) || (bAtlas && (!bFrameMips || FrameWidth <= 1 || FrameHeight <= 1)))
			break;
		FImage Next;
		Reduce(Level, Next, Binding.bNormal);
		Level = MoveTemp(Next);
		FrameWidth /= 2;
		FrameHeight /= 2;
	}

	UTexture2D* Texture = nullptr;
#if WITH_EDITOR
	if (!AssetFolder.IsEmpty())
	{
		const FTCHARToUTF8 Utf8Key(*Key);
		const FString Name = TEXT("T_Authored_") + FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8Key.Get()), Utf8Key.Length());
		const FString PackageName = AssetFolder / Name;
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		Texture = FindObject<UTexture2D>(Package, *Name);
		if (!Texture)
		{
			Texture = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Texture);
		}
		Texture->PreEditChange(nullptr);
		TArray64<uint8> Bytes;
		for (const FImage& Mip : Mips)
			Bytes.Append(Mip.RawData);
		const auto SrcFormat = Format == ERawImageFormat::RGBA32F ? TSF_RGBA32F : Format == ERawImageFormat::RGBA16F ? TSF_RGBA16F : TSF_BGRA8;
		Texture->Source.Init(Width, Height, 1, Mips.Num(), SrcFormat, Bytes.GetData());
		Texture->CompressionSettings = Format == ERawImageFormat::RGBA32F ? TC_HDR_F32 : Format == ERawImageFormat::RGBA16F ? TC_HDR : TC_VectorDisplacementmap;
		Texture->MipGenSettings = Mips.Num() == 1 ? TMGS_NoMipmaps : TMGS_LeaveExistingMips;
		Texture->VirtualTextureStreaming = false;
		Texture->NeverStream = true;
		Texture->SRGB = false;
		Texture->Filter = TF_Trilinear;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
	}
#endif
	if (!Texture)
	{
		Texture = NewObject<UTexture2D>(Outer, NAME_None, RF_Transient);
		auto* Data = new FTexturePlatformData();
		Data->SizeX = Width;
		Data->SizeY = Height;
		Data->SetNumSlices(1);
		Data->PixelFormat = Format == ERawImageFormat::RGBA32F ? PF_A32B32G32R32F : Format == ERawImageFormat::RGBA16F ? PF_FloatRGBA : PF_B8G8R8A8;
		for (const FImage& Image : Mips)
		{
			auto* Mip = new FTexture2DMipMap(Image.SizeX, Image.SizeY, 1);
			Data->Mips.Add(Mip);
			Mip->BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Mip->BulkData.Realloc(Image.RawData.Num()), Image.RawData.GetData(), Image.RawData.Num());
			Mip->BulkData.Unlock();
		}
		Texture->SetPlatformData(Data);
		Texture->bNotOfflineProcessed = true;
		Texture->NeverStream = true;
		Texture->SRGB = false;
		Texture->Filter = TF_Trilinear;
		Texture->UpdateResource();
	}
	Cache.Add(Key, Texture);
	return Texture;
}
