using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using OpenScct.Core.Engine;
using OpenScct.Net.Cosmetics;
using OpenScct.Net.Discovery;

namespace OpenScct.UnrealBridge.Loadout;

internal sealed class PrivateWeaponMastery
{
    public required int[] Kills { get; set; }
    public int[] Headshots { get; set; } = new int[WeaponCareerCounters.Count];
    public Dictionary<string, DateTimeOffset> Earned { get; set; } = new(StringComparer.Ordinal);

    public static PrivateWeaponMastery Read(string? path)
    {
        if (path == null || !File.Exists(path))
            return new() { Kills = new int[WeaponCareerCounters.Count] };
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var save = document.RootElement.ValueKind == JsonValueKind.Array
            ? new PrivateWeaponMastery { Kills = document.RootElement.Deserialize<int[]>()! }
            : document.RootElement.Deserialize<PrivateWeaponMastery>();
        if (save?.Kills == null || save.Headshots == null || save.Earned == null)
            throw new JsonException("The weapon career file has invalid counters.");
        save.Kills = Grow(save.Kills);
        save.Headshots = Grow(save.Headshots);
        return save;
    }

    private static int[] Grow(int[] counts)
    {
        if (counts.Length < WeaponCareerCounters.Count) Array.Resize(ref counts, WeaponCareerCounters.Count);
        return counts;
    }

    public void Increment(int index, bool headshot)
    {
        if (Kills[index] < int.MaxValue) Kills[index]++;
        if (headshot && Headshots[index] < int.MaxValue) Headshots[index]++;
    }

    public Dictionary<string, AchievementInfo> Milestones(DateTimeOffset now)
    {
        var feats = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var gun in MercPrimaryMastery.All)
        {
            int index = WeaponCareerCounters.IndexOf(gun.ClassPath);
            feats[FeatKeys.GunKills(gun.Id)] = Math.Max(0, Kills[index]);
            feats[FeatKeys.GunHeadshots(gun.Id)] = Math.Clamp(Headshots[index], 0, feats[FeatKeys.GunKills(gun.Id)]);
        }
        var progress = new AccountProgress { Feats = feats };
        var rows = new Dictionary<string, AchievementInfo>(StringComparer.Ordinal);
        foreach (var achievement in Achievements.PrimaryMasteries)
        {
            var (current, target) = achievement.Goal(progress);
            DateTimeOffset? earned = null;
            if (current >= target)
            {
                progress.Earned.Add(achievement.Id);
                if (!Earned.TryGetValue(achievement.Id, out var date)) Earned[achievement.Id] = date = now;
                earned = date;
            }
            rows.Add(achievement.Id, new AchievementInfo
            {
                Id = achievement.Id, Title = achievement.Title, Description = achievement.Description,
                Category = achievement.Category.ToString(), Current = current, Target = target,
                Earned = earned, Rewards = achievement.Rewards,
            });
        }
        return rows;
    }
}
