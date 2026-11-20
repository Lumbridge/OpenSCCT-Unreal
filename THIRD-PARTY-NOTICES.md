# Third-party notices

A release of OpenSCCT Unreal ships the third-party components below. `Tools/Package-Release.ps1` copies this file
into every release folder. Update it when the bridge gains a package or the release gains a component.

## Mercenary weapon artwork

OpenSCCT adapted the following artists' models for the character rigs, material system and equipment views.

| In-game weapon | Original artwork | Licence |
|---|---|---|
| M4A1-S and rifle animation reference | [M4 - FPS Weapon Animations Pack V.1 by BarcodeGames](https://sketchfab.com/3d-models/m4-fps-weapon-animations-pack-v1-662fc74dda2646cfb48fc610705768ef) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Shared first-person sleeves and gloves | [animated pistol by DJMaesen](https://sketchfab.com/3d-models/animated-pistol-bd896167e7ca44f19597d3afe6a8d83f) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| AK-47 | [AK47 by wburton](https://sketchfab.com/3d-models/ak47-831519a097d84e079fd8bc4b15e5b57d) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Galil | [IMI Galil weapon by MilotoR1](https://sketchfab.com/3d-models/imi-galil-weapon-c04464da23af47458533a8a8c74f4b4b) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| FAMAS | [FAMAS by Frostoise](https://sketchfab.com/3d-models/famas-7d35e14ad8874d148ce9cc46e5765a6b) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Compact SMG | [Submachine Gun (Scorpion-inspired) by Kutejnikov](https://opengameart.org/content/submachine-gun-scorpion-inspired) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| SPAS-12 | [SPAS 12 shotgun by quick_loop](https://sketchfab.com/3d-models/spas-12-shotgun-a0f118ec8a984f09b602fb3a54f56a9e), distributed through [Objaverse](https://huggingface.co/datasets/allenai/objaverse) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Bolt-action rifle | [Bolt action Rifle: Lee Enfield by Lucian Pavel](https://opengameart.org/content/bolt-action-rifle-lee-enfield) | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| Designated marksman rifle | [Various Small Arms by Tabasco](https://opengameart.org/content/various-small-arms-assault-rifles-sniper-pistol) | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

## Engine and runtimes

| Component | Where it ships | Licence |
|---|---|---|
| Unreal Engine 5.8 (Epic Games, Inc.) | the game and `Engine\` | Unreal Engine End User License Agreement: https://www.unrealengine.com/eula. The engine's own third-party notices ship as `NOTICES.txt`. |
| .NET runtime 10 (.NET Foundation and contributors) | `OpenSCCTUnreal\Binaries\DotNet` | MIT: https://github.com/dotnet/runtime/blob/main/LICENSE.TXT. Its `LICENSE.txt` and `ThirdPartyNotices.txt` ship in that folder. |
| Microsoft Visual C++ Redistributable | `Prerequisites\vc_redist.x64.exe` | Microsoft Software License Terms for the Visual C++ Redistributable: https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/ |

## NuGet packages in the bridge (`OpenSCCTUnreal\Binaries\Bridge`)

The bridge (`Bridge/OpenScct.UnrealBridge`) and the OpenSCCT assemblies it is built with
(https://github.com/OpenSCCT/OpenSCCT) use these packages at run time:

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

## AMD FidelityFX

Windows builds with AMD FSR use the AMD FSR Upscaling Unreal Engine plugin and the signed AMD FidelityFX runtime for upscaling and frame generation. Their respective licence texts ship in `OpenSCCTUnreal/Licenses/AMD-FSR-Plugin.txt` and `OpenSCCTUnreal/Licenses/AMD-FidelityFX-SDK.md`. [AMD FSR Upscaling](https://gpuopen.com/amd-fsr-upscaling/)

## NVIDIA DLSS and Streamline

Windows builds that include NVIDIA DLSS use NVIDIA NGX and NVIDIA Streamline for Super Resolution,
DLAA and Frame Generation. NVIDIA and the NVIDIA logo are trademarks or registered trademarks
of NVIDIA Corporation. The installed SDK licence ships in `OpenSCCTUnreal/Licenses/NVIDIA-RTX-SDK.txt` with the NVIDIA runtime. [NVIDIA DLSS](https://developer.nvidia.com/rtx/dlss).

The Streamline integration uses NVIDIA's Streamline headers under the following licence.

```text
Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

The NVIDIA DLSS runtime includes the following third-party software. The acknowledgements below
come from the NVIDIA DLSS Programming Guide supplied with the UE 5.8 plugin, version 8.9.0.

```text
9.6.1 CURL
COPYRIGHT AND PERMISSION NOTICE
Copyright © 1996 - 2018, Daniel Stenberg, daniel@haxx.se, and many contributors, see the THANKS file.
All rights reserved.
Permission to use, copy, modify, and distribute this software for any purpose with or without fee is
hereby granted, provided that the above copyright notice and this permission notice appear in all
copies.
THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
Except as contained in this notice, the name of a copyright holder shall not be used in advertising or
otherwise to promote the sale, use or other dealings in this Software without prior written authorization
of the copyright holder.

9.6.2 8x13 BITMAP FONT
https://courses.cs.washington.edu/courses/cse457/98a/tech/OpenGL/font.c
* (c) Copyright 1993, Silicon Graphics, Inc.
* ALL RIGHTS RESERVED
* Permission to use, copy, modify, and distribute this software for
* any purpose and without fee is hereby granted, provided that the above
* copyright notice appear in all copies and that both the copyright notice
* and this permission notice appear in supporting documentation, and that
* the name of Silicon Graphics, Inc. not be used in advertising
* or publicity pertaining to distribution of the software without specific,
* written prior permission.
*



* THE MATERIAL EMBODIED ON THIS SOFTWARE IS PROVIDED TO YOU "AS-IS"
* AND WITHOUT WARRANTY OF ANY KIND, EXPRESS, IMPLIED OR OTHERWISE,
* INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY OR
* FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL SILICON
* GRAPHICS, INC.  BE LIABLE TO YOU OR ANYONE ELSE FOR ANY DIRECT,
* SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY
* KIND, OR ANY DAMAGES WHATSOEVER, INCLUDING WITHOUT LIMITATION,
* LOSS OF PROFIT, LOSS OF USE, SAVINGS OR REVENUE, OR THE CLAIMS OF
* THIRD PARTIES, WHETHER OR NOT SILICON GRAPHICS, INC.  HAS BEEN
* ADVISED OF THE POSSIBILITY OF SUCH LOSS, HOWEVER CAUSED AND ON
* ANY THEORY OF LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE
* POSSESSION, USE OR PERFORMANCE OF THIS SOFTWARE.

9.6.3 d3dx12.h

https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Multithreading/src/d3dx12.h
*     Copyright © 2017 NVIDIA Corporation.  All rights reserved.
*
*  NOTICE TO USER:
*
*  This software is subject to NVIDIA ownership rights under U.S. and international Copyright laws.
*
*  This software and the information contained herein are PROPRIETARY and CONFIDENTIAL to NVIDIA
*  and are being provided solely under the terms and conditions of an NVIDIA software license agreement
*  and / or non-disclosure agreement.  Otherwise, you have no rights to use or access this software in any manner.
*
*  If not covered by the applicable NVIDIA software license agreement:
*  NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOFTWARE FOR ANY PURPOSE.
*  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.
*  NVIDIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
*  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
*  IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
*  OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,  WHETHER IN AN ACTION OF CONTRACT,
*  NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.
*
*  U.S. Government End Users.
*  This software is a "commercial item" as that term is defined at 48 C.F.R. 2.101 (OCT 1995),
*  consisting  of "commercial computer  software"  and "commercial computer software documentation"
*  as such terms are  used in 48 C.F.R. 12.212 (SEPT 1995) and is provided to the U.S. Government only as a commercial
end item.
*  Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through 227.7202-4 (JUNE 1995),
*  all U.S. Government End Users acquire the software with only those rights set forth herein.
*
*  Any use of this software in individual and commercial software must include,
*  in the user documentation and internal comments to the code,
*  the above Disclaimer (as applicable) and U.S. Government End Users Notice.
*

* Original code by Microsoft Corporation was obtained from
*  https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Multithreading
*  and licensed under MIT license (below)
*  NVIDIA license applies to changes made relative to original code.

* Copyright (c) Microsoft. All rights reserved.
* This code is licensed under the MIT License (MIT).
* THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
* ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
* IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
* PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.

9.6.4 xml
https://pugixml.org/license.html
* This library is available to anybody free of charge, under the terms of MIT License:



* Copyright (c) 2006-2018 Arseny Kapoulkine
* Permission is hereby granted, free of charge, to any person obtaining a copy of this
* software and associated documentation files (the “Software”), to deal in the Software
* without restriction, including without limitation the rights to use, copy, modify, merge,
* publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
* to whom the Software is furnished to do so, subject to the following conditions:
* The above copyright notice and this permission notice shall be included in all copies or
* substantial portions of the Software.
* THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
* PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
* FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
* OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
* This means that you can freely use pugixml in your applications, both open-source and
* proprietary. If you use pugixml in a product, it is sufficient to add an acknowledgment
* like this to the product distribution:
* This software is based on pugixml library (http://pugixml.org). pugixml is Copyright (C) *
2006-2018 Arseny Kapoulkine.



9.6.5 npy
https://github.com/llohse/libnpy
https://github.com/llohse/libnpy/blob/master/LICENSE
* MIT License

* Copyright (c) 2021 Leon Merten Lohse

* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:

* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.

9.6.6 stb
https://github.com/nothings/stb/blob/master/LICENSE
/*
* ALTERNATIVE A - MIT License
* Copyright (c) 2017 Sean Barrett
* Permission is hereby granted, free of charge, to any person obtaining a copy of
* this software and associated documentation files (the "Software"), to deal in



* the Software without restriction, including without limitation the rights to
* use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
* of the Software, and to permit persons to whom the Software is furnished to do
* so, subject to the following conditions:
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/



9.6.7 DirectX-Graphics-Samples

https://github.com/Microsoft/DirectX-Graphics-
Samples/tree/master/Samples/Desktop/D3D12Multithreading

/*
* Copyright (c) Microsoft. All rights reserved.
* This code is licensed under the MIT License (MIT).
* THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
* ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
* IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
* PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
*/

9.6.8 Vulkan-Headers

https://github.com/KhronosGroup/Vulkan-Headers/
  Copyright 2015-2023 The Khronos Group Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
```
