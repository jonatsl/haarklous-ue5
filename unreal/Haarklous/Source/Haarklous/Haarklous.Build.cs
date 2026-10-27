using UnrealBuildTool;

public class Haarklous : ModuleRules
{
	public Haarklous(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate", "SlateCore", "ApplicationCore", "Json", "JsonUtilities", "WebSockets",
			"ImageWrapper", "RenderCore", "RHI", "AudioMixer", "SignalProcessing", "Projects", "UMG"
		});
	}
}
