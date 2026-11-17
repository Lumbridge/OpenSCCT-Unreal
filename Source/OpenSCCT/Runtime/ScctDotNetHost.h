#pragma once

#include "CoreMinimal.h"

/**
 * Hosts the .NET runtime inside the Unreal process and hands out native entry points of the bridge
 * assembly. The runtime can be started once per process and is never unloaded, so one instance lives in the
 * game module for the whole run (play-in-editor sessions share it).
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
	static FString FindHostFxr();

	FString BridgeAssemblyPath;
	void* HostFxrLibrary = nullptr;
	void* LoadAssemblyAndGetFunctionPointer = nullptr;
};
