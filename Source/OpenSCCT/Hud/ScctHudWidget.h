#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScctHudWidget.generated.h"

class FScctHudModel;

/**
 * The HUD's root widget in the viewport. It is built in code (no Blueprint asset): two painted layers, one over
 * the whole picture and one inside the display's safe zone. The viewport's DPI scaling applies to both, so the
 * HUD is the same size on screen at 1080p and 4K.
 */
UCLASS()
class OPENSCCT_API UScctHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** What the layers draw from. Set before the widget is added to the viewport. */
	void SetModel(const TSharedPtr<const FScctHudModel>& InModel) { Model = InModel; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<const FScctHudModel> Model;
};
