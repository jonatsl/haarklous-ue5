using UnrealBuildTool;

public class Haarklous : ModuleRules
{
	public Haarklous(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate", "SlateCore", "ApplicationCore", "Json", "JsonUtilities", "WebSockets",
			"ImageWrapper", "RenderCore", "RHI", "AudioMixer", "SignalProcessing", "Projects", "UMG",
			"WebBrowser", "AudioCaptureCore",  // lobby voice chat (HKVoice): hidden CEF page + microphone capture
			"ProceduralMeshComponent"           // the construction site's soil (HKSite)
		});
		// DualShock 4 over Windows HID (HKInput.cpp).
		if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.AddRange(new string[] { "hid.lib", "setupapi.lib" });
	}
}
