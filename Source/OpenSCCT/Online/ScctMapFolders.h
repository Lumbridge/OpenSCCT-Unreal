#pragma once

#include "CoreMinimal.h"

/**
 * Where the game finds converted maps besides the content folder, and which of them it can draw. The bridge's play
 * catalog searches, in order: every folder of -ScctMaps=<dir> (several separated by ';'), the content's svmap, the
 * downloaded packs' svmap, maps next to the bridge, maps next to the game's executable and the project, and the
 * content's own maps (Bridge/OpenScct.UnrealBridge/Online/MapFolders.cs, after the Godot client's MapLocator). A map
 * found there plays offline and online; Unreal draws it from its imported level when the editor imported it, else
 * from its render files in the runtime level (Play/ScctRuntimeMapActor).
 */
namespace ScctMapFolders
{
	/** Tells the bridge the folders of the command line and the executable, once per run. Safe to call often. */
	void Apply();

	/** The folder of a map (holding manifest.json), or empty when no folder has it. */
	FString FolderOf(const FString& Map);

	/** True when the map's folder has the render files the runtime level is built from (render/world.glb). */
	bool CanBuild(const FString& Map);

	/** The maps whose render files a level can be built from. */
	TArray<FString> RenderableMaps();

	/** Every folder searched, in order. */
	TArray<FString> Roots();
}
