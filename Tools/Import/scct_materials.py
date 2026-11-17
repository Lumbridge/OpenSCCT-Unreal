"""The project's master materials, built from code so they can be rebuilt and reviewed as text.

Runs inside the editor's Python. import_assets.py builds the masters (and deletes the old ones first: a material
that exists cannot be re-wired in place); import_map.py makes one instance per original material.

  M_ScctSurface  every solid surface, and characters. The original texture gives colour and identity; a CC0 PBR
                 set, sampled world-aligned from three sides, gives the close-up detail the 2005 textures lack.
  M_ScctGlass    window and tank glass: thin, see-through, reflecting.
  M_ScctWater    water surfaces: moving ripples, reflecting.
  M_ScctSky      the sky dome: an HDRI panorama with drawn stars.
"""
import unreal

MATERIALS = "/Game/OpenSCCT/Materials"
THIRD_PARTY = "/Game/OpenSCCT/ThirdParty"
SURFACE = MATERIALS + "/M_ScctSurface"
GLASS = MATERIALS + "/M_ScctGlass"
WATER = MATERIALS + "/M_ScctWater"
SKY = MATERIALS + "/M_ScctSky"
WATER_NORMAL = MATERIALS + "/T_WaterNormal"

# Custom primitive data slot 0 on a mesh component: 1 = sample the detail layer in the mesh's own space, so the
# grain travels with a door or a lift instead of sliding across it. import_map.py sets it on everything movable.
LOCAL_DETAIL_SLOT = 0
# Custom primitive data slot 1: 1 = this mesh's vertex colours are the original's baked light (see "baked light"
# in build_surface). import_map.py sets it on the meshes the map lists as vertex lit.
BAKED_VERTEX_SLOT = 1

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
lib = unreal.MaterialEditingLibrary

# How far a glow is taken along with the exposure (Graph.in_the_picture): from the darkest the eye adapts to
# (the scale is about 0.2 there) to the brightest (about 10).
EXPOSURE_BOUNDS = (0.08, 16.0)

FLOAT1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
FLOAT2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
FLOAT3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3


class Graph:
    """Small helpers over the material editing library: every node is laid out left of the last."""

    def __init__(self, path):
        folder, name = path.rsplit("/", 1)
        if editor_assets.does_asset_exist(path):
            editor_assets.delete_asset(path)
        self.path = path
        self.material = asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        self.count = 0

    def node(self, cls, **properties):
        self.count += 1
        expression = lib.create_material_expression(self.material, cls, -300 - 220 * (self.count % 12), 140 * (self.count // 12) + 110 * (self.count % 12))
        for name, value in properties.items():
            expression.set_editor_property(name, value)
        return expression

    def link(self, source, target, target_input, source_output=""):
        # A node with a single input has no name for it.
        if not (lib.connect_material_expressions(source, source_output, target, target_input)
                or lib.connect_material_expressions(source, source_output, target, "")):
            raise RuntimeError("could not connect to input '%s' in %s" % (target_input, self.path))

    def scalar(self, name, default):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default)

    def colour(self, name, default):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*default))

    def texture(self, name, default, sampler=None, uvs=None):
        properties = {"parameter_name": name, "texture": unreal.load_asset(default)}
        if sampler is not None:
            properties["sampler_type"] = sampler
        expression = self.node(unreal.MaterialExpressionTextureSampleParameter2D, **properties)
        if uvs is not None:
            self.link(uvs, expression, "UVs")
        return expression

    def custom(self, code, output_type, **inputs):
        """A node of shader maths. `inputs` maps the name used in the code to a node or (node, output name)."""
        expression = self.node(unreal.MaterialExpressionCustom, code=code, output_type=output_type)
        slots = []
        for name in inputs:
            slot = unreal.CustomInput()
            slot.set_editor_property("input_name", name)
            slots.append(slot)
        expression.set_editor_property("inputs", slots)
        for name, source in inputs.items():
            node, output = source if isinstance(source, tuple) else (source, "")
            self.link(node, expression, name, output)
        return expression

    def in_the_picture(self, light, amount=None):
        """`light` as a brightness in the finished picture instead of in the scene: divided by the exposure the
        eye has settled on (the engine's eye adaptation node). The levels are played with automatic exposure over
        a wide range, and a glow of a fixed scene brightness is a dim patch in a lit hall and a white hole in a
        dark cellar; a screen, a lamp glass or a painted flare is meant to look the same in both, as the original
        drew them. `amount` (a node, 0..1) says how far to go: 0 leaves the scene brightness. The correction is
        bounded, so a pass without an exposure of its own (Lumen's surface cache) cannot blow a glow up."""
        inverse = self.node(unreal.MaterialExpressionEyeAdaptationInverse)
        one = self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(1, 1, 1, 1))
        linked = False
        for name in ("LightValueInput", "LightValue"):
            if lib.connect_material_expressions(one, "", inverse, name):
                linked = True
                break
        if not linked:
            raise RuntimeError("could not connect the eye adaptation node in " + self.path)
        if amount is not None:
            if not (lib.connect_material_expressions(amount, "", inverse, "AlphaInput")
                    or lib.connect_material_expressions(amount, "", inverse, "Alpha")):
                raise RuntimeError("could not connect the eye adaptation amount in " + self.path)
        return self.custom("float s = (K.r + K.g + K.b) / 3.0; return L * clamp(s, %f, %f);" % EXPOSURE_BOUNDS, FLOAT3,
                           L=light, K=inverse)

    def output(self, source, material_property, source_output=""):
        if not lib.connect_material_property(source, source_output, material_property):
            raise RuntimeError("could not connect a material output in " + self.path)

    def finish(self):
        lib.recompile_material(self.material)
        editor_assets.save_asset(self.path)
        unreal.log_warning("SCCT-IMPORT made " + self.path)
        return self.material


# ---------------------------------------------------------------------------------------------------------------
# Surface
# ---------------------------------------------------------------------------------------------------------------

# The three planar projections of a world-aligned ("triplanar") sample, blended by how much the surface faces each
# axis. The power keeps the blend zone narrow so two projections rarely ghost over each other.
TRIPLANAR_WEIGHTS = "float3 w = pow(abs(N), 4.0); w /= max(w.x + w.y + w.z, 1e-4);"

# Each projection's tangent-space normal is added to the surface normal in that projection's own axes ("whiteout"
# blending), so the detail bends the surface normal it lies on instead of replacing it.
DETAIL_NORMAL_CODE = TRIPLANAR_WEIGHTS + """
TX.xy *= S; TY.xy *= S; TZ.xy *= S;
float3 nx = float3(TX.xy + N.yz, abs(TX.z) * N.x).zxy;
float3 ny = float3(TY.xy + N.xz, abs(TY.z) * N.y).xzy;
float3 nz = float3(TZ.xy + N.xy, abs(TZ.z) * N.z);
return normalize(nx * w.x + ny * w.y + nz * w.z);"""

DETAIL_BLEND_CODE = TRIPLANAR_WEIGHTS + " return OX * w.x + OY * w.y + OZ * w.z;"


def build_surface():
    g = Graph(SURFACE)
    material = g.material
    # Normals leave the graph in world space: the detail layer is projected along world axes.
    material.set_editor_property("tangent_space_normal", False)
    # One material serves level surfaces and characters.
    material.set_editor_property("used_with_skeletal_mesh", True)
    material.set_editor_property("used_with_nanite", True)

    # ---- the original texture, with the original's scrolling ("panner") where it had one
    # and its flipbooks (caustics, rain): BaseColor is then the sheet of frames, played at the original's rate.
    uv = g.custom("float2 uv = UV + T * float2(U, V);"
                  " if (Cols * Rows > 1.5) {"
                  " float frame = floor(fmod(T * Fps, max(Frames, 1.0)));"
                  " uv = (frac(uv) + float2(fmod(frame, Cols), floor(frame / Cols))) / float2(Cols, Rows); }"
                  " return uv;", FLOAT2,
                  UV=g.node(unreal.MaterialExpressionTextureCoordinate), T=g.node(unreal.MaterialExpressionTime),
                  U=g.scalar("PanU", 0.0), V=g.scalar("PanV", 0.0),
                  Cols=g.scalar("FlipColumns", 1.0), Rows=g.scalar("FlipRows", 1.0),
                  Fps=g.scalar("FlipRate", 0.0), Frames=g.scalar("FlipFrames", 1.0))
    base = g.texture("BaseColor", "/Engine/EngineMaterials/DefaultDiffuse", uvs=uv)
    tint = g.colour("Tint", (1, 1, 1, 1))

    # ---- the surface normal: the map derived from the original texture, turned into world space
    macro = g.texture("Normal", "/Engine/EngineMaterials/DefaultNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, uvs=uv)
    macro_scaled = g.custom("return normalize(float3(N.xy * S, N.z));", FLOAT3, N=(macro, "RGB"), S=g.scalar("MacroNormal", 1.0))
    macro_world = g.node(unreal.MaterialExpressionTransform,
                         transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT,
                         transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(macro_scaled, macro_world, "Input")

    # ---- where the detail layer is sampled: world space, or the mesh's own space for things that move
    local = g.node(unreal.MaterialExpressionScalarParameter, parameter_name="LocalDetail", default_value=0.0,
                   use_custom_primitive_data=True, primitive_data_index=LOCAL_DETAIL_SLOT)
    world_position = g.node(unreal.MaterialExpressionWorldPosition)
    local_position = g.node(unreal.MaterialExpressionTransformPosition,
                            transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                            transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    g.link(world_position, local_position, "Input")
    local_normal = g.node(unreal.MaterialExpressionTransform,
                          transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                          transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    g.link(macro_world, local_normal, "Input")
    tiling = g.scalar("DetailTiling", 0.005)   # repeats per world unit (1 / the set's size in cm)
    position = g.custom("return lerp(W, L, saturate(S)) * T;", FLOAT3, W=world_position, L=local_position, S=local, T=tiling)
    facing = g.custom("return normalize(lerp(normalize(W), normalize(L), saturate(S)));", FLOAT3, W=macro_world, L=local_normal, S=local)

    detail_normal = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="DetailNormal",
                           texture=unreal.load_asset("/Engine/EngineMaterials/DefaultNormal"),
                           sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    detail_packed = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="DetailORD",
                           texture=unreal.load_asset("/Engine/EngineMaterials/DefaultDiffuse_TC_Masks"),
                           sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    normals, packs = {}, {}
    for axis, swizzle in (("X", "yz"), ("Y", "xz"), ("Z", "xy")):
        plane = g.custom("return P.%s;" % swizzle, FLOAT2, P=position)
        for store, source, sampler in ((normals, detail_normal, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                                       (packs, detail_packed, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)):
            sample = g.node(unreal.MaterialExpressionTextureSample, sampler_type=sampler)
            g.link(source, sample, "Tex")
            g.link(plane, sample, "UVs")
            store[axis] = sample

    detailed = g.custom(DETAIL_NORMAL_CODE, FLOAT3, N=facing, S=g.scalar("DetailNormalStrength", 0.0),
                        TX=(normals["X"], "RGB"), TY=(normals["Y"], "RGB"), TZ=(normals["Z"], "RGB"))
    detailed_world = g.node(unreal.MaterialExpressionTransform,
                            transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                            transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(detailed, detailed_world, "Input")
    normal = g.custom("return normalize(lerp(D, normalize(L), saturate(S)));", FLOAT3, D=detailed, L=detailed_world, S=local)
    g.output(normal, unreal.MaterialProperty.MP_NORMAL)

    # R = occlusion, G = roughness around 0.5, B = brightness around 0.5 (fetch_assets.py).
    packed = g.custom(DETAIL_BLEND_CODE, FLOAT3, N=facing,
                      OX=(packs["X"], "RGB"), OY=(packs["Y"], "RGB"), OZ=(packs["Z"], "RGB"))

    # ---- colour: the original, with the set's grain and pore shadows laid over it. Far away the grain would
    # only show as a repeating pattern, so it fades with distance.
    occlusion_amount = g.scalar("DetailOcclusion", 0.0)
    colour = g.custom(
        "float fade = lerp(1.0, 0.35, saturate((Depth - 500.0) / 2500.0));"
        " float grain = clamp(1.0 + (D.b * 2.0 - 1.0) * Amount * fade, 0.45, 1.6);"
        " return Base * Tint * grain * lerp(1.0, D.r, AO * 0.5 * fade);", FLOAT3,
        Base=(base, "RGB"), Tint=tint, D=packed, Amount=g.scalar("DetailColour", 0.0), AO=occlusion_amount,
        Depth=g.node(unreal.MaterialExpressionPixelDepth))
    g.output(colour, unreal.MaterialProperty.MP_BASE_COLOR)

    # ---- roughness: the class sets the level (darker parts of the original read as rougher grime, brighter parts
    # as worn, smoother material); the set adds scratches, pores and smears around it.
    roughness = g.custom("return saturate(lerp(Dark, Bright, G) + (D.g - 0.5) * 2.0 * Amount);", FLOAT1,
                         Dark=g.scalar("RoughnessDark", 0.85), Bright=g.scalar("RoughnessBright", 0.45),
                         G=(base, "G"), D=packed, Amount=g.scalar("DetailRoughness", 0.0))
    g.output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(g.scalar("Metallic", 0.0), unreal.MaterialProperty.MP_METALLIC)
    g.output(g.scalar("Specular", 0.5), unreal.MaterialProperty.MP_SPECULAR)
    occlusion = g.custom("return lerp(1.0, D.r, AO);", FLOAT1, D=packed, AO=occlusion_amount)
    g.output(occlusion, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    # ---- light it gives off. "Emissive" lights the whole surface with its own colour (surfaces the original drew
    # unlit); "SelfIllum" adds the original's self-illumination texture where its mask's alpha says so (lamp
    # glass, screens, lit windows), so Lumen spreads that light into the room.
    self_lit = g.texture("SelfIllumTexture", "/Engine/EngineResources/Black", uvs=uv)
    self_mask = g.texture("SelfIllumMask", "/Engine/EngineResources/WhiteSquareTexture", uvs=uv)

    # ---- baked light. The original lit its levels with lightmaps (on the level's own surfaces, second UV set)
    # and baked vertex colours (on placed meshes), and many of the lights those were baked from are not in the
    # map file any more. A share of that baked light is given back as a faint glow of the surface's own colour:
    # rooms the artists lit stay lit where no lamp survives, and because Lumen bounces a glow like any light it
    # also reaches characters and furniture. Both were stored at half strength and multiplied the texture in
    # display space, hence the doubling and the power.
    lightmap_uv = g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=1)
    lightmap = g.texture("Lightmap", "/Engine/EngineResources/Black", uvs=lightmap_uv)
    baked_vertex = g.node(unreal.MaterialExpressionScalarParameter, parameter_name="BakedVertexLight", default_value=0.0,
                          use_custom_primitive_data=True, primitive_data_index=BAKED_VERTEX_SLOT)
    # The baked light keeps only part of its colour: it is a fill under lamps that carry the same tint, and with
    # their bounces on top a tinted room turned into one flat hue.
    baked = g.custom("float3 vertex = pow(saturate(VC.rgb) * 2.0, 2.2) * saturate(UseVC);"
                     " float3 light = min(LM * 4.6, 4.6) + vertex;"
                     " return lerp(dot(light, float3(0.2126, 0.7152, 0.0722)), light, 0.6) * Fill;", FLOAT3,
                     LM=(lightmap, "RGB"), VC=g.node(unreal.MaterialExpressionVertexColor), UseVC=baked_vertex,
                     Fill=g.scalar("BakedFill", 0.0))
    # The glow of a screen, a lamp glass or an additive flare is a brightness in the finished picture (see
    # Graph.in_the_picture): 1 is the texture as painted, whatever the exposure. The baked light is light in the
    # scene like any lamp's and stays that.
    glow = g.custom("return Base * Tint * Whole + Lit * Mask * Strength;", FLOAT3,
                    Base=(base, "RGB"), Tint=tint, Whole=g.scalar("Emissive", 0.0),
                    Lit=(self_lit, "RGB"), Mask=(self_mask, "A"), Strength=g.scalar("SelfIllum", 0.0))
    # ---- the least light. The original never drew a lit surface black: every zone had an ambient light, and its
    # darkest rooms were dim shapes, not a black screen. Real lights and real shadows have no such floor, and a room
    # whose lamps did not survive in the map file is pitch dark however far the eye opens. So every lit surface
    # gives off a trace of its own colour, a little stronger on what faces up and weaker in the detail layer's
    # pores, so a dark room keeps its forms and not only its outlines. It is light in the scene, far below any
    # lamp's: lost in a lit room, and what the eye finds once it has opened for a dark one. (Not a brightness in
    # the picture like the glows: Lumen gathers every surface's glow without the eye's exposure, and a whole
    # level glowing "the same at any exposure" lit itself up like a lamp.)
    least = g.custom("return Base * Tint * Floor * (0.55 + 0.45 * saturate(N.z * 0.5 + 0.5)) * AO;", FLOAT3,
                     Base=(base, "RGB"), Tint=tint, Floor=g.scalar("AmbientFloor", 0.0), N=normal, AO=occlusion)
    emissive = g.custom("return Glow + Least + Base * Tint * Baked;", FLOAT3, Glow=g.in_the_picture(glow, g.scalar("GlowInPicture", 1.0)),
                        Least=least, Base=(base, "RGB"), Tint=tint, Baked=baked)
    g.output(emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    alpha = g.custom("return A * O;", FLOAT1, A=(base, "A"), O=g.scalar("Opacity", 1.0))
    g.output(alpha, unreal.MaterialProperty.MP_OPACITY)
    g.output(alpha, unreal.MaterialProperty.MP_OPACITY_MASK)
    return g.finish()


# ---------------------------------------------------------------------------------------------------------------
# Glass and water
# ---------------------------------------------------------------------------------------------------------------

def forward_lit_translucent(material):
    """Translucent, lit per pixel like a solid surface, with reflections: what glass and water need."""
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    material.set_editor_property("screen_space_reflections", True)
    material.set_editor_property("used_with_nanite", True)


def build_glass():
    g = Graph(GLASS)
    forward_lit_translucent(g.material)
    g.material.set_editor_property("two_sided", True)
    base = g.texture("BaseColor", "/Engine/EngineMaterials/DefaultDiffuse")
    tint = g.colour("Tint", (1, 1, 1, 1))
    colour = g.custom("return Base * Tint;", FLOAT3, Base=(base, "RGB"), Tint=tint)
    g.output(colour, unreal.MaterialProperty.MP_BASE_COLOR)
    g.output(g.scalar("Roughness", 0.05), unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(g.scalar("Specular", 0.9), unreal.MaterialProperty.MP_SPECULAR)
    # Glass is clearest seen straight on and turns to a mirror at a glancing angle. What the original painted
    # on the pane (dirt, frames, tint) stays as opaque as it painted it.
    fresnel = g.node(unreal.MaterialExpressionFresnel, exponent=4.0, base_reflect_fraction=0.04)
    # SoftDepth (world units, 0 = off) is for the original's haze sheets (the layers of blue it hung in a fish tank
    # to fake deep water): such a sheet thins out where it meets a wall and where the eye comes up to it, so it
    # reads as depth in the water and never as a pane.
    opacity = g.custom("float soft = Soft > 0.0 ? saturate((Scene - Pixel) / Soft) * saturate((Pixel - 12.0) / Soft) : 1.0;"
                       " return saturate(max(A * Scale, Floor) + F * Glance) * soft;", FLOAT1,
                       A=(base, "A"), Scale=g.scalar("Opacity", 1.0), Floor=g.scalar("OpacityFloor", 0.12),
                       F=fresnel, Glance=g.scalar("GlanceOpacity", 0.5), Soft=g.scalar("SoftDepth", 0.0),
                       Scene=g.node(unreal.MaterialExpressionSceneDepth), Pixel=g.node(unreal.MaterialExpressionPixelDepth))
    g.output(opacity, unreal.MaterialProperty.MP_OPACITY)
    emissive = g.custom("return Base * Tint * E;", FLOAT3, Base=(base, "RGB"), Tint=tint, E=g.scalar("Emissive", 0.0))
    g.output(g.in_the_picture(emissive), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


# Water. What is under the surface is seen through it, less and less the more water lies in front of it: the
# thickness is the distance from the surface to the scene behind it along the view. Shallow water over a pool
# floor is clear, deep water is its own colour, and where the surface meets a wall or a bank it thins to nothing
# instead of ending in a ruled line. x: opacity, y: how much water the eye looks through (0..1).
WATER_DEPTH_CODE = """
float thickness = max(Scene - Pixel, 0.0);
float deep = 1.0 - exp(-thickness / max(Clear, 1.0));
float edge = saturate(thickness / 6.0);
float body = lerp(Shallow, Deep, deep);
// A fall or a spray sheet is water in the air: as see-through as the original painted it.
body = lerp(saturate(Alpha * 1.6 + 0.1), body, Flat);
return float2(saturate(body + F * 0.5) * edge, deep);"""

# The ripples are strong near the eye and calm far away: a small wave seen from across a canal is smaller than
# a pixel, and what is left of it is glitter that crawls. The surface gets a little rougher there instead, which
# is what many small waves add up to. x, y: the tilt, z: the roughness.
WATER_RIPPLE_CODE = """
float calm = saturate((Depth - 300.0) / 2500.0);
float2 tilt = (A.xy + B.xy) * S * lerp(1.0, 0.25, calm) * Flat;
return float3(tilt, lerp(R, max(R, 0.22), calm));"""


def build_water():
    g = Graph(WATER)
    forward_lit_translucent(g.material)
    g.material.set_editor_property("tangent_space_normal", False)
    # What lies under the surface is bent by the ripples.
    for name, value in (("refraction_method", "RM_PIXEL_NORMAL_OFFSET"), ("refraction_mode", "RM_PIXEL_NORMAL_OFFSET")):
        try:
            g.material.set_editor_property(name, getattr(unreal.RefractionMode, value))
            break
        except Exception:  # pylint: disable=broad-except
            continue

    # The original's water texture is a painted picture of ripples and highlights. Drawn as it is on a real
    # reflecting surface it reads as a cartoon; its average (the texture's smallest copy) is the colour of the
    # water body, and only a little of the picture itself is laid over that, scrolling as the original scrolled
    # it, as foam and movement. A surface that is not level (a waterfall, a spray sheet) shows the picture.
    uv = g.custom("return UV + T * float2(U, V);", FLOAT2, UV=g.node(unreal.MaterialExpressionTextureCoordinate),
                  T=g.node(unreal.MaterialExpressionTime), U=g.scalar("PanU", 0.0), V=g.scalar("PanV", 0.0))
    base = g.texture("BaseColor", "/Engine/EngineMaterials/DefaultDiffuse", uvs=uv)
    average = g.node(unreal.MaterialExpressionTextureSample, mip_value_mode=unreal.TextureMipValueMode.TMVM_MIP_LEVEL, const_mip_value=12)
    texture_object = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="BaseColor",
                            texture=unreal.load_asset("/Engine/EngineMaterials/DefaultDiffuse"))
    g.link(texture_object, average, "Tex")
    tint = g.colour("Tint", (0.75, 0.9, 0.95, 1))
    surface_normal = g.node(unreal.MaterialExpressionVertexNormalWS)
    level = g.custom("return saturate(abs(normalize(N).z) * 3.0 - 1.5);", FLOAT1, N=surface_normal)
    body_colour = g.colour("BodyColour", (0.02, 0.07, 0.08, 0.35))
    colour = g.custom("float3 body = lerp(Mean, Deep, DeepShare); return lerp(body, Base, lerp(0.85, Picture, Flat)) * Tint;", FLOAT3,
                      Base=(base, "RGB"), Mean=(average, "RGB"), Tint=tint, Picture=g.scalar("TextureAmount", 0.18), Flat=level,
                      Deep=body_colour, DeepShare=(body_colour, "A"))
    g.output(colour, unreal.MaterialProperty.MP_BASE_COLOR)

    # Two copies of one ripple map slide across each other at different sizes and speeds, in world space so
    # neighbouring water polygons join up whatever their texture mapping.
    time = g.node(unreal.MaterialExpressionTime)
    position = g.node(unreal.MaterialExpressionWorldPosition)
    scale, speed = g.scalar("RippleScale", 0.0016), g.scalar("RippleSpeed", 0.02)
    ripples = unreal.load_asset(WATER_NORMAL)
    samples = []
    for index, code in enumerate(("return P.xy * S + T * V * float2(1.0, 0.35);",
                                  "return P.yx * S * 2.3 - T * V * float2(0.6, 1.3);")):
        plane = g.custom(code, FLOAT2, P=position, S=scale, T=time, V=speed)
        sample = g.node(unreal.MaterialExpressionTextureSample, texture=ripples, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        g.link(plane, sample, "UVs")
        samples.append(sample)
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    rippled = g.custom(WATER_RIPPLE_CODE, FLOAT3, A=(samples[0], "RGB"), B=(samples[1], "RGB"), S=g.scalar("RippleStrength", 0.14),
                       Depth=depth, Flat=level, R=g.scalar("Roughness", 0.05))
    normal = g.custom("return normalize(N + float3(R.xy, 0.0));", FLOAT3, R=rippled, N=surface_normal)
    g.output(normal, unreal.MaterialProperty.MP_NORMAL)
    g.output(g.custom("return R.z;", FLOAT1, R=rippled), unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(g.scalar("Specular", 1.0), unreal.MaterialProperty.MP_SPECULAR)

    fresnel = g.node(unreal.MaterialExpressionFresnel, exponent=5.0, base_reflect_fraction=0.02)
    g.link(normal, fresnel, "Normal")
    through = g.custom(WATER_DEPTH_CODE, FLOAT2, Scene=g.node(unreal.MaterialExpressionSceneDepth), Pixel=depth,
                       Clear=g.scalar("ClearDepth", 160.0), Shallow=g.scalar("ShallowOpacity", 0.12), Deep=g.scalar("Opacity", 0.8),
                       Alpha=(base, "A"), Flat=level, F=fresnel)
    g.output(g.custom("return W.x;", FLOAT1, W=through), unreal.MaterialProperty.MP_OPACITY)
    # Water the original drew at full brightness keeps a little light of its own, so a pool in a dark club is
    # not a black hole: a brightness in the picture, like every glow.
    glow = g.custom("return C * G * (0.4 + 0.6 * W.y);", FLOAT3, C=colour, G=g.scalar("Glow", 0.0), W=through)
    g.output(g.in_the_picture(glow), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Air to water; nothing is bent where the water is too thin to bend it.
    refraction = g.custom("return lerp(1.0, 1.33, saturate(W.y * 3.0) * Flat);", FLOAT1, W=through, Flat=level)
    try:
        g.output(refraction, unreal.MaterialProperty.MP_REFRACTION)
    except RuntimeError:
        unreal.log_warning("SCCT-IMPORT water: this engine has no refraction input by that name; the water does not bend what is under it")
    return g.finish()


# ---------------------------------------------------------------------------------------------------------------
# Sky
# ---------------------------------------------------------------------------------------------------------------

# Stars: the view direction is cut into small cells; a few cells hold a star at a pseudo-random spot, with its
# own size and brightness. They fade out toward the horizon, where haze hides real ones.
STARS_CODE = """
float3 d = normalize(D);
float3 p = d * Density;
float3 cell = floor(p);
float h = frac(sin(dot(cell, float3(12.9898, 78.233, 37.719))) * 43758.5453);
float h2 = frac(h * 91.73);
float3 offset = frac(p) - 0.5 - (float3(h2, frac(h * 17.31), frac(h * 53.9)) - 0.5) * 0.6;
float star = step(0.985, h) * saturate(1.0 - length(offset) * (5.0 + 6.0 * h2));
star *= star * (0.25 + 0.75 * h2);
float3 tint = lerp(float3(1.0, 0.82, 0.7), float3(0.75, 0.85, 1.0), frac(h * 7.77));
return tint * star * saturate(d.z * 5.0);"""


def build_sky():
    g = Graph(SKY)
    material = g.material
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    # A sky material is left out of fog and lit-scene passes and is what Lumen sees where a ray leaves the level.
    material.set_editor_property("is_sky", True)

    view = g.custom("return normalize(P - C);", FLOAT3, P=g.node(unreal.MaterialExpressionWorldPosition),
                    C=g.node(unreal.MaterialExpressionCameraPositionWS))
    # Turn the panorama about the vertical, to put its moon where the level's moon light comes from.
    turned = g.custom("float a = radians(Turn); float s = sin(a); float c = cos(a);"
                      " return float3(D.x * c - D.y * s, D.x * s + D.y * c, D.z);", FLOAT3,
                      D=view, Turn=g.scalar("Rotation", 0.0))
    panorama = g.node(unreal.MaterialExpressionTextureSampleParameterCube, parameter_name="Panorama",
                      texture=unreal.load_asset("/Engine/EngineResources/DefaultTextureCube"))
    g.link(turned, panorama, "UVs")
    stars = g.custom(STARS_CODE, FLOAT3, D=view, Density=g.scalar("StarDensity", 260.0))
    emissive = g.custom("return Sky * Tint * Brightness + Stars * StarBrightness;", FLOAT3,
                        Sky=(panorama, "RGB"), Tint=g.colour("Tint", (1, 1, 1, 1)), Brightness=g.scalar("Brightness", 1.0),
                        Stars=stars, StarBrightness=g.scalar("StarBrightness", 0.0))
    g.output(emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


def build_all():
    build_surface()
    build_glass()
    build_water()
    build_sky()
