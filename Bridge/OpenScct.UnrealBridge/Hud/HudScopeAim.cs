using OpenScct.Core;
using OpenScct.Game.SBase;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// The merc's scope aim point (the point 1000 uu out along MyDesiredRotation from the eye, as the HUD reader gives it)
/// sampled after every client tick and given between the last two samples at the frame's interpolation alpha, so the
/// scope's sights move with the drawn camera instead of stepping at the tick against it (the Godot client's
/// HudBinocularAim). A jump longer than <see cref="CutDistance"/> (a respawn) is given at once.
/// </summary>
public sealed class HudScopeAim
{
    public const float CutDistance = 512f;

    /// <summary>How far out along the aim the point is (SHud s_H_Binoculars_Def.PostRender).</summary>
    public const float AimDistance = 1000f;

    private readonly PlaySession _session;
    private FVector _previous;
    private FVector _current;
    private int _samples;

    private HudScopeAim(BridgeSession bridge)
    {
        _session = bridge.Session;
        bridge.Ticked += OnTicked;
        OnTicked();
    }

    public static HudScopeAim Of(BridgeSession bridge) => bridge.Feature(static b => new HudScopeAim(b));

    private void OnTicked()
    {
        SPlayerController? pc = _session.LocalController;
        if (pc?.Pawn is not { bDeleteMe: false } pawn)
        {
            _samples = 0;
            return;
        }

        pc.MyDesiredRotation.GetAxes(out FVector x, out _, out _);
        _previous = _current;
        _current = pawn.Location + OpenScct.Game.Combat.CombatServices.Ports(pc).EyePosition(pawn) + x * AimDistance;
        _samples = _samples < 2 ? _samples + 1 : 2;
    }

    /// <summary>The aim point <paramref name="alpha"/> of the way from the previous tick's to the last one's, or null before a sample.</summary>
    public FVector? At(float alpha)
    {
        if (_samples == 0)
        {
            return null;
        }

        if (_samples == 1 || (_current - _previous).SizeSquared() > CutDistance * CutDistance)
        {
            return _current;
        }

        return _previous + (_current - _previous) * System.Math.Clamp(alpha, 0f, 1f);
    }
}
