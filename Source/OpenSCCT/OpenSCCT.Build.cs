using System.IO;
using UnrealBuildTool;

public class OpenSCCT : ModuleRules
{
	public OpenSCCT(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Headers are included by their path from the module folder ("Play/ScctGameMode.h").
		PublicIncludePaths.Add(ModuleDirectory);
		// NVIDIA distributes its optional UE SDK separately. A checkout without it still builds and runs.
		string NvidiaSdk = Path.Combine(EngineDirectory, "Plugins", "Marketplace");
		bool HasNvidiaDlss = Target.Platform == UnrealTargetPlatform.Win64 && Target.Type != TargetType.Server
			&& File.Exists(Path.Combine(NvidiaSdk, "DLSS", "DLSS.uplugin"))
			&& File.Exists(Path.Combine(NvidiaSdk, "StreamlineDLSSG", "StreamlineDLSSG.uplugin"));
		PrivateDefinitions.Add("SCCT_WITH_NVIDIA_DLSS=" + (HasNvidiaDlss ? "1" : "0"));
		if (HasNvidiaDlss)
		{
			PrivateDependencyModuleNames.AddRange(new[] { "DLSSBlueprint", "StreamlineDLSSGBlueprint" });
			// An editor build can also run a game or commandlet with optional plugins disabled.
			// Resolve their SDK calls only after ScctNvidiaDlss has checked the modules are loaded.
			if (Target.Type == TargetType.Editor)
				PublicDelayLoadDLLs.AddRange(new[] { "UnrealEditor-DLSSBlueprint.dll", "UnrealEditor-StreamlineDLSSGBlueprint.dll" });
		}
		string AmdFsr = Path.Combine(EngineDirectory, "Plugins", "Marketplace", "FSR");
		bool HasAmdFsr = Target.Platform == UnrealTargetPlatform.Win64 && Target.Type != TargetType.Server
			&& File.Exists(Path.Combine(AmdFsr, "FSR.uplugin"));
		PrivateDefinitions.Add("SCCT_WITH_AMD_FSR=" + (HasAmdFsr ? "1" : "0"));
		if (HasAmdFsr)
			PrivateIncludePaths.Add(Path.Combine(AmdFsr, "Source", "fidelityfx-sdk", "Kits", "FidelityFX", "upscalers", "include"));
		// The runtime level and the editor importer use the same panorama, moon and fog definitions.
		RuntimeDependencies.Add("$(ProjectDir)/Tools/Import/map_skies.json", StagedFileType.UFS);
		RuntimeDependencies.Add("$(ProjectDir)/Tools/Import/surface_classes.json", StagedFileType.UFS);

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Niagara",
			"UMG",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			// The two-bone solver that closes a hand on a replacement weapon's grip (Play/ScctArmIk).
			"AnimationCore",
			// The effect meshes are imported on the player's machine and found by looking (Play/ScctEffectsSubsystem).
			"AssetRegistry",
			// The monitors' list and work areas for the display settings (Settings/ScctDisplaySettings).
			"ApplicationCore",
			// A sound wave class of our own (Play/ScctSoundWave) links against the audio proxy interfaces.
			"AudioExtensions",
			// The output device the player picks and its following of devices that come and go (Play/ScctAudioOutput).
			"AudioMixer",
			"AudioMixerCore",
			// Voice chat's microphone (Voice/ScctVoiceCapture): the AudioCapture plugin's device back ends, and the
			// lock-free ring its samples cross threads in.
			"AudioCapture",
			"AudioCaptureCore",
			"SignalProcessing",
			// Maps the editor never imported are built at run time from their glTF render files (Play/ScctRuntimeMapActor).
			"GLTFCore",
			// Runtime import of the locally converted skinned GLBs on packaged clients.
			"glTFRuntime",
			"ImageWrapper",
			"MeshDescription",
			"StaticMeshDescription",
			// Cosmetic camo textures are loose PNGs read at run time (Play/ScctPawnLookSubsystem).
			"ImageCore",
			"Json",
			// The map's soft bodies are meshes whose vertices are rewritten each frame (Play/ScctLevelStateSoftBodies).
			"ProceduralMeshComponent",
			// The loading page is shown by the engine's movie player while a level loads (Menu/ScctLoadingSubsystem).
			"MoviePlayer",
			"Projects",
			"RenderCore",
			// The frame recorder of the named tests reads the GPU frame time and the pipeline cache's counters (Test/ScctFrameRecorder);
			// the flash-bang after-image is copied out of the post-processing chain (Hud/ScctScreenEffectsSubsystem).
			"Renderer",
			"RHI",
			"Slate",
			"SlateCore",
			// The game's sounds are loose Ogg Vorbis and PCM files decoded at run time (Play/ScctSoundData).
			"VorbisAudioDecoder",
		});

		// The account's sign-in is sealed with DPAPI for the Windows user (Online/ScctTokenStore).
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("crypt32.lib");
			// Read the active GPU process-memory budget even when RHI stats are compiled out.
			PublicSystemLibraries.Add("dxgi.lib");
			// Independent gamepad paddles and the viewport's Windows input-message origin filter.
			PublicSystemLibraries.AddRange(new[] { "WindowsApp.lib", "Comctl32.lib" });
		}

		// FSoundQualityInfo, which the engine's audio decoders fill.
		PrivateIncludePathModuleNames.Add("TargetPlatform");

		// The game cannot start a match without the C# bridge, and the bridge is not something Unreal builds or
		// cooks: it is staged as loose files. A packaged game finds both folders where the editor does
		// (FOpenSCCTModule::FindBridgeDirectory, FScctDotNetHost).
		//   Binaries/Bridge   the built bridge and OpenSCCT's assemblies (dotnet build Bridge/OpenScct.UnrealBridge)
		//   Binaries/DotNet   optional: a private .NET runtime laid out as an install root, so players need none
		// Build the bridge before packaging; a folder that does not exist yet is skipped, not an error.
		string Binaries = Path.Combine(Target.ProjectFile.Directory.FullName, "Binaries");
		if (Directory.Exists(Path.Combine(Binaries, "Bridge")))
		{
			RuntimeDependencies.Add("$(ProjectDir)/Binaries/Bridge/...", StagedFileType.NonUFS);
		}
		// A developer may link DotNet to an entire SDK. Only the host and the core runtime belong in the game.
		foreach (string Part in new[] { "dotnet.exe", "LICENSE.txt", "ThirdPartyNotices.txt", "host/fxr", "shared/Microsoft.NETCore.App" })
		{
			string Source = Path.Combine(Binaries, "DotNet", Part);
			string Staged = "$(ProjectDir)/Binaries/DotNet/" + Part;
			if (Directory.Exists(Source))
			{
				RuntimeDependencies.Add(Staged + "/...", StagedFileType.NonUFS);
			}
			else if (File.Exists(Source))
			{
				RuntimeDependencies.Add(Staged, StagedFileType.NonUFS);
			}
		}
	}
}
