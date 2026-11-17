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
    baked = g.custom("float3 vertex = pow(saturate(VC.rgb) * 2.0, 2.2) * saturate(UseVC);"
                     " return (min(LM * 4.6, 4.6) + vertex) * Fill;", FLOAT3,
                     LM=(lightmap, "RGB"), VC=g.node(unreal.MaterialExpressionVertexColor), UseVC=baked_vertex,
                     Fill=g.scalar("BakedFill", 0.0))
    emissive = g.custom("return Base * Tint * (Whole + Baked) + Lit * Mask * Strength;", FLOAT3,
                        Base=(base, "RGB"), Tint=tint, Whole=g.scalar("Emissive", 0.0), Baked=baked,
                        Lit=(self_lit, "RGB"), Mask=(self_mask, "A"), Strength=g.scalar("SelfIllum", 0.0))
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
    opacity = g.custom("return saturate(max(A * Scale, Floor) + F * Glance);", FLOAT1,
                       A=(base, "A"), Scale=g.scalar("Opacity", 1.0), Floor=g.scalar("OpacityFloor", 0.12),
                       F=fresnel, Glance=g.scalar("GlanceOpacity", 0.5))
    g.output(opacity, unreal.MaterialProperty.MP_OPACITY)
    emissive = g.custom("return Base * Tint * E;", FLOAT3, Base=(base, "RGB"), Tint=tint, E=g.scalar("Emissive", 0.0))
    g.output(emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


def build_water():
    g = Graph(WATER)
    forward_lit_translucent(g.material)
    g.material.set_editor_property("tangent_space_normal", False)
    base = g.texture("BaseColor", "/Engine/EngineMaterials/DefaultDiffuse")
    tint = g.colour("Tint", (0.55, 0.75, 0.8, 1))
    colour = g.custom("return Base * Tint;", FLOAT3, Base=(base, "RGB"), Tint=tint)
    g.output(colour, unreal.MaterialProperty.MP_BASE_COLOR)

    # Two copies of one ripple map slide across each other at different sizes and speeds, in world space so
    # neighbouring water polygons join up whatever their texture mapping.
    time = g.node(unreal.MaterialExpressionTime)
    position = g.node(unreal.MaterialExpressionWorldPosition)
    scale, speed = g.scalar("RippleScale", 0.0025), g.scalar("RippleSpeed", 0.03)
    ripples = unreal.load_asset(WATER_NORMAL)
    samples = []
    for index, code in enumerate(("return P.xy * S + T * V * float2(1.0, 0.35);",
                                  "return P.yx * S * 2.3 - T * V * float2(0.6, 1.3);")):
        plane = g.custom(code, FLOAT2, P=position, S=scale, T=time, V=speed)
        sample = g.node(unreal.MaterialExpressionTextureSample, texture=ripples, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        g.link(plane, sample, "UVs")
        samples.append(sample)
    # The ripples tilt whatever way the surface faces; on the usual level water that is straight up.
    normal = g.custom("float2 r = (A.xy + B.xy) * S; return normalize(N + float3(r, 0.0) * (1.0 - 0.7 * abs(N.x + N.y)));", FLOAT3,
                      A=(samples[0], "RGB"), B=(samples[1], "RGB"), S=g.scalar("RippleStrength", 0.25),
                      N=g.node(unreal.MaterialExpressionVertexNormalWS))
    g.output(normal, unreal.MaterialProperty.MP_NORMAL)
    g.output(g.scalar("Roughness", 0.07), unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(g.scalar("Specular", 1.0), unreal.MaterialProperty.MP_SPECULAR)
    fresnel = g.node(unreal.MaterialExpressionFresnel, exponent=5.0, base_reflect_fraction=0.02)
    g.link(normal, fresnel, "Normal")
    opacity = g.custom("return saturate(O + F * 0.6);", FLOAT1, O=g.scalar("Opacity", 0.55), F=fresnel)
    g.output(opacity, unreal.MaterialProperty.MP_OPACITY)
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
