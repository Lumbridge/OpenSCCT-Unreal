using System;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
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
/// The Help &amp; feedback page's table (Source/OpenSCCT/Runtime/ScctReportApi.h mirrors it): the report the Godot
/// client's HelpFeedbackScreen builds (GameReportContext.Capture, ProjectLinks.EnvironmentDetails and Url, and
/// ReportScreenshotUpload), from what the bridge knows of the moment: the build, whether a match, an online round, a
/// hub or a demo plays, its map, mode and the local player's side, and the local pawn's position and movement state
/// (PlayerLocationReport). Only the product details ReportContext allows go in: no account, address or log.
/// The screenshot upload runs only when Unreal asks for it (the player pressed Attach screenshot).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ReportApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetDetails;
    public delegate* unmanaged[Cdecl]<int, char*, char*, int, int> GetUrl;
    public delegate* unmanaged[Cdecl]<byte*, int, int> BeginUpload;
    public delegate* unmanaged[Cdecl]<char*, int, int> PollUpload;
}

/// <summary>Where a link of the page goes (ProjectDestination's order).</summary>
public enum BridgeReportLink
{
    BugReport = 0,
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
    public const int ApiVersion = 1;

    /// <summary>The website's limit on a capture (ReportScreenshotUpload): Unreal shrinks the picture below it.</summary>
    public const int MaxScreenshotBytes = 2 * 1024 * 1024;

    private static readonly object s_gate = new();
    private static Task<string>? s_upload;

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
        api->BeginUpload = &BeginUpload;
        api->PollUpload = &PollUpload;
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
        // The report includes a map identifier, never the local content directory used to load it.
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

    /// <summary>The details the report form is prefilled with (ProjectLinks.EnvironmentDetails).</summary>
    public static string Details() => ProjectLinks.EnvironmentDetails(Capture());

    /// <summary>The address of a link, the bug report's with the details and the capture (when given) in it.</summary>
    public static string Url(BridgeReportLink link, string? capture = null) =>
        ProjectLinks.Url((ProjectDestination)(int)link, Capture(), string.IsNullOrEmpty(capture) ? null : capture);

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
    private static int GetUrl(int link, char* capture, char* buffer, int capacity)
    {
        try
        {
            if (link < 0 || link > (int)BridgeReportLink.Help)
            {
                return 0;
            }

            return Write(Url((BridgeReportLink)link, capture == null ? null : new string(capture)), buffer, capacity);
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>Starts posting a PNG (copied first). 1 started, 0 refused (one is already going, or the picture is empty or too big).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int BeginUpload(byte* png, int length)
    {
        try
        {
            if (png == null || length <= 0 || length > MaxScreenshotBytes)
            {
                return 0;
            }

            byte[] copy = new ReadOnlySpan<byte>(png, length).ToArray();
            lock (s_gate)
            {
                if (s_upload is { IsCompleted: false })
                {
                    return 0;
                }

                s_upload = Task.Run(() => ReportUpload.Send(copy));
            }

            return 1;
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>
    /// The upload's state: 0 none asked for, 1 going, 2 done (the capture token written), 3 failed (the reason
    /// written). A finished one is reported once, then the state is 0 again.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PollUpload(char* buffer, int capacity)
    {
        try
        {
            lock (s_gate)
            {
                if (s_upload is null)
                {
                    return 0;
                }

                if (!s_upload.IsCompleted)
                {
                    return 1;
                }

                Task<string> done = s_upload;
                s_upload = null;
                if (done.IsCompletedSuccessfully)
                {
                    Write(done.Result, buffer, capacity);
                    return 2;
                }

                Exception? error = done.Exception?.GetBaseException();
                Write(error is null ? "cancelled" : error.GetType().Name + ": " + error.Message, buffer, capacity);
                return 3;
            }
        }
        catch (Exception)
        {
            return 3;
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

/// <summary>ReportScreenshotUpload.Send, kept apart from the unsafe table (an async method cannot be unsafe).</summary>
public static class ReportUpload
{
    private static readonly HttpClient s_client = new(new HttpClientHandler { AllowAutoRedirect = false, UseCookies = false })
    {
        Timeout = TimeSpan.FromSeconds(25), MaxResponseContentBufferSize = 4096,
    };

    /// <summary>Posts the picture to the website's capture endpoint; the answer is a 64 hex digit token.</summary>
    public static async Task<string> Send(byte[] png, CancellationToken cancellation = default)
    {
        using var content = new ByteArrayContent(png);
        content.Headers.ContentType = new MediaTypeHeaderValue("image/png");
        using HttpResponseMessage response = await s_client.PostAsync(ProjectLinks.Website + "/help/report-bug/capture", content, cancellation).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        using JsonDocument json = JsonDocument.Parse(await response.Content.ReadAsStringAsync(cancellation).ConfigureAwait(false));
        string? token = json.RootElement.GetProperty("capture").GetString();
        if (token is null || token.Length != 64)
        {
            throw new JsonException("Invalid capture response.");
        }

        foreach (char c in token)
        {
            if (c is not (>= '0' and <= '9') and not (>= 'a' and <= 'f'))
            {
                throw new JsonException("Invalid capture response.");
            }
        }

        return token;
    }
}