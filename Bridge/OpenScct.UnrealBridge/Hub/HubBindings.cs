using System;
using System.Collections.Generic;
using System.Linq;
using OpenScct.UnrealBridge.Controls;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// The hub-only commands (port of the Godot client's game/Scripts/Input/HubBindings.cs). They share the saved key
/// layouts and the action editor with everything else: each one is a <see cref="ControlAction"/> of the "Hub" category,
/// and a layout without them gets each on its default key, after what that key already does (J, free in the default
/// layout, becomes "HubSpy"; K keeps the spy's cloak and becomes "Button bCloak | HubMerc"). The input mapper
/// raises them as interface commands like any command it does not know, so they reach Unreal through
/// AScctPlayerController::OnLocalCommand; only the hub's HUD (Source/OpenSCCT/Hub/ScctHubHud) acts on them, so in a
/// match they do nothing.
/// </summary>
public static class HubBindings
{
    /// <summary>The version a bindings file records once it has the hub's keys (its "hub" field), so a key the player unbound stays unbound.</summary>
    public const int Version = 1;

    /// <summary>The category of the hub's actions in the action editor.</summary>
    public const string Category = "Hub";

    public static readonly IReadOnlyList<(string Id, string Command, string Key, string Label)> All =
    [
        ("hub_travel", "HubTravel", "F4", "Travel"),
        ("hub_spy", "HubSpy", "J", "Play as spy"),
        ("hub_merc", "HubMerc", "K", "Play as mercenary"),
        ("hub_plaza", "HubPlaza", "N", "Return to plaza"),
        ("hub_emotes", "HubEmotes", "G", "Emote menu"),
        ("hub_use", "HubUse", "U", "Ready up / use activity"),
        ("hub_cancel", "HubCancel", "X", "Cancel activity"),
        ("hub_watch", "HubWatch", "H", "Watch queued match"),
        ("hub_players", "HubPlayers", "P", "Player list"),
        ("hub_armoury", "HubArmoury", "O", "Armoury"),
        .. Enumerable.Range(1, 8).Select(i => ($"hub_emote{i}", $"HubEmote{i}", i.ToString(System.Globalization.CultureInfo.InvariantCulture), $"Choose emote {i}")),
    ];

    /// <summary>The hub's commands as actions of the action editor (and of a controller button: "Action:hub_spy").</summary>
    public static IEnumerable<ControlAction> Actions => All.Select(static b => new ControlAction(b.Id, Category, b.Label, b.Command));

    /// <summary>True for a command only the hub acts on.</summary>
    public static bool IsHubCommand(string command) => command.StartsWith("Hub", StringComparison.Ordinal);

    /// <summary>
    /// Puts every hub command the layout does not have anywhere on its default key, after what that key already does
    /// (HubBindings.Upgrade): the hub consumes them only in its own context, and the combat or traversal action sharing
    /// that physical key keeps working everywhere else. Enter opens the chat when it is free.
    /// </summary>
    public static void Upgrade(KeyLayout layout)
    {
        ArgumentNullException.ThrowIfNull(layout);
        foreach (var binding in All)
        {
            if (Bound(layout, binding.Command))
            {
                continue;
            }

            string previous = layout.Bindings.GetValueOrDefault(binding.Key, "");
            layout.Bind(binding.Key, previous.Length == 0 ? binding.Command : previous + " | " + binding.Command);
        }

        if (!layout.Bindings.ContainsKey("Enter"))
        {
            layout.Bind("Enter", "Talk");
        }
    }

    /// <summary>The first key of <paramref name="layout"/> bound to <paramref name="command"/>, or "Unbound".</summary>
    public static string Hint(KeyLayout? layout, string command)
    {
        if (layout is null)
        {
            return "Unbound";
        }

        foreach (string key in layout.Bindings.Keys.OrderBy(static k => k.Length).ThenBy(static k => k, StringComparer.Ordinal))
        {
            foreach (KeyAction action in layout.Actions(key))
            {
                if (action.Kind == KeyActionKind.Command && action.Name.Equals(command, StringComparison.OrdinalIgnoreCase))
                {
                    return key;
                }
            }
        }

        return "Unbound";
    }

    private static bool Bound(KeyLayout layout, string command)
    {
        foreach (string key in layout.Bindings.Keys)
        {
            foreach (KeyAction action in layout.Actions(key))
            {
                if (action.Kind == KeyActionKind.Command && action.Name.Equals(command, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
        }

        return false;
    }
}
