using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The tutorial message's state (<see cref="HudTutorialApi.GetState"/>). Source/OpenSCCT/Runtime/ScctHudTutorialApi.h
/// mirrors it field for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudTutorialState
{
    /// <summary>1 on a tutorial map (TME or TSP, as GadgetWorld.CurLevelIsTutorial): the end banner is "Finished", not a winner.</summary>
    public int TutorialMap;

    /// <summary>
    /// 1 when the HUD takes tutorial messages: SHud.AddTutoMapMessage stores them only offline (a standalone level or an
    /// offline session), the zone tips' own rule.
    /// </summary>
    public int Offline;

    /// <summary>1 while a tutorial message is up (SHud.sTutoMap longer than 4 characters): the game message queue waits.</summary>
    public int Shown;
}

/// <summary>
/// The tutorial message's table of entry points (Godot's SpyHudPage / MercHudPage DriveTutorial reads SHud.sTutoMap the
/// same way). Unreal asks for <see cref="HudTutorialExports.GetApi"/> once
/// (<c>Host.GetFunction("OpenScct.UnrealBridge.HudTutorialExports", "GetApi")</c>). The text itself crosses as
/// FScctHudState.TutorialId; this table says whether the map is a tutorial and lets an unattended run raise a message
/// through the game's own SHud.AddTutoMapMessage (the zone tips' path), so the box and the queue's wait can be checked
/// on any map.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudTutorialApi
{
    public int Version;

    /// <summary>Fills the state; returns 1 while a session with a local HUD runs.</summary>
    public delegate* unmanaged[Cdecl]<HudTutorialState*, int> GetState;

    /// <summary>
    /// (text): SHud.AddTutoMapMessage(text) on the local player's HUD; an empty text clears the message. 1 when the HUD
    /// now holds that text, 0 when there is no HUD or it refused (not offline).
    /// </summary>
    public delegate* unmanaged[Cdecl]<char*, int> ShowMessage;
}

public static unsafe class HudTutorialExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the tutorial API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudTutorialApi* api)
    {
        api->Version = ApiVersion;
        api->GetState = &GetState;
        api->ShowMessage = &ShowMessage;
        return ApiVersion;
    }

    private static SHud? LocalHud =>
        Exports.Current is { IsDisposed: false } session && session.Session.LocalController is { bDeleteMe: false, myHUD: SHud hud } ? hud : null;

    private static bool IsTutorialMap(string map) =>
        map.StartsWith("TME", StringComparison.OrdinalIgnoreCase) || map.StartsWith("TSP", StringComparison.OrdinalIgnoreCase);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(HudTutorialState* state)
    {
        try
        {
            SHud? hud = LocalHud;
            var read = new HudTutorialState
            {
                TutorialMap = IsTutorialMap(Exports.Current?.Map ?? "") ? 1 : 0,
                Offline = hud?.PlayerOwner is SPlayerController pc &&
                    (pc.Level.NetMode == OpenScct.Core.Engine.ENetMode.NM_Standalone || pc.LocalProfileOptions() is { OfflineSession: true }) ? 1 : 0,
                Shown = hud is { sTutoMap.Length: > 4 } ? 1 : 0,
            };
            if (state != null)
            {
                *state = read;
            }

            return hud is null ? 0 : 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ShowMessage(char* text)
    {
        try
        {
            if (LocalHud is not { } hud)
            {
                return 0;
            }

            string message = text == null ? "" : new string(text);
            hud.AddTutoMapMessage(message);
            return hud.sTutoMap == message ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
