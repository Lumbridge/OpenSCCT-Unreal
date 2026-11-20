using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Net.Discovery;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Social;

namespace OpenScct.UnrealBridge.Loadout;

/// <summary>Offline matches own a local career. Online matches always read the game server's totals.</summary>
public static class WeaponCareerView
{
    private static readonly object Gate = new();
    private static string? _path;
    private static WeaponCareer? _local;
    private static AccountInfo? _lastAccount;
    private static PrivateWeaponMastery _save = new() { Kills = new int[WeaponCareerCounters.Count] };
    private static IReadOnlyDictionary<string, AchievementInfo> _rows = new Dictionary<string, AchievementInfo>();
    private static IReadOnlySet<string> _unlocks = new HashSet<string>();
    private static long _revision;

    public static long MasteryRevision { get { lock (Gate) { Local(); return _revision; } } }
    public static IReadOnlyDictionary<string, AchievementInfo> PrivateMastery()
    {
        lock (Gate) { Local(); return _rows; }
    }
    public static IReadOnlySet<string> PrivateUnlocks()
    {
        lock (Gate) { Local(); return _unlocks; }
    }

    private static void Refresh(PrivateWeaponMastery save, Dictionary<string, AchievementInfo> rows)
    {
        _save = save;
        _rows = rows;
        _unlocks = rows.Values.Where(row => row.Earned.HasValue)
            .SelectMany(row => row.Rewards).ToHashSet(StringComparer.Ordinal);
        _revision++;
    }

    public static int GetKills(string weaponClass)
    {
        if (OnlineBridge.Existing?.Client is { State: OnlineClientState.Lobby or OnlineClientState.Loading
            or OnlineClientState.Match or OnlineClientState.Results } visit)
            return visit.WeaponProgress.Kills(weaponClass);
        return Local().Kills(weaponClass);
    }

    internal static WeaponCareer Local()
    {
        var service = SocialBridge.Existing?.Account;
        long account = service?.SignedInAccountId ?? 0;
        var signedAccount = account > 0 && service?.Account?.Id == account ? service.Account : null;
        string? folder = Path.GetDirectoryName(LoadoutStore.FilePath);
        string? path = string.IsNullOrEmpty(folder) ? null : Path.Combine(folder, $"weapon-kills-{account}.json");
        lock (Gate)
        {
            if (_local is not null && path == _path && ReferenceEquals(signedAccount, _lastAccount)) return _local;
            if (_local == null || _path != path)
            {
                PrivateWeaponMastery saved;
                try { saved = PrivateWeaponMastery.Read(path); }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
                {
                    Exports.ReportFailure("Could not read weapon mastery: " + ex.Message);
                    saved = PrivateWeaponMastery.Read(null);
                }
                var earnedAt = path != null && File.Exists(path) ? File.GetLastWriteTimeUtc(path) : DateTime.UtcNow;
                Refresh(saved, saved.Milestones(earnedAt));
                _local = new WeaponCareer(saved.Kills);
                _local.KillConfirmed += (index, headshot) => SaveKill(path, index, headshot);
                _path = path;
            }
            // Account kills can also unlock gear against bots. The online server never reads this local file.
            if (signedAccount?.Career?.Feats is { } feats)
            {
                int[] online = WeaponCareerCounters.FromFeats(feats);
                for (int i = 0; i < online.Length; i++)
                {
                    long offline = Math.Max(0, _save.Kills[i]);
                    _local.SetMinimum(i, (int)Math.Min(int.MaxValue, online[i] + offline));
                }
            }
            _lastAccount = signedAccount;
            return _local;
        }
    }

    private static void SaveKill(string? path, int index, bool headshot)
    {
        lock (Gate)
        {
            if (path == null)
            {
                if (_path == null)
                {
                    _save.Increment(index, headshot);
                    Refresh(_save, _save.Milestones(DateTimeOffset.UtcNow));
                }
                return;
            }
            SaveKillToDisk(path, index, headshot);
        }
    }

    private static void SaveKillToDisk(string path, int index, bool headshot)
    {
        try
        {
            string key = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(path.ToUpperInvariant())));
            using var mutex = new Mutex(false, "OpenSCCT-WeaponCareer-" + key);
            try { mutex.WaitOne(); } catch (AbandonedMutexException) { /* The previous writer exited. */ }
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                var saved = PrivateWeaponMastery.Read(path);
                // Reconstruct old earned dates from the original file before awarding the current kill.
                if (saved.Earned.Count == 0 && File.Exists(path)) saved.Milestones(File.GetLastWriteTimeUtc(path));
                saved.Increment(index, headshot);
                var rows = saved.Milestones(DateTimeOffset.UtcNow);
                string temporary = path + ".tmp";
                File.WriteAllText(temporary, JsonSerializer.Serialize(saved));
                File.Move(temporary, path, overwrite: true);
                if (_path == path) Refresh(saved, rows);
            }
            finally { mutex.ReleaseMutex(); }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            Exports.ReportFailure("Could not save weapon kills: " + ex.Message);
        }
    }
}
