#pragma once

#include "CoreMinimal.h"

/**
 * Short Splinter Cell facts, the Godot client's SplashTrivia (game/Scripts/UI/SplashTrivia.cs) line for line: the
 * main menu shows one under its tagline, a different one each time the page opens, and the loading page shows one
 * when it has no tip of the side to give (the way back to the front end). Keep lines short enough for one line
 * across the menu, and keep them true.
 */
namespace ScctSplashTrivia
{
	/** A line at random, never the same one twice in a row. */
	OPENSCCT_API FString Pick();

	/** How many lines there are. */
	OPENSCCT_API int32 Num();
}
