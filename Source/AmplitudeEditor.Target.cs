using UnrealBuildTool;

public class AmplitudeEditorTarget : TargetRules
{
	public AmplitudeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Amplitude");
	}
}
