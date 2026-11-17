#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctBridgeApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctSessionSubsystem.generated.h"

class AScctPawnProxy;

/**
 * Runs the OpenSCCT match of a game world. Each frame it hands the player's input to the simulation, runs the
 * ticks that are due, and moves the Unreal actors that stand for the simulation's pawns and camera.
 */
UCLASS()
class OPENSCCT_API UScctSessionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Hosts Map locally (the dedicated server's code, with bots) and joins it. Blocks while the map loads. */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT")
	bool StartLocalMatch(const FString& Map, const FString& Mode, bool bMerc, int32 Bots);

	UFUNCTION(BlueprintCallable, Category = "OpenSCCT")
	void StopMatch();

	UFUNCTION(BlueprintPure, Category = "OpenSCCT")
	bool IsPlaying() const { return bPlaying; }

	/** Why the match stopped or did not start; empty while all is well. */
	UFUNCTION(BlueprintPure, Category = "OpenSCCT")
	const FString& GetFailure() const { return Failure; }

	/** The input the next simulation ticks take. The player controller fills it every frame. */
	void SetInput(const FScctBridgeInput& Input);

	/** Queues one of the simulation's exec commands (Jump, SwitchHeadset...). */
	void SendCommand(const TCHAR* Name, int32 Arg = 0);

	/** The local camera this frame; Valid is 0 before the player has a view. */
	const FScctBridgeView& GetView() const { return View; }

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Fail(const FString& Reason);
	void UpdatePawns();

	const FScctBridgeApi* Api = nullptr;
	bool bPlaying = false;
	FString Failure;
	FScctBridgeView View = {};
	TArray<FScctBridgePawn> PawnBuffer;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<AScctPawnProxy>> Proxies;
};
