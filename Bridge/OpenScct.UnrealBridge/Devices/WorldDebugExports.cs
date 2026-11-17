using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Server;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Entry points for unattended checks of the level state only (Source/OpenSCCT/Play/ScctLevelStateDebug.h mirrors
/// the table; the game's -ScctLevelDo switch calls them). They make things happen on the SERVER's world of a local
/// match, as a player or a script would, so what Unreal then draws is what came back through replication. Nothing
/// in normal play calls them, and on a remote round (no server here) every entry returns 0 and does nothing.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct WorldDebugApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int> FireMovers;
    public delegate* unmanaged[Cdecl]<int> FireLights;
    public delegate* unmanaged[Cdecl]<float, float, float, int, int> BreakLights;
    public delegate* unmanaged[Cdecl]<float, float, float, int, int> BreakProps;
    public delegate* unmanaged[Cdecl]<int> FireEmitters;
}

public static unsafe class WorldDebugExports
{
    /// <summary>2: FireEmitters added.</summary>
    public const int ApiVersion = 2;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(WorldDebugApi* api)
    {
        api->Version = ApiVersion;
        api->FireMovers = &FireMovers;
        api->FireLights = &FireLights;
        api->BreakLights = &BreakLights;
        api->BreakProps = &BreakProps;
        api->FireEmitters = &FireEmitters;
        return ApiVersion;
    }

    /// <summary>Fires the tag of every mover, as a switch or a trigger would: doors open, lifts start. Returns 1 when queued.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FireMovers() => OnServer(static host =>
    {
        Level level = host.World.Level;
        var tags = new HashSet<Name>();
        foreach (Mover mover in level.AllActors<Mover>())
        {
            tags.Add(mover.Tag);
        }

        Fire(level, tags);
    });

    /// <summary>Fires the tag of every switched light (STriggerLight): each one toggles. Returns 1 when queued.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FireLights() => OnServer(static host =>
    {
        Level level = host.World.Level;
        var tags = new HashSet<Name>();
        foreach (STriggerLight light in level.AllActors<STriggerLight>())
        {
            tags.Add(light.Tag);
        }

        Fire(level, tags);
    });

    /// <summary>
    /// Breaks the <paramref name="count"/> lit switched lights nearest the point, as a shot or a chaff grenade does
    /// (STriggerLight.LightOff(true): half a second of flicker, then dark). Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int BreakLights(float x, float y, float z, int count)
    {
        var near = new FVector(x, y, z);
        return OnServer(host =>
        {
            var lit = new List<STriggerLight>();
            foreach (STriggerLight light in host.World.Level.AllActors<STriggerLight>())
            {
                if (!light.bDeleteMe && light.LightType != ELightType.LT_None)
                {
                    lit.Add(light);
                }
            }

            lit.Sort((a, b) => FVector.DistSquared(a.Location, near).CompareTo(FVector.DistSquared(b.Location, near)));
            for (int i = 0; i < lit.Count && i < count; i++)
            {
                lit[i].LightOff(true);
            }
        });
    }

    /// <summary>
    /// Destroys the <paramref name="count"/> breakable props nearest the point (lamps, vents, glass panes) with one
    /// heavy hit each. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int BreakProps(float x, float y, float z, int count)
    {
        var near = new FVector(x, y, z);
        return OnServer(host =>
        {
            var targets = new List<SGamePlayObject>();
            foreach (SGamePlayObject prop in host.World.Level.AllActors<SGamePlayObject>())
            {
                if (prop.bDamageable && !prop.bDeleteMe && prop is not SObjectiveTrigger)
                {
                    targets.Add(prop);
                }
            }

            targets.Sort((a, b) => FVector.DistSquared(a.Location, near).CompareTo(FVector.DistSquared(b.Location, near)));
            for (int i = 0; i < targets.Count && i < count; i++)
            {
                SGamePlayObject prop = targets[i];
                SDamageType type = prop.TakeDamageTypes.Count > 0 ? prop.TakeDamageTypes[0] : SDamageType.SDTBase;
                prop.eventTakeDamage(10000, null, prop.Location + new FVector(0f, 0f, 4f), new FVector(300f, 0f, 0f), type);
            }
        });
    }

    /// <summary>
    /// Fires the tag of every map Emitter, as the map's events would: the waiting ones start, the running ones
    /// stop, the bursts fire. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FireEmitters() => OnServer(static host =>
    {
        Level level = host.World.Level;
        var tags = new HashSet<Name>();
        foreach (Emitter emitter in level.AllActors<Emitter>())
        {
            tags.Add(emitter.Tag);
        }

        Fire(level, tags);
    });

    private static void Fire(Level level, HashSet<Name> tags)
    {
        foreach (Name tag in tags)
        {
            if (!tag.IsNone)
            {
                level.TriggerEvent(tag, null, null);
            }
        }
    }

    /// <summary>
    /// Runs <paramref name="action"/> once on the local match's server thread, just before its next world step. The
    /// server's world belongs to that thread, so nothing of it is touched from here. A failure there is dropped: these
    /// are checks, and a failed one shows as nothing happening.
    /// </summary>
    private static int OnServer(Action<ServerHost> action)
    {
        try
        {
            if (Exports.Current is not { IsRemote: false, IsDisposed: false } session || session.Session.Host is not { } host)
            {
                return 0;
            }

            void Run(ServerHost running)
            {
                running.BeforeWorldTick -= Run;
                try
                {
                    action(running);
                }
                catch (Exception)
                {
                    // See the summary: a check that fails must not take the server down.
                }
            }

            host.BeforeWorldTick += Run;
            return 1;
        }
        catch (Exception)
        {
            return 0;
        }
    }
}
