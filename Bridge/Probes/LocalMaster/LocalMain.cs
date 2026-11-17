using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Threading.Tasks;

namespace OpenScct.Master;

/// <summary>
/// A master on this PC for checking the account features signed in (see LocalMaster.csproj).
/// <code>
/// LocalMaster seed &lt;dataDir&gt; [--prestige P --level L] [--name NAME]   a test account and a session; prints its token
/// LocalMaster run &lt;dataDir&gt; [port]                                        the master on http://127.0.0.1:port (default 28800)
/// </code>
/// The account is linked to the made-up Discord id "local-test"; its token goes to &lt;dataDir&gt;/token.txt as well.
/// The run takes OpenSCCT's shipped invite key, so a game with the default settings is let in.
/// </summary>
internal static class LocalMain
{
    private const string InviteKey = "b7cee2a094feca62d43bdac7f80be755a98c62a85c5977a0";

    private static async Task<int> Main(string[] args)
    {
        if (args.Length < 2 || args[0] is not ("seed" or "run"))
        {
            Console.WriteLine("usage: LocalMaster seed <dataDir> [--prestige P --level L] [--name NAME] | LocalMaster run <dataDir> [port]");
            return 2;
        }

        string dir = Path.GetFullPath(args[1]);
        Directory.CreateDirectory(dir);
        return args[0] == "seed" ? Seed(dir, args[2..]) : await Run(dir, args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 28800).ConfigureAwait(false);
    }

    private static int Seed(string dir, string[] args)
    {
        int prestige = -1, level = -1;
        string? name = null;
        for (int i = 0; i + 1 < args.Length; i += 2)
        {
            switch (args[i])
            {
                case "--prestige": prestige = int.Parse(args[i + 1], CultureInfo.InvariantCulture); break;
                case "--level": level = int.Parse(args[i + 1], CultureInfo.InvariantCulture); break;
                case "--name": name = args[i + 1]; break;
            }
        }

        using AccountStore store = AccountStore.Open(Path.Combine(dir, "accounts.db"));
        using (AccountStore.ActingAs("local-master-seed"))
        {
            AccountRow account = store.GetOrCreate("local-test");
            if (prestige >= 0 && level >= 1)
            {
                store.SetRank(account.Id, prestige, level);
            }

            if (name is not null)
            {
                NameClaim claim = store.ClaimName(account.Id, name, ignoreCooldown: true, out _);
                Console.WriteLine("name: " + claim);
            }

            string token = store.CreateSession(account.Id);
            File.WriteAllText(Path.Combine(dir, "token.txt"), token);
            Console.WriteLine($"account {account.Id} (discord local-test){(prestige >= 0 ? $", prestige {prestige} level {level}" : string.Empty)}");
            Console.WriteLine("TOKEN=" + token);
        }

        return 0;
    }

    private static async Task<int> Run(string dir, int port)
    {
        var options = new MasterOptions
        {
            HttpUrl = "http://127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture),
            PublicHost = "127.0.0.1",
            PublicUrl = "http://127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture),
            Introducer = false,
            Relay = false,
            Probe = false,
            InviteKeys = new List<string> { InviteKey },
            Motd = "Local test master",
            DataDirectory = dir,
            Discord = new DiscordOptions { ClientId = "local-test", ClientSecret = "local-test" },
        };
        await using MasterHost master = await MasterHost.StartAsync(options).ConfigureAwait(false);
        Console.WriteLine("local master on " + master.HttpAddress + ", data in " + dir);
        await master.WaitForShutdownAsync().ConfigureAwait(false);
        return 0;
    }
}
