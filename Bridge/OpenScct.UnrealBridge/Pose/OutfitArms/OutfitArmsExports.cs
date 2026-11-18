using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Pose.OutfitArms;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Entry point for the first-person arms in the map's outfit (<see cref="OutfitArmsSkin"/>;
/// Source/OpenSCCT/Runtime/ScctOutfitArmsApi.h mirrors it).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OutfitArmsApi
{
    public int Version;

    /// <summary>
    /// Writes the full path of the arms texture of a view model (its package folder and mesh, as "SWeaponFirst" and
    /// "p190") for a team (0 spies, 1 mercs) as the map dresses it. Returns its length, 0 when the arms keep their look.
    /// The first call for a pair of meshes makes the texture.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, char*, int, char*, int, char*, int, int> ArmsTexture;
}

public static unsafe class OutfitArmsExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(OutfitArmsApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->ArmsTexture = &ArmsTexture;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ArmsTexture(int kind, char* package, int packageLength, char* mesh, int meshLength, char* into, int capacity)
    {
        try
        {
            if (Exports.Current is not { IsDisposed: false } session || package == null || mesh == null || into == null || packageLength <= 0 ||
                meshLength <= 0 || capacity <= 0 ||
                OutfitArmsSkin.For(session.Outfits, kind, new string(package, 0, packageLength), new string(mesh, 0, meshLength)) is not { } path)
            {
                return 0;
            }

            int length = Math.Min(path.Length, capacity);
            path.AsSpan(0, length).CopyTo(new Span<char>(into, capacity));
            return length;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
