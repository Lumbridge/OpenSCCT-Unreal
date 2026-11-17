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
			"Projects",
			"RenderCore",
			"Slate",
			"SlateCore",
		});

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
