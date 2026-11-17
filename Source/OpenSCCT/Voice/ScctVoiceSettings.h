#pragma once

#include "CoreMinimal.h"

/**
 * The Voice options page in the settings registry (Settings/ScctSettings.h), from Godot's GameSettings voice section
 * and its Voice page (game/Scripts/Options/GameSettings.cs, OptionsMenu.Voice.cs), with Godot's defaults and ranges.
 * The menus draw the page; UScctVoiceSubsystem reads the values back and applies a change the moment it is made.
 *
 * Playback & devices: Voice.InputDevice (a choice: "Default", then the microphones the system lists), Voice.Enabled,
 * Voice.OutputVolume (registered by UScctAudioMix, whose voice chat submix it is), Voice.JitterFrames.
 * Microphone: Voice.MicMuted, Voice.InputGain, Voice.AutoGain, Voice.NoiseSuppression, Voice.NoiseSuppressionDb,
 * Voice.EchoCancellation.
 * Transmission: Voice.Threshold, Voice.HoldMs. Voice shortcuts: Voice.PushToTalk. Lobby: Voice.TeamOnly ("Restrict
 * Mic to Team", sent to the server of the online visit). Microphone test: Voice.Test, a switch that runs Godot's
 * "Test my microphone" and turns itself off when the test is over (never kept on across a restart).
 * Auto-hack enemy communications stays with the controls (UScctControlSettings), as does every key.
 *
 * The device choice stores an index, which means nothing once a headset is unplugged, so the name it stands for is
 * kept too, in the hidden Voice.InputDeviceName; the choice is registered again whenever the device list changes, and
 * a saved device that is not connected stays listed as "(not connected)" so it is not forgotten.
 */
namespace ScctVoiceSettings
{
	extern OPENSCCT_API const FName EnabledKey;
	extern OPENSCCT_API const FName PushToTalkKey;
	extern OPENSCCT_API const FName MicMutedKey;
	extern OPENSCCT_API const FName InputDeviceKey;
	extern OPENSCCT_API const FName InputDeviceNameKey;
	extern OPENSCCT_API const FName ThresholdKey;
	extern OPENSCCT_API const FName HoldKey;
	extern OPENSCCT_API const FName InputGainKey;
	extern OPENSCCT_API const FName AutoGainKey;
	extern OPENSCCT_API const FName NoiseKey;
	extern OPENSCCT_API const FName NoiseDbKey;
	extern OPENSCCT_API const FName EchoKey;
	extern OPENSCCT_API const FName JitterKey;
	extern OPENSCCT_API const FName OutputVolumeKey;
	extern OPENSCCT_API const FName TeamOnlyKey;
	extern OPENSCCT_API const FName TestKey;

	/** The page's options other than the device choice (harmless when they are already in). */
	void Register();

	/**
	 * Registers the device choice for these devices (as the system names them) and points it at the saved device.
	 * Raises the choice's change only when its index moves; the saved name is left alone.
	 */
	void RegisterDevices(const TArray<FString>& Devices);

	/** The device the player chose, by name; empty for the system's. */
	FString ChosenDevice();

	/** After the player picked another entry of the device choice: keeps the name it stands for. */
	void RememberChosenDevice();

	/** The suppression level in dB (15, 30, 45 or 60) of Voice.NoiseSuppressionDb. */
	int32 NoiseDecibels();

	/** Every option of a page, visible or not, with its type, range, default and value, into the log. */
	void LogPage(FName Page);

	/** "Key=Value" (or "Key=default"): sets one option as the Options screen would. False when the key is unknown. */
	bool SetFromText(const FString& Assignment);
}
