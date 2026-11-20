using System;
using System.Diagnostics;
using OpenScct.Core.Engine;
using OpenScct.Game.Animation;
using OpenScct.Game.SBase;
using OpenScct.Game.Svm;
using OpenScct.Net.Cosmetics;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Loadout;
using OpenScct.UnrealBridge.Pose;

if (args is ["--weapon-finishes"])
{
    var look = CosmeticLook.FromIds(CosmeticSide.Merc, ["merc.ar.pyre", "merc.smg.golden", "merc.shotgun.golden",
        "merc.m4a1s.desert", "merc.famas.blue_tiger", "merc.unsuppressed.cryo"]);
    var pawn = new SPAWNDEFENSE
    {
        Role = ENetRole.ROLE_SimulatedProxy,
        SidearmWeapon = new MercSidearm { Kind = (int)SidearmKind.Unsuppressed },
        PrimaryWeapon = new M4A1S(),
    };
    void Check(string? held, string? carried)
    {
        string? actualHeld = PawnLooks.GunItem(pawn, look)?.Id;
        string? actualCarried = PawnLooks.GunItem(pawn, look, carried: true)?.Id;
        if (actualHeld != held || actualCarried != carried)
            throw new InvalidOperationException($"Expected held '{held}', carried '{carried}'. Actual '{actualHeld}', '{actualCarried}'.");
    }
    pawn.NetworkAttachments = new NetworkPawnAttachments(0, true);
    Check("merc.m4a1s.desert", "merc.unsuppressed.cryo");
    pawn.NetworkAttachments = new NetworkPawnAttachments(1, true);
    Check("merc.unsuppressed.cryo", "merc.m4a1s.desert");
    pawn.PrimaryWeapon = new BoltRifle();
    Check("merc.unsuppressed.cryo", null);
    pawn.NetworkAttachments = new NetworkPawnAttachments(0, false);
    Check(null, "merc.unsuppressed.cryo");
    pawn.PrimaryWeapon = null;
    pawn.WeaponDummy = new FamasDummy();
    Check("merc.famas.blue_tiger", "merc.unsuppressed.cryo");
    pawn.WeaponDummy = new PumpShotgunDummy();
    Check(null, "merc.unsuppressed.cryo");
    Console.WriteLine("Observer held and holstered weapon finishes, weapon swaps, deferred finishes and dummy-only replication passed.");
    return 0;
}

// Usage: LookProbe [contentRoot] [map] [look] [bots] [merc|merc-kit|merc-kit-no-sidearm]
// Hosts the map with the bots asked for. At 1 s the look (PawnLooks.Parse text, "woodland+blue" by default) goes on the
// player's info on the local server and every bot gets one of its own; at 3 s each pawn's look as this client draws it
// is printed (it came back through replication), with the camo files Unreal would draw on the stock surfaces. The map's
// outfits (render/outfits.json) are printed at the start, and for an outfit model the posed skeleton is checked: how
// many bones the stock clips move on it.
string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
string text = args.Length > 2 ? args[2] : "woodland+blue";
int bots = args.Length > 3 ? int.Parse(args[3], System.Globalization.CultureInfo.InvariantCulture) : 3;
bool checkWeaponPair = args.Length > 4 && args[4] is "merc-kit" or "merc-kit-no-sidearm";
bool enableSidearm = checkWeaponPair && args[4] == "merc-kit";
bool merc = checkWeaponPair || (args.Length > 4 && args[4] == "merc");

BridgeSession.Configure(content);
if (checkWeaponPair)
    LoadoutStore.Set(new LoadoutChoice { MercWeapon = "Svm.M4A1S", MercSidearm = "OpenScct.Sidearm_unsuppressed" });
using BridgeSession session = BridgeSession.StartLocal(new LocalMatch
{
    Map = map, Mode = "Story", Merc = merc, Bots = bots, Rifles = checkWeaponPair, Sidearms = enableSidearm, OfflineRules = checkWeaponPair,
});
PawnLooks looks = PawnLooks.Of(session);
PawnOutfits outfits = session.Outfits;
Console.WriteLine($"outfits of {session.Map}: {(outfits.Outfits.IsEmpty ? "none" : "yes")} {outfits.Warnings}");
for (int kind = 0; kind < 2; kind++)
{
    BridgeKindOutfit o = looks.Outfit(kind);
    Console.WriteLine($"  {(kind == 0 ? "spies" : "mercs")}: dressed {o.Dressed} model '{looks.Name(o.Model)}' map glb {o.MapGlb} policy {o.Policy} " +
        $"body '{looks.Name(o.BodyTexture)}' head '{looks.Name(o.HeadTexture)}' goggles '{looks.Name(o.GoggleBone)}' ({o.GoggleX},{o.GoggleY},{o.GoggleZ}) " +
        $"bones {o.ModelBones}; posed skeleton {session.Character(kind)?.Path} ({session.Character(kind)?.BoneCount} bones), clips from {session.StockCharacter(kind)?.Path}");
}

var run = Stopwatch.StartNew();
bool given = false, printed = false, checkedPair = false;
var buffer = new BridgePawnLook[32];
var bones = new BridgeBone[128];
while (run.Elapsed.TotalSeconds < 4)
{
    var input = default(BridgeInput);
    session.SetInput(in input);
    session.Update();
    if (session.Failure is { } failure)
    {
        Console.WriteLine("FAILED: " + failure);
        return 1;
    }

    if (!given && run.Elapsed.TotalSeconds > 1)
    {
        given = true;
        Console.WriteLine($"force local '{text}': {PawnLooks.ForceLocal(session, text)}, give others: {PawnLooks.GiveOthers(session)}");
    }

    if (!printed && run.Elapsed.TotalSeconds > 3)
    {
        printed = true;
        int count = looks.Looks(buffer);
        Console.WriteLine($"{count} pawns (server failure: {PawnLooks.LastFailure?.Message ?? "none"})");
        for (int i = 0; i < Math.Min(count, buffer.Length); i++)
        {
            BridgePawnLook l = buffer[i];
            const string package = "SPerso";
            string retail = l.Side == 0 ? "SPersoTextures_Shadw_agent" : "SPersoTextures_DEF_01_A_Body";
            int posed = session.GetPawnPose(l.ActorId, bones);
            Console.WriteLine($"  pawn {l.ActorId} {(l.Side == 0 ? "spy" : "merc")} flags {l.Flags} body '{looks.Name(l.BodyItem)}' pattern '{looks.Name(l.BodyPattern)}' " +
                $"kind {l.BodyKind} rgb {l.BodyRgb:x6} light '{looks.Name(l.LightItem)}' hue {l.LightHue} turn {l.HueTurn:F3} gun '{looks.Name(l.GunItem)}' " +
                $"pattern '{looks.Name(l.GunPattern)}' posed {posed} bones");
            Console.WriteLine($"    body camo file: {looks.CosmeticTexture(looks.Name(l.BodyPattern), package, retail) ?? "(none)"}");
            if (checkWeaponPair && (l.Flags & BridgePawnLookFlags.Mine) != 0 &&
                session.Session.Client.World.Level.FindActor(l.ActorId) is SPAWNDEFENSE owner)
            {
                var expected = PawnLooks.Parse(text, CosmeticSide.Merc, out _);
                string? held = looks.Name(l.GunItem), carried = PawnLooks.GunItem(owner, looks.LookOf(owner), carried: true)?.Id;
                Console.WriteLine($"    inventory {owner.PrimaryWeapon?.ItemName} / {owner.SidearmWeapon?.Definition.Id}, " +
                    $"replicated M4 finish '{held}', carried pistol finish '{carried}', " +
                    $"PRI look '{new CosmeticLook(owner.PlayerReplicationInfo!.SvmLook, owner.PlayerReplicationInfo.SvmLookExtra).ToHex()}'");
                Console.WriteLine($"    sidearms enabled {session.Session.LocalController?.GameReplicationInfo?.SidearmsEnabled}, " +
                    $"kit source calls {LoadoutServed.Merc}");
                if (session.Session.Host is { } host)
                    foreach (var serverPawn in host.World.Level.AllActors<SPAWNDEFENSE>())
                        if (!serverPawn.bDeleteMe)
                            Console.WriteLine($"    server starter sidearm '{serverPawn.SidearmWeapon?.Definition.Id}', " +
                                $"unclaimed sidearm grant {serverPawn.GearGrantSidearm}, grant window {serverPawn.GearGrantUntil - serverPawn.Level.TimeSeconds:F1}s");
                if (owner.PrimaryWeapon is not M4A1S || owner.SidearmWeapon?.Definition.Id != (enableSidearm ? "unsuppressed" : null) ||
                    held != expected.WeaponItem("Svm.M4A1S")?.Id || carried != (enableSidearm ? expected.WeaponItem("OpenScct.Sidearm_unsuppressed")?.Id : null))
                    return 1;
                checkedPair = true;
            }
        }

        for (int kind = 0; kind < 2; kind++)
        {
            if (session.PoserOf(FirstOf(kind)) is { } poser)
            {
                int moved = 0;
                foreach (ChannelPose ch in poser.DrawnChannels)
                {
                    if (ch.Clip is not null)
                    {
                        moved++;
                    }
                }

                Console.WriteLine($"  {(kind == 0 ? "spy" : "merc")} poser: skeleton {poser.Character.Path} clips {poser.Clips.Path}, {moved} channels drawn");
            }
        }
    }

    System.Threading.Thread.Sleep(5);
}

return checkWeaponPair && !checkedPair ? 1 : 0;

int FirstOf(int kind)
{
    int count = looks.Looks(buffer);
    for (int i = 0; i < Math.Min(count, buffer.Length); i++)
    {
        if (buffer[i].Side == kind)
        {
            return buffer[i].ActorId;
        }
    }

    return 0;
}
