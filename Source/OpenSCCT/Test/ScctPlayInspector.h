#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"

class UWorld;
class UScctSessionSubsystem;
class AScctPawnProxy;
struct FScctInspectApi;
struct FScctInspectPawn;

/** The areas the play inspector checks; every issue belongs to one (the Godot client's InspectChecks). */
enum class EScctInspectArea : uint8
{
	Animation,
	Feet,
	Camera,
	Audio,
	Net,
	Hud,
	PauseMenu,
	Input,
	Count,
};

/** One problem the inspector saw, merged with later sightings of the same area and subject. */
struct FScctInspectIssue
{
	EScctInspectArea Area = EScctInspectArea::Animation;
	FString Subject;
	FString Message;      // the first sighting's
	FString FirstStep;    // the test's step it was first seen in
	double FirstSeconds = 0.0;
	int32 Count = 0;
	float Worst = TNumericLimits<float>::Lowest();
	FString WorstMessage;

	FString ToString() const;
};

/**
 * Per-frame checks of what the player sees and hears in a match of the real client (the Godot client's PlayInspector,
 * InspectPose and InspectNet, game/Scripts/Smoke): every drawn pawn body's bones move while its pawn moves and its
 * animation channel 0 is not stuck, no channel plays a sequence without a clip, the lowest drawn foot bone stands on
 * the floor the simulation has under the pawn, the camera never jumps without a camera cut and is never inside the
 * level's geometry or outside the level, sound events reach the audio and voices start, the server does not keep
 * correcting the local player and the local pawn does not snap, and the HUD shows the side's figures. The pause menu
 * and the keys are judged by the test that drives the match (Test/ScctPlayInspectTest.cpp) through Note.
 *
 * It reads Unreal's drawn bodies and camera and the bridge's inspect table (Test/ScctInspectApi.h), never writes them.
 * Issues are merged by area and subject and printed as one SCCT_INSPECT line each.
 */
class OPENSCCT_API FScctPlayInspector
{
public:
	/** Seconds over which a pawn's motion and its bones' motion are compared. */
	static constexpr double WindowSeconds = 0.5;
	/** Horizontal speed (uu/s) above which a pawn counts as moving. */
	static constexpr float MovingSpeed = 60.0f;
	/** A bone of an animated walking body moves far more than this (uu) in a window. */
	static constexpr float BoneMotion = 0.5f;
	/** How far the lowest drawn foot bone may sink under the floor (skin and sole) or sit over it (an ankle with the foot flat). */
	static constexpr float FeetBelow = 8.0f;
	static constexpr float FeetAbove = 30.0f;
	/** How far under the collision bottom the floor is searched. */
	static constexpr float FloorSearch = 40.0f;
	/** The camera's cut rule (AScctPlayerController::OverrideView): a frame's move over this needs a cut... */
	static constexpr float CutDistance = 128.0f;
	/** ...or over this speed for a frame of at most CutFrameSeconds. */
	static constexpr float CutSpeed = 6000.0f;
	static constexpr float CutFrameSeconds = 0.1f;
	/** Corrections the client may apply per 10 s of play on a loopback match. */
	static constexpr float CorrectionsPer10s = 2.0f;

	~FScctPlayInspector();

	/** Starts watching World's match. LocalSide: 0 spy, 1 merc (the HUD check wants that side's figures). */
	bool Attach(UWorld* World, int32 LocalSide, FString* OutError = nullptr);
	void Detach();

	/** The step label issues are tagged with. */
	void SetStep(const FString& Step) { CurrentStep = Step; }
	const FString& GetStep() const { return CurrentStep; }

	/** Records an issue, merged with earlier ones of the same area and subject. Value orders the sightings (larger is worse). */
	void Note(EScctInspectArea Area, const FString& Subject, const FString& Message, float Value = TNumericLimits<float>::Lowest());

	/** The end-of-run checks (sound never played, HUD never filled, too many corrections, never animated). Idempotent. */
	void Finish();

	double GetSeconds() const { return Seconds; }
	const TArray<FScctInspectIssue>& GetIssues() const { return Issues; }

	/** The local pawn as the simulation had it at the last tick this frame, and whether there is one. */
	bool GetLocal(FScctInspectPawn& Out) const;

	/** A box sweep through the client world's level geometry, for the test's "a wall is in the way" (false when nothing is hit). */
	bool TraceWorld(const FVector& Start, const FVector& End, const FVector& Extent, FVector* OutHit = nullptr, float* OutNormalZ = nullptr) const;

	/** The test's own figures for an area's summary line (the pause menu's and the keys'), in place of the inspector's. */
	void SetAreaNote(EScctInspectArea Area, const FString& Text) { AreaNotes[static_cast<int32>(Area)] = Text; }

	/** Voices the audio subsystem has started since the match began. */
	int32 GetVoicesStarted() const;

	/** One line per area: OK or how many issues, and the figures behind it. */
	TArray<FString> AreaSummaries() const;

	static const TCHAR* AreaName(EScctInspectArea Area);

private:
	struct FPoseWatch
	{
		FString Label;
		TArray<FVector> Start;
		TArray<int32> FootBones;
		bool bFootBonesFound = false;
		FVector StartLocation = FVector::ZeroVector;
		int32 StartSeq = 0;
		float StartFrame = 0.0f;
		double WindowTime = 0.0;
		float WindowBoneMotion = 0.0f;
		bool bWindowOpen = false;
		bool bChannelAdvanced = false;
		int32 MovingWindows = 0;
		int32 AnimatedWindows = 0;
		float MaxBoneMotion = 0.0f;
		float MinFoot = TNumericLimits<float>::Max();
		float MaxFoot = TNumericLimits<float>::Lowest();
		int32 FootSamples = 0;
		int32 LandingSamples = 0;
		int32 CylinderSupportSamples = 0;
		double UnsupportedSeconds = 0.0;
		bool bSeen = false;
	};

	void HandleFrame(float DeltaSeconds);
	void HandlePostActorTick(UWorld* TickedWorld, ELevelTick TickType, float DeltaSeconds);
	void CheckPose(FPoseWatch& Watch, const AScctPawnProxy& Proxy, const FScctInspectPawn* Sim, float DeltaSeconds);
	void CheckFeet(FPoseWatch& Watch, const AScctPawnProxy& Proxy, const FScctInspectPawn& Sim, float Lowest, FName Bone, float DeltaSeconds);
	void CheckCamera(float DeltaSeconds);
	void CheckHud();
	FString NameOf(int32 Id) const;
	static FString Stance(const FScctInspectPawn& Sim);

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UScctSessionSubsystem> Session;
	const FScctInspectApi* Api = nullptr;
	FDelegateHandle FrameHandle;
	FDelegateHandle PostTickHandle;
	int32 Side = 0;
	int32 SessionId = 0;

	FString CurrentStep;
	double Seconds = 0.0;
	int32 Frames = 0;
	TArray<FScctInspectIssue> Issues;
	TMap<FString, int32> IssueIndex;
	bool bFinished = false;
	FString AreaNotes[static_cast<int32>(EScctInspectArea::Count)];

	TArray<FScctInspectPawn> SimPawns;
	TMap<int32, FPoseWatch> Poses;

	// Camera.
	bool bHadCamera = false;
	FVector LastCamera = FVector::ZeroVector;
	int32 CameraFrames = 0;
	int32 CutFrames = 0;
	float LargestCameraMove = 0.0f;

	// HUD.
	int32 HudFrames = 0;
	int32 HudFilledFrames = 0;
	FString LastHudState;

	// Audio and net, kept from the last frame for the summary.
	int32 VoicesAtStart = -1;
	int64 SoundEvents = 0;
	int64 SoundsStarted = 0;
	int64 SoundsUnknown = 0;
	int32 Snaps = 0;
	float WorstSnap = 0.0f;
	int64 CorrectionsApplied = 0;
	int64 CorrectionsReceived = 0;
	int64 ClientTicks = 0;
	int64 MovingTicks = 0;
};
