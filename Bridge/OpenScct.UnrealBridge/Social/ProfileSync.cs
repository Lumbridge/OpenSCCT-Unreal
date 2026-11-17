using System;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// The settings record that follows a signed-in player between PCs (a port of the Godot client's ProfileSync). The
/// master keeps it as opaque text, one record per account, and the Godot client and this one share it, so each keeps
/// to its own part: the Godot client writes <c>{"version":1, "settings": {...}, "keys_spy": "...", "keys_merc": "..."}</c>
/// and this client adds <c>"unreal": {"version":1, "settings": {key: text}, "controls": "...", "bindings": "..."}</c>
/// beside it, leaving the Godot fields as the account last had them. <c>settings</c> holds the settings registry's
/// values (Unreal leaves out the ones that belong to one PC: display, graphics, performance and devices),
/// <c>controls</c> and <c>bindings</c> the text of Saved/Controls/controls.json and bindings.json (both sides' key
/// layouts; empty when the file is missing). <see cref="AccountService"/> decides when to send and when to take it.
/// </summary>
public static class ProfileSync
{
    /// <summary>The record's version this build writes and the newest it reads (the Godot client's too).</summary>
    public const int Version = 1;

    /// <summary>The version of this client's own part.</summary>
    public const int UnrealVersion = 1;

    public const string UnrealKey = "unreal";

    /// <summary>The record to send: <paramref name="previous"/> (the account's record as last seen, or null) with this client's part replaced.</summary>
    public static string Build(string? previous, string settingsJson, string controls, string bindings)
    {
        JsonObject record = Parse(previous) ?? new JsonObject();
        if (record["version"] is null)
        {
            record["version"] = Version;
        }

        JsonObject settings;
        try
        {
            settings = JsonNode.Parse(string.IsNullOrWhiteSpace(settingsJson) ? "{}" : settingsJson) as JsonObject ?? new JsonObject();
        }
        catch (JsonException)
        {
            settings = new JsonObject();
        }

        record[UnrealKey] = new JsonObject
        {
            ["version"] = UnrealVersion,
            ["settings"] = settings,
            ["controls"] = controls ?? string.Empty,
            ["bindings"] = bindings ?? string.Empty,
        };
        return record.ToJsonString();
    }

    /// <summary>What a record from the master holds for this client.</summary>
    public enum ReadResult
    {
        /// <summary>The record cannot be read.</summary>
        Unreadable,

        /// <summary>A newer build wrote it: left alone.</summary>
        Newer,

        /// <summary>A good record without this client's part (only the Godot client has written it so far).</summary>
        NoUnrealPart,

        Ok,
    }

    /// <summary>Reads this client's part of <paramref name="data"/>; <paramref name="problem"/> says why it cannot be used.</summary>
    public static ReadResult Read(string? data, out string settingsJson, out string controls, out string bindings, out string? problem)
    {
        settingsJson = "{}";
        controls = bindings = string.Empty;
        problem = null;
        JsonObject? record;
        try
        {
            record = string.IsNullOrWhiteSpace(data) ? null : JsonNode.Parse(data) as JsonObject;
        }
        catch (JsonException e)
        {
            problem = "the account's settings could not be read: " + e.Message;
            return ReadResult.Unreadable;
        }

        if (record is null)
        {
            problem = "the account's settings are empty";
            return ReadResult.Unreadable;
        }

        if (record["version"] is JsonValue v && v.TryGetValue(out int version) && version > Version)
        {
            problem = "the account's settings come from a newer build; left alone";
            return ReadResult.Newer;
        }

        if (record[UnrealKey] is not JsonObject mine)
        {
            return ReadResult.NoUnrealPart;
        }

        if (mine["version"] is JsonValue mv && mv.TryGetValue(out int myVersion) && myVersion > UnrealVersion)
        {
            problem = "this client's part of the account's settings comes from a newer build; left alone";
            return ReadResult.Newer;
        }

        settingsJson = mine["settings"] is JsonObject s ? s.ToJsonString() : "{}";
        controls = Text(mine["controls"]);
        bindings = Text(mine["bindings"]);
        return ReadResult.Ok;
    }

    private static string Text(JsonNode? node) => node is JsonValue value && value.TryGetValue(out string? text) && text is not null ? text : string.Empty;

    private static JsonObject? Parse(string? data)
    {
        if (string.IsNullOrWhiteSpace(data))
        {
            return null;
        }

        try
        {
            return JsonNode.Parse(data) as JsonObject;
        }
        catch (JsonException)
        {
            return null;
        }
    }
}
