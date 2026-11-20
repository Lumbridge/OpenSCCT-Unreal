"""Surface response for first-person merc materials with an authored reflection or metal/roughness map."""
import unreal


def weapon_finish(g, roughness, metallic):
    scalar = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    pair = unreal.CustomMaterialOutputType.CMOT_FLOAT2
    current = g.custom("return float2(R, M);", pair, R=roughness, M=metallic)
    texture = g.texture("WeaponFinishTexture", "/Engine/EngineMaterials/DefaultDiffuse_TC_Masks",
                        unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    # The legacy reflection mask is its alpha. It describes where the painted finish is glossy.
    # The additional sidearms supply glTF roughness (G) and metallic (B), including the glove atlas.
    response = g.custom(
        "float2 finish = lerp(float2(lerp(0.70, 0.29, T.a), 0.0), T.gb, Packed);"
        " return lerp(Current, finish * float2(RoughScale, MetalScale), Active);", pair,
        T=(texture, "RGBA"), Current=current, Packed=g.scalar("WeaponPackedPBR", 0),
        RoughScale=g.scalar("WeaponRoughnessFactor", 1), MetalScale=g.scalar("WeaponMetallicFactor", 1),
        Active=g.light("BodycamSurfaces"))
    selected = g.static_switch("UseWeaponFinish", response, current)
    return g.custom("return P.x;", scalar, P=selected), g.custom("return P.y;", scalar, P=selected)
