#include "Runtime/ScctDotNetHost.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"

namespace
{
	// The hosting interface of hostfxr (see dotnet/runtime, docs/design/features/native-hosting.md).
	using FHostFxrInitializeForRuntimeConfig = int32(__cdecl*)(const TCHAR* RuntimeConfigPath, const void* Parameters, void** OutHandle);
	using FHostFxrGetRuntimeDelegate = int32(__cdecl*)(void* Handle, int32 DelegateType, void** OutDelegate);
	using FHostFxrClose = int32(__cdecl*)(void* Handle);
	using FLoadAssemblyAndGetFunctionPointer = int32(__stdcall*)(
		const TCHAR* AssemblyPath, const TCHAR* TypeName, const TCHAR* MethodName, const TCHAR* DelegateTypeName, void* Reserved, void** OutDelegate);

	/** hostfxr_initialize_parameters: tells the host which install root to take the frameworks from. */
	struct FHostFxrInitializeParameters
	{
		SIZE_T Size;
		const TCHAR* HostPath;
		const TCHAR* DotNetRoot;
	};

	constexpr int32 HostDelegateLoadAssemblyAndGetFunctionPointer = 5;
	const TCHAR* const UnmanagedCallersOnlyMethod = reinterpret_cast<const TCHAR*>(static_cast<intptr_t>(-1));
	const TCHAR* const BridgeAssemblyName = TEXT("OpenScct.UnrealBridge");

	/** Orders "10.0.2" style folder names by their numeric parts. */
	bool IsVersionNewer(const FString& Candidate, const FString& Than)
	{
		TArray<FString> A, B;
		Candidate.ParseIntoArray(A, TEXT("."));
		Than.ParseIntoArray(B, TEXT("."));
		for (int32 Index = 0; Index < FMath::Max(A.Num(), B.Num()); ++Index)
		{
			const int32 Left = A.IsValidIndex(Index) ? FCString::Atoi(*A[Index]) : 0;
			const int32 Right = B.IsValidIndex(Index) ? FCString::Atoi(*B[Index]) : 0;
			if (Left != Right)
			{
				return Left > Right;
			}
		}
		return false;
	}
}

FString FScctDotNetHost::FindHostFxr(const FString& BridgeDirectory, FString& OutRoot)
{
	TArray<FString> Roots;
	// A runtime shipped with the game wins over whatever the machine has installed.
	Roots.Add(FPaths::ConvertRelativePathToFull(BridgeDirectory / TEXT("..") / TEXT("DotNet")));
	const FString DotNetRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("DOTNET_ROOT"));
	if (!DotNetRoot.IsEmpty())
	{
		Roots.Add(DotNetRoot);
	}
	const FString ProgramFiles = FPlatformMisc::GetEnvironmentVariable(TEXT("ProgramFiles"));
	if (!ProgramFiles.IsEmpty())
	{
		Roots.Add(ProgramFiles / TEXT("dotnet"));
	}

	for (const FString& Root : Roots)
	{
		const FString FxrRoot = Root / TEXT("host") / TEXT("fxr");
		TArray<FString> Versions;
		IFileManager::Get().FindFiles(Versions, *(FxrRoot / TEXT("*")), false, true);

		FString Best;
		for (const FString& Version : Versions)
		{
			if (Best.IsEmpty() || IsVersionNewer(Version, Best))
			{
				Best = Version;
			}
		}
		if (!Best.IsEmpty())
		{
			const FString Library = FxrRoot / Best / TEXT("hostfxr.dll");
			if (FPaths::FileExists(Library))
			{
				OutRoot = Root;
				return Library;
			}
		}
	}
	return FString();
}

bool FScctDotNetHost::Start(const FString& BridgeDirectory, FString& OutError)
{
	if (IsStarted())
	{
		return true;
	}

	const FString Directory = FPaths::ConvertRelativePathToFull(BridgeDirectory);
	const FString RuntimeConfig = Directory / FString(BridgeAssemblyName) + TEXT(".runtimeconfig.json");
	BridgeAssemblyPath = Directory / FString(BridgeAssemblyName) + TEXT(".dll");
	if (!FPaths::FileExists(RuntimeConfig) || !FPaths::FileExists(BridgeAssemblyPath))
	{
		OutError = FString::Printf(TEXT("The bridge is not built: %s is missing. Build Bridge/OpenScct.UnrealBridge first."), *BridgeAssemblyPath);
		return false;
	}

	FString DotNetRoot;
	const FString HostFxrPath = FindHostFxr(Directory, DotNetRoot);
	if (HostFxrPath.IsEmpty())
	{
		OutError = TEXT("The .NET runtime was not found (no hostfxr.dll under Binaries\\DotNet, DOTNET_ROOT or Program Files\\dotnet).");
		return false;
	}

	HostFxrLibrary = FPlatformProcess::GetDllHandle(*HostFxrPath);
	if (HostFxrLibrary == nullptr)
	{
		OutError = FString::Printf(TEXT("Could not load %s."), *HostFxrPath);
		return false;
	}

	// Until the runtime itself is loaded the host library is only a library: every failure up to there gives it back.
	const auto FailBeforeRuntime = [this]()
	{
		FPlatformProcess::FreeDllHandle(HostFxrLibrary);
		HostFxrLibrary = nullptr;
		return false;
	};

	const auto Initialize = static_cast<FHostFxrInitializeForRuntimeConfig>(FPlatformProcess::GetDllExport(HostFxrLibrary, TEXT("hostfxr_initialize_for_runtime_config")));
	const auto GetDelegate = static_cast<FHostFxrGetRuntimeDelegate>(FPlatformProcess::GetDllExport(HostFxrLibrary, TEXT("hostfxr_get_runtime_delegate")));
	const auto Close = static_cast<FHostFxrClose>(FPlatformProcess::GetDllExport(HostFxrLibrary, TEXT("hostfxr_close")));
	if (Initialize == nullptr || GetDelegate == nullptr || Close == nullptr)
	{
		OutError = FString::Printf(TEXT("%s does not have the hosting entry points."), *HostFxrPath);
		return FailBeforeRuntime();
	}

	// The paths go to the runtime with the platform's separators.
	FString NativeRuntimeConfig = RuntimeConfig;
	FPaths::MakePlatformFilename(NativeRuntimeConfig);
	FPaths::MakePlatformFilename(BridgeAssemblyPath);
	FPaths::MakePlatformFilename(DotNetRoot);

	// The frameworks come from the root the host was found in: a private runtime must not pick up the machine's.
	FHostFxrInitializeParameters Parameters = {};
	Parameters.Size = sizeof(Parameters);
	Parameters.DotNetRoot = *DotNetRoot;

	void* Handle = nullptr;
	int32 Result = Initialize(*NativeRuntimeConfig, &Parameters, &Handle);
	if (Result < 0 || Handle == nullptr)
	{
		OutError = FString::Printf(TEXT("The .NET runtime did not start from %s with the runtime at %s (0x%08x)."),
			*NativeRuntimeConfig, *DotNetRoot, static_cast<uint32>(Result));
		if (Handle != nullptr)
		{
			Close(Handle);
		}
		return FailBeforeRuntime();
	}

	void* Delegate = nullptr;
	Result = GetDelegate(Handle, HostDelegateLoadAssemblyAndGetFunctionPointer, &Delegate);
	Close(Handle);
	if (Result < 0 || Delegate == nullptr)
	{
		// The runtime may be loaded by now and it calls back into the host library, so that one stays loaded.
		OutError = FString::Printf(TEXT("The .NET runtime gave no assembly loader (0x%08x)."), static_cast<uint32>(Result));
		return false;
	}

	LoadAssemblyAndGetFunctionPointer = Delegate;
	UE_LOG(LogOpenSCCT, Log, TEXT(".NET runtime started through %s (runtime root %s)"), *HostFxrPath, *DotNetRoot);
	return true;
}

void* FScctDotNetHost::GetFunction(const TCHAR* TypeName, const TCHAR* MethodName, FString& OutError) const
{
	if (!IsStarted())
	{
		OutError = TEXT("The .NET runtime is not started.");
		return nullptr;
	}

	const FString QualifiedType = FString::Printf(TEXT("%s, %s"), TypeName, BridgeAssemblyName);
	void* Function = nullptr;
	const int32 Result = static_cast<FLoadAssemblyAndGetFunctionPointer>(LoadAssemblyAndGetFunctionPointer)(
		*BridgeAssemblyPath, *QualifiedType, MethodName, UnmanagedCallersOnlyMethod, nullptr, &Function);
	if (Result < 0 || Function == nullptr)
	{
		OutError = FString::Printf(TEXT("%s.%s was not found in the bridge (0x%08x)."), TypeName, MethodName, static_cast<uint32>(Result));
		return nullptr;
	}
	return Function;
}
