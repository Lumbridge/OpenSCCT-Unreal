using System;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using OpenScct.Net.Discovery;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// The website's Join button (the Godot client's game/Scripts/Online/WebsiteJoinService.cs without its screens). The
/// site starts the game with --svm-join-server=&lt;server id&gt;; a game already running gets the id over the named
/// pipe OpenSCCT.WebJoin.Game from the process the site started, which then quits. The join waits until the player is
/// in the front end with nothing visited, then checks the account and the server on the official HTTPS master with the
/// player's own sign-in, and asks Unreal (<see cref="SocialBridge"/>'s requests) to join it as the server list would:
/// a hub through the master's hub join, a game server through the master's join with a password asked first when it
/// has one. Only the official master is used: a link never chooses where the player's credentials go.
/// </summary>
public sealed class WebsiteJoin : IDisposable
{
    public const string ArgumentPrefix = "--svm-join-server=";
    public const string PipeName = "OpenSCCT.WebJoin.Game";

    /// <summary>OpenSCCT's own master (OnlineSettings.DefaultMasterUrl in the Godot client).</summary>
    public const string OfficialMasterUrl = "https://master.lumbridge.co.uk";

    private readonly SocialBridge _owner;
    private readonly CancellationTokenSource _stop = new();
    private NamedPipeServerStream? _pipe;
    private string? _incoming;
    private string? _startupId;
    private Task<bool>? _forward;
    private bool _forwardRetried;
    private string? _pending;
    private bool _queuedShown;
    private bool _signInShown;
    private string? _serverName;
    private Task<LookupResult>? _lookup;
    private bool _lookupSignedIn;
    private string? _lookupToken;
    private CancellationTokenSource? _lookupStop;
    private double _checkAfter;

    private sealed record LookupResult(AccountInfo? Account = null, ServerEntry? Server = null, string? Error = null, bool SignedOut = false);

    internal WebsiteJoin(SocialBridge owner)
    {
        _owner = owner;
    }

    /// <summary>This game owns the pipe a later website join comes through.</summary>
    public bool Listening => _pipe is not null;

    /// <summary>A join waits for the menus, the sign-in or the master's answer.</summary>
    public bool Pending => _pending is not null || _forward is not null;

    public static bool IsServerId(string? id)
    {
        if (id is not { Length: >= 1 and <= 128 })
        {
            return false;
        }

        foreach (char c in id)
        {
            if (c is not (>= 'a' and <= 'z' or >= 'A' and <= 'Z' or >= '0' and <= '9' or '-' or '_'))
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>Website ids only belong to the official HTTPS master; any other master the game is set to is refused.</summary>
    public static bool UsesOfficialMaster(string? url) =>
        Uri.TryCreate(url, UriKind.Absolute, out Uri? configured) &&
        configured.Scheme == Uri.UriSchemeHttps &&
        configured.Host.Equals(new Uri(OfficialMasterUrl).Host, StringComparison.OrdinalIgnoreCase) &&
        configured.IsDefaultPort && configured.AbsolutePath == "/" && configured.Query.Length == 0 &&
        configured.Fragment.Length == 0 && configured.UserInfo.Length == 0;

    /// <summary>
    /// Called once at start with the id of the game's command line, or null. With an id the game first tries to hand
    /// it to a game already running (and quits when that one took it); otherwise it listens on the pipe.
    /// </summary>
    public void Start(string? startupId)
    {
        if (_forward is not null || _pipe is not null)
        {
            return;
        }

        if (IsServerId(startupId))
        {
            _startupId = startupId;
            _owner.Log("Website join: " + startupId + " from the command line; offering it to a game already running");
            _forward = ForwardAsync(startupId!, _stop.Token);
        }
        else
        {
            if (!string.IsNullOrEmpty(startupId))
            {
                _owner.Log("Website join: ignored a server id that is not one (" + startupId + ")");
            }

            StartListening();
        }
    }

    /// <summary>Takes a website join as if it came over the pipe (unattended checks).</summary>
    public void Queue(string id)
    {
        if (IsServerId(id))
        {
            Interlocked.Exchange(ref _incoming, id);
        }
    }

    /// <summary>Once a frame on the game thread: the forward, the pipe's ids, and a waiting join's next step.</summary>
    public void Update(double delta, SocialReadiness ready)
    {
        if (_forward is { IsCompleted: true } forward)
        {
            _forward = null;
            if (forward.Result)
            {
                _owner.Log("Website join: the game already running took " + _startupId + "; this one quits");
                _owner.Request(SocialRequestKind.Quit, SocialRequestFlags.FromWebsite, string.Empty, string.Empty);
                return;
            }

            if (!StartListening())
            {
                // Two games can start together. Only the pipe owner joins; the other forwards once it is ready.
                if (!_forwardRetried && _startupId is { } retry)
                {
                    _forwardRetried = true;
                    _forward = ForwardAsync(retry, _stop.Token);
                    return;
                }

                _owner.Request(SocialRequestKind.Notice, SocialRequestFlags.FromWebsite, string.Empty,
                    "Could not send the website join to the running game. Close the other game and try Join again.");
                _startupId = null;
            }
            else
            {
                _owner.Log("Website join: no other game is running; this one joins " + _startupId);
                Interlocked.Exchange(ref _incoming, _startupId);
                _startupId = null;
            }
        }

        if (_forward is not null)
        {
            return;
        }

        if (Interlocked.Exchange(ref _incoming, null) is { } incoming)
        {
            CancelLookup();
            _pending = incoming;
            _serverName = null;
            _queuedShown = _signInShown = false;
            _checkAfter = 0;
            _owner.Log("Website join: " + incoming + " queued");
        }

        if (_pending is null || (_checkAfter -= delta) > 0)
        {
            return;
        }

        _checkAfter = 0.25;
        Step(ready);
    }

    private void Step(SocialReadiness ready)
    {
        if (!ready.HasFlag(SocialReadiness.Idle))
        {
            if (!_queuedShown)
            {
                _queuedShown = true;
                _owner.Request(SocialRequestKind.Notice, SocialRequestFlags.FromWebsite, string.Empty,
                    "Website join queued. Return to the main menu when you are ready to join the selected server.");
            }

            return;
        }

        OnlineMaster master = OnlineBridge.Instance.Master;
        if (!UsesOfficialMaster(master.Url))
        {
            string url = _pending!;
            CancelPending();
            _owner.Log("Website join: " + url + " refused, the game's master is " + (master.Url ?? "not set"));
            _owner.Request(SocialRequestKind.Notice, SocialRequestFlags.FromWebsite, string.Empty,
                "This website join uses " + OfficialMasterUrl + ". Your game uses another master server, so the join was cancelled.");
            return;
        }

        bool signedIn = master.AccountState == OnlineAccountState.SignedIn && master.AccountToken is not null;
        if (_lookup is null)
        {
            if (!signedIn && _signInShown)
            {
                // Waiting for the player to sign in (or for the sign-in in progress to finish).
                return;
            }

            // Signed out, the server alone is looked up, so the sign-in notice can name it and a server that is
            // gone is said at once; signed in, the account and the server (the Godot client's lookup).
            _lookupSignedIn = signedIn;
            _lookupToken = signedIn ? master.AccountToken : null;
            _lookupStop = CancellationTokenSource.CreateLinkedTokenSource(_stop.Token);
            _lookup = LookupAsync(_pending!, _lookupToken, master.InviteKey, _lookupStop.Token);
            _owner.Log("Website join: asking " + OfficialMasterUrl + " for server " + _pending + (signedIn ? " and the account" : " (not signed in)"));
            if (signedIn)
            {
                _owner.Request(SocialRequestKind.Notice, SocialRequestFlags.FromWebsite, string.Empty, "Checking your game account and the selected server…");
            }

            return;
        }

        if (!_lookup.IsCompleted)
        {
            return;
        }

        LookupResult result = _lookup.Result;
        bool sameAccount = _lookupToken == (signedIn ? master.AccountToken : null) && _lookupSignedIn == signedIn;
        bool lookedUpSignedIn = _lookupSignedIn;
        CancelLookup();
        if (!sameAccount)
        {
            // Signed in or out meanwhile: asked again for the account as it is now.
            return;
        }

        if (result.SignedOut)
        {
            _owner.Log("Website join: the master refused the saved sign-in; it is forgotten here");
            _owner.Request(SocialRequestKind.SignedOut, SocialRequestFlags.FromWebsite, string.Empty, string.Empty);
            _signInShown = false;
            return;
        }

        if (result.Error is { } error)
        {
            string id = _pending!;
            CancelPending();
            _owner.Log("Website join: the master refused " + id + ": " + error);
            _owner.Request(SocialRequestKind.Notice, SocialRequestFlags.FromWebsite, string.Empty, "Website join: " + error);
            return;
        }

        ServerEntry server = result.Server!;
        _serverName = server.Name;
        if (!lookedUpSignedIn)
        {
            _signInShown = true;
            _owner.Log("Website join: the master lists " + _pending + " as '" + server.Name + "' (" + (server.Kind == ServerKind.Hub ? "hub" : "server") +
                "); waiting for the player to sign in");
            _owner.Request(SocialRequestKind.SignIn, SocialRequestFlags.FromWebsite, string.Empty,
                "Sign in to OpenSCCT to join " + server.Name + " from the website. Your join will continue automatically.");
            return;
        }

        if (result.Account?.Name is not { Length: > 0 } accountName)
        {
            CancelPending();
            _owner.Request(SocialRequestKind.Notice, SocialRequestFlags.FromWebsite, string.Empty,
                "Your account has no player name yet. Choose one on the OpenSCCT website, then press Join again.");
            return;
        }

        string serverId = _pending!;
        CancelPending();
        _owner.Log("Website join: joining '" + server.Name + "' (" + serverId + ") as " + accountName + (server.Password ? ", it asks for a password" : string.Empty));
        if (server.Kind == ServerKind.Hub)
        {
            _owner.Request(SocialRequestKind.JoinHubListed, SocialRequestFlags.FromWebsite | SocialRequestFlags.RequireAccount, serverId, server.Name);
        }
        else
        {
            _owner.Request(SocialRequestKind.JoinMaster,
                SocialRequestFlags.FromWebsite | SocialRequestFlags.RequireAccount | (server.Password ? SocialRequestFlags.NeedsPassword : SocialRequestFlags.None),
                serverId, server.Name);
        }
    }

    private void CancelLookup()
    {
        _lookupStop?.Cancel();
        _lookupStop?.Dispose();
        _lookupStop = null;
        _lookup = null;
        _lookupToken = null;
    }

    private void CancelPending()
    {
        _pending = null;
        CancelLookup();
    }

    private static async Task<LookupResult> LookupAsync(string id, string? token, string? inviteKey, CancellationToken cancellationToken)
    {
        try
        {
            using var client = new MasterClient(OfficialMasterUrl, inviteKey) { SessionToken = token };
            AccountInfo? account = token is null ? null : await client.AccountAsync(cancellationToken).ConfigureAwait(false);
            ServerEntry server = await client.ServerAsync(id, cancellationToken).ConfigureAwait(false);
            return new(account, server);
        }
        catch (MasterException e)
        {
            return new(Error: e.Message, SignedOut: token is not null && e.StatusCode == 401 && !e.Message.Contains("invite key", StringComparison.OrdinalIgnoreCase));
        }
        catch (OperationCanceledException)
        {
            return new(Error: "The request was cancelled. Try Join again.");
        }
        catch (Exception e) when (e is System.Net.Http.HttpRequestException or System.Text.Json.JsonException)
        {
            return new(Error: "The master server could not be reached. Try Join again.");
        }
    }

    private bool StartListening()
    {
        if (_pipe is not null)
        {
            return true;
        }

        try
        {
            _pipe = new NamedPipeServerStream(PipeName, PipeDirection.InOut, 1, PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly | PipeOptions.FirstPipeInstance, 256, 256);
            _ = ListenAsync(_pipe, _stop.Token);
            _owner.Log("Website join: listening on the pipe " + PipeName);
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or PlatformNotSupportedException)
        {
            _owner.Log("Website join: another game owns the website join pipe, or this platform does not support it");
            return false;
        }
    }

    private async Task ListenAsync(NamedPipeServerStream pipe, CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                await pipe.WaitForConnectionAsync(cancellationToken).ConfigureAwait(false);
                try
                {
                    using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                    deadline.CancelAfter(TimeSpan.FromSeconds(1));
                    string? id = await ReadIdAsync(pipe, deadline.Token).ConfigureAwait(false);
                    if (id is not null)
                    {
                        _owner.Log("Website join: received " + id + " over the pipe from another process");
                        Interlocked.Exchange(ref _incoming, id);
                        await pipe.WriteAsync("OK\n"u8.ToArray(), deadline.Token).ConfigureAwait(false);
                        await pipe.FlushAsync(deadline.Token).ConfigureAwait(false);
                    }
                    else
                    {
                        _owner.Log("Website join: a process wrote something on the pipe that is not a server id; ignored");
                    }
                }
                catch (Exception e) when (e is IOException or OperationCanceledException)
                {
                }
                finally
                {
                    if (pipe.IsConnected)
                    {
                        pipe.Disconnect();
                    }
                }
            }
        }
        catch (Exception e) when (e is IOException or ObjectDisposedException or OperationCanceledException)
        {
        }
    }

    private static async Task<string?> ReadIdAsync(Stream stream, CancellationToken cancellationToken)
    {
        var bytes = new byte[129];
        int count = 0;
        while (count < bytes.Length)
        {
            int received = await stream.ReadAsync(bytes.AsMemory(count, 1), cancellationToken).ConfigureAwait(false);
            if (received == 0)
            {
                return null;
            }

            if (bytes[count] == '\n')
            {
                string id = Encoding.ASCII.GetString(bytes, 0, count);
                return IsServerId(id) ? id : null;
            }

            if (bytes[count] > 127)
            {
                return null;
            }

            count++;
        }

        return null;
    }

    /// <summary>Hands a server id to the game that owns the pipe. True when it answered OK (it will join).</summary>
    public static async Task<bool> ForwardAsync(string id, CancellationToken cancellationToken)
    {
        try
        {
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            deadline.CancelAfter(TimeSpan.FromSeconds(1));
            using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
            await pipe.ConnectAsync(deadline.Token).ConfigureAwait(false);
            await pipe.WriteAsync(Encoding.ASCII.GetBytes(id + "\n"), deadline.Token).ConfigureAwait(false);
            await pipe.FlushAsync(deadline.Token).ConfigureAwait(false);
            var answer = new byte[3];
            await pipe.ReadExactlyAsync(answer, deadline.Token).ConfigureAwait(false);
            return answer.AsSpan().SequenceEqual("OK\n"u8);
        }
        catch (Exception e) when (e is IOException or OperationCanceledException or UnauthorizedAccessException or PlatformNotSupportedException or TimeoutException)
        {
            return false;
        }
    }

    public void Dispose()
    {
        _stop.Cancel();
        _pipe?.Dispose();
        _pipe = null;
        CancelLookup();
    }
}
