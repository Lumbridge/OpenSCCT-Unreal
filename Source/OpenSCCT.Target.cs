using UnrealBuildTool;

public class OpenSCCTTarget : TargetRules
{
	public OpenSCCTTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("OpenSCCT");
	}
}
