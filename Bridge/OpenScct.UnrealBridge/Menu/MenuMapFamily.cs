using System;
using System.Text.RegularExpressions;
using OpenScct.Server;

namespace OpenScct.UnrealBridge;

internal static class MenuMapFamily
{
    // Downloadable legacy packs can have an empty Reloaded variant-word table.
    private static readonly Regex Prefix = new(@"^(Enhanced|Enh|Phoenix['’]s|Snake|UMP|PS2|Covert) (.+)$",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex Suffix = new(@"^(.+) ([0-9]{1,2}v[0-9]{1,2}|Sabotage|Sabo|Extraction|Mod|Classic \(N\))$",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    internal static (string Family, string Variant) Of(MapCatalogEntry entry)
    {
        string family = entry.SortName, variant = entry.VariantWord ?? "";
        // An authored family can give its editions entirely different names.
        if (family != entry.Name && !entry.Name.Equals(variant + " " + family, StringComparison.OrdinalIgnoreCase))
        {
            return (family, variant.Length > 0 ? variant : entry.Name);
        }

        // These legacy editions have their own titles and no family metadata.
        if (entry.Id.Equals("ClubS", StringComparison.OrdinalIgnoreCase) && family.Equals("Shadow House", StringComparison.OrdinalIgnoreCase))
        {
            return ("Club House", family);
        }
        if (entry.Id.Equals("OffsE", StringComparison.OrdinalIgnoreCase) && family.Equals("Oilrig2", StringComparison.OrdinalIgnoreCase))
        {
            return ("Offshore Oilrig", family);
        }

        if (variant.Length == 0 && Prefix.Match(family) is { Success: true } prefix)
        {
            variant = prefix.Groups[1].Value;
            family = prefix.Groups[2].Value;
        }

        if (family.EndsWith(" P", StringComparison.OrdinalIgnoreCase) && Regex.IsMatch(entry.Id, @"^[A-Za-z]{3}[0-9]{2}[Pp]$"))
        {
            return (family[..^2], "PS2 port");
        }

        if (variant.Equals("Enh", StringComparison.OrdinalIgnoreCase))
        {
            variant = "Enhanced";
        }
        else if (variant.Equals("UMP", StringComparison.OrdinalIgnoreCase) || variant.Equals("PS2", StringComparison.OrdinalIgnoreCase))
        {
            variant = variant.ToUpperInvariant();
        }
        if (Suffix.Match(family) is { Success: true } suffix)
        {
            family = suffix.Groups[1].Value;
            string word = suffix.Groups[2].Value;
            if (word.Equals("Sabo", StringComparison.OrdinalIgnoreCase))
            {
                word = "Sabotage";
            }
            variant = variant.Length == 0 ? word : variant + " " + word;
        }
        return (family, variant);
    }
}
