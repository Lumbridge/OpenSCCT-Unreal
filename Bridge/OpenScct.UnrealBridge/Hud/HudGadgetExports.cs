using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The camera a gadget view looks through (<see cref="HudGadgetApi.GetCameraView"/>): the sticky cam the spy rides or the
/// network camera the merc watches. Source/OpenSCCT/Runtime/ScctHudGadgetApi.h mirrors it field for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudCameraView
{
    /// <summary>As <c>HudState.GadgetView</c>: 0 none, 1 sticky cam (spy), 2 camera network (merc).</summary>
    public int View;

    /// <summary>The camera's field of view now and its zoom limits, in degrees (fVisionFOV, fMinFOV, fMaxFOV).</summary>
    public float Fov;

    public float MinFov;

    public float MaxFov;

    /// <summary>Gas doses left in the sticky cam (Smoke_NbDoses), -1 for a camera of the network (it has no gas).</summary>
    public int GasDoses;

    /// <summary>Seconds until the next dose can be released (NextUseTime), 0 when it can go now.</summary>
    public float GasReadySeconds;
}

/// <summary>
/// The gadget views' table of entry points, beside the HUD's own (<see cref="HudExports"/>): what the sticky cam and
/// camera network pages show that the HUD state does not carry, and an unattended check's way into the gadget menus.
/// Unreal asks for <see cref="GetApi"/> once (<c>Host.GetFunction("OpenScct.UnrealBridge.HudGadgetExports", "GetApi")</c>).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudGadgetApi
{
    public int Version;

    /// <summary>Fills the camera of the gadget view; 1 while the local player looks through one, else 0 (and View 0).</summary>
    public delegate* unmanaged[Cdecl]<HudCameraView*, int> GetCameraView;

    /// <summary>
    /// Unattended checks only: selects a gadget through the gadget keys' own selection (1 the sticky cam, 2 the mine
    /// launcher, 3 the camera network). Returns 1 when that gadget is the selected one.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, int> SelectGadget;

    /// <summary>
    /// Unattended checks only: selects the gadget of the menu (1 the sticky cam, 2 the mine launcher) and puts the local
    /// controller in that menu's state (s_StickyCam_MenuInteraction, s_PutMineOnWall_InteractionMenu), as the original
    /// does when the gadget's Alt-fire is held. Both menus act when Alt-fire is up: the controller's bAltFire is set for the
    /// next tick and the caller holds the key from then on. Returns 1
    /// when the controller is in the menu, 0 when the pawn has no such gadget or the state was refused.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, int> ForceMenu;
}

public static unsafe class HudGadgetExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the gadget view API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudGadgetApi* api)
    {
        api->Version = ApiVersion;
        api->GetCameraView = &GetCameraView;
        api->SelectGadget = &SelectGadget;
        api->ForceMenu = &ForceMenu;
        return ApiVersion;
    }

    private static SPlayerController? Controller => Exports.Current?.Session.LocalController is { bDeleteMe: false } pc ? pc : null;

    /// <summary>The camera of the local player's gadget view, as <see cref="GetCameraView"/> fills it (the probe calls this).</summary>
    public static HudCameraView ReadCameraView(SPlayerController? pc)
    {
        var view = new HudCameraView { GasDoses = -1 };
        GadgetView kind = GadgetHudRules.ViewOf(pc);
        if (kind == GadgetView.None || pc?.SBPawn?.UsedObject is not SStickyCam cam)
        {
            return view;
        }

        view.View = (int)kind;
        view.Fov = cam.fVisionFOV;
        view.MinFov = cam.fMinFOV;
        view.MaxFov = cam.fMaxFOV;
        if (cam is not SCamNetwork)
        {
            view.GasDoses = Math.Max(cam.Smoke_NbDoses, 0);
            view.GasReadySeconds = Math.Max(cam.NextUseTime - cam.Level.TimeSeconds, 0f);
        }

        return view;
    }

    /// <summary>What <see cref="SelectGadget"/> does: 1 the sticky cam, 2 the mine launcher, 3 the camera network.</summary>
    public static bool Select(SPlayerController? pc, int kind)
    {
        if (pc?.SBPawn is not { } pawn)
        {
            return false;
        }

        // The gadget keys' own selection, slot by slot, until the wanted gadget is the selected one.
        bool Wanted(SGadget? g) => kind switch
        {
            1 => g is SGadStickyCam,
            2 => g is SGadMultiMinesLauncher,
            3 => g is SGadCamNetwork,
            _ => false,
        };
        for (int slot = 0; slot < 8 && !Wanted(pawn.SelectedGadget); slot++)
        {
            pc.SelectGadget(slot);
        }

        return Wanted(pawn.SelectedGadget);
    }

    /// <summary>What <see cref="ForceMenu"/> does, for the probe: 1 sticky cam menu, 2 mine menu.</summary>
    public static bool EnterMenu(SPlayerController? pc, int menu)
    {
        if (pc is null || menu is not (1 or 2) || !Select(pc, menu))
        {
            return false;
        }

        // The menu acts on Alt-fire's release: held for the next tick, until the caller's key takes over.
        pc.bAltFire = 1;
        if (menu == 1)
        {
            pc.GotoStickyInteractionMenu();
            return pc.IsInState(GadgetStates.s_StickyCam_MenuInteraction);
        }

        pc.eventChangeState(GadgetStates.s_PutMineOnWall_InteractionMenu);
        return pc.GetStateName() == GadgetStates.s_PutMineOnWall_InteractionMenu;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetCameraView(HudCameraView* view)
    {
        try
        {
            HudCameraView read = ReadCameraView(Controller);
            if (view != null)
            {
                *view = read;
            }

            return read.View != 0 ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SelectGadget(int kind)
    {
        try
        {
            return Select(Controller, kind) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ForceMenu(int menu)
    {
        try
        {
            return EnterMenu(Controller, menu) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
