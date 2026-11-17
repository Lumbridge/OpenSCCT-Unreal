using System;
using System.Diagnostics;
using System.Threading;
using OpenScct.Bots;
using OpenScct.Core;
using OpenScct.Net;
using OpenScct.Server;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// Bot visitors for the hub this PC hosts: OpenSCCT's hub soak crowd (<see cref="HubSoak"/>, the same
/// <see cref="HubWanderBrain"/> walkers <c>svm-server --hub-bots</c> runs), each a headless client over its own UDP
/// socket on the loopback addresses, on a thread of their own. They walk the plaza, chat now and then and change side
/// now and then, so the hub's name tags, chat, player list and side changes can be seen without other people. The walk
/// graph is built on that thread (it loads the hub map's collision), so starting them never stalls the game's frame.
/// </summary>
public sealed class HubBots : IDisposable
{
    private readonly CancellationTokenSource _stop = new();
    private readonly Thread _thread;
    private readonly Action<string> _log;
    private HubSoak? _soak;
    private bool _disposed;

    private HubBots(int count, string address, MapCatalog maps, NetClassRegistry classes, Action<string> log)
    {
        Count = count;
        _log = log;
        _thread = new Thread(() => Run(address, maps, classes)) { IsBackground = true, Name = "OpenSCCT hub bots" };
    }

    /// <summary>How many bots were asked for.</summary>
    public int Count { get; }

    /// <summary>Bots made so far (they join a few a second once the walk graph is built).</summary>
    public int Created => Volatile.Read(ref _soak)?.Created ?? 0;

    /// <summary>The thread's failure, if it stopped.</summary>
    public Exception? Failure { get; private set; }

    /// <summary>
    /// Starts <paramref name="count"/> bots joining the hub at <paramref name="address"/> (the hosted hub's loopback
    /// address). <paramref name="log"/> hears their progress, on the bots' thread.
    /// </summary>
    public static HubBots Start(int count, string address, MapCatalog maps, NetClassRegistry classes, Action<string> log)
    {
        ArgumentNullException.ThrowIfNull(maps);
        ArgumentNullException.ThrowIfNull(classes);
        ArgumentNullException.ThrowIfNull(log);
        var bots = new HubBots(Math.Clamp(count, 1, 48), address, maps, classes, log);
        bots._thread.Start();
        return bots;
    }

    private void Run(string address, MapCatalog maps, NetClassRegistry classes)
    {
        try
        {
            var clock = new StopwatchClock();
            var soak = new HubSoak(new HubSoakSettings
            {
                Bots = Count,
                Maps = maps,
                Classes = classes,
                Seed = 7,
                JoinsPerSecond = 2f,
                // A little chattier than the soak test's crowd, so a visit of a minute sees some chat.
                ChatPerMinute = 3f,
                SideChangesPerMinute = 0.5f,
            }, _ => LiteNetLibTransport.CreateClient(loopbackOnly: true), address, clock.NowMicroseconds);
            Volatile.Write(ref _soak, soak);
            _log($"{Count} hub bot(s) joining {address} (walk graph built)");
            int reported = 0;
            RealtimeLoop.Run(clock, _stop.Token, now =>
            {
                soak.Step(now);
                if (soak.Created != reported)
                {
                    reported = soak.Created;
                    _log($"hub bot {reported} of {Count} is joining");
                }
            });
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception e) when (e is InvalidOperationException or ObjectDisposedException or ArgumentException or System.IO.IOException
            or System.Net.Sockets.SocketException)
        {
            Failure = e;
            _log("the hub bots stopped: " + e.Message);
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _stop.Cancel();
        _thread.Join(TimeSpan.FromSeconds(3));
        Volatile.Read(ref _soak)?.Dispose();
        _stop.Dispose();
    }

    private sealed class StopwatchClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
