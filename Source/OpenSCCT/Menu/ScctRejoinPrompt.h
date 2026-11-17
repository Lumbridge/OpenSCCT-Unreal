#pragma once

#include "CoreMinimal.h"

class SScctDialogLayer;
class UScctRejoinSubsystem;

/**
 * The main menu's question when a dropped round can be rejoined (Online/ScctRejoinSubsystem.h): REJOIN joins the
 * server again, LATER closes the question (the home page's REJOIN entry stays), FORGET IT drops the offer for good.
 */
namespace ScctRejoinPrompt
{
	/** Asks about the round offered now; does nothing when none is offered. */
	void Show(const TSharedPtr<SScctDialogLayer>& Dialogs, UScctRejoinSubsystem* Rejoin);

	/** "MapTitle on Server" for an entry or a line. */
	FString Describe(const UScctRejoinSubsystem* Rejoin);
}
