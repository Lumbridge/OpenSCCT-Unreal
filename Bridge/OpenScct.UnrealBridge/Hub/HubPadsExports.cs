using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Server.Hub;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Hub;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The hub's match pads table (Source/OpenSCCT/Runtime/ScctHubPadsApi.h mirrors it): each pad's state, match, queue
/// counts, floor, sign, theme and picture; the player's own queue line; the trip to a pad's match and the note about
/// it; the open pad laptop's draft (rows, maps, apply); and the unattended pad run. It reads the visit of
/// <see cref="HubBridge"/>. Every entry catches its exceptions (none may cross into native code); an unexpected one
/// goes to the main table's LastError and the call returns its empty answer. Strings are UTF-16, zero terminated.
/// New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubPadsApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<HubPadsState*, int> GetState;
    public delegate* unmanaged[Cdecl]<HubPadRow*, int, int> GetPads;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<HubLaptopRow*, int, int> GetLaptopRows;
    public delegate* unmanaged[Cdecl]<int, int, int> StepLaptopRow;
    public delegate* unmanaged[Cdecl]<HubLaptopMap*, int, int> GetLaptopMaps;
    public delegate* unmanaged[Cdecl]<char*, int> SetLaptopMap;
    public delegate* unmanaged[Cdecl]<int> LaptopDefaults;
    public delegate* unmanaged[Cdecl]<char*, int> LaptopMerge;
    public delegate* unmanaged[Cdecl]<int> ApplyLaptop;
    public delegate* unmanaged[Cdecl]<void> CloseLaptop;
    public delegate* unmanaged[Cdecl]<int, int, char*, int> QueueOnPad;
}

public static unsafe class HubPadsExports
{
    public const int ApiVersion = 1;

    private static HubBridge Bridge => HubBridge.Instance;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HubPadsApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetState = &GetState;
        api->GetPads = &GetPads;
        api->GetText = &GetText;
        api->GetLaptopRows = &GetLaptopRows;
        api->StepLaptopRow = &StepLaptopRow;
        api->GetLaptopMaps = &GetLaptopMaps;
        api->SetLaptopMap = &SetLaptopMap;
        api->LaptopDefaults = &LaptopDefaults;
        api->LaptopMerge = &LaptopMerge;
        api->ApplyLaptop = &ApplyLaptop;
        api->CloseLaptop = &CloseLaptop;
        api->QueueOnPad = &QueueOnPad;
        return ApiVersion;
    }

    // ---- reading ------------------------------------------------------------------------------------------------

    /// <summary>The state of the pads' view. Returns Valid.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(HubPadsState* state)
    {
        if (state == null)
        {
            return 0;
        }

        try
        {
            *state = State();
            return state->Valid;
        }
        catch (Exception e)
        {
            Failed(e);
            *state = default;
            state->QueueSide = -1;
            return 0;
        }
    }

    internal static HubPadsState State()
    {
        HubBridge bridge = Bridge;
        HubClient? client = bridge.Client;
        bool inMatch = bridge.State == HubState.InMatch;
        return new HubPadsState
        {
            Valid = client is not null || inMatch ? 1 : 0,
            PadCount = client?.Pads.Count ?? 0,
            Revision = bridge.Revision,
            LaptopSerial = bridge.LaptopSerial,
            QueueSide = client?.Queue.PadId is not null ? client.Queue.Side : -1,
            Flags = (bridge.Round is not null ? HubPadsFlags.InHub : 0)
                | (bridge.LaptopPad is not null ? HubPadsFlags.Laptop : 0)
                | (inMatch ? HubPadsFlags.InMatch : 0)
                | (bridge.Host is not null ? HubPadsFlags.Hosting : 0),
            Now = (float)(bridge.Clock.NowMicroseconds / 1_000_000.0),
        };
    }

    /// <summary>Writes the pads in order of their number. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPads(HubPadRow* rows, int capacity) => Guarded(() => Pads(Span(rows, capacity)));

    internal static int Pads(Span<HubPadRow> rows)
    {
        HubBridge bridge = Bridge;
        if (bridge.Client is not { } client)
        {
            return 0;
        }

        List<HubPadInfo> pads = [.. client.Pads.Values.OrderBy(p => Number(p.Id)).ThenBy(p => p.Id, StringComparer.Ordinal)];
        for (int i = 0; i < pads.Count && i < rows.Length; i++)
        {
            HubPadInfo pad = pads[i];
            bool queued = client.Queue.PadId is { } on && string.Equals(on, pad.Id, StringComparison.OrdinalIgnoreCase);
            bool customised = client.PadSetups.TryGetValue(pad.Id, out (LobbySetup Setup, bool Changed) set) && set.Changed;
            string? picture = bridge.PictureOf(pad.Map);
            bool hasPicture = picture is not null && System.IO.File.Exists(picture);
            bool hasSign = client.Signs.TryGetValue("Pad." + pad.Id, out HubSign sign);
            ref HubPadRow row = ref rows[i];
            row = default;
            row.State = (int)pad.State;
            row.Seconds = pad.Seconds;
            row.Number = Number(pad.Id);
            row.Mode = (int)pad.Mode;
            row.TeamSize = pad.TeamSize;
            row.Spies = pad.Spies;
            row.SpySlots = pad.SpySlots;
            row.Mercs = pad.Mercs;
            row.MercSlots = pad.MercSlots;
            row.Flags = (queued ? HubPadsFlags.Queued : 0)
                | (customised ? HubPadsFlags.Customised : 0)
                | (hasPicture ? HubPadsFlags.HasPicture : 0)
                | (hasSign ? HubPadsFlags.HasSign : 0)
                | (hasSign && sign.Face.Mounted ? HubPadsFlags.SignMounted : 0)
                | (queued && client.Queue.Text.Contains("you are ready", StringComparison.Ordinal) ? HubPadsFlags.Ready : 0);
            row.Rgba = HubBridge.StateColour(pad.State);
            row.LoX = pad.LoX;
            row.LoY = pad.LoY;
            row.HiX = pad.HiX;
            row.HiY = pad.HiY;
            row.FloorZ = pad.FloorZ;
            if (hasSign)
            {
                row.SignX = sign.Position.X;
                row.SignY = sign.Position.Y;
                row.SignZ = sign.Position.Z;
                row.SignYaw = sign.Face.Yaw;
                row.SignWidth = sign.Face.Width;
                row.SignHeight = sign.Face.Height;
            }

            fixed (char* id = row.Id, map = row.Map, title = row.Title, theme = row.Theme, mode = row.ModeName, pic = row.Picture)
            {
                OnlineTextCopy.Fixed(id, 16, pad.Id);
                OnlineTextCopy.Fixed(map, 32, pad.Map);
                OnlineTextCopy.Fixed(title, 64, pad.Title.Length > 0 ? pad.Title : pad.Map);
                OnlineTextCopy.Fixed(theme, 16, HubBridge.ThemeForMap(pad.Map));
                OnlineTextCopy.Fixed(mode, 32, LobbySetup.ModeName(pad.Mode));
                OnlineTextCopy.Fixed(pic, 260, hasPicture ? picture : string.Empty);
            }
        }

        return pads.Count;
    }

    private static int Number(string padId) =>
        padId.Length > 3 && int.TryParse(padId.AsSpan(3), System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out int n) ? n : 0;

    /// <summary>Copies a text (<see cref="HubPadsText"/>). Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity) => Copy(buffer, capacity, () => Text((HubPadsText)kind));

    internal static string Text(HubPadsText kind)
    {
        HubBridge bridge = Bridge;
        HubClient? client = bridge.Client;
        return kind switch
        {
            HubPadsText.Queue => client is not null ? bridge.QueueTextOf(client) : string.Empty,
            HubPadsText.QueuePad => client?.Queue.PadId ?? string.Empty,
            HubPadsText.Note => bridge.Note,
            HubPadsText.MatchStatus => bridge.MatchStatus,
            HubPadsText.LaptopPad => bridge.LaptopPad ?? string.Empty,
            HubPadsText.LaptopStatus => bridge.LaptopStatus,
            HubPadsText.LaptopMap => bridge.LaptopDraft?.Map ?? string.Empty,
            HubPadsText.LaptopSummary => bridge.LaptopDraft?.Describe() ?? string.Empty,
            _ => string.Empty,
        };
    }

    // ---- the laptop ---------------------------------------------------------------------------------------------

    /// <summary>Writes the open laptop's rows. Returns how many there are (0 when no laptop is open).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLaptopRows(HubLaptopRow* rows, int capacity) => Guarded(() =>
    {
        IReadOnlyList<HubBridge.LaptopRow> all = Bridge.LaptopRows();
        Span<HubLaptopRow> span = Span(rows, capacity);
        for (int i = 0; i < all.Count && i < span.Length; i++)
        {
            HubBridge.LaptopRow r = all[i];
            ref HubLaptopRow row = ref span[i];
            row = default;
            row.Index = r.Index;
            row.Count = r.Values.Count;
            row.Enabled = r.Enabled ? 1 : 0;
            fixed (char* key = row.Key, label = row.Label, value = row.Value)
            {
                OnlineTextCopy.Fixed(key, 24, r.Key);
                OnlineTextCopy.Fixed(label, 32, r.Label);
                OnlineTextCopy.Fixed(value, 48, r.Value);
            }
        }

        return all.Count;
    });

    /// <summary>Steps a row of the open laptop by Delta (it wraps). Returns 1 when it changed.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StepLaptopRow(int row, int delta) => Guarded(() => Bridge.StepLaptopRow(row, delta) ? 1 : 0);

    /// <summary>Writes the maps the open laptop offers. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLaptopMaps(HubLaptopMap* maps, int capacity) => Guarded(() =>
    {
        HubBridge bridge = Bridge;
        if (bridge.LaptopDraft is not { } draft)
        {
            return 0;
        }

        IReadOnlyList<OpenScct.Server.MapCatalogEntry> all = bridge.LaptopMaps();
        Span<HubLaptopMap> span = Span(maps, capacity);
        for (int i = 0; i < all.Count && i < span.Length; i++)
        {
            OpenScct.Server.MapCatalogEntry e = all[i];
            ref HubLaptopMap row = ref span[i];
            row = default;
            row.Flags = string.Equals(e.Id, draft.Map, StringComparison.OrdinalIgnoreCase) ? HubPadsFlags.Selected : 0;
            string picture = e.PreviewImage is { } p && System.IO.File.Exists(p) ? p : string.Empty;
            fixed (char* id = row.Id, name = row.Name, modes = row.Modes, pic = row.Picture)
            {
                OnlineTextCopy.Fixed(id, 32, e.Id);
                OnlineTextCopy.Fixed(name, 64, e.Name);
                OnlineTextCopy.Fixed(modes, 32, string.Join(", ", e.PlayableModes.Select(ShortMode)));
                OnlineTextCopy.Fixed(pic, 260, picture);
            }
        }

        // Then the maps of OpenSCCT-Content the player lacks: picking one downloads it (HubBridge.Downloads.cs).
        IReadOnlyList<OpenScct.Maps.ContentMap> lacking = bridge.LaptopDownloads();
        for (int i = 0; i < lacking.Count && all.Count + i < span.Length; i++)
        {
            OpenScct.Maps.ContentMap m = lacking[i];
            ref HubLaptopMap row = ref span[all.Count + i];
            row = default;
            row.Flags = HubPadsFlags.Downloadable;
            fixed (char* id = row.Id, name = row.Name, modes = row.Modes)
            {
                OnlineTextCopy.Fixed(id, 32, m.Code);
                OnlineTextCopy.Fixed(name, 64, HubBridge.DownloadTitle(m));
                OnlineTextCopy.Fixed(modes, 32, "pick to download");
            }
        }

        return all.Count + lacking.Count;
    });

    private static string ShortMode(OpenScct.Core.EGameMode mode) => mode switch
    {
        OpenScct.Core.EGameMode.GM_Multi => "Story",
        OpenScct.Core.EGameMode.GM_DiskHunt => "Disk Hunt",
        OpenScct.Core.EGameMode.GM_DeathMatch => "DM",
        OpenScct.Core.EGameMode.GM_MercDeathMatch => "Merc DM",
        _ => LobbySetup.ModeName(mode),
    };

    /// <summary>Picks the open laptop's map. Returns 1 when it changed.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetLaptopMap(char* map)
    {
        string id = map == null ? string.Empty : new string(map);
        return Guarded(() => Bridge.SetLaptopMap(id) ? 1 : 0);
    }

    /// <summary>The map's own settings for the laptop's draft. Returns 1 when done.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LaptopDefaults() => Guarded(() => Bridge.LaptopDefaults() ? 1 : 0);

    /// <summary>Lays a lobby's options over the laptop's draft. Returns 1 when done.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LaptopMerge(char* options)
    {
        string text = options == null ? string.Empty : new string(options);
        return Guarded(() => Bridge.LaptopMerge(text) ? 1 : 0);
    }

    /// <summary>Sends the laptop's draft to the hub. Returns 1 when sent.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ApplyLaptop() => Guarded(() => Bridge.ApplyLaptop() ? 1 : 0);

    /// <summary>Closes the laptop.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void CloseLaptop() => Guarded(() =>
    {
        Bridge.CloseLaptop();
        return 0;
    });

    /// <summary>The unattended pad run: queue the host's player on pad N as a spy (0) or merc (1), the laptop taking Options first.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int QueueOnPad(int pad, int side, char* options)
    {
        string text = options == null ? string.Empty : new string(options);
        return Guarded(() => Bridge.QueueOnPad(pad, side, text) ? 1 : 0);
    }

    // ---- guards -------------------------------------------------------------------------------------------------

    private static Span<T> Span<T>(T* rows, int capacity)
        where T : unmanaged => rows == null || capacity <= 0 ? default : new Span<T>(rows, capacity);

    private static int Guarded(Func<int> read)
    {
        try
        {
            return read();
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    private static int Copy(char* buffer, int capacity, Func<string> text)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, text());
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    private static void Failed(Exception e) => Exports.ReportFailure(e.ToString());
}
