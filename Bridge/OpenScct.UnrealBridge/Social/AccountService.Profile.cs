using System;
using System.Text;
using System.Threading.Tasks;
using OpenScct.Net.Discovery;

namespace OpenScct.UnrealBridge.Social;

/// <summary>The settings sync of <see cref="AccountService"/> (the Godot client's Pull, Push, Take and their timing).</summary>
public sealed partial class AccountService
{
    private Task<ProfileData?>? _pull;
    private Task<ProfileData>? _push;
    private long _pushAt;
    private bool _pushAgain;
    private bool _repushed;
    private string? _localSettings;
    private string _localControls = string.Empty;
    private string _localBindings = string.Empty;

    /// <summary>The account's settings waiting for Unreal to apply them, and when they were saved.</summary>
    private (string Settings, string Controls, string Bindings, DateTimeOffset Updated, string Record)? _incoming;

    /// <summary>The account's settings for Unreal to apply (registry values, controls.json, bindings.json), or null.</summary>
    public (string Settings, string Controls, string Bindings)? Incoming =>
        _incoming is { } i ? (i.Settings, i.Controls, i.Bindings) : null;

    public ProfileSyncState SyncState =>
        !CanSync ? ProfileSyncState.Off
        : _pull is not null || _incoming is not null ? ProfileSyncState.Pulling
        : _push is not null ? ProfileSyncState.Sending
        : _pushAt != 0 ? ProfileSyncState.Queued
        : ProfileSyncState.Idle;

    /// <summary>Settings can follow the account now: signed in, the account known, this PC's copy handed over, sync on.</summary>
    private bool CanSync => !_syncOff && !Preview && SignedIn && _state.AccountId > 0 && _localSettings is not null;

    /// <summary>
    /// This PC's copy of the synced settings: the registry's values as a JSON object, and the text of controls.json and
    /// bindings.json. <paramref name="changed"/> says the player changed one of them just now: it is sent 3 s later
    /// (further changes in that time send once). Unreal hands the copy over once at start without a change.
    /// </summary>
    public void SetLocalProfile(string settingsJson, string controls, string bindings, bool changed)
    {
        _localSettings = string.IsNullOrWhiteSpace(settingsJson) ? "{}" : settingsJson;
        _localControls = controls ?? string.Empty;
        _localBindings = bindings ?? string.Empty;
        if (!changed || Preview)
        {
            return;
        }

        _state.ProfileUpdatedMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        SaveState();
        if (CanSync)
        {
            bool already = _pushAt != 0;
            _pushAt = NowMs + PushDelayMs;
            if (!already)
            {
                _social.Log($"Account: profile sync queued (the settings go to the account in {PushDelayMs / 1000} s)");
            }

            Touch();
        }
        else if (!_syncOff)
        {
            _social.Log("Account: a setting changed; not signed in, so it stays on this PC");
        }
    }

    /// <summary>Unreal applied the account's settings (or could not): this PC's copy now matches the account's.</summary>
    public void FinishIncoming(bool applied)
    {
        if (_incoming is not { } incoming)
        {
            return;
        }

        _incoming = null;
        if (applied)
        {
            _state.SyncedAccountId = _state.AccountId;
            _state.ProfileUpdatedMs = incoming.Updated.ToUnixTimeMilliseconds();
            _state.LastRecord = incoming.Record;
            SaveState();
            _pushAt = 0;
            _social.Log("Account: took the account's settings");
            // After Unreal put the settings in (its stored title among them): the record's look is the newest word.
            AcceptProfileLook(incoming.Record, settingsChanged: true);
        }
        else
        {
            _social.Log("Account: the account's settings could not be applied here");
        }

        Touch();
    }

    private void ResetProfile()
    {
        _pull = null;
        _push = null;
        _pushAt = 0;
        _pushAgain = false;
        _repushed = false;
        _incoming = null;
        _profileLook = null;
    }

    /// <summary>Asks for the account's settings (after each sign-in, and at start-up with a saved one).</summary>
    private void Pull()
    {
        if (_syncOff || !SignedIn || _pull is not null)
        {
            return;
        }

        _social.Log("Account: asking for the account's settings");
        _pull = Call(c => c.GetProfileAsync());
        Touch();
    }

    private void PumpProfile()
    {
        if (_pull is { IsCompleted: true } pull)
        {
            _pull = null;
            if (pull.IsCompletedSuccessfully)
            {
                Pulled(pull.Result);
            }
            else if (!SignedOutBy(pull))
            {
                _social.Log("Account: the account's settings could not be fetched: " + ErrorOf(pull));
            }

            Touch();
        }

        if (_push is { IsCompleted: true } push)
        {
            _push = null;
            if (push.IsCompletedSuccessfully)
            {
                Pushed(push.Result);
            }
            else if (!SignedOutBy(push))
            {
                _social.Log("Account: profile sync failed: " + ErrorOf(push));
            }

            Touch();
        }

        if (_pushAt != 0 && _push is null && _pull is null && _incoming is null && NowMs >= _pushAt)
        {
            _pushAt = 0;
            Push();
        }
        else if (_pushAgain && _push is null && _pushAt == 0)
        {
            _pushAgain = false;
            Push();
        }
    }

    private void Pulled(ProfileData? remote)
    {
        if (remote is null)
        {
            _social.Log("Account: the account has no settings yet; sending this PC's");
            Push();
            return;
        }

        switch (ProfileSync.Read(remote.Data, out string settings, out string controls, out string bindings, out string? problem))
        {
            case ProfileSync.ReadResult.Unreadable:
            case ProfileSync.ReadResult.Newer:
                _social.Log("Account: " + problem);
                return;
            case ProfileSync.ReadResult.NoUnrealPart:
                // Only the Godot client wrote it: its look is taken by the rule the settings follow below.
                if (_state.SyncedAccountId != _state.AccountId || _state.ProfileUpdatedMs == 0 || remote.Updated.ToUnixTimeMilliseconds() > _state.ProfileUpdatedMs)
                {
                    TakeProfileLook(remote.Data);
                }

                AddOwnPart(remote);
                return;
        }

        long local = _state.ProfileUpdatedMs;
        long stored = remote.Updated.ToUnixTimeMilliseconds();
        if (_state.SyncedAccountId != _state.AccountId || local == 0 || stored > local)
        {
            Take(remote, settings, controls, bindings);
        }
        else if (local > stored)
        {
            _state.LastRecord = remote.Data;
            _social.Log("Account: this PC's settings are newer than the account's; sending them");
            Push();
        }
        else
        {
            _state.LastRecord = remote.Data;
            _state.SyncedAccountId = _state.AccountId;
            SaveState();
            _social.Log("Account: the account's settings match this PC's");
            AcceptProfileLook(remote.Data, settingsChanged: false);
        }
    }

    /// <summary>
    /// The account's record has no part of this client's yet (only the Godot client wrote it): this PC's part is added
    /// beside the Godot one, which goes back unchanged, and dated after the record so the master keeps it.
    /// </summary>
    private void AddOwnPart(ProfileData remote)
    {
        _state.LastRecord = remote.Data;
        _state.ProfileUpdatedMs = Math.Max(_state.ProfileUpdatedMs, remote.Updated.ToUnixTimeMilliseconds() + 1);
        SaveState();
        if (_repushed)
        {
            return;
        }

        _repushed = true;
        _social.Log("Account: the account's settings have no part from this client yet; adding this PC's");
        Push();
    }

    // Older Unreal profiles stored the cosmetics in the Unreal part alone. Backfill after settling which
    // settings record won so an existing sign-in also becomes readable on the legacy Godot client.
    private void AcceptProfileLook(string record, bool settingsChanged)
    {
        bool backfill = Account?.AppearanceSync == false && ProfileSync.HasUnsharedLook(record);
        if (settingsChanged || backfill) TakeProfileLook(record);
        if (!backfill) return;

        _state.ProfileUpdatedMs++;
        SaveState();
        _pushAt = NowMs + 1;
    }

    /// <summary>Sends this PC's settings now (a send already on its way sends again when it is back).</summary>
    private void Push()
    {
        if (!CanSync)
        {
            return;
        }

        if (_push is not null)
        {
            _pushAgain = true;
            return;
        }

        string? previous = _state.SyncedAccountId == _state.AccountId || _state.LastRecord is not null ? _state.LastRecord : null;
        string data = ProfileSync.Build(previous, _localSettings!, _localControls, _localBindings, ProfileLook);
        int bytes = Encoding.UTF8.GetByteCount(data);
        if (bytes > MasterApi.MaxProfileBytes)
        {
            _social.Log($"Account: the settings are {bytes} bytes, more than the master keeps ({MasterApi.MaxProfileBytes}); not sent");
            return;
        }

        if (_state.ProfileUpdatedMs == 0)
        {
            _state.ProfileUpdatedMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            SaveState();
        }

        var profile = new ProfileData { Data = data, Updated = DateTimeOffset.FromUnixTimeMilliseconds(_state.ProfileUpdatedMs) };
        _social.Log($"Account: sending the settings to the account ({bytes} bytes)");
        _push = Call(c => c.PutProfileAsync(profile));
    }

    private void Pushed(ProfileData stored)
    {
        if (stored.Updated.ToUnixTimeMilliseconds() > _state.ProfileUpdatedMs)
        {
            // The master had a newer copy from another PC and kept it.
            _social.Log("Account: the account had newer settings from another PC; taking those");
            switch (ProfileSync.Read(stored.Data, out string settings, out string controls, out string bindings, out string? problem))
            {
                case ProfileSync.ReadResult.Ok:
                    Take(stored, settings, controls, bindings);
                    return;
                case ProfileSync.ReadResult.NoUnrealPart:
                    TakeProfileLook(stored.Data);
                    AddOwnPart(stored);
                    return;
                default:
                    _social.Log("Account: " + problem);
                    return;
            }
        }

        _state.SyncedAccountId = _state.AccountId;
        _state.LastRecord = stored.Data;
        SaveState();
        _social.Log("Account: profile sync sent: the settings are on the account");
    }

    /// <summary>Hands the account's settings to Unreal to apply (without sending them back).</summary>
    private void Take(ProfileData remote, string settings, string controls, string bindings)
    {
        _incoming = (settings, controls, bindings, remote.Updated, remote.Data);
        _pushAt = 0;
        _social.Log($"Account: the account's settings from {remote.Updated:u} are newer; applying them");
    }
}
