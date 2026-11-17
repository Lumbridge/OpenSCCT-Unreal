#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctBindingsTest.generated.h"

/**
 * -ScctBindTest=<side>,<key>,<action>[,replace|swap|share][;...] (or <side>,reset): a second after a play world
 * begins, binds keys through the key bindings editor's capture path and applies and saves them as the Options
 * screen's SAVE would (ScctBindings::RunUnattendedTest, Menu/ScctBindingsModel.h). Combined with -ScctAutoplay
 * pressing the new key, the interface command log ("Interface command ...") shows the key acting in play.
 * Example: -ScctBindTest="spy,P,players;spy,F,use" -ScctAutoplay="wait 3;key P 0.3;wait 2"
 * Only made when the switch is given.
 */
UCLASS()
class OPENSCCT_API UScctBindingsTest : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
