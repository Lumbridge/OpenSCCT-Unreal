using System;
using System.Collections.Generic;
using System.Linq;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>How a key that already does something takes a new action (the bindings editor's conflict answers).</summary>
public enum BindingCommitMode
{
    /// <summary>The key was free (or the player chose to keep what it does): the action is added to it.</summary>
    Share = 0,

    /// <summary>The key does only the new action; what it did before is dropped from it.</summary>
    Replace = 1,

    /// <summary>The key and the slot being changed trade what they do.</summary>
    Swap = 2,
}

/// <summary>
/// Edits of one key layout by action, ported from the Godot client's ActionBindingDraft (game/Scripts/UI/ControlActions.cs).
/// A key's binding is a list of commands ("Button bFire | Function Fire | GadgetQuickChoice 0"), so an action is on
/// every key whose list holds one of its parts, and taking an action off a key keeps the key's other commands. The
/// layouts here have no aliases (the bridge writes commands out), so nothing has to be expanded first.
/// </summary>
public static class BindingDraft
{
    /// <summary>The keys of an action, in its kept order (the first is its primary key).</summary>
    public static IReadOnlyList<string> Keys(KeyLayout layout, ControlActionInfo action)
    {
        ArgumentNullException.ThrowIfNull(layout);
        ArgumentNullException.ThrowIfNull(action);
        return layout.InputOrder(action.Id, layout.Bindings.Where(b => Parts(b.Value).Any(action.Matches)).Select(b => b.Key));
    }

    public static string CommandOf(KeyLayout layout, string key) =>
        layout.Bindings.TryGetValue(Canonical(key), out string? command) ? command : "";

    /// <summary>
    /// What a key does, by the names of the actions in its binding: the side's own names first, then any action's,
    /// then the command itself for a part no action has (DisplayHud).
    /// </summary>
    public static string Describe(KeyLayout layout, string key, bool merc)
    {
        string command = CommandOf(layout, key);
        ControlActionInfo[] role = ControlActionList.ForRole(merc).ToArray();
        var names = new List<string>();
        foreach (string part in Parts(command))
        {
            string name = role.FirstOrDefault(a => a.Matches(part))?.Label ?? ControlActionList.All.FirstOrDefault(a => a.Matches(part))?.Label ?? part;
            if (!names.Contains(name))
            {
                names.Add(name);
            }
        }

        return names.Count > 0 ? string.Join(" / ", names) : command;
    }

    /// <summary>Takes the action off <paramref name="key"/> (every key when null); the keys' other commands stay.</summary>
    public static void Remove(KeyLayout layout, ControlActionInfo action, string? key = null)
    {
        string[] order = Keys(layout, action).ToArray();
        string[] removed = order.Where(k => key is null || k.Equals(Canonical(key), StringComparison.OrdinalIgnoreCase)).ToArray();
        if (removed.Length == 0) return;
        foreach (string k in removed)
        {
            layout.Bind(k, string.Join(" | ", Parts(CommandOf(layout, k)).Where(p => !action.Matches(p))));
        }

        layout.SetInputOrder(action.Id, order.Except(removed, StringComparer.OrdinalIgnoreCase));
    }

    /// <summary>Adds one more key to the action, keeping what the key did unless <paramref name="replaceDestination"/>.</summary>
    public static void Add(KeyLayout layout, ControlActionInfo action, string key, bool replaceDestination = false)
    {
        ArgumentException.ThrowIfNullOrEmpty(key);
        key = Canonical(key);
        string existing = replaceDestination ? "" : CommandOf(layout, key);
        if (Parts(existing).Any(action.Matches)) return;
        string[] order = Keys(layout, action).ToArray();
        layout.Bind(key, Join(existing, action.Command));
        layout.SetInputOrder(action.Id, order.Append(key));
    }

    /// <summary>Moves one of the action's keys to another; its other keys, and other commands on the old key, stay.</summary>
    public static void Replace(KeyLayout layout, ControlActionInfo action, string oldKey, string newKey, bool replaceDestination = false)
    {
        ArgumentException.ThrowIfNullOrEmpty(oldKey);
        ArgumentException.ThrowIfNullOrEmpty(newKey);
        oldKey = Canonical(oldKey);
        newKey = Canonical(newKey);
        if (oldKey.Equals(newKey, StringComparison.OrdinalIgnoreCase)) return;
        string[] order = Keys(layout, action).ToArray();
        Remove(layout, action, oldKey);
        Add(layout, action, newKey, replaceDestination);
        layout.SetInputOrder(action.Id, order.Select(k => k.Equals(oldKey, StringComparison.OrdinalIgnoreCase) ? newKey : k).Append(newKey));
    }

    /// <summary>The action goes to <paramref name="newKey"/> and what that key did goes to <paramref name="oldKey"/>.</summary>
    public static void Swap(KeyLayout layout, ControlActionInfo action, string oldKey, string newKey)
    {
        ArgumentException.ThrowIfNullOrEmpty(oldKey);
        ArgumentException.ThrowIfNullOrEmpty(newKey);
        oldKey = Canonical(oldKey);
        newKey = Canonical(newKey);
        if (oldKey.Equals(newKey, StringComparison.OrdinalIgnoreCase)) return;
        string[] order = Keys(layout, action).ToArray();
        string displaced = CommandOf(layout, newKey);
        Remove(layout, action, oldKey);
        layout.Bind(oldKey, Join(CommandOf(layout, oldKey), displaced));
        layout.Bind(newKey, action.Command);
        layout.SetInputOrder(action.Id, order.Select(k => k.Equals(oldKey, StringComparison.OrdinalIgnoreCase) ? newKey : k).Append(newKey));
    }

    /// <summary>
    /// A swap would put a held action (team ping, mine hack) on a wheel direction, which has no release
    /// (OptionsMenu.CanSwapKeyboardInput).
    /// </summary>
    public static bool CanSwap(KeyLayout layout, string from, string to) => !BindingKey.IsPulseName(to) ||
        !KeyLayout.Resolve(CommandOf(layout, from)).Any(a =>
            a.Kind == KeyActionKind.Button && (a.Name.Equals(ControlActionList.MercFreeLookButton, StringComparison.OrdinalIgnoreCase) ||
                a.Name.Equals(MercBindings.AimButton, StringComparison.OrdinalIgnoreCase)) ||
            a.Kind == KeyActionKind.Command && (a.Name.Equals(ControlActionList.PingCommand, StringComparison.OrdinalIgnoreCase) ||
             a.Name.Equals("UseGadget", StringComparison.OrdinalIgnoreCase) && a.Arg == SGadget.BindMineHack));

    public static string Canonical(string key) => BindingKey.TryParse(key, out BindingKey parsed) ? parsed.Canonical : key;

    private static IEnumerable<string> Parts(string command) =>
        command.Split('|', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);

    private static string Join(string first, string second) => string.Join(" | ", new[] { first, second }.Where(s => s.Length > 0));
}

/// <summary>
/// The Options screen's edits of the key bindings. The screen edits a copy of the bindings in play (the draft); APPLY
/// puts the copy in play (a running match plays the new keys from its next frame), SAVE writes bindings.json, and
/// leaving without saving goes back to what the file has. Both sides are in the one draft, with the split (one key
/// layout per side, or the spy's for both).
/// </summary>
public static class BindingsEditor
{
    private static ControlBindings? s_draft;
    private static string s_baseJson = "";

    // Comparisons are asked every drawn frame; the JSON is only made again when something changed.
    private static (object? Bindings, object Spy, int SpyRevision, object Merc, int MercRevision, object SpyPad, object MercPad, bool Split) s_liveKey;
    private static string s_liveJson = "";
    private static int s_draftRevision;
    private static int s_draftJsonRevision = -1;
    private static string s_draftJson = "";

    public static ControlBindings Draft => s_draft ?? Begin();

    /// <summary>
    /// Starts editing from the bindings in play, unless edits are already held (the screen was rebuilt): those are
    /// kept. Returns the draft.
    /// </summary>
    public static ControlBindings Begin()
    {
        if (s_draft is null || !HasEdits)
        {
            s_draft = ControlsStore.Bindings.Clone();
            s_baseJson = LiveJson();
            Touch();
        }

        return s_draft;
    }

    /// <summary>The draft differs from what was in play when it was started or last applied.</summary>
    public static bool HasEdits => s_draft is not null && DraftJson() != s_baseJson;

    /// <summary>What is in play differs from the file.</summary>
    public static bool HasUnsaved => ControlsStore.SavedBindingsJson is { } saved && LiveJson() != saved;

    /// <summary>Puts the draft in play. A running match takes the new layout on its next frame (ControlsFeature.Frame).</summary>
    public static void Apply()
    {
        if (s_draft is null) return;
        if (!s_draft.Split)
        {
            s_draft.SetSplit(false);
        }

        ControlsStore.Bindings = s_draft.Clone();
        s_baseJson = LiveJson();
        Touch();
    }

    /// <summary>Drops the draft and puts the file's bindings back in play.</summary>
    public static void Discard()
    {
        s_draft = null;
        if (ControlsStore.SavedBindingsJson is { } saved && LiveJson() != saved)
        {
            ControlsStore.Bindings = ControlBindings.FromJson(saved, ControlsStore.Settings.MouseWheel);
        }
    }

    /// <summary>The draft's key layout of a side (the spy's for both while shared).</summary>
    public static KeyLayout LayoutOf(bool merc) => Draft.KeysOf(merc);

    public static void SetSplit(bool split)
    {
        Draft.SetSplit(split);
        Touch();
    }

    public static void ResetKeys(bool merc)
    {
        Draft.ResetKeys(merc, ControlsStore.Settings.MouseWheel);
        Touch();
    }

    /// <summary>Why <paramref name="key"/> cannot be bound to the action on this side, or "" when it can.</summary>
    public static string Check(bool merc, string actionId, string key)
    {
        if (ControlActionList.Find(actionId) is not { } action || !(merc ? action.Merc : action.Spy))
        {
            return $"'{actionId}' is not an action of the {(merc ? "mercenary" : "spy")}.";
        }

        if (!BindingKey.TryParse(key, out BindingKey parsed))
        {
            return $"'{key}' is not the name of a key.";
        }

        bool doubleTap = parsed.Modifiers.HasFlag(BindingModifiers.Double);
        if (doubleTap && !ControlActionList.SupportsDoubleTap(action))
        {
            return action.Label + " is read from the plain key and cannot take a double tap.";
        }

        if (doubleTap && parsed.IsPulse)
        {
            return "A double tap needs a key or mouse button, not the mouse wheel.";
        }

        if (parsed.IsPulse && action.Id is ControlActionList.PingId or ControlActionList.MineHackId or ControlActionList.MercFreeLookId or MercBindings.AimId)
        {
            return action.Label + " needs a key or mouse button you can hold.";
        }

        if (ControlActionList.NeedsKeyboardKey(action) && parsed.Base.Contains("Mouse", StringComparison.OrdinalIgnoreCase))
        {
            return "This action requires a keyboard key. Choose a key or keyboard combination.";
        }

        return "";
    }

    /// <summary>
    /// What the key does now apart from the action: "" when it is free or already the action's. CanSwap: a swap with
    /// <paramref name="oldKey"/> is possible (there is a slot being changed, and a swap would not put a held action
    /// on the wheel).
    /// </summary>
    public static string Conflict(bool merc, string actionId, string? oldKey, string key, out bool alreadyBound, out bool canSwap)
    {
        alreadyBound = false;
        canSwap = false;
        if (ControlActionList.Find(actionId) is not { } action)
        {
            return "";
        }

        KeyLayout layout = LayoutOf(merc);
        string canonical = BindingDraft.Canonical(key);
        alreadyBound = BindingDraft.Keys(layout, action).Contains(canonical, StringComparer.OrdinalIgnoreCase);
        if (alreadyBound || BindingDraft.CommandOf(layout, canonical).Length == 0)
        {
            return "";
        }

        canSwap = !string.IsNullOrEmpty(oldKey) && BindingDraft.CanSwap(layout, canonical, BindingDraft.Canonical(oldKey));
        return BindingDraft.Describe(layout, canonical, merc);
    }

    /// <summary>
    /// Binds the key to the action in place of <paramref name="oldKey"/> (as one more key when that is empty). False
    /// with the reason for a key the action cannot take or a swap that is not possible.
    /// </summary>
    public static bool Commit(bool merc, string actionId, string? oldKey, string key, BindingCommitMode mode, out string error)
    {
        error = Check(merc, actionId, key);
        if (error.Length > 0)
        {
            return false;
        }

        ControlActionInfo action = ControlActionList.Find(actionId)!;
        KeyLayout layout = LayoutOf(merc);
        string canonical = BindingDraft.Canonical(key);
        if (string.Equals(oldKey, canonical, StringComparison.OrdinalIgnoreCase) || BindingDraft.Keys(layout, action).Contains(canonical, StringComparer.OrdinalIgnoreCase))
        {
            return true;
        }

        bool replace = mode == BindingCommitMode.Replace;
        if (mode == BindingCommitMode.Swap)
        {
            if (string.IsNullOrEmpty(oldKey) || !BindingDraft.CanSwap(layout, canonical, oldKey))
            {
                error = "Only a key that is already bound can be swapped, and not onto a mouse wheel direction while it holds a held action.";
                return false;
            }

            BindingDraft.Swap(layout, action, oldKey, canonical);
        }
        else if (string.IsNullOrEmpty(oldKey))
        {
            BindingDraft.Add(layout, action, canonical, replace);
        }
        else
        {
            BindingDraft.Replace(layout, action, oldKey, canonical, replace);
        }

        Touch();
        return true;
    }

    /// <summary>Takes the action off one key (its other keys and the key's other commands stay).</summary>
    public static void Clear(bool merc, string actionId, string key)
    {
        if (ControlActionList.Find(actionId) is { } action)
        {
            BindingDraft.Remove(LayoutOf(merc), action, key);
            Touch();
        }
    }

    /// <summary>The keys the draft has for an action on a side, primary first.</summary>
    public static IReadOnlyList<string> KeysOf(bool merc, string actionId) =>
        ControlActionList.Find(actionId) is { } action ? BindingDraft.Keys(LayoutOf(merc), action) : Array.Empty<string>();

    /// <summary>
    /// The side's rows as text for the editor: one line per action, fields split by tabs: id, category, label,
    /// description, double tap allowed (1/0), then its keys, primary first.
    /// </summary>
    public static string Rows(bool merc)
    {
        KeyLayout layout = LayoutOf(merc);
        var text = new System.Text.StringBuilder();
        foreach (ControlActionInfo action in ControlActionList.ForRole(merc))
        {
            text.Append(action.Id).Append('\t').Append(action.Category).Append('\t').Append(action.Label).Append('\t')
                .Append(action.Description).Append('\t').Append(ControlActionList.SupportsDoubleTap(action) ? '1' : '0');
            foreach (string key in BindingDraft.Keys(layout, action))
            {
                text.Append('\t').Append(key);
            }

            text.Append('\n');
        }

        return text.ToString();
    }

    private static void Touch() => s_draftRevision++;

    private static string DraftJson()
    {
        if (s_draft is null) return "";
        if (s_draftJsonRevision != s_draftRevision)
        {
            s_draftJson = s_draft.ToJson();
            s_draftJsonRevision = s_draftRevision;
        }

        return s_draftJson;
    }

    private static string LiveJson()
    {
        ControlBindings live = ControlsStore.Bindings;
        (object? Bindings, object Spy, int SpyRevision, object Merc, int MercRevision, object SpyPad, object MercPad, bool Split) key = (live, live.Spy.Keys, live.Spy.Keys.Revision, live.Merc.Keys, live.Merc.Keys.Revision, live.Spy.Pad, live.Merc.Pad, live.Split);
        if (!Equals(key, s_liveKey) || s_liveJson.Length == 0)
        {
            s_liveKey = key;
            s_liveJson = live.ToJson();
        }

        return s_liveJson;
    }
}
