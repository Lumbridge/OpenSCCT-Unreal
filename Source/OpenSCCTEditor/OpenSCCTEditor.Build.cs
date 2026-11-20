using UnrealBuildTool;

public class OpenSCCTEditor : ModuleRules
{
	public OpenSCCTEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "OpenSCCT", "RenderCore", "RHI" });
	}
}
