using System.IO;
using UnrealBuildTool;

public class OpenSCCT : ModuleRules
{
	public OpenSCCT(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Headers are included by their path from the module folder ("Play/ScctGameMode.h").
		PublicIncludePaths.Add(ModuleDirectory);
		// The runtime level and the editor importer use the same panorama, moon and fog definitions.
		RuntimeDependencies.Add("$(ProjectDir)/Tools/Import/map_skies.json", StagedFileType.UFS);

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
			"ImageWrapper",
			"MeshDescription",
			"StaticMeshDescription",
			// Cosmetic camo textures are loose PNGs read at run time (Play/ScctPawnLookSubsystem).
			"ImageCore",
			// The cooked map packs of OpenSCCT-Unreal-Content are downloaded at run time (Online/ScctMapPacks).
			"HTTP",
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
			// Independent gamepad paddles and the viewport's Windows input-message origin filter.
			PublicSystemLibraries.AddRange(new[] { "WindowsApp.lib", "Comctl32.lib" });
			// The downloaded map packs are checked with SHA-256 from CNG (Online/ScctMapPacks).
			PublicSystemLibraries.Add("bcrypt.lib");
		}

		// FSoundQualityInfo, which the engine's audio decoders fill.
		PrivateIncludePathModuleNames.Add("TargetPlatform");

		// The game cannot start a match without the C# bridge, and the bridge is not something Unreal builds or
		// cooks: it is staged as loose files. A packaged game finds both folders where the editor does
		// (FOpenSCCTModule::FindBridgeDirectory, FScctDotNetHost).
		//   Binaries/Bridge   the built bridge and OpenSCCT's assemblies (dotnet build Bridge/OpenScct.UnrealBridge)
		//   Binaries/DotNet   optional: a private .NET runtime laid out as an install root, so players need none
		// Build the bridge before packaging; a folder that does not exist yet is skipped, not an error.
		foreach (string Folder in new[] { "Bridge", "DotNet" })
		{
			if (Directory.Exists(Path.Combine(Target.ProjectFile.Directory.FullName, "Binaries", Folder)))
			{
				RuntimeDependencies.Add("$(ProjectDir)/Binaries/" + Folder + "/...", StagedFileType.NonUFS);
			}
		}
	}
}
