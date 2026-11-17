using System.IO;
using UnrealBuildTool;

public class OpenSCCT : ModuleRules
{
	public OpenSCCT(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Headers are included by their path from the module folder ("Play/ScctGameMode.h").
		PublicIncludePaths.Add(ModuleDirectory);

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
			// The effect meshes are imported on the player's machine and found by looking (Play/ScctEffectsSubsystem).
			"AssetRegistry",
			// A sound wave class of our own (Play/ScctSoundWave) links against the audio proxy interfaces.
			"AudioExtensions",
			"Json",
			"Projects",
			"RenderCore",
			"Slate",
			"SlateCore",
			// The game's sounds are loose Ogg Vorbis and PCM files decoded at run time (Play/ScctSoundData).
			"VorbisAudioDecoder",
		});

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
