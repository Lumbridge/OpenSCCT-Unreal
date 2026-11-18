using System.Text.Json;

namespace OpenScctGame.Render.Data;

// FxDoc's only dependency on the Godot material reader is its optional JSON string accessor.
internal static class MaterialsDoc
{
    internal static string? Str(JsonElement obj, string key) =>
        obj.ValueKind == JsonValueKind.Object && obj.TryGetProperty(key, out JsonElement value) &&
        value.ValueKind == JsonValueKind.String ? value.GetString() : null;
}
