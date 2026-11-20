# Enhanced Mount Hospital texture restoration

## Playing the converted community map

Install your own converted Enhanced Mount Hospital in a map folder and select it in the game. You can also point the client at that folder with `-ScctMaps="<folder containing MounE>"`. The first time Unreal loads the vending cabinet, it combines the map's original colours and alpha with three project-authored plates. Both cabinets use the resulting 2048 × 2048 artwork and the map's original illumination mask.

The Windows release includes the builder and plates in `contenttools/community/MounE` and runs them with its bundled Python. The generated texture lives in `%LOCALAPPDATA%/OpenSCCT/unreal/cache/MounE/<input digest>/Prison_TEX_machine_HD.png`. Subsequent loads reuse it. Changing the source textures, plates or builder selects a fresh cache entry. If generation is unavailable, the map renders its original cabinet.

An explicit `render/overrides/Prison_TEX_machine_HD.png` inside your map takes priority. To create it manually from a source checkout, run `python Tools/Import/build_vending_override.py "<path to MounE>"`. The artwork credits are in [Art/Maps/MounE/Vending/CREDITS.txt](../Art/Maps/MounE/Vending/CREDITS.txt).

## Restore the complete preview

Start with an independently imported `Content/Imported/Maps/MounE/MounE.umap` in the candidate checkout.
Use the normal [map importer](../Tools/Import/Import-Map.ps1) and the editor build for this checkout.
A normal import uses the vending cabinet from the player's converted map.

To rebuild the reviewed HD cabinet, place the approved 2048 × 2048 RGBA atlas at
`Art/Maps/MounE/Prison_TEX_machine_HD.png` in the checkout. This private artwork uses the original
cabinet's alpha coverage. The optional `Prison_TEX_machine_WrapperDetail.png` in the same directory is
an authoring intermediate. Git ignores both files. Then run:

```powershell
$env:SVM_CONTENT = 'D:\OpenSCCTUnreal\Converted'
Tools/Import/Restore-HospitalPreview.ps1 -Stage Saved/HospitalPreview -Weights 'C:\models\realesr-general-x4v3.pth'
```

The [preview command](../Tools/Import/Restore-HospitalPreview.ps1) prepares the source images, verifies the stock
and staged file hashes, and backs up the existing private map before the editor pass. It can reuse the same
prepared stage on a later run. The [editor steps](../Tools/Import/apply_hospital_preview.py) apply the approved
full vending cabinet atlas (packets, shelf coils, keypad and grille) to both colour slots, the water and drain artwork and drain normal, the six glow masks across seven
slots, the cardboard material cleanup, uncompressed effect inputs, the shared-prop colours and active skins,
and the three refined lightmaps. They then save the Side Stairs shaft and floor projection into the physical
MounE map package.

The public door and floor finish masks contain roughness and metallic values drawn from rectangles and a
tile grid. Run `python Tools/Import/build_hospital_finish_masks.py` to regenerate both source PNGs in
`Art/Maps/MounE`. Their Unreal textures live under `Content/OpenSCCT/Materials/Finishes`.

Use `-PrepareOnly` to create the stage on the CPU. Use `-ValidateOnly` to verify an existing stage and its private
map destination without writing files or starting Unreal. Set `SVM_CONTENT` to your converted content directory
or pass `-Content` for one run. Use `-Engine` for a different Unreal installation and `-NeuralPython` if CPU PyTorch is in a
separate environment. Keep the stage and its colour rollback record together for later runs on the same map.

## Prepare source textures separately

The source-only batch takes the installed `svmap/MounE` directory, including `render/textures`,
`render/materials.json`, and `render/world.glb`. It checks the installed material inventory and PNG hashes.

The CPU tools need Python with NumPy, OpenCV, Pillow and SciPy, plus PowerShell 7 and ImageMagick on `PATH`.
The reviewed build used Python 3.10, NumPy 2.2.6, OpenCV 4.12.0.88, Pillow 12.0.0, SciPy 1.15.3, and
ImageMagick 7.1.2-15 Q16-HDRI.

```powershell
$map = Join-Path $env:SVM_CONTENT 'svmap/MounE'
$stage = Join-Path $PWD 'Saved/HospitalRestoration'
python -B Tools/Import/restore_hospital_textures.py $map $stage
```

For the reviewed neural detail, add `--weights <path-to-realesr-general-x4v3.pth>`. Use the official
[Real-ESRGAN general x4v3 v0.2.5.0 checkpoint](https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth).
The loader verifies SHA-256 `8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292` and runs
inference on the CPU. Supply `--neural-python <python.exe>` if CPU PyTorch is installed in a separate environment.
The reviewed neural stage used PyTorch 2.7.1+cpu, NumPy 1.26.4 and Pillow 11.3.0 in that environment. The default
run uses source-guided edge finishing for those textures and can differ in fine detail.

The colour manifest is `OriginalRestoration/manifest.json`. It covers the 90 live colour textures and the two
unbound original environment images, preserving names for original aliases. Its paths are relative to the manifest.
`OriginalRestoration/restoration-audit.json` records each source hash, output dimensions, original alpha coverage
and round-trip colour error. The stage includes the six glow masks for seven material slots under
`OriginalRestoration/GlowMasks` and the drain and water receipts under `Specialists/OriginalSewer`.

The release cook uses the resulting physical map package and the assets it references.
