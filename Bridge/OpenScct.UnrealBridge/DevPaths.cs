using System;
using System.IO;
using OpenScct.Server;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The default folders of the console probes under Bridge/Probes and of BridgeProbe, used when a probe is not given
/// them on its command line. Unreal does not use this class.
/// </summary>
public static class DevPaths
{
    /// <summary>
    /// The converted content folder: <paramref name="given"/> when it is not null (returned unchanged), else the
    /// SVM_CONTENT variable (as the game reads it), else the launcher's packs folder
    /// (%LOCALAPPDATA%\OpenSCCT\packs). A default that does not exist throws with a message naming both ways to set it.
    /// </summary>
    public static string ContentRoot(string? given = null)
    {
        if (given is not null)
        {
            return given;
        }

        string? variable = Environment.GetEnvironmentVariable("SVM_CONTENT");
        string root = string.IsNullOrWhiteSpace(variable) ? ContentPaths.DefaultPacksFolder : variable;
        return Directory.Exists(root)
            ? root
            : throw new DirectoryNotFoundException(
                $"The OpenSCCT content folder was not found ('{root}'). Pass it as the probe's content argument or set SVM_CONTENT.");
    }

    /// <summary>
    /// The OpenSCCT checkout: <paramref name="given"/> when it is not null (returned unchanged), else the OPENSCCT_ROOT
    /// variable, else OSC-unreal-bridge beside this repository, as the bridge's csproj assumes. A default that
    /// does not exist throws with a message naming both ways to set it.
    /// </summary>
    public static string OpenScctCheckout(string? given = null)
    {
        if (given is not null)
        {
            return given;
        }

        string? variable = Environment.GetEnvironmentVariable("OPENSCCT_ROOT");
        string? root = !string.IsNullOrWhiteSpace(variable) ? variable
            : RepositoryRoot() is { } repo ? Path.GetFullPath(Path.Combine(repo, "..", "OSC-unreal-bridge"))
            : null;
        return root is not null && Directory.Exists(root)
            ? root
            : throw new DirectoryNotFoundException(
                $"The OpenSCCT checkout was not found ('{root ?? "no OpenSCCTUnreal.uproject above " + AppContext.BaseDirectory}'). "
                + "Pass it as the probe's checkout argument or set OPENSCCT_ROOT.");
    }

    /// <summary>The folder holding OpenSCCTUnreal.uproject above the running program, or null.</summary>
    private static string? RepositoryRoot()
    {
        for (DirectoryInfo? folder = new(AppContext.BaseDirectory); folder is not null; folder = folder.Parent)
        {
            if (File.Exists(Path.Combine(folder.FullName, "OpenSCCTUnreal.uproject")))
            {
                return folder.FullName;
            }
        }

        return null;
    }
}
