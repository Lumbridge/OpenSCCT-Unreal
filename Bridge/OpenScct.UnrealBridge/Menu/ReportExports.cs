using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game.SBase;
using OpenScct.Net.Discovery;
using OpenScct.Project;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Demo;
using OpenScct.UnrealBridge.Hub;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.Play.Session;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The Help &amp; feedback page's local game details and project links. Mirrored by ScctReportApi.h.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ReportApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetDetails;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetUrl;
}

/// <summary>Project link identifiers shared with the native menu.</summary>
public enum BridgeReportLink
{
    FeatureRequest = 1,
    Issues = 2,
    Documentation = 3,
    Contributing = 4,
    Development = 5,
    PullRequests = 6,
    NewPullRequest = 7,
    Help = 8,
}

public static unsafe class ReportExports
{
    public const int ApiVersion = 2;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(ReportApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetDetails = &GetDetails;
        api->GetUrl = &GetUrl;
        return ApiVersion;
    }

    /// <summary>GameReportContext.Capture from the bridge's state.</summary>
    internal static ReportContext Capture()
    {
        BridgeSession? current = Exports.Current;
        PlaySession? session = current?.Session;
        SPlayerController? controller = session?.LocalController;
        OnlineBridge? online = OnlineBridge.Existing;
        HubBridge? hub = HubBridge.Existing;
        OnlineClient? client = online?.Client;
        var state = new GameReportState(DemoBridge.Player is not null, client is not null || online?.Hosting is not null, online?.Hosting is not null,
            hub?.Client is not null, hub?.Host is not null, current is not null);
        var player = controller?.PlayerReplicationInfo;
        int? team = player?.Team?.TeamIndex ?? session?.Settings.Team;
        bool spectator = player?.bOnlySpectator ?? false;
        LobbySetup? setup = client?.Lobby.Setup;
        // The game details include a map identifier, never the local content directory used to load it.
        string? map = GameReportState.MapId(current?.Map ?? client?.CurrentMap ?? setup?.Map);
        return new ReportContext
        {
            Source = ReportSource.Game,
            Version = BuildInfo.Version,
            Build = BuildInfo.BuildNumber,
            Commit = BuildInfo.Commit,
            Platform = "Windows (Unreal Engine)",
            Session = state.Session,
            Map = map,
            Mode = session?.Settings.Mode ?? (setup is null ? null : LobbySetup.ModeName(setup.Mode)),
            Side = GameReportState.Side(team, spectator),
            PlayerLocation = controller?.SBPawn is { } pawn ? PlayerLocationReport.Format(map ?? "unknown", pawn, controller) : null,
        };
    }

    /// <summary>The session and build details displayed by the page.</summary>
    public static string Details() => ProjectLinks.EnvironmentDetails(Capture());

    public static string Url(BridgeReportLink link)
    {
        var destination = link switch
        {
            BridgeReportLink.FeatureRequest => ProjectDestination.FeatureRequest,
            BridgeReportLink.Issues => ProjectDestination.Issues,
            BridgeReportLink.Documentation => ProjectDestination.Documentation,
            BridgeReportLink.Contributing => ProjectDestination.Contributing,
            BridgeReportLink.Development => ProjectDestination.Development,
            BridgeReportLink.PullRequests => ProjectDestination.PullRequests,
            BridgeReportLink.NewPullRequest => ProjectDestination.NewPullRequest,
            BridgeReportLink.Help => ProjectDestination.Help,
            _ => throw new ArgumentOutOfRangeException(nameof(link)),
        };
        return ProjectLinks.Url(destination, destination == ProjectDestination.FeatureRequest ? Capture() : null);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetDetails(char* buffer, int capacity)
    {
        try
        {
            return Write(Details(), buffer, capacity);
        }
        catch (Exception e)
        {
            // Nothing may cross into native code.
            return Write("Source: OpenSCCT game\n(the details could not be read: " + e.Message + ")", buffer, capacity);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetUrl(int link, char* buffer, int capacity)
    {
        try
        {
            return Write(Url((BridgeReportLink)link), buffer, capacity);
        }
        catch (Exception)
        {
            return 0;
        }
    }

    private static int Write(string text, char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, capacity));
            buffer[count] = '\0';
        }

        return text.Length;
    }
}
