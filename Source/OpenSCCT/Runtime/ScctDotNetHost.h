#pragma once

#include "CoreMinimal.h"

/**
 * Hosts the .NET runtime inside the Unreal process and hands out native entry points of the bridge
 * assembly. The runtime can be started once per process and is never unloaded, so one instance lives in the
 * game module for the whole run (play-in-editor sessions share it).
 *
 * Where the runtime comes from, first match wins:
 *   1. A private runtime shipped with the game: the folder "DotNet" next to the bridge folder
 *      (Binaries/DotNet), laid out as a .NET install root (host/fxr/<version>/hostfxr.dll and
 *      shared/Microsoft.NETCore.App/<version>/). Copying the "dotnet" folder of a .NET 10 runtime zip there
 *      is all a packaged build needs; nothing has to be installed on the player's machine.
 *   2. The folder named by the DOTNET_ROOT environment variable.
 *   3. The machine-wide install under Program Files\dotnet.
 * The root that was found is also given to the runtime as its own root, so the frameworks are taken from the
 * same place as the host.
 */
class FScctDotNetHost
{
public:
	/** Starts the runtime from the bridge folder (holding OpenScct.UnrealBridge.dll and its runtimeconfig). */
	bool Start(const FString& BridgeDirectory, FString& OutError);

	bool IsStarted() const { return LoadAssemblyAndGetFunctionPointer != nullptr; }

	/** Returns a static [UnmanagedCallersOnly] method of the bridge assembly, or null with a reason. */
	void* GetFunction(const TCHAR* TypeName, const TCHAR* MethodName, FString& OutError) const;

private:
	/** The newest hostfxr.dll under one of the roots above; OutRoot is the root it was found in. */
	static FString FindHostFxr(const FString& BridgeDirectory, FString& OutRoot);

	FString BridgeAssemblyPath;
	void* HostFxrLibrary = nullptr;
	void* LoadAssemblyAndGetFunctionPointer = nullptr;
};
