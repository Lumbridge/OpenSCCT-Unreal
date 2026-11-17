#include "Engine/World.h"
#include "Hud/ScctHudSubsystem.h"
#include "Hud/ScctHudTextures.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctHudTextureApi.h"

// The HUD subsystem's part for the HUD's pictures: the coop icon's clip (evaluated by the bridge from the content's
// A_Coop, at the time since the icon appeared) and the name of the Use key the respawn weapon pick shows, into the
// model; the -ScctHudTexture check; and the timed commands PreviewCoop and Die. The painting is in
// ScctHudElementsIcons.cpp.

void UScctHudSubsystem::InitIcons()
{
	// -ScctHudTexture=SHudTextures.Icones_Weapons,Hud.Move_coop
	FString Switch;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctHudTexture="), Switch, false))
	{
		Switch.ParseIntoArray(TextureChecks, TEXT(","));
	}
}

bool UScctHudSubsystem::RunIconCommand(const FString& Name, int32 Argument)
{
	if (Name.Equals(TEXT("PreviewCoop"), ESearchCase::IgnoreCase))
	{
		Model->PreviewCoopIcon = FMath::Clamp(Argument, 0, 4);
		return true;
	}
	if (Name.Equals(TEXT("Die"), ESearchCase::IgnoreCase))
	{
		const FScctHudTextureApi* Textures = ScctFeature<FScctHudTextureApi>();
		const bool bQueued = Textures != nullptr && Textures->KillLocalPawn() != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: Die %s"), bQueued ? TEXT("queued on the server") : TEXT("refused (remote round, no pawn or no bridge table)"));
		return true;
	}
	return false;
}

void UScctHudSubsystem::ReadIcons(float DeltaSeconds)
{
	FScctHudModel& M = *Model;
	const FScctHudTextureApi* Textures = ScctFeature<FScctHudTextureApi>();

	if (!bTextureChecksDone && Textures != nullptr)
	{
		bTextureChecksDone = true;
		UScctHudTextures::Forget();
		for (const FString& Check : TextureChecks)
		{
			FString Package, Name;
			if (!Check.Split(TEXT("."), &Package, &Name) || UScctHudTextures::Find(Package, Name) == nullptr)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctHudTexture: '%s' did not load (Package.Name)"), *Check);
			}
		}
	}

	// The coop icon: the spy page's, so a spy's only; it plays from its first frame each time it appears.
	const bool bSpy = M.bHasState && M.State.Side == 0 && (M.State.Flags & EScctHudFlags::HasPawn) != 0;
	const int32 Icon = M.PreviewCoopIcon != 0 ? M.PreviewCoopIcon : (bSpy ? FMath::Clamp(M.State.CoopIcon, 0, 4) : 0);
	if (Icon != M.CoopIcon)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: coop icon %d -> %d%s"), M.CoopIcon, Icon, M.PreviewCoopIcon != 0 ? TEXT(" (PreviewCoop)") : TEXT(""));
		M.CoopIcon = Icon;
		M.CoopAge = 0.0f;
		M.CoopBounds = FBox2f(ForceInit);
	}
	else
	{
		M.CoopAge += DeltaSeconds;
	}
	M.CoopLayers.Reset();
	if (Icon != 0 && Textures != nullptr)
	{
		if (!M.CoopBounds.bIsValid)
		{
			float Ltrb[4];
			if (Textures->GetCoopBounds(Icon, Ltrb) != 0)
			{
				M.CoopBounds = FBox2f(FVector2f(Ltrb[0], Ltrb[1]), FVector2f(Ltrb[2], Ltrb[3]));
			}
		}
		M.CoopLayers.SetNumUninitialized(8);
		const int32 Count = Textures->GetCoopLayers(Icon, M.CoopAge, M.CoopLayers.GetData(), M.CoopLayers.Num());
		M.CoopLayers.SetNum(FMath::Clamp(Count, 0, 8));
	}

	// The respawn pick names the Use key, which cycles the weapon: read as it appears and every two seconds while it
	// is up (the player can rebind while dead).
	const FIntPoint Tile(M.State.RespawnWeaponIconU, M.State.RespawnWeaponIconV);
	const bool bPicking = M.bHasState && Tile.X >= 0 && Tile.Y >= 0;
	if (bPicking && (LastRespawnTile.X < 0 || M.Clock - RespawnKeyAge > 2.0f))
	{
		RespawnKeyAge = M.Clock;
		if (const AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			// A binding is matched action by action, as the controls table holds it ("InvButton bUsing").
			const TArray<FKey> Keys = Player->FindKeysBoundTo(TEXT("InvButton bUsing"));
			M.RespawnUseKey = Keys.Num() > 0 ? Keys[0].GetDisplayName().ToString() : FString();
		}
	}
	const FIntPoint Shown = bPicking ? Tile : FIntPoint(-1, -1);
	if (Shown != LastRespawnTile)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: respawn weapon pick %s (tile %d,%d of SHudTextures.Icones_Weapons, class '%s', Use key '%s')"),
			bPicking ? TEXT("up") : TEXT("down"), Shown.X, Shown.Y, *M.Text(M.State.RespawnWeaponClassId), *M.RespawnUseKey);
		LastRespawnTile = Shown;
	}
}
