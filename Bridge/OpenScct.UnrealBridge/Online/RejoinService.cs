using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using OpenScct.Net;
using OpenScct.Net.Discovery;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// The round this game was last playing, kept in a file Unreal names (Saved/Online/rejoin.json) from the moment the
/// round loads until the visit is back in a lobby, so that after a crash, a lost connection or a quit the game can
/// offer to rejoin it (OpenSCCT docs/ONLINE.md, "Rejoining a round"). Ported from the Godot client's
/// game/Scripts/Online/RejoinService.cs. The server's password, when there is one, is sealed with DPAPI for the
/// Windows user, never written in plain text.
/// </summary>
public sealed record RejoinRecord
{
    /// <summary>A record older than this is not offered (the round is long over).</summary>
    public static readonly TimeSpan MaxAge = TimeSpan.FromHours(3);

    public string ServerName { get; init; } = string.Empty;

    /// <summary>The server's master id (rejoin through the master), or null.</summary>
    public string? MasterId { get; init; }

    /// <summary>The typed, saved or LAN address (rejoin directly), or null for a server only the master can reach.</summary>
    public string? Address { get; init; }

    public string? Password { get; init; }

    public string Map { get; init; } = string.Empty;

    public DateTimeOffset At { get; init; }

    public bool Fresh => DateTimeOffset.UtcNow - At < MaxAge && (MasterId is { Length: > 0 } || Address is { Length: > 0 });

    /// <summary>The record in <paramref name="path"/>, or null (none, unreadable, or written by another Windows user).</summary>
    public static RejoinRecord? Load(string path)
    {
        try
        {
            if (!File.Exists(path))
            {
                return null;
            }

            using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path));
            JsonElement root = doc.RootElement;
            string Text(string key) => root.TryGetProperty(key, out JsonElement e) && e.ValueKind == JsonValueKind.String ? e.GetString() ?? string.Empty : string.Empty;
            long at = root.TryGetProperty("at", out JsonElement t) && t.TryGetInt64(out long seconds) ? seconds : 0;
            string sealedPassword = Text("password");
            return new RejoinRecord
            {
                ServerName = Text("server"),
                MasterId = Text("masterId") is { Length: > 0 } id ? id : null,
                Address = Text("address") is { Length: > 0 } address ? address : null,
                Password = sealedPassword.Length > 0 ? RejoinSeal.Open(sealedPassword) : null,
                Map = Text("map"),
                At = DateTimeOffset.FromUnixTimeSeconds(at),
            };
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException or FormatException)
        {
            return null;
        }
    }

    public void Save(string path)
    {
        string? folder = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(folder))
        {
            Directory.CreateDirectory(folder);
        }

        using var stream = new MemoryStream();
        using (var json = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
        {
            json.WriteStartObject();
            json.WriteString("server", ServerName);
            json.WriteString("masterId", MasterId ?? string.Empty);
            json.WriteString("address", Address ?? string.Empty);
            // Sealed for this Windows user: another user of the PC, or a copy of the file elsewhere, cannot read it.
            json.WriteString("password", Password is { Length: > 0 } p ? RejoinSeal.Seal(p) ?? string.Empty : string.Empty);
            json.WriteString("map", Map);
            json.WriteNumber("at", At.ToUnixTimeSeconds());
            json.WriteEndObject();
        }

        File.WriteAllBytes(path, stream.ToArray());
    }
}

/// <summary>A round the player may rejoin, as the main menu offers it.</summary>
public sealed record RejoinChoice(string ServerName, string Map, string? MasterId, string? Address, string? Password, bool FromAccount);

/// <summary>
/// Offers to rejoin a round the player dropped out of: follows the online visit (a round is remembered while it is
/// played), and when the main menu shows (<see cref="Check"/>) asks whether the remembered round's server is still
/// playing it (the master's entry for a listed server, a server query for an address). A signed-in player is also
/// offered the round the account dropped out of on any PC: the master's account answer carries it
/// (<see cref="AccountInfo.Rejoin"/>). <see cref="Rejoin"/> joins the server again; the server puts the player back on
/// the side and with the score and lives the player left with (OnlineServer.Rejoin.cs). Game thread only.
/// </summary>
public sealed class RejoinService : IDisposable
{
    private const long QueryTimeoutMs = 4000;
    private const int LineCapacity = 32;

    private readonly OnlineBridge _bridge;
    private readonly Queue<string> _lines = new();
    private readonly Stopwatch _watch = Stopwatch.StartNew();
    private string? _path;
    private RejoinRecord? _record;
    private bool _loaded;
    private bool _checked;
    private bool _leftByChoice;
    private bool _rejoining;
    private string? _declinedMasterId;
    private OnlineClient? _remembered;
    private OnlineClient? _followed;
    private OnlineClientState _followedState;
    private Task<ServerEntry>? _masterCheck;
    private ServerQuery? _query;
    private long _queryUntil;
    private AccountInfo? _account;

    internal RejoinService(OnlineBridge bridge) => _bridge = bridge;

    /// <summary>The round the main menu offers to rejoin, or null.</summary>
    public RejoinChoice? Offer { get; private set; }

    /// <summary>Goes up whenever <see cref="Offer"/> changes.</summary>
    public int Revision { get; private set; }

    /// <summary>A check of the remembered round is waiting for the master or the server.</summary>
    public bool Checking => _masterCheck is not null || _query is not null;

    /// <summary>
    /// Where the record is kept; null or empty turns the service off (nothing is remembered or offered: test runs).
    /// A new file is read on the next use.
    /// </summary>
    public void SetFile(string? path)
    {
        string? next = string.IsNullOrWhiteSpace(path) ? null : path;
        if (string.Equals(next, _path, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        _path = next;
        _record = null;
        _loaded = false;
        _checked = false;
        SetOffer(null);
        Say(next is null ? "off: no rejoin file is set" : "rejoin file " + next);
    }

    private RejoinRecord? Record
    {
        get
        {
            if (!_loaded && _path is not null)
            {
                _loaded = true;
                _record = RejoinRecord.Load(_path);
                if (_record is not null)
                {
                    Say($"found the record of a round on {_record.ServerName} ({_record.Map}, {(_record.MasterId is not null ? "master id " + _record.MasterId : "address " + _record.Address)}), " +
                        $"{(int)(DateTimeOffset.UtcNow - _record.At).TotalMinutes} min old");
                }
            }

            return _record;
        }
    }

    /// <summary>The next line for Unreal's log, oldest first; false when none waits.</summary>
    public bool TakeLine(out string line) => _lines.TryDequeue(out line!);

    /// <summary>
    /// Follows the visit's state (called from <see cref="OnlineBridge.Update"/>): a round (loading, match, results) is
    /// remembered with how to reach its server; the lobby forgets it (the round is over). A kick forgets it, and so
    /// does a rejoin that failed. The player's own listen server is never remembered (the server keeps no place for
    /// its host).
    /// </summary>
    internal void Follow(OnlineClient client, string? masterId, string? password)
    {
        if (ReferenceEquals(_followed, client) && _followedState == client.State)
        {
            return;
        }

        _followed = client;
        _followedState = client.State;
        if (_path is null || _bridge.Hosting is not null)
        {
            return;
        }

        switch (client.State)
        {
            case OnlineClientState.Loading or OnlineClientState.Match or OnlineClientState.Results:
                _rejoining = false;
                _leftByChoice = false;
                // The account's offer from the master (made before this round) is used up.
                _declinedMasterId = _account?.Rejoin?.ServerId ?? _declinedMasterId;
                SetOffer(null);
                if (!ReferenceEquals(_remembered, client) || Record?.Map != client.CurrentMap)
                {
                    _remembered = client;
                    string? address = client.Target.Attempts is [{ Kind: ConnectKind.Direct } only] ? only.Address : null;
                    Remember(new RejoinRecord
                    {
                        ServerName = client.Lobby.ServerName.Length > 0 ? client.Lobby.ServerName : client.Target.Name,
                        MasterId = masterId,
                        Address = address,
                        Password = password,
                        Map = client.CurrentMap ?? client.Lobby.Setup.Map,
                        At = DateTimeOffset.UtcNow,
                    });
                }

                break;
            case OnlineClientState.Lobby:
                _rejoining = false;
                _remembered = null;
                Forget("the visit is back in the lobby");
                break;
            case OnlineClientState.Failed when client.Kicked || _rejoining:
                Say(client.Kicked ? "kicked: the round is not offered again" : "the rejoin failed: " + client.Error);
                _rejoining = false;
                Forget(null);
                break;
        }
    }

    /// <summary>The player left the visit: the round is not offered again until the game starts next.</summary>
    internal void LeftByChoice()
    {
        if (_record is not null)
        {
            _leftByChoice = true;
        }
    }

    /// <summary>The main menu is up: checks the remembered round once per drop (and shows the account's offer).</summary>
    public void Check()
    {
        if (_path is null || Offer is not null || Checking)
        {
            return;
        }

        if (Record is { } record && !_leftByChoice && !_checked)
        {
            _checked = true;
            if (!record.Fresh)
            {
                Forget("the remembered round is more than three hours old");
            }
            else if (record.MasterId is { } id && _bridge.Master.AskServer(id) is { } ask)
            {
                Say($"asking the master whether {record.ServerName} is still playing");
                _masterCheck = ask;
                return;
            }
            else if (record.Address is { } address && StartQuery(address))
            {
                return;
            }
            else
            {
                Forget("there is no way to ask the remembered server (no master is set and no address was kept)");
            }
        }

        OnAccountChanged();
    }

    /// <summary>Rejoin pressed: joins the round's server again (a listed one through the master, with the ticket). False when nothing is offered.</summary>
    public bool Rejoin()
    {
        if (Offer is not { } offer)
        {
            return false;
        }

        SetOffer(null);
        _rejoining = true;
        _checked = false;
        Say($"rejoining {offer.ServerName}");
        if (offer.MasterId is { } id && _bridge.Master.HasMaster)
        {
            _bridge.Master.JoinServer(id, offer.Password, offer.Address, preferredTeam: -1);
        }
        else if (offer.Address is { } address)
        {
            _bridge.Join(address, offer.Password);
        }
        else
        {
            _rejoining = false;
            _bridge.FailJoin("The round cannot be rejoined: no master server is set to reach it through.");
            return false;
        }

        return true;
    }

    /// <summary>Not now: the round is not offered again.</summary>
    public void Dismiss()
    {
        if (Offer is { MasterId: { } id })
        {
            _declinedMasterId = id;
        }

        Say("declined");
        SetOffer(null);
        Forget(null);
    }

    /// <summary>Once a frame (OnlineBridge.Update): the master's and the server's answers, and the account's offer.</summary>
    internal void Pump()
    {
        if (!ReferenceEquals(_account, _bridge.Master.Account))
        {
            _account = _bridge.Master.Account;
            OnAccountChanged();
        }

        if (_masterCheck is { IsCompleted: true } check)
        {
            _masterCheck = null;
            if (check.IsCompletedSuccessfully && InProgress(check.Result.Phase) && check.Result.Kind == ServerKind.Game)
            {
                OfferRecord(check.Result.Name, check.Result.Map);
            }
            else
            {
                Forget("the remembered round is over: " + (check.IsCompletedSuccessfully
                    ? check.Result.Phase.ToString()
                    : check.Exception?.GetBaseException().Message ?? "the master does not list the server"));
                OnAccountChanged();
            }
        }

        if (_query is { } query)
        {
            query.Poll();
            if (query.Results.Count > 0 || _watch.ElapsedMilliseconds >= _queryUntil)
            {
                ServerQueryResult? answer = query.Results.Count > 0 ? query.Results[^1] : null;
                query.Dispose();
                _query = null;
                if (answer is not null && InProgress(answer.Info.Phase))
                {
                    Say($"the server confirmed the round: {answer.Info.Name} is playing {answer.Info.Map} ({answer.Info.Phase})");
                    OfferRecord(answer.Info.Name, answer.Info.Map);
                }
                else
                {
                    Forget("the remembered round is over: " + (answer?.Info.Phase.ToString() ?? "no answer from the server"));
                    OnAccountChanged();
                }
            }
        }
    }

    /// <summary>A round is being played (the results count as over: a series's next game shows as loading).</summary>
    private static bool InProgress(SessionPhase phase) => phase is SessionPhase.Loading or SessionPhase.Match;

    private bool StartQuery(string address)
    {
        try
        {
            IPEndPoint? endpoint = LiteNetLibTransport.Resolve(JoinTarget.FromAddress(address).Attempts[0].Address);
            if (endpoint is null)
            {
                return false;
            }

            // A server on this PC is asked from a loopback socket (no firewall prompt).
            _query = new ServerQuery(_bridge.Clock, IPAddress.IsLoopback(endpoint.Address));
            _query.QueryServer(endpoint);
            _queryUntil = _watch.ElapsedMilliseconds + QueryTimeoutMs;
            Say($"asking {address} whether its round is still being played");
            return true;
        }
        catch (Exception e) when (e is InvalidOperationException or System.Net.Sockets.SocketException or ArgumentException or FormatException)
        {
            Say("cannot ask the server: " + e.Message);
            _query?.Dispose();
            _query = null;
            return false;
        }
    }

    private void OfferRecord(string serverName, string map)
    {
        if (Record is not { } record)
        {
            return;
        }

        Say($"offering {serverName} ({map})");
        SetOffer(new RejoinChoice(serverName.Length > 0 ? serverName : record.ServerName, map, record.MasterId, record.Address, record.Password, FromAccount: false));
    }

    /// <summary>The master's account answer names a round the account dropped out of (any PC): offered when nothing else is.</summary>
    private void OnAccountChanged()
    {
        if (_path is null || Offer is not null || Checking || _leftByChoice ||
            _account?.Rejoin is not { ServerId.Length: > 0 } rejoin ||
            rejoin.ServerId == _declinedMasterId || _bridge.Client is not null)
        {
            return;
        }

        Say($"the master says this account may rejoin {rejoin.ServerName} ({rejoin.Map})");
        string? password = Record is { MasterId: { } id } r && id == rejoin.ServerId ? r.Password : null;
        SetOffer(new RejoinChoice(rejoin.ServerName, rejoin.Map, rejoin.ServerId, null, password, FromAccount: true));
    }

    private void Remember(RejoinRecord record)
    {
        _record = record;
        _loaded = true;
        _checked = false;
        try
        {
            record.Save(_path!);
            Say($"remembering the round on {record.ServerName} ({record.Map}) for a rejoin");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Say("could not write the rejoin file: " + e.Message);
        }
    }

    private void Forget(string? why)
    {
        if (Record is null)
        {
            return;
        }

        _record = null;
        _checked = false;
        if (why is not null)
        {
            Say("forgetting the remembered round: " + why);
        }

        try
        {
            if (_path is not null && File.Exists(_path))
            {
                File.Delete(_path);
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Say("could not delete the rejoin file: " + e.Message);
        }
    }

    private void SetOffer(RejoinChoice? offer)
    {
        if (Offer == offer)
        {
            return;
        }

        Offer = offer;
        Revision++;
        _bridge.Touch();
    }

    private void Say(string line)
    {
        if (_lines.Count == LineCapacity)
        {
            _lines.Dequeue();
        }

        _lines.Enqueue(line);
    }

    public void Dispose()
    {
        _query?.Dispose();
        _query = null;
        _masterCheck = null;
    }
}

/// <summary>DPAPI for the Windows user (CryptProtectData), as the account token's store in Unreal seals it.</summary>
internal static class RejoinSeal
{
    private static readonly byte[] Entropy = Encoding.UTF8.GetBytes("OpenSCCT rejoin password");

    public static string? Seal(string text)
    {
        byte[]? sealedBytes = Run(Encoding.UTF8.GetBytes(text), protect: true);
        return sealedBytes is null ? null : Convert.ToBase64String(sealedBytes);
    }

    public static string? Open(string base64)
    {
        byte[]? plain = Run(Convert.FromBase64String(base64), protect: false);
        return plain is null ? null : Encoding.UTF8.GetString(plain);
    }

    private static unsafe byte[]? Run(byte[] data, bool protect)
    {
        if (!OperatingSystem.IsWindows())
        {
            return null;
        }

        fixed (byte* input = data)
        fixed (byte* entropy = Entropy)
        {
            var inBlob = new DataBlob { Size = data.Length, Data = (IntPtr)input };
            var entropyBlob = new DataBlob { Size = Entropy.Length, Data = (IntPtr)entropy };
            var outBlob = default(DataBlob);
            const int UiForbidden = 0x1;
            bool ok = protect
                ? CryptProtectData(ref inBlob, "OpenSCCT rejoin", ref entropyBlob, IntPtr.Zero, IntPtr.Zero, UiForbidden, ref outBlob)
                : CryptUnprotectData(ref inBlob, IntPtr.Zero, ref entropyBlob, IntPtr.Zero, IntPtr.Zero, UiForbidden, ref outBlob);
            if (!ok)
            {
                return null;
            }

            try
            {
                var result = new byte[outBlob.Size];
                Marshal.Copy(outBlob.Data, result, 0, outBlob.Size);
                return result;
            }
            finally
            {
                LocalFree(outBlob.Data);
            }
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct DataBlob
    {
        public int Size;
        public IntPtr Data;
    }

    [DllImport("crypt32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptProtectData(ref DataBlob dataIn, string description, ref DataBlob entropy, IntPtr reserved, IntPtr prompt, int flags, ref DataBlob dataOut);

    [DllImport("crypt32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptUnprotectData(ref DataBlob dataIn, IntPtr description, ref DataBlob entropy, IntPtr reserved, IntPtr prompt, int flags, ref DataBlob dataOut);

    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr handle);
}
