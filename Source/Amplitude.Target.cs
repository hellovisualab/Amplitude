using UnrealBuildTool;

public class AmplitudeTarget : TargetRules
{
	public AmplitudeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Amplitude");
	}
}
