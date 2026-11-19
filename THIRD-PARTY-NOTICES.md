# Third-party notices

A release of OpenSCCT Unreal ships the third-party components below. `Tools/Package-Release.ps1` copies this file
into every release folder. Update it when the bridge gains a package or the release gains a component.

## Engine and runtimes

| Component | Where it ships | Licence |
|---|---|---|
| Unreal Engine 5.8 (Epic Games, Inc.) | the game and `Engine\` | Unreal Engine End User License Agreement: https://www.unrealengine.com/eula. The engine's own third-party notices ship as `NOTICES.txt`. |
| .NET runtime 10 (.NET Foundation and contributors) | `OpenSCCTUnreal\Binaries\DotNet` | MIT: https://github.com/dotnet/runtime/blob/main/LICENSE.TXT. Its `LICENSE.txt` and `ThirdPartyNotices.txt` ship in that folder. |
| Microsoft Visual C++ Redistributable | `Prerequisites\vc_redist.x64.exe` | Microsoft Software License Terms for the Visual C++ Redistributable: https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/ |

## NuGet packages in the bridge (`OpenSCCTUnreal\Binaries\Bridge`)

The bridge (`Bridge/OpenScct.UnrealBridge`) and the OpenSCCT assemblies it is built with
(https://github.com/Lumbridge/OpenSCCT) use these packages at run time:

| Package | Version | Project | Licence |
|---|---|---|---|
| DiscordRichPresence (`DiscordRPC.dll`) | 1.6.1.70 | https://github.com/Lachee/discord-rpc-csharp | MIT: https://github.com/Lachee/discord-rpc-csharp/blob/master/LICENSE |
| Newtonsoft.Json (used by DiscordRichPresence) | 13.0.1 | https://www.newtonsoft.com/json | MIT: https://github.com/JamesNK/Newtonsoft.Json/blob/master/LICENSE.md |
| LiteNetLib | 2.1.4 | https://github.com/RevenantX/LiteNetLib | MIT: https://github.com/RevenantX/LiteNetLib/blob/master/LICENSE.txt |
| Concentus (C# port of the Opus codec) | 2.2.2 | https://github.com/lostromb/concentus | BSD 3-Clause (the Opus licence): https://github.com/lostromb/concentus/blob/master/LICENSE |
| BouncyCastle.Cryptography | 2.6.2 | https://www.bouncycastle.org/ | MIT (the Bouncy Castle licence): https://www.bouncycastle.org/about/license/ |

The console probes under `Bridge/Probes` and `Bridge/BridgeProbe` use further packages (for example AWSSDK.S3 and
Microsoft.Data.Sqlite through the local master server probe). They are development tools and never ship.

## Textures

| Source | What | Licence |
|---|---|---|
| ambientCG | the surface texture sets under `Content/OpenSCCT/ThirdParty/ambientCG` | CC0 1.0: https://docs.ambientcg.com/license/ |
| Poly Haven | the sky and the texture sets under `Content/OpenSCCT/ThirdParty/PolyHaven` | CC0 1.0: https://polyhaven.com/license |

`docs/ASSETS.md` lists every asset taken from these sources.

## Hub scenery

The Hub includes adapted models and textures from the Godot Procedural Forest Demo, Poly Haven, Kenney, and
SimpleGrassTextured. Model authors, source locations, changes and licenses are listed in
`OpenSCCTUnreal/Binaries/Bridge/maps/Hub/render/scenery/CREDITS.txt`.
