using System.IO;
using UnrealBuildTool;

public class Amplitude : ModuleRules
{
	public Amplitude(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// Every file is its own translation unit: helpers in anonymous namespaces and the bundled
		// minimp3 decoder's macros cannot collide across files.
		bUseUnity = false;

		// Sources include each other relative to the module root, e.g. "Core/AmpSimulation.h".
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"ProceduralMeshComponent"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AudioMixer",
			"Json",
			"Slate",
			"SlateCore"
		});

		// Songs are loaded at runtime from <Project>/Songs (JSON charts + WAV audio), so stage them loose.
		RuntimeDependencies.Add(Path.Combine("$(ProjectDir)", "Songs", "..."), StagedFileType.NonUFS);
	}
}
