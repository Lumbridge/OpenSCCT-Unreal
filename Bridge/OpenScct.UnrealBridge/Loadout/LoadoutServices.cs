using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using OpenScct.Core;
using OpenScct.Core.Uc;
using OpenScct.Game.Devices;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Hud;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Loadout;

/// <summary>
/// The player's own services on every level the play session loads, as the Godot client's
/// game/Scripts/Play/GearServices.cs installs them: the saved gear (<see cref="StoreLoadoutSource"/>), the HUD line and
/// queue for the server's answer to a gear change (<see cref="QueuedGearFeedback"/>) and the profile choices the
/// script reads (<see cref="PushedProfileOptions"/>). Only the machine's own player reads them (his kit, his
/// controller's profile), so the local match's server level, loaded in this process too, is not affected.
/// </summary>
public static class LoadoutServices
{
    private static int s_installed;

    /// <summary>
    /// Hooks <see cref="Install"/> into <see cref="PlayMapLoader.AddLevelServices"/> once, keeping whatever was hooked
    /// there before. Runs when the bridge assembly loads, so it is in place before any session starts.
    /// </summary>
    public static void Hook()
    {
        if (System.Threading.Interlocked.Exchange(ref s_installed, 1) != 0)
        {
            return;
        }

        Action<Level>? before = PlayMapLoader.AddLevelServices;
        PlayMapLoader.AddLevelServices = level =>
        {
            before?.Invoke(level);
            Install(level);
        };
    }

#pragma warning disable CA2255 // The bridge is loaded once by the Unreal host; the hook must be there before its first map.
    [ModuleInitializer]
    internal static void HookOnLoad() => Hook();
#pragma warning restore CA2255

    /// <summary>Adds the saved gear, the gear change's answer and the profile choices to <paramref name="level"/>.</summary>
    public static void Install(Level level)
    {
        ArgumentNullException.ThrowIfNull(level);
        level.SetService<ILoadoutSource>(new StoreLoadoutSource(level));
        level.SetService<IGearChangeFeedback>(new QueuedGearFeedback(level));
        level.SetService<IProfileOptions>(new PushedProfileOptions());
    }
}

/// <summary>
/// The saved gear (<see cref="LoadoutStore.Current"/>) as the level's <see cref="ILoadoutSource"/>: read each time the
/// server asks this machine for its kit, so a new choice goes on at the next spawn (or at once while the server allows).
/// </summary>
public sealed class StoreLoadoutSource(Level level) : ILoadoutSource
{
    public ClassRef[] Gadgets(bool Spy)
    {
        LoadoutServed.Note(Spy);
        LoadoutChoice saved = LoadoutStore.Current;
        string[] gadgets = saved.GadgetsOf(Spy);
        if (OnlineBridge.Existing?.Client is { } visit && ReferenceEquals(visit.Session?.World.Level, level))
        {
            // Send the effective kit shown by Equipment. A restricted gadget from a practice or
            // higher-ranked kit would otherwise simply be rejected by the server, leaving an empty slot.
            var match = GearMenuModel.For(level.Info.LocalPlayerController as SPlayerController, saved, hub: false, visit.Lobby);
            gadgets = match is not null && match.Spy == Spy ? match.Gadgets : GearMenuModel.ForLobby(Spy, visit.Lobby, saved).Gadgets;
        }

        return Array.ConvertAll(gadgets, p => p.Length == 0 ? default : new ClassRef(p));
    }

    public ClassRef Sidearm() => new(LoadoutStore.Current.MercSidearm);

    public ClassRef Weapon(bool Spy)
    {
        if (Spy)
        {
            return Loadouts.SpyWeapon;
        }

        string merc = LoadoutStore.Current.MercWeapon;
        return merc.Length == 0 ? Loadouts.MercWeapon : new ClassRef(merc);
    }
}

/// <summary>How many times a spawn asked for the saved kit, per side (Unreal logs the kit the pawn then has).</summary>
public static class LoadoutServed
{
    private static int s_spy, s_merc;

    public static int Spy => System.Threading.Volatile.Read(ref s_spy);

    public static int Merc => System.Threading.Volatile.Read(ref s_merc);

    internal static void Note(bool spy)
    {
        if (spy)
        {
            System.Threading.Interlocked.Increment(ref s_spy);
        }
        else
        {
            System.Threading.Interlocked.Increment(ref s_merc);
        }
    }
}

/// <summary>One answer of the server to a gear change.</summary>
public readonly record struct GearAnswer(GearChangeResult Result, float WaitSeconds, string Text);

/// <summary>
/// The server's answer to a gear change: shown in the local player's HUD message area at once (as the Godot client's
/// HudGearFeedback does, through the same SHud message stack the HUD draws), and queued for Unreal to read
/// (<see cref="TryTake"/>), so the gear picker can say it too and the log has it.
/// </summary>
public sealed class QueuedGearFeedback : IGearChangeFeedback
{
    /// <summary>How long the HUD line stays (Godot's HudGearFeedback.ShowSeconds).</summary>
    private const int ShowSeconds = 4;

    /// <summary>Answers kept for Unreal; older ones are dropped past this many.</summary>
    private const int MaxQueued = 16;

    private static readonly Queue<GearAnswer> s_queue = new();
    private static readonly object s_gate = new();

    private readonly Level _level;

    public QueuedGearFeedback(Level level)
    {
        _level = level;
    }

    public void GearChangeAnswered(GearChangeResult Result, float WaitSeconds)
    {
        var answer = new GearAnswer(Result, WaitSeconds, ResultText(Result, WaitSeconds));
        Enqueue(answer);
        if (_level.Info.LocalPlayerController?.myHUD is SHud hud)
        {
            HudMessageRules.AddMessageToStack(hud, answer.Text, ShowSeconds, 0);
        }
    }

    /// <summary>The HUD line for the server's answer (Godot's GearMenuModel.ResultText).</summary>
    public static string ResultText(GearChangeResult result, float waitSeconds) => result switch
    {
        GearChangeResult.Now => "Gear changed.",
        GearChangeResult.Cooldown => $"Wait {Math.Max(1, (int)Math.Ceiling(waitSeconds))} s before changing gear again.",
        _ => "Gear will change on your next spawn.",
    };

    internal static void Enqueue(GearAnswer answer)
    {
        lock (s_gate)
        {
            s_queue.Enqueue(answer);
            while (s_queue.Count > MaxQueued)
            {
                s_queue.Dequeue();
            }
        }
    }

    /// <summary>Takes the oldest answer not read yet.</summary>
    public static bool TryTake(out GearAnswer answer)
    {
        lock (s_gate)
        {
            return s_queue.TryDequeue(out answer);
        }
    }

    /// <summary>Forgets answers not read (a new session starts with none).</summary>
    public static void Clear()
    {
        lock (s_gate)
        {
            s_queue.Clear();
        }
    }
}

/// <summary>
/// The profile choices the script reads in play (<see cref="IProfileOptions"/>), as Unreal last pushed them
/// (<see cref="ProfileOptionValues"/>), read live so a change applies at once: objective sounds (the Audio page's
/// "Objective sounds"), the in-game help (on for a Visit a Map play) and whether this is an offline session (its
/// zone tips show). What the mouse wheel does follows the controls in use (controls.json's mouse_wheel, which the
/// Spy and Mercenary pages set), unless Unreal pushed a value of its own.
/// </summary>
public sealed class PushedProfileOptions : IProfileOptions
{
    public bool ObjectiveSounds => ProfileOptionValues.ObjectiveSounds;

    public byte SelectedMouse => ProfileOptionValues.SelectedMouse;

    public bool DisplayHelpInGame => ProfileOptionValues.DisplayHelpInGame;

    public bool OfflineSession => ProfileOptionValues.OfflineSession;
}

/// <summary>The values behind <see cref="PushedProfileOptions"/>. "Follow" (-1) values are worked out when read.</summary>
public static class ProfileOptionValues
{
    private static volatile bool s_objectiveSounds = true;
    private static volatile bool s_help;
    private static volatile int s_selectedMouse = -1;
    private static volatile int s_offline = -1;

    /// <summary>bObjectiveSounds: the objective jingles play (default on, SPlayerProfile's default).</summary>
    public static bool ObjectiveSounds
    {
        get => s_objectiveSounds;
        set => s_objectiveSounds = value;
    }

    /// <summary>bDisplayHelpInGame: the help paths and help-only zone tips show.</summary>
    public static bool DisplayHelpInGame
    {
        get => s_help;
        set => s_help = value;
    }

    /// <summary>SelectedMouse as pushed: 0 the wheel changes gadgets, 1 it walks and runs, -1 follow the controls.</summary>
    public static int SelectedMousePushed
    {
        get => s_selectedMouse;
        set => s_selectedMouse = value is 0 or 1 ? value : -1;
    }

    /// <summary>OfflineSession as pushed: 1 or 0, or -1 to follow whether a local match (its own host) runs.</summary>
    public static int OfflineSessionPushed
    {
        get => s_offline;
        set => s_offline = value is 0 or 1 ? value : -1;
    }

    /// <summary>SelectedMouse as the script reads it: 0 the wheel changes gadgets, 1 it toggles walk and run.</summary>
    public static byte SelectedMouse => s_selectedMouse switch
    {
        0 => 0,
        1 => 1,
        _ => Controls.ControlsStore.Settings.MouseWheel == "Speed" ? (byte)1 : (byte)0,
    };

    /// <summary>Whether this machine plays an offline session, as the script reads it.</summary>
    public static bool OfflineSession => s_offline switch
    {
        0 => false,
        1 => true,
        // The match behind the main menu has its own host, but the player is not in it.
        _ => PlaySession.OfflineSessionRunning && Exports.Current is not { IsBackdrop: true },
    };
}
