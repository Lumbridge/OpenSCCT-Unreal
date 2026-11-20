using UnrealBuildTool;

public class OpenSCCTGraphicsStartup : ModuleRules
{
	public OpenSCCTGraphicsStartup(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "Projects", "RHI" });
	}
}
