#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/EngineSubsystem.h"
#include "ScctHudTextures.generated.h"

class UTexture2D;

/**
 * The HUD's textures from the player's converted content (the bridge's HudTextureExports: shared/ui/textures, as the
 * Godot client loads them). A texture is fetched from the bridge the first time a painter asks for it, made into a
 * transient texture and a brush, and kept for the rest of the run; one the content lacks is remembered as missing
 * and asked again only when the bridge's content folder may have changed (Forget).
 *
 * Painters draw with FScctHudPainter::Image and a texel region of the texture:
 *
 *   if (const UScctHudTextures::FEntry* Atlas = UScctHudTextures::Find(TEXT("SHudTextures"), TEXT("Icones_Weapons")))
 *   {
 *       Painter.Image(Box, Atlas->Brush, Atlas->Region(U, V, U + 64, V + 64), Tint);
 *   }
 */
UCLASS()
class OPENSCCT_API UScctHudTextures : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	struct FEntry
	{
		FSlateBrush Brush;
		FIntPoint Size = FIntPoint::ZeroValue;

		/** A texel rectangle of the texture as the 0..1 region a brush takes. */
		FBox2f Region(float U0, float V0, float U1, float V1) const
		{
			const FVector2f Texels(FMath::Max(Size.X, 1), FMath::Max(Size.Y, 1));
			return FBox2f(FVector2f(U0, V0) / Texels, FVector2f(U1, V1) / Texels);
		}
	};

	/** The texture Package.Name of the content, or null when the content (or the bridge) does not have it. */
	static const FEntry* Find(const FString& Package, const FString& Name);

	/** Drops what was remembered as missing, so the next Find asks the bridge again (a new session, a new content folder). */
	static void Forget();

	virtual void Deinitialize() override;

private:
	const FEntry* FindOrLoad(const FString& Package, const FString& Name);

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Textures;

	TMap<FString, TUniquePtr<FEntry>> Entries;
	TSet<FString> Missing;
};
