using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge;

/// <summary>
/// What the script of one SFlicFlocEmitter (rain splashing on a pawn, spec/equipment/rain.md rule 4) set into its
/// sub-emitters, for Unreal to apply to the drawn splashes as the Godot client's Fx/EffectViews.FlicFloc.cs does.
/// Source/OpenSCCT/Runtime/ScctFlicFlocApi.h mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeFlicFloc
{
    /// <summary>The pawn the emitter sits on (FScctBridgePawn.ActorId), 0 when it has none.</summary>
    public int PawnActorId;

    /// <summary>0 spy (SPawnAttaque), 1 merc: which bone table the body splashes use.</summary>
    public int PawnKind;

    /// <summary>Sub-emitter 0, the body splashes: spawns a second, RespawnDeadParticles (0/1) and VariableMaxParticles.</summary>
    public float SplashRate;

    public int SplashRespawn;
    public int SplashBudget;

    /// <summary>Sub-emitter 1, the head drips after the rain: the same three.</summary>
    public float DripRate;

    public int DripRespawn;
    public int DripBudget;

    /// <summary>Times sub-emitter 0 and 1 were restarted, and sub-emitter 2 (the puff) triggered: count changes, each once.</summary>
    public int SplashStarts, DripStarts, PuffTriggers;

    /// <summary>The pawn's velocity (uu a second): a drip starts with its X and Y.</summary>
    public float PawnVelX, PawnVelY, PawnVelZ;
}

/// <summary>
/// The rain-on-pawn table (Source/OpenSCCT/Runtime/ScctFlicFlocApi.h mirrors it). The effect actor list hands out the
/// SFlicFlocEmitter on the pawn's head; this reads what its script set, from the client world. It never writes it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct FlicFlocApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, BridgeFlicFloc*, int> GetFlicFloc;
    public delegate* unmanaged[Cdecl]<int> StandInRain;
}

public static unsafe class FlicFlocExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(FlicFlocApi* api)
    {
        api->Version = ApiVersion;
        api->GetFlicFloc = &GetFlicFloc;
        api->StandInRain = &StandInRain;
        return ApiVersion;
    }

    /// <summary>
    /// The script state of the SFlicFlocEmitter with this client ActorId (an effect actor's id), into
    /// <paramref name="state"/>. Returns 1, or 0 when it is not one or there is no session.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetFlicFloc(int actorId, BridgeFlicFloc* state)
    {
        try
        {
            if (state == null)
            {
                return 0;
            }

            *state = default;
            if (Exports.Current is not { IsDisposed: false } session ||
                session.Session.Client.World.Level.FindActor(actorId) is not SFlicFlocEmitter f || f.bDeleteMe)
            {
                return 0;
            }

            SBasePawn? pawn = f.Pawn;
            state->PawnActorId = pawn?.ActorId ?? 0;
            state->PawnKind = pawn is SPawnAttaque ? 0 : 1;
            state->SplashRate = f.SplashRate;
            state->SplashRespawn = f.SplashRespawn ? 1 : 0;
            state->SplashBudget = f.SplashBudget;
            state->DripRate = f.DripRate;
            state->DripRespawn = f.DripRespawn ? 1 : 0;
            state->DripBudget = f.DripBudget;
            state->SplashStarts = f.SplashStarts;
            state->DripStarts = f.DripStarts;
            state->PuffTriggers = f.PuffTriggers;
            if (pawn is not null)
            {
                state->PawnVelX = pawn.Velocity.X;
                state->PawnVelY = pawn.Velocity.Y;
                state->PawnVelZ = pawn.Velocity.Z;
            }

            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// The check of -ScctLevelDo "rain": stands every living pawn of the local match's server inside the map's rain
    /// volume with the widest floor, a little apart and just above its floor, so the rain splashes on the bodies.
    /// Nothing in normal play calls it. Returns 1 when queued, 0 without a local server.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StandInRain()
    {
        return WorldDebugExports.OnServer(host =>
        {
            Level level = host.World.Level;
            ERainVolume? widest = null;
            float best = 0f;
            foreach (ERainVolume v in level.GetService<RainVolumeList>()?.Volumes ?? [])
            {
                FBox box = v.m_RainVolume;
                float area = (box.Max.X - box.Min.X) * (box.Max.Y - box.Min.Y);
                if (area > best)
                {
                    best = area;
                    widest = v;
                }
            }

            if (widest is null)
            {
                return;
            }

            FBox room = widest.m_RainVolume;
            var centre = new FVector((room.Min.X + room.Max.X) * 0.5f, (room.Min.Y + room.Max.Y) * 0.5f, room.Min.Z + 60f);
            int placed = 0;
            foreach (Pawn p in level.AllActors<Pawn>())
            {
                if (p.bDeleteMe || p.Health <= 0 || p is not SBasePawn)
                {
                    continue;
                }

                for (int attempt = 0; attempt < 12; attempt++)
                {
                    int slot = placed + attempt;
                    var offset = new FVector(((slot % 3) - 1) * 90f, ((slot / 3 % 3) - 1) * 90f, 0f);
                    if (p.SetLocation(centre + offset))
                    {
                        placed = slot + 1;
                        break;
                    }
                }
            }
        });
    }
}
