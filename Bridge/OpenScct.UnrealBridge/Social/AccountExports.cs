using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core.Engine;
using OpenScct.Net.Cosmetics;
using OpenScct.UnrealBridge.Social;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The account table (Source/OpenSCCT/Runtime/ScctAccountApi.h mirrors it): the signed-in account, its rank and
/// progress, its titles, the Account page's actions and the settings sync, all of <see cref="AccountService"/>, which
/// the social table's Update runs each frame. Unreal asks for <see cref="GetApi"/> once and keeps the table. Every
/// entry catches its exceptions (none may cross into native code); an unexpected one goes to the main table's
/// LastError and the call returns its empty answer. Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct AccountApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int, void> Start;
    public delegate* unmanaged[Cdecl]<AccountInfoBlock*, int> GetInfo;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<int, AccountTitle*, char*, int, char*, int, char*, int, int> GetTitle;
    public delegate* unmanaged[Cdecl]<int, void> Refresh;
    public delegate* unmanaged[Cdecl]<char*, int> ClaimName;
    public delegate* unmanaged[Cdecl]<int> Prestige;
    public delegate* unmanaged[Cdecl]<int> SignOut;
    public delegate* unmanaged[Cdecl]<char*, void> SetChosenTitle;
    public delegate* unmanaged[Cdecl]<char*, char*, char*, int, void> SetLocalProfile;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetIncomingProfile;
    public delegate* unmanaged[Cdecl]<int, void> FinishIncomingProfile;
    public delegate* unmanaged[Cdecl]<int, AccountRank*, char*, int, int> DescribeRank;
    public delegate* unmanaged[Cdecl]<int, int> EmblemColour;
}

public static unsafe class AccountExports
{
    public const int ApiVersion = 2;

    /// <summary>AccountApi.Start's flags.</summary>
    public const int StartSyncOff = 1;

    public const int StartPreview = 2;

    private static AccountService Account => SocialBridge.Instance.Account;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(AccountApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Start = &Start;
        api->GetInfo = &GetInfo;
        api->GetText = &GetText;
        api->GetTitle = &GetTitle;
        api->Refresh = &Refresh;
        api->ClaimName = &ClaimName;
        api->Prestige = &Prestige;
        api->SignOut = &SignOut;
        api->SetChosenTitle = &SetChosenTitle;
        api->SetLocalProfile = &SetLocalProfile;
        api->GetIncomingProfile = &GetIncomingProfile;
        api->FinishIncomingProfile = &FinishIncomingProfile;
        api->DescribeRank = &DescribeRank;
        api->EmblemColour = &EmblemColour;
        return ApiVersion;
    }

    /// <summary>Starts the service once with the file that keeps its state between runs and <see cref="StartSyncOff"/> or <see cref="StartPreview"/>.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Start(char* stateFile, int flags)
    {
        string file = Text(stateFile);
        Guarded(() =>
        {
            Account.Start(file, (flags & StartSyncOff) != 0, (flags & StartPreview) != 0);
            return 0;
        });
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetInfo(AccountInfoBlock* info)
    {
        if (info == null)
        {
            return 0;
        }

        try
        {
            AccountInfoBlock block = default;
            Account.Fill(ref block);
            *info = block;
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            *info = default;
            return 0;
        }
    }

    /// <summary>A text of the account (<see cref="AccountText"/>). Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, Account.Text((AccountText)kind));
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>The catalogue's title at <paramref name="index"/>: bit, unlocked, chosen, and its id, text and how it is unlocked. Returns 0 past the end.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetTitle(int index, AccountTitle* title, char* id, int idCapacity, char* text, int textCapacity, char* how, int howCapacity)
    {
        try
        {
            if (!Account.Title(index, out AccountTitle t, out string i, out string x, out string h))
            {
                return 0;
            }

            if (title != null)
            {
                *title = t;
            }

            OnlineTextCopy.Buffer(id, idCapacity, i);
            OnlineTextCopy.Buffer(text, textCapacity, x);
            OnlineTextCopy.Buffer(how, howCapacity, h);
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Asks the master for the account (at most once a minute unless <paramref name="force"/>).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Refresh(int force) =>
        Guarded(() =>
        {
            Account.Refresh(force != 0);
            return 0;
        });

    /// <summary>Claims or changes the name. Returns 1 when the master is asked; otherwise the reason is the Status text.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ClaimName(char* name)
    {
        string n = Text(name);
        return Guarded(() => Account.ClaimName(n) ? 1 : 0);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Prestige() => Guarded(() => Account.Prestige() ? 1 : 0);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SignOut() => Guarded(() => Account.SignOut() ? 1 : 0);

    /// <summary>The title the settings hold (its id, empty for none).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetChosenTitle(char* id)
    {
        string t = Text(id);
        Guarded(() =>
        {
            Account.SetChosenTitle(t);
            return 0;
        });
    }

    /// <summary>This PC's synced settings: the registry's values (JSON), controls.json and bindings.json; <paramref name="changed"/> queues a send.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetLocalProfile(char* settings, char* controls, char* bindings, int changed)
    {
        string s = Text(settings), c = Text(controls), b = Text(bindings);
        Guarded(() =>
        {
            Account.SetLocalProfile(s, c, b, changed != 0);
            return 0;
        });
    }

    /// <summary>A part (<see cref="ProfilePart"/>) of the account's settings waiting to be applied. Returns its full length, or -1 when none waits.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetIncomingProfile(int part, char* buffer, int capacity)
    {
        try
        {
            if (Account.Incoming is not { } incoming)
            {
                return -1;
            }

            string text = (ProfilePart)part switch
            {
                ProfilePart.Settings => incoming.Settings,
                ProfilePart.Controls => incoming.Controls,
                ProfilePart.Bindings => incoming.Bindings,
                _ => string.Empty,
            };
            return OnlineTextCopy.Buffer(buffer, capacity, text);
        }
        catch (Exception e)
        {
            Failed(e);
            return -1;
        }
    }

    /// <summary>Unreal applied the waiting settings (1) or could not (0).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void FinishIncomingProfile(int applied) =>
        Guarded(() =>
        {
            Account.FinishIncoming(applied != 0);
            return 0;
        });

    /// <summary>A packed rank (FScctOnlinePlayer.Rank) unpacked, and its line ("P3 · 42 - Field Agent"). Returns the line's full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int DescribeRank(int packed, AccountRank* rank, char* text, int capacity)
    {
        try
        {
            PlayerRank r = PlayerRank.Unpack(packed);
            if (rank != null)
            {
                *rank = new AccountRank
                {
                    Level = r.IsNone ? 0 : r.Level, Prestige = r.Prestige, TitleBit = r.TitleBit, Colour = AccountService.ColourOf(r), Master = r.IsMaster ? 1 : 0,
                };
            }

            string line = AccountService.TagOf(r);
            if (r.Title is { } t)
            {
                line += " - " + t.Text;
            }

            return OnlineTextCopy.Buffer(text, capacity, line);
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(text, capacity, string.Empty);
        }
    }

    /// <summary>The prestige emblem's colour, 0xRRGGBB (1 to 10; above 10 the master emblem's).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int EmblemColour(int prestige) =>
        Guarded(() => prestige > ProgressionK.MaxPrestige ? AccountService.EmblemColour(ProgressionK.MaxPrestige) : AccountService.EmblemColour(prestige));

    // ---- guards -------------------------------------------------------------------------------------------------

    private static string Text(char* text) => text == null ? string.Empty : new string(text);

    private static int Guarded(Func<int> call)
    {
        try
        {
            return call();
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    private static void Failed(Exception e) => Exports.ReportFailure(e.ToString());
}
