#pragma once

#include "CoreMinimal.h"

// The flat interface of the C# bridge. These mirror Bridge/OpenScct.UnrealBridge/BridgeTypes.cs and Exports.cs
// field for field; change both together and bump the version.

constexpr int32 ScctBridgeApiVersion = 1;
constexpr int32 ScctButtonCount = 20;

/** Indices into FScctBridgeInput::Buttons, in the order of the simulation's LocalInput.ButtonNames. */
enum class EScctButton : uint8
{
	Up, Down, Left, Right, PrevItem, NextItem, Fire, AltFire, Crouch, Roll, Using, FireMode, BackToWall, Reload,
	RateOfFire, HoldBreath, SpecialsChat, ZoomIn, ZoomOut, Cloak,
};

/** One pawn as it should be drawn this frame. Simulation units are Unreal centimetres, same axes. */
struct FScctBridgePawn
{
	int32 ActorId;
	int32 Kind; // 0 spy, 1 merc
	int32 Team;
	int32 Health;
	int32 Flags; // EScctPawnFlags
	float X, Y, Z;
	float PrePivotX, PrePivotY, PrePivotZ;
	float Yaw; // degrees
	float CollisionRadius, CollisionHeight;
};

namespace EScctPawnFlags
{
	constexpr int32 Visible = 1;
	constexpr int32 Local = 2;
}

/** The local player's camera this frame. Angles in degrees. */
struct FScctBridgeView
{
	float X, Y, Z;
	float Pitch, Yaw, Roll;
	float Fov;
	int32 Valid;
	int32 SpyFirstPerson;
};

struct FScctBridgeInput
{
	uint8 Buttons[ScctButtonCount];
	float MouseX, MouseY; // counts since the last call, Y positive up
	float Up;
};

enum class EScctBridgeState : int32
{
	Idle = 0,
	Playing = 1,
	Failed = 2,
};

struct FScctBridgeApi
{
	int32 Version;
	int32 (__cdecl* Configure)(const TCHAR* ContentRoot);
	int32 (__cdecl* StartLocal)(const TCHAR* Map, const TCHAR* Mode, int32 bMerc, int32 Bots);
	void (__cdecl* Stop)();
	int32 (__cdecl* GetState)();
	int32 (__cdecl* Update)();
	void (__cdecl* SetInput)(const FScctBridgeInput* Input);
	void (__cdecl* Command)(const TCHAR* Name, int32 Arg);
	int32 (__cdecl* GetPawns)(FScctBridgePawn* Pawns, int32 Capacity);
	int32 (__cdecl* GetView)(FScctBridgeView* View);
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};
