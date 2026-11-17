using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Social;

/// <summary>
/// <c>SocialProbe account [masterUrl] [inviteKey] [options]</c>: the account service against a master, without Unreal.
/// The sign-in token comes from --token, else the SCCT_ACCOUNT_TOKEN variable, else the sign-in an Unreal run
/// saved sealed with DPAPI in Saved/Account/AccountToken.dpapi (searched from the working folder up); with none a made-up
/// token is sent, and the master's 401 must sign the player out locally. Options:
///   --token T        the session token to use
///   --sync           also sync settings: take the account's, then change one and see it sent 3 s later
///   --claim NAME     claim or change the name
///   --prestige       enter the next prestige (only at level 55)
///   --signout        sign out on the master at the end (the token stops working)
/// It prints the account's info, progress, titles and career, and every line the service logs.
/// </summary>
internal static class AccountCheck
{
    private const string DefaultMaster = "https://master.lumbridge.co.uk";
    private const string DefaultInvite = "b7cee2a094feca62d43bdac7f80be755a98c62a85c5977a0";

    public static int Run(string[] args)
    {
        var positional = new List<string>();
        string? token = null, claim = null;
        bool sync = false, prestige = false, signOut = false;
        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--token" when i + 1 < args.Length: token = args[++i]; break;
                case "--claim" when i + 1 < args.Length: claim = args[++i]; break;
                case "--sync": sync = true; break;
                case "--prestige": prestige = true; break;
                case "--signout": signOut = true; break;
                default: positional.Add(args[i]); break;
            }
        }

        int failures = 0;
        void Check(bool ok, string what)
        {
            if (!ok)
            {
                failures++;
                Console.WriteLine("FAILED: " + what);
            }
        }

        // The record sizes Source/OpenSCCT/Runtime/ScctAccountApi.h asserts for its mirrors.
        unsafe
        {
            Console.WriteLine($"account table: {sizeof(AccountApi)} bytes; info {sizeof(AccountInfoBlock)}, title {sizeof(AccountTitle)}, rank {sizeof(AccountRank)} bytes");
            Check(sizeof(AccountApi) == 8 + 14 * sizeof(nint) && sizeof(AccountInfoBlock) == 104 && sizeof(AccountTitle) == 12 && sizeof(AccountRank) == 20,
                "the layout does not match Runtime/ScctAccountApi.h");
        }

        CheckRecords(Check);

        string url = positional.Count > 0 && positional[0].Length > 0 ? positional[0] : DefaultMaster;
        string invite = positional.Count > 1 ? positional[1] : DefaultInvite;
        string source = "--token";
        if (token is null && Environment.GetEnvironmentVariable("SCCT_ACCOUNT_TOKEN") is { Length: > 0 } env)
        {
            token = env;
            source = "SCCT_ACCOUNT_TOKEN";
        }

        token ??= SavedToken(out source);
        bool madeUp = token is null;
        if (madeUp)
        {
            token = "probe-" + Guid.NewGuid().ToString("N");
            source = "made up (no saved sign-in found)";
        }

        Console.WriteLine($"master {url}, token: {source}");
        string stateFile = Path.Combine(Path.GetTempPath(), "scct-account-probe-" + Environment.ProcessId + ".json");
        OnlineBridge online = OnlineBridge.Instance;
        SocialBridge social = SocialBridge.Instance;
        AccountService account = social.Account;
        online.Master.Configure(url, invite);
        online.Master.SetAccountToken(token);
        account.Start(stateFile, syncOff: !sync, preview: false);
        // Nothing made up is ever sent to the account: empty key layouts mean "keep each PC's own", and the settings
        // sent are the account's own copy (or none) plus one probe-only key no client reads.
        string localSettings = "{}", localControls = string.Empty, localBindings = string.Empty;
        account.SetLocalProfile(localSettings, localControls, localBindings, changed: false);

        var log = new List<string>();
        var requests = new List<(SocialRequest Request, string Text)>();
        void Pump()
        {
            online.Update();
            social.Update(0.05, SocialReadiness.None);
            while (social.TakeLog(out string line))
            {
                if (line.StartsWith("Account:", StringComparison.Ordinal) || line.StartsWith("Social:", StringComparison.Ordinal))
                {
                    Console.WriteLine($"  [{Clock.Elapsed.TotalSeconds,6:0.00}s] {line}");
                    log.Add(line);
                }
            }

            while (social.TakeRequest(out SocialRequest request, out _, out string text))
            {
                requests.Add((request, text));
                Console.WriteLine($"  [{Clock.Elapsed.TotalSeconds,6:0.00}s] request for Unreal: {(SocialRequestKind)request.Kind} ({text})");
            }

            Thread.Sleep(50);
        }

        bool Wait(Func<bool> done, double seconds)
        {
            var until = Clock.Elapsed + TimeSpan.FromSeconds(seconds);
            while (Clock.Elapsed < until)
            {
                Pump();
                if (done())
                {
                    return true;
                }
            }

            return false;
        }

        AccountInfoBlock info = default;
        bool Known() { account.Fill(ref info); return (info.Flags & (int)AccountFlags.Known) != 0; }
        bool SignedOutRequest() => requests.Any(r => r.Request.Kind == (int)SocialRequestKind.SignedOut);
        bool RefreshFailed() { account.Fill(ref info); return (info.Flags & (int)AccountFlags.RefreshFailed) != 0; }

        Wait(() => Known() || SignedOutRequest() || RefreshFailed(), 20);
        account.Fill(ref info);
        if (SignedOutRequest())
        {
            Console.WriteLine($"the master refused the sign-in: signed out locally (view {(AccountView)info.View}, token kept: {online.Master.AccountToken is not null}); status: {account.Text(AccountText.Status)}");
            Check(online.Master.AccountToken is null && info.View == (int)AccountView.SignedOut, "a 401 must forget the token and show the signed-out view");
        }
        else if (RefreshFailed())
        {
            Console.WriteLine("the master did not answer (offline): " + log.LastOrDefault());
        }
        else if (Known())
        {
            PrintAccount(account, ref info);
            if (claim is not null)
            {
                Check(account.ClaimName(claim), "the claim should be sent: " + account.Text(AccountText.Status));
                Wait(() => { account.Fill(ref info); return (info.Flags & (int)AccountFlags.Claiming) == 0; }, 15);
                Console.WriteLine("claim: " + account.Text(AccountText.Status) + $" (online bridge's name now '{online.Master.AccountName}')");
            }

            if (prestige)
            {
                Check(account.Prestige(), "the prestige should be sent");
                Wait(() => { account.Fill(ref info); return (info.Flags & (int)AccountFlags.Prestiging) == 0; }, 15);
                Console.WriteLine("prestige: " + account.Text(AccountText.Status));
                PrintAccount(account, ref info);
            }

            if (sync)
            {
                // The pull after the sign-in: the account's copy is taken, or this PC's is sent when the account has none.
                Wait(() => account.SyncState is ProfileSyncState.Idle && account.Incoming is null || account.Incoming is not null, 15);
                if (account.Incoming is { } incoming)
                {
                    Console.WriteLine($"incoming settings: {incoming.Settings}; controls {incoming.Controls.Length} chars; bindings {incoming.Bindings.Length} chars (applied, as Unreal would)");
                    localSettings = incoming.Settings.Length > 0 ? incoming.Settings : "{}";
                    localControls = incoming.Controls;
                    localBindings = incoming.Bindings;
                    account.FinishIncoming(true);
                }

                Wait(() => account.SyncState == ProfileSyncState.Idle, 15);
                var settings = System.Text.Json.Nodes.JsonNode.Parse(localSettings)?.AsObject() ?? new System.Text.Json.Nodes.JsonObject();
                settings["Probe.SyncCheck"] = DateTimeOffset.UtcNow.ToUnixTimeSeconds().ToString(System.Globalization.CultureInfo.InvariantCulture);
                string changed = settings.ToJsonString();
                Console.WriteLine($"changing the probe-only key Probe.SyncCheck ({settings.Count} setting(s), {changed.Length} chars)");
                TimeSpan queued = Clock.Elapsed;
                int seen = log.Count;
                account.SetLocalProfile(changed, localControls, localBindings, changed: true);
                Check(account.SyncState == ProfileSyncState.Queued, "a change must queue a send");
                bool sent = Wait(() => log.Skip(seen).Any(l => l.StartsWith("Account: profile sync sent", StringComparison.Ordinal)), 15);
                double after = (Clock.Elapsed - queued).TotalSeconds;
                Console.WriteLine(sent ? $"profile sync: sent {after:0.0} s after the change" : "profile sync: nothing was sent");
                Check(sent && after >= 2.9, "the settings must be sent 3 s after the change");
            }

            if (signOut)
            {
                Check(account.SignOut(), "the sign-out should be sent");
                Wait(SignedOutRequest, 15);
                account.Fill(ref info);
                Console.WriteLine($"signed out: view {(AccountView)info.View}; status: {account.Text(AccountText.Status)}");
                Check(info.View == (int)AccountView.SignedOut && online.Master.AccountToken is null, "the sign-out must forget the token");
            }
        }
        else
        {
            Console.WriteLine("no answer from the master in 20 s");
            failures++;
        }

        try
        {
            File.Delete(stateFile);
        }
        catch (IOException)
        {
        }

        Console.WriteLine();
        Console.WriteLine(failures == 0 ? "PROBE OK" : $"PROBE FAILED ({failures})");
        return failures == 0 ? 0 : 1;
    }

    private static readonly Stopwatch Clock = Stopwatch.StartNew();

    private static void PrintAccount(AccountService account, ref AccountInfoBlock info)
    {
        account.Fill(ref info);
        AccountFlags flags = (AccountFlags)info.Flags;
        Console.WriteLine($"account {info.AccountId}: '{account.Text(AccountText.Name)}', view {(AccountView)info.View}, flags {flags}");
        if ((flags & AccountFlags.NameLocked) != 0)
        {
            Console.WriteLine("  " + account.Text(AccountText.NameCooldown));
        }

        if ((flags & AccountFlags.HasProgress) != 0)
        {
            Console.WriteLine($"  rank {account.Text(AccountText.RankTag)} ({account.Text(AccountText.RankDescription)}), colour #{info.RankColour:x6}");
            Console.WriteLine($"  level {info.Level}, prestige {info.Prestige}, XP {info.XpIntoLevel} / {info.XpForNext}: {account.Text(AccountText.NextLevel)}; {account.Text(AccountText.TotalXp)}; best level {info.BestLevel}");
            Console.WriteLine($"  emblems reached: {info.EmblemsReached} of 11{((flags & AccountFlags.Master) != 0 ? " (the master emblem too)" : string.Empty)}; prestige: {account.Text(AccountText.PrestigeHelp)}");
            var unlocked = new List<string>();
            for (int i = 0; account.Title(i, out AccountTitle t, out string id, out string text, out _); i++)
            {
                if (t.Unlocked != 0)
                {
                    unlocked.Add(text + (t.Chosen != 0 ? " (chosen)" : string.Empty));
                }
            }

            Console.WriteLine($"  titles {info.TitlesUnlocked} of {info.TitlesTotal}: {string.Join(", ", unlocked)}; shown: {account.Text(AccountText.Title)}");
        }
        else
        {
            Console.WriteLine("  this master has no levels");
        }

        Console.WriteLine($"  career: {info.Matches} matches, {info.SpyWins} spy wins, {info.MercWins} merc wins, {info.Losses} losses, {info.Kills} kills, {info.Deaths} deaths; {account.Text(AccountText.CareerHelp)}");
    }

    /// <summary>The settings record kept apart from the Godot client's part, offline.</summary>
    private static void CheckRecords(Action<bool, string> check)
    {
        const string godot = "{\"version\":1,\"settings\":{\"master_volume\":0.8},\"keys_spy\":\"[spy]\\nfire=Mouse1\",\"keys_merc\":null}";
        check(ProfileSync.Read(godot, out _, out _, out _, out _) == ProfileSync.ReadResult.NoUnrealPart, "a Godot-only record has no part of this client");
        string built = ProfileSync.Build(godot, "{\"Audio.Jukebox\":\"0.5\"}", "{\"a\":1}", "{\"b\":2}");
        check(built.Contains("master_volume", StringComparison.Ordinal) && built.Contains("keys_spy", StringComparison.Ordinal), "the Godot part must be sent back unchanged");
        check(ProfileSync.Read(built, out string s, out string c, out string b, out _) == ProfileSync.ReadResult.Ok && s.Contains("Audio.Jukebox", StringComparison.Ordinal) &&
            c == "{\"a\":1}" && b == "{\"b\":2}", "this client's part must read back");
        check(ProfileSync.Read("{\"version\":2}", out _, out _, out _, out _) == ProfileSync.ReadResult.Newer, "a newer record is left alone");
        check(ProfileSync.Read("not json", out _, out _, out _, out _) == ProfileSync.ReadResult.Unreadable, "a broken record is refused");
        Console.WriteLine("records: " + built);
    }

    private static string? SavedToken(out string source)
    {
        source = string.Empty;
        for (DirectoryInfo? dir = new(Directory.GetCurrentDirectory()); dir is not null; dir = dir.Parent)
        {
            // The sign-in an Unreal run saved, sealed with DPAPI for this Windows user (Online/ScctTokenStore.cpp).
            string sealedFile = Path.Combine(dir.FullName, "Saved", "Account", "AccountToken.dpapi");
            if (File.Exists(sealedFile) && Unseal(File.ReadAllBytes(sealedFile)) is { Length: > 0 } token)
            {
                source = sealedFile;
                return token;
            }
        }

        return null;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct DataBlob
    {
        public int Size;
        public IntPtr Data;
    }

    [DllImport("crypt32.dll", SetLastError = true)]
    private static extern bool CryptUnprotectData(ref DataBlob input, IntPtr description, ref DataBlob entropy, IntPtr reserved, IntPtr prompt, int flags, out DataBlob output);

    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);

    /// <summary>A token sealed by ScctTokenStore (same entropy), or null when this user cannot open it.</summary>
    private static string? Unseal(byte[] sealedBytes)
    {
        byte[] entropyBytes = Encoding.ASCII.GetBytes("OpenSCCT.AccountToken.v1");
        GCHandle input = GCHandle.Alloc(sealedBytes, GCHandleType.Pinned);
        GCHandle entropy = GCHandle.Alloc(entropyBytes, GCHandleType.Pinned);
        try
        {
            var inBlob = new DataBlob { Size = sealedBytes.Length, Data = input.AddrOfPinnedObject() };
            var entropyBlob = new DataBlob { Size = entropyBytes.Length, Data = entropy.AddrOfPinnedObject() };
            if (!CryptUnprotectData(ref inBlob, IntPtr.Zero, ref entropyBlob, IntPtr.Zero, IntPtr.Zero, 1, out DataBlob outBlob))
            {
                return null;
            }

            byte[] plain = new byte[outBlob.Size];
            Marshal.Copy(outBlob.Data, plain, 0, outBlob.Size);
            LocalFree(outBlob.Data);
            return Encoding.UTF8.GetString(plain);
        }
        finally
        {
            input.Free();
            entropy.Free();
        }
    }
}