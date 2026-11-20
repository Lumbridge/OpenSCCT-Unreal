using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using OpenScct.Net.Cosmetics;

namespace OpenScct.UnrealBridge.Loadout;

/// <summary>Weapon finishes earned against bots belong to this local career. They never enter the account look.</summary>
internal sealed class PrivateWeaponFinishes
{
    private string? _path;
    private string? _loadout;
    private long _accountId;
    private Dictionary<CosmeticSlot, string> _selected = [];

    private static Dictionary<CosmeticSlot, string> Read(string path) =>
        File.Exists(path) ? JsonSerializer.Deserialize<Dictionary<CosmeticSlot, string>>(File.ReadAllText(path))
            ?? throw new JsonException("Missing weapon finish selections.") : [];

    public void UseProfile(string loadoutPath, long accountId)
    {
        if (_loadout == loadoutPath && _accountId == accountId) return;
        _loadout = loadoutPath;
        _accountId = accountId;
        string? folder = Path.GetDirectoryName(loadoutPath);
        string? path = string.IsNullOrEmpty(folder) ? null : Path.Combine(folder, $"weapon-finishes-{accountId}.json");
        if (path == _path) return;
        _path = path;
        _selected = [];
        if (path is null) return;
        try { _selected = Read(path); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            Exports.ReportFailure("Could not read private weapon finishes: " + e.Message);
        }
    }

    public bool Has(CosmeticSlot slot) => _selected.ContainsKey(slot);

    public CosmeticLook Apply(CosmeticLook look)
    {
        if (_selected.Count == 0) return look;
        var unlocked = WeaponCareerView.PrivateUnlocks();
        foreach (var (slot, id) in _selected)
        {
            if (CosmeticCatalog.PrimaryClass(slot) is null) continue;
            if (id == string.Empty) look = look.With(slot, null);
            else if (CosmeticCatalog.Find(id) is { Side: CosmeticSide.Merc } item && item.Slot == slot &&
                     unlocked.Contains(id))
                look = look.With(slot, item);
        }
        return look;
    }

    public bool Set(CosmeticSlot slot, string? id)
    {
        if (_path is not { } path) return false;
        try
        {
            string key = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(path.ToUpperInvariant())));
            using var mutex = new Mutex(false, "OpenSCCT-WeaponFinishes-" + key);
            try { mutex.WaitOne(); } catch (AbandonedMutexException) { }
            try
            {
                var selections = Read(path);
                if (id is null) selections.Remove(slot);
                else selections[slot] = id;
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                string temporary = path + ".tmp";
                File.WriteAllText(temporary, JsonSerializer.Serialize(selections));
                File.Move(temporary, path, overwrite: true);
                _selected = selections;
                return true;
            }
            finally { mutex.ReleaseMutex(); }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            Exports.ReportFailure("Could not save private weapon finishes: " + e.Message);
            return false;
        }
    }
}
