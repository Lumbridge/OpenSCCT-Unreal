using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using OpenScct.Net.Cosmetics;
using OpenScct.Net.Discovery;
using OpenScct.UnrealBridge.Hub;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// The Armoury's choice for this run (the Godot client's GameSettings.CosmeticsSpy and CosmeticsMerc): the chosen
/// cosmetic ids per side. Unreal's settings registry keeps them between runs and hands them over at start; the
/// online and hub bridges read the look they send from here.
/// </summary>
internal static class ArmouryLook
{
    public static string[] Spy { get; set; } = [];

    public static string[] Merc { get; set; } = [];

    public static string[] Of(CosmeticSide side) => side == CosmeticSide.Spy ? Spy : Merc;

    /// <summary>The chosen look of a side (GameSettings.OwnLook).</summary>
    public static CosmeticLook Own(CosmeticSide side) => CosmeticLook.FromIds(side, Of(side));

    /// <summary>The look sent to servers: the chosen one, or retail while "Hide my cosmetics" is on (GameSettings.SharedLook).</summary>
    public static CosmeticLook Shared(CosmeticSide side) => PawnLooks.HideMine ? CosmeticLook.Default : Own(side);

    /// <summary>The ids of a list that are items of <paramref name="side"/>, one per slot (the last wins), sorted.</summary>
    public static string[] Clean(CosmeticSide side, IEnumerable<string> ids)
    {
        var bySlot = new Dictionary<CosmeticSlot, string>();
        foreach (string id in ids)
        {
            if (CosmeticCatalog.Find(id.Trim()) is { } item && item.Side == side)
            {
                bySlot[item.Slot] = item.Id;
            }
        }

        return bySlot.Values.Order(StringComparer.Ordinal).ToArray();
    }
}

/// <summary>
/// The appearance part of <see cref="AccountService"/> (a port of the Godot client's AccountService.Appearance and
/// the model of its ArmouryScreen): the chosen cosmetics and title follow the account with revision-checked writes
/// (pulled every 30 s while in the menus, a lobby or a hub; sent 3 s after a choice), a change made elsewhere while
/// this PC has unsent choices is a conflict the Armoury page resolves (reload the account's, or reapply this PC's),
/// and the look is sent to the lobby or hub the player is in whenever it changes.
/// </summary>
public sealed partial class AccountService
{
    private sealed record AppearanceSnapshot(AccountInfo Account, AccountAppearance? Appearance);

    private bool? _appearanceSupported;
    private long _appearanceAccount;
    private string _appearanceMaster = string.Empty;
    private AccountAppearance? _appearanceBaseline;
    private AccountAppearance? _appearanceRemote;
    private AccountAppearance? _observedAppearance;
    private AccountAppearance? _appearanceSending;
    private Task<AppearanceSnapshot>? _appearancePull;
    private Task<AppearanceUpdateResult>? _appearancePush;
    private long _appearanceNextPoll;
    private long _appearancePushAt;
    private bool _appearanceDirty;
    private (string Spy, string Merc)? _lookSent;
    private object? _lookSentTo;

    public string AppearanceStatus { get; private set; } = string.Empty;

    public bool AppearanceConflict { get; private set; }

    public bool AppearanceManaged => _appearanceSupported == true;

    /// <summary>Goes up each time the account's title was taken: Unreal then stores <see cref="AppearanceTitle"/> as the chosen title.</summary>
    public int AppearanceTitleSerial { get; private set; }

    public string AppearanceTitle { get; private set; } = string.Empty;

    private bool CanSyncAppearance => !_syncOff && !Preview && SignedIn && Account is not null;

    private AccountAppearance LocalAppearance() => new()
    {
        Spy = ArmouryLook.Spy.Order(StringComparer.Ordinal).ToArray(),
        Merc = ArmouryLook.Merc.Order(StringComparer.Ordinal).ToArray(),
        Title = _chosenTitle,
    };

    private static bool SameAppearance(AccountAppearance a, AccountAppearance b) => a.Title == b.Title &&
        a.Spy.Order(StringComparer.Ordinal).SequenceEqual(b.Spy.Order(StringComparer.Ordinal)) &&
        a.Merc.Order(StringComparer.Ordinal).SequenceEqual(b.Merc.Order(StringComparer.Ordinal));

    /// <summary>Not while a round loads or runs: a look changes between rounds (a hub takes it at once).</summary>
    private static bool SafeForAppearance()
    {
        if (OnlineBridge.Existing?.State is OnlineState.Loading or OnlineState.Match)
        {
            return false;
        }

        return Exports.Current is not { IsDisposed: false } || OnlineBridge.Existing?.Client is not null || HubBridge.Existing?.Client is not null;
    }

    // ---- the Armoury's choices ------------------------------------------------------------------------------------

    /// <summary>The saved choice of a side from Unreal's settings at start (ids joined by commas): no change to send.</summary>
    public void SetChosenCosmetics(CosmeticSide side, string? ids)
    {
        string[] clean = ArmouryLook.Clean(side, (ids ?? string.Empty).Split([',', '+'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries));
        if (side == CosmeticSide.Spy) ArmouryLook.Spy = clean;
        else ArmouryLook.Merc = clean;
        _observedAppearance = LocalAppearance();
        Touch();
    }

    /// <summary>
    /// Puts <paramref name="id"/> (null or empty: the retail look) on a slot of a side. Refused (false) for an item the
    /// account has not unlocked unless <paramref name="force"/> (unattended checks; the servers check the look anyway).
    /// </summary>
    public bool ChooseCosmetic(CosmeticSide side, CosmeticSlot slot, string? id, bool force)
    {
        CosmeticItem? item = string.IsNullOrWhiteSpace(id) ? null : CosmeticCatalog.Find(id.Trim());
        if (item is not null && (item.Side != side || item.Slot != slot))
        {
            return false;
        }

        if (item is not null && !force && Account?.Unlocked.Contains(item.Id) != true)
        {
            return false;
        }

        string[] kept = ArmouryLook.Of(side).Where(i => CosmeticCatalog.Find(i) is { } other && other.Slot != slot).ToArray();
        string[] next = ArmouryLook.Clean(side, item is null ? kept : [.. kept, item.Id]);
        if (side == CosmeticSide.Spy) ArmouryLook.Spy = next;
        else ArmouryLook.Merc = next;
        _social.Log($"Armoury: {(side == CosmeticSide.Spy ? "spy" : "merc")} {CosmeticCatalog.SlotName(slot, side).ToLowerInvariant()} is now {item?.Name ?? "retail"}");
        Touch();
        return true;
    }

    /// <summary>The catalogue's item at <paramref name="index"/> with the account's unlock and this PC's choice; false past the end.</summary>
    public bool Cosmetic(int index, out ArmouryItemBlock block, out string id, out string name, out string tip)
    {
        block = default;
        id = name = tip = string.Empty;
        if (index < 0 || index >= CosmeticCatalog.All.Count)
        {
            return false;
        }

        CosmeticItem item = CosmeticCatalog.All[index];
        AccountInfo? account = Account;
        bool open = account?.Unlocked.Contains(item.Id) == true;
        AchievementInfo? from = account?.Achievements.FirstOrDefault(a => a.Rewards.Contains(item.Id));
        block.Side = (int)item.Side;
        block.Slot = (int)item.Slot;
        block.Kind = (int)item.Kind;
        block.Rgb = item.Kind == CosmeticKind.Hue ? HueRgb(item.Hue)
            : item.Kind == CosmeticKind.Texture && CosmeticCatalog.PatternOf(item.Pattern) is { Colours.Count: > 0 } p ? (int)p.Colours[p.Colours.Count / 2]
            : (int)item.Rgb;
        block.Rgb2 = item.Kind == CosmeticKind.Texture && CosmeticCatalog.PatternOf(item.Pattern) is { Colours.Count: > 1 } q ? (int)q.Colours[0] : block.Rgb;
        block.Flags = (open ? 1 : 0) | (ArmouryLook.Of(item.Side).Contains(item.Id) ? 2 : 0);
        block.Current = from?.Current ?? 0;
        block.Target = from?.Target ?? 0;
        id = item.Id;
        name = item.Name;
        tip = open ? CosmeticCatalog.Describe(item)
            : "Locked. Unlocked by: " + (from is null ? LevelSkinRequirement(item) : from.Title + " (" + from.Description + ")");
        return true;
    }

    /// <summary>A hue (degrees) at full saturation, as 0xRRGGBB, for a light item's swatch.</summary>
    private static int HueRgb(float hue)
    {
        float h = ((hue % 360f) + 360f) % 360f / 60f;
        float x = 1f - Math.Abs(h % 2f - 1f);
        (float r, float g, float b) = (int)h switch
        {
            0 => (1f, x, 0f), 1 => (x, 1f, 0f), 2 => (0f, 1f, x), 3 => (0f, x, 1f), 4 => (x, 0f, 1f), _ => (1f, 0f, x),
        };
        return ((int)(r * 255f) << 16) | ((int)(g * 255f) << 8) | (int)(b * 255f);
    }

    private static string LevelSkinRequirement(CosmeticItem item)
    {
        if ((int)item.Slot >= 6 && (int)item.Slot < 6 + OpenScct.Core.Engine.Sidearms.All.Count && item.Pattern is "night_ops" or "cryo")
        {
            var pistol = OpenScct.Core.Engine.Sidearms.Get((OpenScct.Core.Engine.SidearmKind)((int)item.Slot - 6));
            return "account level " + (pistol.Level + (item.Pattern == "night_ops" ? 5 : 10)).ToString(CultureInfo.InvariantCulture);
        }

        return "an achievement";
    }

    // ---- the look on the servers ----------------------------------------------------------------------------------

    /// <summary>The lobby or hub the player is in gets the look whenever it changed (OnlineLobby.SendLook, HubService).</summary>
    private void FollowLook()
    {
        (string Spy, string Merc) now = (ArmouryLook.Shared(CosmeticSide.Spy).ToHex(), ArmouryLook.Shared(CosmeticSide.Merc).ToHex());
        object? place = (object?)HubBridge.Existing?.Client ?? (OnlineBridge.Existing is { State: OnlineState.Lobby, Client: { } lobby } ? lobby : null);
        if (place is null)
        {
            // The next connection logs in with the look of that moment.
            _lookSentTo = null;
            return;
        }

        if (!ReferenceEquals(place, _lookSentTo))
        {
            _lookSentTo = place;
            _lookSent = now;
            return;
        }

        if (_lookSent == now)
        {
            return;
        }

        _lookSent = now;
        if (place is OpenScct.Server.Hub.HubClient hub)
        {
            hub.SetLook(ArmouryLook.Shared(CosmeticSide.Spy), ArmouryLook.Shared(CosmeticSide.Merc));
            _social.Log($"Armoury: look sent to the hub (spy {now.Spy}, merc {now.Merc})");
        }
        else if (place is OpenScct.Server.Online.OnlineClient client)
        {
            client.SetLook(now.Spy, now.Merc);
            _social.Log($"Armoury: look sent to the lobby (spy {now.Spy}, merc {now.Merc})");
        }
    }

    // ---- the account's appearance ---------------------------------------------------------------------------------

    private void ConfigureAppearance(AccountInfo account)
    {
        string master = (MasterUrl ?? string.Empty).TrimEnd('/');
        if (_appearanceAccount != account.Id || _appearanceMaster != master)
        {
            ResetAppearance();
            _appearanceAccount = account.Id;
            _appearanceMaster = master;
            if (_state.AppearanceAccountId == account.Id && _state.AppearanceMaster == master)
            {
                try
                {
                    _appearanceBaseline = string.IsNullOrEmpty(_state.AppearanceBaseline) ? null : JsonSerializer.Deserialize<AccountAppearance>(_state.AppearanceBaseline, MasterApi.Json);
                    _appearanceDirty = _state.AppearanceDirty;
                }
                catch (JsonException)
                {
                    _appearanceBaseline = null;
                }
            }

            _observedAppearance = LocalAppearance();
        }

        if (_appearanceSupported != account.AppearanceSync)
        {
            _appearanceSupported = account.AppearanceSync;
            if (!account.AppearanceSync)
            {
                AppearanceStatus = "This master uses appearance sync at sign-in.";
            }

            Touch();
        }
    }

    private void ResetAppearance()
    {
        _appearanceSupported = null;
        _appearanceAccount = 0;
        _appearanceMaster = string.Empty;
        _appearanceBaseline = null;
        _appearanceRemote = null;
        _appearancePull = null;
        _appearancePush = null;
        _appearanceSending = null;
        _appearanceDirty = false;
        _appearanceNextPoll = 0;
        _appearancePushAt = 0;
        AppearanceConflict = false;
        AppearanceStatus = string.Empty;
    }

    private void PersistAppearance()
    {
        if (_appearanceAccount == 0)
        {
            return;
        }

        _state.AppearanceAccountId = _appearanceAccount;
        _state.AppearanceMaster = _appearanceMaster;
        _state.AppearanceBaseline = _appearanceBaseline is null ? string.Empty : JsonSerializer.Serialize(_appearanceBaseline, MasterApi.Json);
        _state.AppearanceDirty = _appearanceDirty;
        SaveState();
    }

    private void ObserveAppearanceChange()
    {
        AccountAppearance current = LocalAppearance();
        bool changed = _observedAppearance is not null && !SameAppearance(_observedAppearance, current);
        _observedAppearance = current;
        if (!changed || _appearanceSupported == false)
        {
            return;
        }

        _appearanceDirty = true;
        _appearancePushAt = NowMs + PushDelayMs;
        AppearanceStatus = AppearanceConflict ? "Your choices are kept locally. Resolve the appearance conflict to sync them." : "Appearance changes waiting to sync.";
        PersistAppearance();
        Touch();
    }

    private void PullAppearance()
    {
        if (_appearancePull is not null || _appearancePush is not null)
        {
            return;
        }

        _appearancePull = Call(async client =>
        {
            AccountInfo info = await client.AccountAsync().ConfigureAwait(false);
            AccountAppearance? appearance = info.AppearanceSync ? await client.AppearanceAsync().ConfigureAwait(false) : null;
            return new AppearanceSnapshot(info, appearance);
        });
        _appearanceNextPoll = NowMs + 30_000;
    }

    /// <summary>Once a frame from <see cref="Update"/>: the look on the servers, then the account's copy.</summary>
    private void PumpAppearance()
    {
        FollowLook();
        if (!CanSyncAppearance || Account is not { } account)
        {
            if (_appearanceAccount != 0)
            {
                ResetAppearance();
                Touch();
            }

            return;
        }

        ConfigureAppearance(account);
        ObserveAppearanceChange();
        if (!SafeForAppearance())
        {
            return;
        }

        long now = NowMs;
        if (_appearancePull is { IsCompleted: true } pull)
        {
            _appearancePull = null;
            if (pull.IsCompletedSuccessfully)
            {
                Remember(pull.Result.Account);
                if (pull.Result.Appearance is { } remote)
                {
                    ReceiveAppearance(remote);
                }
            }
            else
            {
                AppearanceFailure(pull);
            }
        }

        if (_appearancePush is { IsCompleted: true } push)
        {
            _appearancePush = null;
            if (push.IsCompletedSuccessfully)
            {
                AppearanceUpdateResult result = push.Result;
                _appearanceRemote = result.Appearance;
                if (result.Success)
                {
                    _appearanceBaseline = result.Appearance;
                    // A second choice made while the first was being sent must not be replaced by its response.
                    _appearanceDirty = _appearanceSending is not null && !SameAppearance(LocalAppearance(), _appearanceSending);
                    if (!_appearanceDirty) ApplyAppearance(result.Appearance);
                    else PersistAppearance();
                    AppearanceStatus = _appearanceDirty ? "Saving your latest appearance…" : "Appearance synced. Titles are shown to others from your next join.";
                    _social.Log("Armoury: the account took the appearance (revision " + result.Appearance.Revision.ToString(CultureInfo.InvariantCulture) + ")");
                }
                else if (result.Conflict)
                {
                    ConflictAppearance(result.Appearance);
                }
                else
                {
                    // Keep the rejected draft; do not repeatedly submit it or discard it silently.
                    AppearanceConflict = true;
                    AppearanceStatus = result.Error ?? "The appearance could not be saved. Reload or adjust your choices.";
                    PersistAppearance();
                }

                _appearanceSending = null;
                Touch();
            }
            else
            {
                _appearanceSending = null;
                AppearanceFailure(push);
            }
        }

        if (_appearanceSupported != true)
        {
            return;
        }

        if (_appearanceDirty && !AppearanceConflict && _appearanceBaseline is not null && _appearancePull is null && _appearancePush is null && now >= _appearancePushAt)
        {
            PushAppearance();
        }
        else if (_appearancePull is null && _appearancePush is null && now >= _appearanceNextPoll)
        {
            PullAppearance();
        }
    }

    private void ReceiveAppearance(AccountAppearance remote)
    {
        _appearanceRemote = remote;
        if (_appearanceDirty && !SameAppearance(LocalAppearance(), remote))
        {
            if (_appearanceBaseline is null || remote.Revision != _appearanceBaseline.Revision)
            {
                ConflictAppearance(remote);
                return;
            }

            PushAppearance();
            return;
        }

        AppearanceStatus = "Appearance is up to date. Website changes sync here while you are in menus or a lobby.";
        ApplyAppearance(remote);
    }

    private void PushAppearance()
    {
        if (_appearanceBaseline is null || _appearancePush is not null || !SafeForAppearance())
        {
            return;
        }

        AccountAppearance selection = LocalAppearance();
        _appearanceSending = selection;
        var request = new AppearanceUpdateRequest
        {
            ExpectedRevision = _appearanceBaseline.Revision, Spy = selection.Spy, Merc = selection.Merc, Title = selection.Title,
        };
        _appearancePush = Call(c => c.UpdateAppearanceAsync(request));
        _appearancePushAt = NowMs + 30_000;
        AppearanceStatus = "Saving appearance…";
        Touch();
    }

    private void ApplyAppearance(AccountAppearance remote)
    {
        _appearanceBaseline = remote;
        _appearanceRemote = remote;
        _appearanceDirty = false;
        AppearanceConflict = false;
        if (!SameAppearance(LocalAppearance(), remote))
        {
            ArmouryLook.Spy = ArmouryLook.Clean(CosmeticSide.Spy, remote.Spy);
            ArmouryLook.Merc = ArmouryLook.Clean(CosmeticSide.Merc, remote.Merc);
            if (_chosenTitle != remote.Title)
            {
                _chosenTitle = remote.Title;
                AppearanceTitle = remote.Title ?? string.Empty;
                AppearanceTitleSerial++;
            }

            _social.Log("Armoury: took the account's appearance (revision " + remote.Revision.ToString(CultureInfo.InvariantCulture) + ")");
        }

        _observedAppearance = LocalAppearance();
        PersistAppearance();
        Touch();
    }

    private void ConflictAppearance(AccountAppearance remote)
    {
        _appearanceRemote = remote;
        AppearanceConflict = true;
        AppearanceStatus = "Appearance changed on the website or another device. Reload it or reapply your local choices.";
        PersistAppearance();
        Touch();
    }

    private void AppearanceFailure(Task task)
    {
        if (IsSignedOutError(task))
        {
            SignOutLocally("Your sign-in ended. Sign in again.", "the master no longer knows this sign-in (401)");
            return;
        }

        AppearanceStatus = "Appearance sync unavailable: " + ErrorOf(task) + " Your local choices are kept; retrying in 30 seconds.";
        _appearanceNextPoll = NowMs + 30_000;
        _appearancePushAt = _appearanceNextPoll;
        PersistAppearance();
        Touch();
    }

    /// <summary>Resolves a conflict with the account's appearance.</summary>
    public void ReloadAppearance()
    {
        if (!SafeForAppearance() || _appearanceRemote is not { } remote)
        {
            return;
        }

        AppearanceStatus = "Loaded the account's current appearance.";
        ApplyAppearance(remote);
    }

    /// <summary>Resolves a conflict with this PC's choices.</summary>
    public void ReapplyAppearance()
    {
        if (!SafeForAppearance() || _appearanceRemote is not { } remote)
        {
            return;
        }

        _appearanceBaseline = remote;
        AppearanceConflict = false;
        _appearanceDirty = true;
        PersistAppearance();
        PushAppearance();
    }
}

/// <summary>An item of the Armoury (ArmouryApi.GetItem). 32 bytes.</summary>
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct ArmouryItemBlock
{
    public int Side;
    public int Slot;
    public int Kind;

    /// <summary>The swatch's colour, 0xRRGGBB: the tint, the light's hue, or a camo's middle colour.</summary>
    public int Rgb;

    /// <summary>A camo's darkest colour (else <see cref="Rgb"/>).</summary>
    public int Rgb2;

    /// <summary>1 unlocked, 2 chosen.</summary>
    public int Flags;

    /// <summary>The unlocking achievement's progress (Target 0: none known).</summary>
    public int Current;
    public int Target;
}

/// <summary>The Armoury at a glance (ArmouryApi.GetInfo). 24 bytes.</summary>
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct ArmouryInfoBlock
{
    public int Revision;

    /// <summary>1 a master is set, 2 the account is known, 4 the account's appearance is revision-synced, 8 conflict, 16 hide mine.</summary>
    public int Flags;
    public int Items;
    public int TitleSerial;
    public int Reserved0;
    public int Reserved1;
}
