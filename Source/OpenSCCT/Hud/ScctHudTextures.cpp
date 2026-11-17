#include "Hud/ScctHudTextures.h"

#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "OpenSCCT.h"
#include "Runtime/ScctHudTextureApi.h"

const UScctHudTextures::FEntry* UScctHudTextures::Find(const FString& Package, const FString& Name)
{
	UScctHudTextures* Self = GEngine != nullptr ? GEngine->GetEngineSubsystem<UScctHudTextures>() : nullptr;
	return Self != nullptr ? Self->FindOrLoad(Package, Name) : nullptr;
}

void UScctHudTextures::Forget()
{
	if (UScctHudTextures* Self = GEngine != nullptr ? GEngine->GetEngineSubsystem<UScctHudTextures>() : nullptr)
	{
		Self->Missing.Reset();
	}
}

void UScctHudTextures::Deinitialize()
{
	Entries.Reset();
	Textures.Reset();
	Missing.Reset();
	Super::Deinitialize();
}

const UScctHudTextures::FEntry* UScctHudTextures::FindOrLoad(const FString& Package, const FString& Name)
{
	const FString Key = (Package + TEXT(".") + Name).ToUpper();
	if (const TUniquePtr<FEntry>* Kept = Entries.Find(Key))
	{
		return Kept->Get();
	}
	if (Missing.Contains(Key))
	{
		return nullptr;
	}

	const FScctHudTextureApi* Api = ScctFeature<FScctHudTextureApi>();
	if (Api == nullptr)
	{
		// The bridge is not up yet: asked again next time, nothing remembered.
		return nullptr;
	}

	int32 Width = 0, Height = 0;
	if (Api->GetTexture(*Package, *Name, &Width, &Height, nullptr, 0) == 0 || Width <= 0 || Height <= 0)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHudTextures: the content has no texture %s.%s"), *Package, *Name);
		Missing.Add(Key);
		return nullptr;
	}

	TArray<uint8> Texels;
	Texels.SetNumUninitialized(Width * Height * 4);
	if (Api->GetTexture(*Package, *Name, &Width, &Height, Texels.GetData(), Texels.Num()) == 0 || Texels.Num() != Width * Height * 4)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHudTextures: %s.%s changed size while it was read"), *Package, *Name);
		Missing.Add(Key);
		return nullptr;
	}

	// The texels are the content's colours as drawn (sRGB), straight alpha, one mip: the HUD draws them at about
	// their own size or larger.
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, FName(*FString::Printf(TEXT("ScctHud_%s_%s"), *Package, *Name)));
	if (Texture == nullptr)
	{
		Missing.Add(Key);
		return nullptr;
	}
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Texels.GetData(), Texels.Num());
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	Textures.Add(Key, Texture);

	TUniquePtr<FEntry> Entry = MakeUnique<FEntry>();
	Entry->Size = FIntPoint(Width, Height);
	Entry->Brush.SetResourceObject(Texture);
	Entry->Brush.ImageSize = FVector2D(Width, Height);
	Entry->Brush.DrawAs = ESlateBrushDrawType::Image;
	Entry->Brush.Tiling = ESlateBrushTileType::NoTile;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudTextures: %s.%s %dx%d"), *Package, *Name, Width, Height);
	return Entries.Add(Key, MoveTemp(Entry)).Get();
}
