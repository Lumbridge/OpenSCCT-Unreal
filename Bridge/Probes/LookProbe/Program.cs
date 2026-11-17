using System;
using System.Diagnostics;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Pose;

// Usage: LookProbe [contentRoot] [map] [look] [bots] [merc]
// Hosts the map with the bots asked for. At 1 s the look (PawnLooks.Parse text, "woodland+blue" by default) goes on the
// player's info on the local server and every bot gets one of its own; at 3 s each pawn's look as this client draws it
// is printed (it came back through replication), with the camo files Unreal would draw on the stock surfaces. The map's
// outfits (render/outfits.json) are printed at the start, and for an outfit model the posed skeleton is checked: how
// many bones the stock clips move on it.
string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
string text = args.Length > 2 ? args[2] : "woodland+blue";
int bots = args.Length > 3 ? int.Parse(args[3], System.Globalization.CultureInfo.InvariantCulture) : 3;
bool merc = args.Length > 4 && args[4] == "merc";

BridgeSession.Configure(content);
using BridgeSession session = BridgeSession.StartLocal(new LocalMatch { Map = map, Mode = "Story", Merc = merc, Bots = bots });
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
bool given = false, printed = false;
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

return 0;

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
