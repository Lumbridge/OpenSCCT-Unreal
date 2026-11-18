using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Content.Maps;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The strategic map page as Unreal draws it (Menu/SScctStrategicMapPage): the shown floor and its picture, the
/// "you are here" marker and the team-mate markers on the picture, and where the local player is in the world.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeMapState
{
    public const int PathCapacity = 260;
    public const int MapCapacity = 64;

    /// <summary>Room for every other player of a full lobby (<see cref="StrategicMapModel.MaxTeamMates"/>).</summary>
    public const int MaxMates = 23;

    public int FloorCount;

    /// <summary>The floor shown, from 0 (the lowest).</summary>
    public int Floor;

    /// <summary>1: the map has a snapshot camera, so markers and places can be put on its pictures.</summary>
    public int HasCamera;

    /// <summary>The markers' blink: 1 bright, 0 dim (it changes every <see cref="StrategicMapModel.TimerSeconds"/>).</summary>
    public int Bright;

    /// <summary>1: opened in a round with the live positions; 0: the lobby's plain map overview.</summary>
    public int Live;

    /// <summary>1: the shown floor's picture is converted (<see cref="Picture"/> is its file).</summary>
    public int HasPicture;

    /// <summary>1: the "you are here" marker is on the shown floor and inside the picture.</summary>
    public int YouShown;
    public float YouU;
    public float YouV;

    /// <summary>The way the player faces on the picture, radians clockwise from pointing right.</summary>
    public float YouAngle;

    /// <summary>1: the local player has a pawn; its world point (sim units) and yaw (65536 a turn) follow.</summary>
    public int HasYou;
    public float YouX;
    public float YouY;
    public float YouZ;
    public int YouYaw;

    /// <summary>The floor the local player is on.</summary>
    public int YouFloor;

    /// <summary>How many team mates have a position; MateShown tells which of them are on the shown floor.</summary>
    public int MateCount;
    public int MatesShown;
    public fixed int MateShown[MaxMates];
    public fixed float MateU[MaxMates];
    public fixed float MateV[MaxMates];
    public fixed float MateAngle[MaxMates];

    public fixed float MateX[MaxMates];
    public fixed float MateY[MaxMates];
    public fixed float MateZ[MaxMates];
    public fixed int MateFloor[MaxMates];
    public float FloorMinZ;
    public float FloorMaxZ;
    public int PlaceCount;
    public fixed char Picture[PathCapacity];

    /// <summary>The map's folder name.</summary>
    public fixed char Map[MapCapacity];
}

/// <summary>One numbered place of the map's identity, on its floor's picture.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeMapPlace
{
    public int Number;
    public int Floor;
    public float U;
    public float V;
    public FVector World;
}

/// <summary>
/// The strategic map's table (Source/OpenSCCT/Runtime/ScctMapApi.h mirrors it field for field): open the page for the
/// round being played (live positions) or a lobby's map (no markers), the blink timer, Up and Down, the state, the
/// legend, the numbered places, whether the DisplayStrategicMap key may open it now, and close.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MapApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, char*, int> Open;
    public delegate* unmanaged[Cdecl]<int> Tick;
    public delegate* unmanaged[Cdecl]<int> Up;
    public delegate* unmanaged[Cdecl]<int> Down;
    public delegate* unmanaged[Cdecl]<BridgeMapState*, int> GetState;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetLegend;
    public delegate* unmanaged[Cdecl]<BridgeMapPlace*, int, int> ListPlaces;
    public delegate* unmanaged[Cdecl]<int> CanDisplay;
    public delegate* unmanaged[Cdecl]<void> Close;
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
    public delegate* unmanaged[Cdecl]<BridgeMapTriangle*, int, int> ListGeometry;
}

/// <summary>
/// The page's model and where its positions come from: the Godot client's StrategicMapModel (linked from
/// game/Scripts/UI) over the map folder, and PlayMenu.LivePositions for the round being played.
/// </summary>
public static class StrategicMapPage
{
    public const int MatchContext = 0, LobbyContext = 1;

    private static StrategicMapModel? s_model;
    private static bool s_live;
    private static string s_map = "";

    // The map data read once per folder (the pictures' files, the camera, the legend, the places).
    private static string? s_dataFolder;
    private static StrategicMapData s_data = StrategicMapData.Empty;

    internal static BridgeMapPlace[] Places { get; private set; } = [];
    internal static StrategicMapPosition? You { get; private set; }
    internal static List<StrategicMapPosition> Team { get; private set; } = [];
    private static readonly Dictionary<int, BridgeMapTriangle[]> GeometryByFloor = new();

    internal static ReadOnlySpan<BridgeMapTriangle> Geometry()
    {
        if (!Live || Model is not { } model || Exports.Current is not { } round)
        {
            return [];
        }
        if (!GeometryByFloor.TryGetValue(model.FloorIndex, out var mesh))
        {
            mesh = StrategicMapGeometry.Build(round.Session.Loader.Map.Collision, model.Data, model.FloorIndex);
            GeometryByFloor.Add(model.FloorIndex, mesh);
        }
        return mesh;
    }

    public static StrategicMapModel? Model => s_model;
    public static bool Live => s_live;
    public static string Map => s_map;

    /// <summary>
    /// Opens the page. In a round (<see cref="MatchContext"/>) it is the map being played, with the live positions,
    /// on the player's floor; in the lobby it is <paramref name="map"/> (empty: the lobby's map) without markers.
    /// False when there is no such map (the page then says so through LastError).
    /// </summary>
    public static bool Open(int context, string map)
    {
        string? folder;
        if (context == MatchContext)
        {
            folder = Exports.Current?.Session.MapFolder;
            if (folder is null)
            {
                throw new InvalidOperationException("No round is being played.");
            }
        }
        else
        {
            if (string.IsNullOrEmpty(map))
            {
                map = OpenScct.UnrealBridge.Online.OnlineBridge.Existing?.Client?.Lobby?.Setup.Map ?? "";
            }

            folder = FindMapFolder(map) ?? throw new InvalidOperationException($"The map \"{map}\" is not in the content.");
        }

        if (!string.Equals(folder, s_dataFolder, StringComparison.OrdinalIgnoreCase))
        {
            s_data = StrategicMapData.Load(folder);
            Places = ReadPlaces(folder, s_data);
            s_dataFolder = folder;
        }

        GeometryByFloor.Clear();
        s_live = context == MatchContext;
        s_map = Path.GetFileName(folder.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
        s_model = new StrategicMapModel(s_data);
        (You, Team) = Positions();
        s_model.Open(You, Team);
        return true;
    }

    /// <summary>The page's half-second timer: the blink, and the markers placed again from fresh positions.</summary>
    public static void Tick()
    {
        if (s_model is { } model)
        {
            (You, Team) = Positions();
            model.Tick(You, Team);
        }
    }

    public static bool Up()
    {
        (You, Team) = Positions();
        return s_model?.Up(You, Team) ?? false;
    }

    public static bool Down()
    {
        (You, Team) = Positions();
        return s_model?.Down(You, Team) ?? false;
    }

    public static void Close()
    {
        GeometryByFloor.Clear();
        You = null;
        Team.Clear();
        s_model = null;
        s_live = false;
    }

    /// <summary>
    /// The DisplayStrategicMap key may open the page (<c>SPlayerController.DisplayStrategicMap</c>): there is a round
    /// with a local player, it is not a tutorial map and he is not working an object.
    /// </summary>
    public static bool CanDisplay() => Exports.Current?.Session.LocalController?.CanDisplayStrategicMap() ?? false;

    /// <summary>
    /// <c>UpdateTeamMatesPositions</c> as the Godot client's PlayMenu.LivePositions does it: asks the server for fresh
    /// positions (UpdateMinimapParams; the answer is there for the next ask), then the local pawn and the team mates'
    /// PRIs (same team, not the player, MinimapYaw not -1). The lobby's page has none.
    /// </summary>
    public static (StrategicMapPosition? You, List<StrategicMapPosition> Mates) Positions()
    {
        if (!s_live || Exports.Current?.Session.LocalController is not { } pc)
        {
            return (null, new List<StrategicMapPosition>());
        }

        pc.RequestMinimapParams();
        StrategicMapPosition? you = pc.Pawn is { } pawn
            ? new StrategicMapPosition(new StrategicMapPoint(pawn.Location.X, pawn.Location.Y, pawn.Location.Z), pawn.Rotation.Yaw)
            : null;
        return (you, Mates(pc));
    }

    /// <summary>
    /// The team mates' positions without asking again. Where this machine runs the round (a local match) the mates'
    /// pawns are read directly, which is what UpdateMinimapParams writes for players and also places the bots, whose
    /// AI controllers it skips (the original had no bots). A client reads the PRIs' Minimap fields as Godot does.
    /// </summary>
    public static List<StrategicMapPosition> Mates(SPlayerController pc)
    {
        var mates = new List<StrategicMapPosition>(2);
        PlayerReplicationInfo? own = pc.PlayerReplicationInfo;
        if (own?.Team is not { } team)
        {
            return mates;
        }

        // The local player's client world is a copy even in a local match: the round itself is the host's world.
        if (Exports.Current?.Session is { IsRemote: false } session && session.Host?.World?.Level?.Info is { } level)
        {
            for (Controller? c = level.ControllerList; c is not null; c = c.nextController)
            {
                if (c.PlayerReplicationInfo is { Team: { } t } pri && pri.PlayerID != own.PlayerID && t.TeamIndex == team.TeamIndex && c.Pawn is { } p)
                {
                    mates.Add(new StrategicMapPosition(new StrategicMapPoint(p.Location.X, p.Location.Y, p.Location.Z), p.Rotation.Yaw));
                }
            }

            return mates;
        }

        if (pc.GameReplicationInfo is { } gri)
        {
            foreach (PlayerReplicationInfo? pri in gri.PRIArray)
            {
                if (pri is null || ReferenceEquals(pri, own) || pri.Team is null || pri.Team.TeamIndex != team.TeamIndex || pri.MinimapYaw == -1)
                {
                    continue;
                }

                mates.Add(new StrategicMapPosition(new StrategicMapPoint(pri.MinimapX, pri.MinimapY, pri.MinimapZ), pri.MinimapYaw));
            }
        }

        return mates;
    }

    private static BridgeMapPlace[] ReadPlaces(string folder, StrategicMapData data)
    {
        IReadOnlyList<MapPlace> places = MapIdentity.Load(folder).Places;
        var rows = new BridgeMapPlace[places.Count];
        for (int i = 0; i < rows.Length; i++)
        {
            MapPlace p = places[i];
            var uv = data.Project(new StrategicMapPoint(p.At.X, p.At.Y, p.At.Z));
            rows[i] = new BridgeMapPlace
            {
                Number = p.Number,
                Floor = p.Floor is { } floor && floor < data.Floors.Count ? floor : data.FloorAt(p.At.Z),
                World = p.At, U = uv?.U ?? -1f, V = uv?.V ?? -1f,
            };
        }
        return rows;
    }

    /// <summary>A map's folder by name in the content's map roots (the content's svmap, the packs, the built-in maps).</summary>
    public static string? FindMapFolder(string map)
    {
        if (string.IsNullOrWhiteSpace(map) || map.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            return null;
        }

        foreach (string root in ContentPaths.MapRoots)
        {
            string folder = Path.Combine(root, map);
            if (Directory.Exists(folder))
            {
                return folder;
            }
        }

        return null;
    }
}

public static unsafe class MapExports
{
    public const int ApiVersion = 2;

    private static string s_lastError = "";

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(MapApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Open = &Open;
        api->Tick = &Tick;
        api->Up = &Up;
        api->Down = &Down;
        api->GetState = &GetState;
        api->GetLegend = &GetLegend;
        api->ListPlaces = &ListPlaces;
        api->CanDisplay = &CanDisplay;
        api->Close = &Close;
        api->LastError = &LastError;
        api->ListGeometry = &ListGeometry;
        return ApiVersion;
    }

    // ---- managed helpers (the probe calls these too) ---------------------------------------------------------------

    public static BridgeMapState State()
    {
        BridgeMapState s = default;
        if (StrategicMapPage.Model is not { } model)
        {
            return s;
        }

        StrategicMapData data = model.Data;
        s.FloorCount = data.Floors.Count;
        s.Floor = model.FloorIndex;
        s.HasCamera = data.Camera is { Valid: true } ? 1 : 0;
        s.Bright = model.Bright ? 1 : 0;
        s.Live = StrategicMapPage.Live ? 1 : 0;
        s.HasPicture = model.Picture is not null ? 1 : 0;
        s.PlaceCount = StrategicMapPage.Places.Length;
        s.FloorMinZ = s.Floor > 0 ? data.Floors[s.Floor].FloorZ : float.MinValue;
        s.FloorMaxZ = s.Floor + 1 < s.FloorCount ? data.Floors[s.Floor + 1].FloorZ : float.MaxValue;
        Put(model.Picture, s.Picture, BridgeMapState.PathCapacity);
        Put(StrategicMapPage.Map, s.Map, BridgeMapState.MapCapacity);
        if (model.You is { } you)
        {
            s.YouShown = 1;
            s.YouU = you.U;
            s.YouV = you.V;
            s.YouAngle = you.Angle;
        }

        // Use the same sample for the model's visibility decision and both representations of each marker.
        if (StrategicMapPage.You is { } player)
        {
            s.HasYou = 1;
            s.YouX = player.Location.X;
            s.YouY = player.Location.Y;
            s.YouZ = player.Location.Z;
            s.YouYaw = player.Yaw;
            s.YouFloor = data.FloorAt(player.Location.Z);
        }

        s.MateCount = Math.Min(StrategicMapPage.Team.Count, BridgeMapState.MaxMates);
        for (int i = 0; i < s.MateCount; i++)
        {
            StrategicMapPoint at = StrategicMapPage.Team[i].Location;
            s.MateX[i] = at.X;
            s.MateY[i] = at.Y;
            s.MateZ[i] = at.Z;
            s.MateFloor[i] = data.FloorAt(at.Z);
        }
        IReadOnlyList<StrategicMapMarker?> mates = model.Mates;
        for (int i = 0; i < mates.Count && i < BridgeMapState.MaxMates; i++)
        {
            if (mates[i] is { } m)
            {
                s.MateShown[i] = 1;
                s.MateU[i] = m.U;
                s.MateV[i] = m.V;
                s.MateAngle[i] = m.Angle;
                s.MatesShown++;
            }
        }

        return s;
    }

    // ---- entry points --------------------------------------------------------------------------------------------

    /// <summary>Opens the page: context 0 the round being played, 1 a lobby's map (empty: the lobby's own). Returns 1 or 0.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Open(int context, char* map) => Guard(() => StrategicMapPage.Open(context, Text(map)) ? 1 : 0);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Tick() => Guard(() =>
    {
        StrategicMapPage.Tick();
        return 1;
    });

    /// <summary>The floor above. Returns 1 when the floor changed.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Up() => Guard(() => StrategicMapPage.Up() ? 1 : 0);

    /// <summary>The floor below. Returns 1 when the floor changed.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Down() => Guard(() => StrategicMapPage.Down() ? 1 : 0);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(BridgeMapState* state) => Guard(() =>
    {
        if (state == null)
        {
            return 0;
        }

        *state = State();
        return StrategicMapPage.Model is null ? 0 : 1;
    });

    /// <summary>The legend, lines broken with '\n'. Returns the full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLegend(char* buffer, int capacity) => Guard(() => Copy(StrategicMapPage.Model?.Data.Legend ?? "", buffer, capacity));

    /// <summary>The numbered places on every floor. Returns the full count; past Capacity is not written.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ListPlaces(BridgeMapPlace* places, int capacity) => Guard(() =>
    {
        ReadOnlySpan<BridgeMapPlace> list = StrategicMapPage.Model is null ? [] : StrategicMapPage.Places;
        int count = Math.Clamp(capacity, 0, list.Length);
        if (places != null) list[..count].CopyTo(new Span<BridgeMapPlace>(places, count));
        return list.Length;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ListGeometry(BridgeMapTriangle* triangles, int capacity) => Guard(() =>
    {
        ReadOnlySpan<BridgeMapTriangle> mesh = StrategicMapPage.Geometry();
        int count = Math.Clamp(capacity, 0, mesh.Length);
        if (triangles != null) mesh[..count].CopyTo(new Span<BridgeMapTriangle>(triangles, count));
        return mesh.Length;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CanDisplay() => Guard(() => StrategicMapPage.CanDisplay() ? 1 : 0);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Close() => Guard(() =>
    {
        StrategicMapPage.Close();
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity)
    {
        try
        {
            return Copy(s_lastError, buffer, capacity);
        }
        catch (Exception)
        {
            return 0;
        }
    }

    private static string Text(char* text) => text == null ? "" : new string(text);

    private static int Guard(Func<int> body)
    {
        try
        {
            return body();
        }
        catch (Exception e)
        {
            // Nothing may cross into native code; a fault here never ends a match.
            s_lastError = e is InvalidOperationException ? e.Message : e.ToString();
            return 0;
        }
    }

    private static void Put(string? text, char* target, int capacity)
    {
        text ??= "";
        int count = Math.Min(text.Length, capacity - 1);
        text.AsSpan(0, count).CopyTo(new Span<char>(target, count));
        new Span<char>(target + count, capacity - count).Clear();
    }

    private static int Copy(string text, char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
            buffer[count] = '\0';
        }

        return text.Length;
    }
}
