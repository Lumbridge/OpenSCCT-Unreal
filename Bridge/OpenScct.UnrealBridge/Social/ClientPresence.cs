using System;
using System.Diagnostics;
using System.Threading.Tasks;
using OpenScct.Net.Discovery;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// One presence lease per running game (the Godot client's game/Scripts/Online/ClientPresenceService.cs): every 30 s
/// the game tells the master it is running (MasterClient.PresenceAsync with a session id of this run, under the
/// player's sign-in when there is one), so the master can count the players online. A new master or invite key starts
/// a new session id; a new sign-in posts at once. A failed post is tried again on the next interval, never more often.
/// </summary>
public sealed class ClientPresence
{
    public const long IntervalMs = 30_000;

    private readonly SocialBridge _owner;
    private readonly long _epoch = Stopwatch.GetTimestamp();
    private Guid _session = Guid.NewGuid();
    private (string Master, string Invite) _identity = (string.Empty, string.Empty);
    private string _token = string.Empty;
    private long _next;
    private long _postedAt;
    private Task<PresenceResponse>? _pending;

    internal ClientPresence(SocialBridge owner)
    {
        _owner = owner;
    }

    /// <summary>False stops the leases (-ScctNoPresenceLease, and the probes that must not count as players).</summary>
    public bool Enabled { get; set; } = true;

    /// <summary>Leases the master accepted so far this run.</summary>
    public int Accepted { get; private set; }

    /// <summary>Posts made so far this run (accepted, refused or failed).</summary>
    public int Posted { get; private set; }

    private long NowMs => (long)Stopwatch.GetElapsedTime(_epoch).TotalMilliseconds;

    /// <summary>Once a frame on the game thread.</summary>
    public void Update()
    {
        if (_pending is { IsCompleted: true } done)
        {
            _pending = null;
            Answered(done);
        }

        OnlineMaster? master = OnlineBridge.Existing?.Master;
        if (!Enabled || master?.Url is not { } url || master.Client is not { } client)
        {
            return;
        }

        var identity = (url.TrimEnd('/'), master.InviteKey ?? string.Empty);
        if (_identity != identity)
        {
            _identity = identity;
            _session = Guid.NewGuid();
            _next = 0;
        }

        string token = master.AccountToken ?? string.Empty;
        if (_token != token)
        {
            _token = token;
            _next = 0;
        }

        if (_pending is not null || NowMs < _next)
        {
            return;
        }

        _next = NowMs + IntervalMs;
        _postedAt = NowMs;
        Posted++;
        try
        {
            _pending = client.PresenceAsync(_session);
        }
        catch (ObjectDisposedException)
        {
            // The master was set again this frame: the next interval posts to the new one.
        }
    }

    private void Answered(Task<PresenceResponse> post)
    {
        string signedIn = _token.Length > 0 ? ", signed in" : ", as a guest";
        string at = $"at {_postedAt / 1000.0:F1} s";
        if (post.IsCompletedSuccessfully)
        {
            if (post.Result.Accepted)
            {
                Accepted++;
            }

            _owner.Log($"Presence lease {Posted} posted {at}{signedIn}: {(post.Result.Accepted ? "accepted" : "not accepted")}, next in {IntervalMs / 1000} s");
            return;
        }

        Exception? e = post.Exception?.GetBaseException();
        _owner.Log($"Presence lease {Posted} {at}{signedIn} failed: {(e is MasterException ? e.Message : e?.GetType().Name + ": " + e?.Message)}; tried again in {IntervalMs / 1000} s");
    }
}
