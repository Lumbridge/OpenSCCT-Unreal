using UnrealBuildTool;

public class OpenSCCTEditorTarget : TargetRules
{
	public OpenSCCTEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new[] { "OpenSCCT", "OpenSCCTEditor" });
		if (Target.Platform == UnrealTargetPlatform.Win64) ExtraModuleNames.Add("OpenSCCTGraphicsStartup");
	}
}
