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
	}
}
