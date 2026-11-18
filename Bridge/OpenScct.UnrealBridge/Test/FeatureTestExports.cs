using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.UnrealBridge.Test;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The entry points of the client's per-feature checks (Source/OpenSCCT/Test/Features) that the other tables lack:
/// where the next local start puts the local player (<see cref="FeatureLaunch"/>). Unreal asks for
/// <see cref="FeatureTestExports.GetApi"/> once (type "OpenScct.UnrealBridge.FeatureTestExports, OpenScct.UnrealBridge",
/// method "GetApi") and keeps the table. Source/OpenSCCT/Test/ScctFeatureTestApi.h mirrors it field for field. No
/// entry throws: a failure returns 0 with the reason in LastError.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct FeatureTestApi
{
    public int Version;

    /// <summary>
    /// Arms the next local start: the local player first spawns at (x, y, z) (the pawn's centre) facing <c>yaw</c>
    /// (65536 a turn), with the editor's Play Level rules. The next start uses it up. Returns 1.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float, float, float, int, int> SetNextStart;

    /// <summary>Drops an armed start that was not used. Returns 1.</summary>
    public delegate* unmanaged[Cdecl]<int> ClearNextStart;

    /// <summary>Copies the last failure's text. Returns its length.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

public static unsafe class FeatureTestExports
{
    public const int ApiVersion = 1;

    private static string s_lastError = "";

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(FeatureTestApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->SetNextStart = &SetNextStart;
        api->ClearNextStart = &ClearNextStart;
        api->LastError = &LastError;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetNextStart(float x, float y, float z, int yaw)
    {
        try
        {
            FeatureLaunch.Arm(new FVector(x, y, z), yaw);
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ClearNextStart()
    {
        FeatureLaunch.Disarm();
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity)
    {
        string text = s_lastError;
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, capacity));
            buffer[count] = '\0';
        }

        return text.Length;
    }
}
