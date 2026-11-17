using UnrealBuildTool;

// The game target is named after the project, not the module: a packaged build is then OpenSCCTUnreal.exe (the
// starter) and OpenSCCTUnreal-Win64-Shipping.exe. The OpenSCCT launcher finds a running Godot game by its process
// name, OpenSCCT, and must never take this game for it.
public class OpenSCCTUnrealTarget : TargetRules
{
	public OpenSCCTUnrealTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("OpenSCCT");
	}
}
