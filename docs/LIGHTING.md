# Lighting reference

Maps use fixed exposure and baked lighting by default. The extra dark-room fill and the whole-body lights on the local player and teammates are off. The original goggle lenses have their own visibility, colour and material.

For legacy lighting, launch with `-ScctLightPlan=0` or enter `scct.Light.Plan 0` in the game console before loading a map. Enter `scct.Light.Plan 1` to restore fixed lighting. If a map is already open, reload it for the change to take full effect.

## Baked strength

The original saved half the lighting value in a lightmap or a vertex colour. The default `scct.Light.LightmapScale 2.0` restores that value. For example, 0.25 stored gives 0.50 of the surface texture's brightness before other lighting. At 2.25 the same sample gives 0.5625. A stored black sample stays black at both settings. The control does not scale flare lights or goggle lenses.

The following measurements compare the imported SteeE level against retail at seven recorded camera positions. Values are mean grayscale brightness from 0 to 255. The three merc comparisons exclude the lower right where the retail rifle appears. The four spy comparisons use the upper 45% of the frame to exclude the third-person pawn.

| View | Retail | Unreal at 2.0 | Unreal at 2.25 |
| --- | ---: | ---: | ---: |
| Dark room | 0.44 | 0.31 | 0.35 |
| Storage | 2.68 | 2.20 | 2.52 |
| Merc spawn | 22.15 | 17.27 | 19.86 |
| Kitchen | 21.24 | 19.57 | 22.06 |
| Lounge | 6.69 | 6.69 | 8.69 |
| Courtyard | 21.45 | 21.64 | 24.72 |
| East yard | 15.88 | 19.13 | 22.47 |

At 2.25, mean absolute pixel error fell in all three merc views and rose in all four spy views. In the merc spawn it fell from 8.17 to 5.83. In the courtyard it rose from 3.60 to 5.07. The kitchen's average is close at both settings, but its ceiling is warmer and its wall lighting is flatter than retail. The higher setting also brightens the east yard, which was already too bright. The global setting therefore stays at 2.0. The earlier six baked-material cards on each of AquaE, StatE and BankP also put 2.0 within 8% of the original formula.

### Reproducing the room comparison

The merc references are `local/merc-kit/eye/<pose>-000.png` in SVM-Rewrite. Their adjacent JSON files give the camera location, rotation and field of view in `rows[0]`. The spy references and camera records are in `local/golden/game/SteeE`. Use `shots[pose].camera` in `cameras.json`.

Run `Tools/Run-Offscreen.ps1` on SteeE with zero bots, the recorded side, one shot per pose and a separate shot directory per setting. Supply each camera to `-Cams` as `X,Y,Z,yaw,pitch`, converting retail rotation units to degrees with `360/65536`. Use `-Exec 'fov 95'` for the merc group and `-Exec 'fov 75'` for the spy group. The renders used `-ScctLightPlan=1 -ResX=1280 -ResY=720` and `-dpcvars=scct.Light.LightmapScale=<scale>`. To reproduce the means, convert both PNGs to Pillow's grayscale `L`. Keep the top 396 rows plus the leftmost 576 columns for mercs and the top 324 rows for spies.

## Character and flare shadows

Ordinary lamps whose light is baked into the level also light characters. The bridge selects up to four lamps and traces from each to the character's centre. Cover removes that lamp from the character's lighting. This gives a whole-character transition when the centre passes cover. Moving shadows on individual limbs or surfaces require a live world light. Flares use such a light, with the original flight and resting ranges, flicker and fade. Their shadow casting is enabled by default under the plan.

A focused AquaE check sampled the 64 positions from the handover. Of 62 selected ordinary lamp paths, 37 were clear and 25 blocked. Seven positions had both a clear and a blocked lamp. Disabling obstruction preserved the selected lamp identities and colours. Rendered cover transitions, flare shadows and local versus teammate brightness still need a side-by-side gameplay check. The camera-tour numbers above measure the rooms.
