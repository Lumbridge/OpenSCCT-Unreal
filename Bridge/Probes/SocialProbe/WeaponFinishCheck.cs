using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using OpenScct.Core.Engine;
using OpenScct.Net.Cosmetics;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Loadout;
using OpenScct.UnrealBridge.Social;

internal static class WeaponFinishCheck
{
    public static unsafe int Run()
    {
        string root = Path.Combine(Path.GetTempPath(), "scct-private-finishes-" + Guid.NewGuid().ToString("N"));
        string veteran = Path.Combine(root, "veteran");
        string rookie = Path.Combine(root, "rookie");
        Directory.CreateDirectory(veteran);
        Directory.CreateDirectory(rookie);
        int[] kills = new int[WeaponCareerCounters.Count];
        kills[WeaponCareerCounters.IndexOf("Svm.M4A1S")] = 50;
        File.WriteAllText(Path.Combine(veteran, "weapon-kills-0.json"), JsonSerializer.Serialize(kills));
        var service = SocialBridge.Instance.Account;
        var side = CosmeticSide.Merc;
        var slot = CosmeticSlot.M4A1S;
        const string wood = "merc.m4a1s.woodland";
        const string desert = "merc.m4a1s.desert";
        int errors = 0;
        void Check(bool ok, string message)
        {
            Console.WriteLine((ok ? "OK   " : "FAIL ") + message);
            if (!ok) errors++;
        }
        bool Chosen(string id)
        {
            int index = CosmeticCatalog.All.ToList().FindIndex(i => i.Id == id);
            return service.Cosmetic(index, out var block, out _, out _, out _) && (block.Flags & 2) != 0;
        }
        string Exported(int kind)
        {
            ArmouryApi api;
            delegate* unmanaged<ArmouryApi*, int> get = &ArmouryExports.GetApi;
            get(&api);
            char* buffer = stackalloc char[512];
            api.GetText(kind, 1, 0, buffer, 512);
            return new string(buffer);
        }
        LoadoutStore.Load(Path.Combine(veteran, "loadout.json"));
        service.SetChosenCosmetics(side, "merc.ar.woodland");
        var before = (Exported(1), Exported(4), Exported(6));
        Check(service.ChooseCosmetic(side, slot, wood, false) && Chosen(wood), "a private career can equip its earned M4 finish");
        Check(!service.ChooseCosmetic(side, slot, desert, false) && Chosen(wood), "an unearned selection preserves the equipped finish");
        Check(before == (Exported(1), Exported(4), Exported(6)), "private choices stay out of saved account settings, network look and shared own-look setting");
        LoadoutStore.Load(Path.Combine(rookie, "loadout.json"));
        Check(!Chosen(wood) && !service.ChooseCosmetic(side, slot, wood, false), "a fresh profile inherits neither the selection nor the reward");
        LoadoutStore.Load(Path.Combine(veteran, "loadout.json"));
        Check(Chosen(wood), "reopening the veteran profile restores the private finish");
        Check(service.ChooseCosmetic(side, slot, null, false) && !Chosen(wood), "the private finish can be taken off");
        Check(before == (Exported(1), Exported(4), Exported(6)), "returning to default preserves the previous shared look");
        return errors == 0 ? 0 : 1;
    }
}
