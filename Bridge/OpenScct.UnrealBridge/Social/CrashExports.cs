using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using OpenScct.Core;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScct.UnrealBridge.Social;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The crash report table (Source/OpenSCCT/Runtime/ScctCrashApi.h mirrors it): where <see cref="CrashLog"/> writes,
/// what is being played, and the test crash. Every entry catches its exceptions. Strings are UTF-16, zero terminated.
/// New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct CrashApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int> Install;
    public delegate* unmanaged[Cdecl]<char*, int, int, char*, int> SetContext;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<int, int> Test;
}

public static unsafe class CrashExports
{
    public const int ApiVersion = 1;

    /// <summary>GetText's kinds.</summary>
    public const int TextContext = 0;
    public const int TextFolder = 1;
    public const int TextLastPath = 2;
    public const int TextNotices = 3;

    private static readonly object Gate = new();
    private static readonly System.Text.StringBuilder Notices = new();
    private static int s_listening;

    [UnmanagedCallersOnly]
    public static int GetApi(CrashApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Install = &Install;
        api->SetContext = &SetContext;
        api->GetText = &GetText;
        api->Test = &Test;
        return ApiVersion;
    }

    /// <summary>Names the folder the reports go to (the handlers are in since the bridge loaded). Returns 1.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Install(char* folder)
    {
        try
        {
            if (Interlocked.Exchange(ref s_listening, 1) == 0)
            {
                CrashLog.Written += line =>
                {
                    lock (Gate)
                    {
                        Notices.AppendLine(line);
                    }
                };
            }

            CrashLog.Install(folder == null ? null : new string(folder));
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// What is being played: the map's id (empty: none), the mode (EGameMode, -1 unknown), the side (0 spy, 1 merc,
    /// else none) and where ("offline", "online server X", "hub X", "demo"; empty in the menus). Returns 1.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetContext(char* map, int mode, int side, char* where)
    {
        try
        {
            string m = map == null ? "" : new string(map);
            string w = where == null ? "" : new string(where);
            string text = m;
            if (m.Length > 0 && mode >= 0)
            {
                text += " " + MapCatalog.ModeKey((EGameMode)mode);
            }

            if (side is 0 or 1)
            {
                text += (text.Length > 0 ? ", " : "") + (side == 0 ? "spy" : "merc");
            }

            if (w.Length > 0)
            {
                text += (text.Length > 0 ? ", " : "") + w;
            }

            CrashLog.Context = text;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>A text (cut to fit); returns its full length. The notices are taken: each is handed over once.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            string text;
            switch (kind)
            {
                case TextContext: text = CrashLog.Context; break;
                case TextFolder: text = CrashLog.Folder; break;
                case TextLastPath: text = CrashLog.LastPath; break;
                case TextNotices:
                    lock (Gate)
                    {
                        text = Notices.ToString();
                        if (buffer != null && capacity > text.Length)
                        {
                            Notices.Clear();
                        }
                    }

                    break;
                default: text = ""; break;
            }

            if (buffer != null && capacity > 0)
            {
                int n = Math.Min(text.Length, capacity - 1);
                text.AsSpan(0, n).CopyTo(new Span<char>(buffer, n));
                buffer[n] = '\0';
            }

            return text.Length;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// The test crash (-ScctCrashTest): 1 throws on a worker thread of the bridge, which nothing catches (the report is
    /// written and the process ends); 2 writes a report for a made-up exception and goes on. Returns 1 when started.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Test(int kind)
    {
        try
        {
            if (kind == 1)
            {
                var thread = new Thread(() => throw new InvalidOperationException("The crash test of -ScctCrashTest=managed."))
                {
                    Name = "ScctCrashTest",
                    IsBackground = true,
                };
                thread.Start();
                return 1;
            }

            if (kind == 2)
            {
                return CrashLog.Write(new InvalidOperationException("The crash test of -ScctCrashTest=report."), "test") is null ? 0 : 1;
            }

            return 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
