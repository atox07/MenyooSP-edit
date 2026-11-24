param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [Parameter(Mandatory)][string]$CodeWalkerDirectory,
    [string]$Output
)
$ErrorActionPreference = 'Stop'
if (!$Output) { $Output = Join-Path $PSScriptRoot "../Solution/source/Scripting/StaticModelOrigins.inc" }
$Output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
$core = Join-Path $CodeWalkerDirectory 'CodeWalker.Core.dll'
$math = Join-Path $CodeWalkerDirectory 'SharpDX.Mathematics.dll'
$dx = Join-Path $CodeWalkerDirectory 'SharpDX.dll'
foreach ($dll in @($dx,$math,$core)) { [void][Reflection.Assembly]::LoadFrom($dll) }
$netstandard = Join-Path ([Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()) "netstandard.dll"
Add-Type -ReferencedAssemblies @($dx,$math,$core,"System.dll","System.Core.dll",$netstandard) -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Globalization;
using CodeWalker.GameFiles;
using SharpDX;
public static class StaticPlacementIndex {
    public static void Build(string game, string output) {
        GTA5Keys.LoadFromPath(game, null);
        var manager = new RpfManager();
        manager.Init(game, s => {}, s => Console.Error.WriteLine(s), false, true);
        var origins = new SortedDictionary<uint, Vector3>();
        int maps = 0, errors = 0;
        // Prefer base-game map placements when expansion maps reuse the same archetype.
        var archives = manager.AllRpfs.OrderBy(a => a.Path.IndexOf("dlcpacks", StringComparison.OrdinalIgnoreCase) >= 0)
            .ThenBy(a => a.Path, StringComparer.OrdinalIgnoreCase);
        foreach (var archive in archives) {
            string path = archive.Path.Replace('/','\\').ToLowerInvariant();
            if (path.StartsWith("mods\\")) continue;
            if (archive.AllEntries == null) continue;
            foreach (var entry in archive.AllEntries) {
                if (!(entry is RpfResourceFileEntry) || !entry.Name.EndsWith(".ymap",StringComparison.OrdinalIgnoreCase)) continue;
                try {
                    var map = RpfFile.GetFile<YmapFile>(entry);
                    if (map == null) { errors++; continue; }
                    maps++;
                    if (map.CEntityDefs == null) continue;
                    foreach (var entity in map.CEntityDefs) {
                        uint hash = entity.archetypeName.Hash;
                        var p = entity.position;
                        if (hash != 0 && !origins.ContainsKey(hash) && !float.IsNaN(p.LengthSquared()) && !float.IsInfinity(p.LengthSquared()))
                            origins.Add(hash,p);
                    }
                } catch (Exception ex) { if (++errors <= 3) Console.Error.WriteLine(entry.Name + ": " + ex.Message); }
            }
        }
        if (origins.Count == 0) throw new InvalidDataException("No map placements found across " + manager.AllRpfs.Count + " archives, " + maps + " maps and " + errors + " errors.");
        using (var writer = new StreamWriter(output + ".tmp", false, new System.Text.UTF8Encoding(false))) {
            foreach (var item in origins) {
                var p = item.Value;
                writer.WriteLine(String.Format(CultureInfo.InvariantCulture,
                    "{{0x{0:X8}u, {1:0.0######}f, {2:0.0######}f, {3:0.0######}f}},",item.Key,(double)p.X,(double)p.Y,(double)p.Z));
            }
        }
        Console.WriteLine("{0} models, {1} maps, {2} unreadable maps", origins.Count,maps,errors);
    }
}
"@
[StaticPlacementIndex]::Build($GameDirectory, $Output)
Move-Item -LiteralPath "$Output.tmp" -Destination $Output -Force
