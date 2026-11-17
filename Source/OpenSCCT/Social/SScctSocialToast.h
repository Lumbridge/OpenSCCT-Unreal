#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * A notice across the top of the screen for a few seconds (the Godot client's WebsiteJoinService toast): a website
 * join that waits for the menus, a sign-in it needs, a master's refusal, a friend's Join that could not start. It
 * lies over the front end and the HUD and takes no input.
 */
class SScctSocialToast : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctSocialToast) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Shows Text for Seconds (replacing what was shown). */
	void Show(const FString& Text, float Seconds = 8.0f);

	bool IsShowing() const { return Remaining > 0.0f; }

private:
	FText Message;
	float Remaining = 0.0f;
};
