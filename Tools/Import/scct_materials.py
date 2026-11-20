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
TRANSLUCENT_SEQUENCE = MATERIALS + "/M_ScctTranslucentSequence"
SCROLLING_OVERLAY = MATERIALS + "/M_ScctScrollingOverlay"
MODULATE = MATERIALS + "/M_ScctModulate"
GLASS = MATERIALS + "/M_ScctGlass"
WATER = MATERIALS + "/M_ScctWater"
SKY = MATERIALS + "/M_ScctSky"
WATER_NORMAL = MATERIALS + "/T_WaterNormal"

# The lighting plan's values (scct.Light.Plan, Source/OpenSCCT/Play/ScctLightPlan.h), in a material parameter
# collection the game sets when a level loads (Source/OpenSCCT/Play/ScctLightRules.cpp). The defaults are the released
# picture: Plan 0 makes the surface material compute exactly what it computed before the plan, so the editor, the
# menus and a match with scct.Light.Plan 0 show the same picture as before. The collection is made once and then only
# added to: a material holds each value by the id the collection gave it, and a collection made anew would orphan them.
#   Plan              0 legacy, 1 imported lighting, 2 native fixtures with real-time GI
#   BakedGain         a factor on the baked light term (display units), 1 unless a measurement says otherwise
#   BakedScale        what the stored lightmap and vertex colour are multiplied by (the original stored them halved: 2)
#   BakedSaturation   1 the baked light's own colour, 0 its grey
#   Boost             added to the term: the night vision's and the squint's (the plan's step 7)
#   Ambient           added to the term: a zone ambient (0 on nearly every map; the plan's step 9)
#   BounceGate        the most bounced and sky light a surface takes (the material's occlusion), reached where its
#                     baked light is BounceHigh or more, none where it is BounceLow or less (luminance of the term)
#   Floor             the share of the released picture's AmbientFloor that stays
LIGHT_COLLECTION = MATERIALS + "/MPC_ScctLight"
LIGHT_PARAMETERS = [
    ("Plan", 0.0),
    ("BakedGain", 1.0),
    ("BakedScale", 2.0),
    ("BakedSaturation", 1.0),
    ("Boost", 0.0),
    ("Ambient", 0.0),
    ("BounceGate", 0.35),
    ("BounceLow", 0.08),
    ("BounceHigh", 0.5),
    ("Floor", 0.0),
    ("BodycamSurfaces", 0.0),
]

# Custom primitive data slot 0 on a mesh component: 1 = sample the detail layer in the mesh's own space, so the
# grain travels with a door or a lift instead of sliding across it. import_map.py sets it on everything movable.
LOCAL_DETAIL_SLOT = 0
# Custom primitive data slot 1: 1 = this mesh's vertex colours are the original's baked light (see "baked light"
# in build_surface). import_map.py sets it on the meshes the map lists as vertex lit.
BAKED_VERTEX_SLOT = 1
# Custom primitive data slot 2: the distance from the camera, in cm, under which the mesh thins out of the picture
# (0: never). Its pixels are whole at that distance and gone at half of it, through a screen door that the temporal
# anti-aliasing smooths. Only instances whose blend mode is masked have it (the bodies and what they carry,
# Tools/Import/import_characters.py); the game sets it on the local player's body, so a camera pushed into his
# shoulder looks through the part of him that is in the way and still shows his arm and gun
# (AScctPawnProxy::SetNearFade).
NEAR_FADE_SLOT = 2
NEAR_FADE_GONE = 0.5

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
lib = unreal.MaterialEditingLibrary

# How far a glow is taken along with the exposure (Graph.in_the_picture): from the darkest the eye adapts to
# (the scale is about 0.2 there) to the brightest (about 10).
EXPOSURE_BOUNDS = (0.08, 16.0)

FLOAT1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
FLOAT2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
FLOAT3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
FLOAT4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4

# The items a MaterialSequence can hold here; the stock maps' longest has 9 (ClarD's war monitors).
SEQUENCE_SLOTS = 9

# UV animation modes, in the W of the animation's second vector (import_map.py, uv_anim_vectors):
# 1 panner (A.xy: UV per second), 2 oscillator, 3 rotator.
UV_ANIM_PANNER, UV_ANIM_OSCILLATOR, UV_ANIM_ROTATOR = 1, 2, 3


def uv_anim_code(uv, a, b, c, t):
    """HLSL that moves `uv` as one UE2 texture modifier does at scene time `t`, after the Godot client's
    svm_anim (game/Shaders/svm_world.gdshaderinc) and the original's GetMatrix of each:
      panner      UTexPanner / UTexFriendlyPanner: the offset wraps, frac(t x speed).
      oscillator  UTexOscillator, per axis with p = frac(t x rate): OT_Pan (0) offsets by amp sin(2 pi (p + phase)),
                  OT_Stretch (1) scales by 1 + amp sin(2 pi (p + phase)), OT_StretchRepeat (2) by
                  1 + amp sin(2 pi phase + p pi / 2), about the pivot (UOffset / USize, VOffset / VSize); OT_Jitter
                  (3, random, no map uses it) stays still. A = (U amp, U rate, U type, U phase),
                  B = (V amp, V rate, V type, mode), C = (V phase, 0, pivot U, pivot V).
      rotator     UTexRotator, constantly rotating: A.x radians per second about the pivot C.zw (UOffset / USize,
                  VOffset / VSize, the centre of a UE2 texture for its stock 64 or 128 offsets)."""
    return (" { float mode_ = {b}.w;"
            " if (mode_ > 2.5) {"
            " float r_ = {a}.x * {t}; float2 d_ = {uv} - {c}.zw; float cs_ = cos(r_); float sn_ = sin(r_);"
            " {uv} = {c}.zw + float2(d_.x * cs_ - d_.y * sn_, d_.x * sn_ + d_.y * cs_); }"
            " else if (mode_ > 1.5) {"
            " float2 p_ = frac(float2({a}.y, {b}.y) * {t}); float2 amp_ = float2({a}.x, {b}.x);"
            " float2 kind_ = float2({a}.z, {b}.z); float2 ph_ = float2({a}.w, {c}.x);"
            " float2 s_ = amp_ * sin(6.2831853 * (p_ + ph_));"
            " float2 sr_ = amp_ * sin(6.2831853 * ph_ + p_ * 1.5707964);"
            " float2 st_ = ({uv} - {c}.zw) * (1.0 + lerp(s_, sr_, step(1.5, kind_))) + {c}.zw;"
            " float2 pan_ = {uv} + s_;"
            " {uv}.x = kind_.x > 2.5 ? {uv}.x : (kind_.x > 0.5 ? st_.x : pan_.x);"
            " {uv}.y = kind_.y > 2.5 ? {uv}.y : (kind_.y > 0.5 ? st_.y : pan_.y); }"
            " else if (mode_ > 0.5) { {uv} += frac({a}.xy * {t}); } }"
            ).replace("{uv}", uv).replace("{a}", a).replace("{b}", b).replace("{c}", c).replace("{t}", t)


# MaterialSequence.PreSetMaterial (Godot: MaterialSequenceClock.cs): the scene time runs through the items' times,
# looping (or held on the last item); an item whose action is MSA_FadeToMaterial blends from the item before it
# over its time, in the byte steps of the original's ConstantColor alpha; a paused sequence shows its first item.
# Info = (count, total time, loop, paused); Tk / Fk the items' times and fade flags, four to a vector.
# Returns (item faded from, item shown, blend).
SEQUENCE_CLOCK_CODE = """
float durs[12] = { T0.x, T0.y, T0.z, T0.w, T1.x, T1.y, T1.z, T1.w, T2.x, T2.y, T2.z, T2.w };
float fades[12] = { F0.x, F0.y, F0.z, F0.w, F1.x, F1.y, F1.z, F1.w, F2.x, F2.y, F2.z, F2.w };
int count = (int)clamp(Info.x, 1.0, %d.0);
float total = max(Info.y, 1e-4);
float t = Info.w > 0.5 ? 0.0 : (Info.z > 0.5 ? fmod(Time, total) : min(Time, total));
float3 picked = float3(0.0, 0.0, 1.0);
float start = 0.0;
[loop] for (int i = 0; i < %d; i++)
{
    if (i >= count) break;
    float d = max(durs[i], 0.0);
    float e = start + d;
    if (t >= start && (t < e || i == count - 1))
    {
        bool fade = fades[i] > 0.5 && d > 0.0 && t < e && Info.w < 0.5;
        float blend = fade ? clamp(floor((t - start) / d * 255.0 + 0.5), 0.0, 255.0) / 255.0 : 1.0;
        picked = float3(fade ? (float)((i + count - 1) %% count) : (float)i, (float)i, blend);
        break;
    }
    start = e;
}
return picked;""" % (SEQUENCE_SLOTS, SEQUENCE_SLOTS)


def sequence_colour(g, time, shared_samplers=False):
    """The colour of a MaterialSequence: each item is its own texture with its own modifier (AnimA/B/C, as
    uv_anim_code) and AnimNext flipbook (SeqFlip = columns, rows, frames, fps), sampled with the material's own
    sampling (so mip selection works on every kind of mesh), and the clock picks the two items to show and blends
    them. The items' textures are SeqTex0..8 (white when unused)."""
    clock = g.custom(SEQUENCE_CLOCK_CODE, FLOAT3, Time=time, Info=g.vector4("SeqInfo", (1, 0, 1, 0)),
                     T0=g.vector4("SeqTimes0", (0, 0, 0, 0)), T1=g.vector4("SeqTimes1", (0, 0, 0, 0)),
                     T2=g.vector4("SeqTimes2", (0, 0, 0, 0)), F0=g.vector4("SeqFades0", (0, 0, 0, 0)),
                     F1=g.vector4("SeqFades1", (0, 0, 0, 0)), F2=g.vector4("SeqFades2", (0, 0, 0, 0)))
    texcoord = g.node(unreal.MaterialExpressionTextureCoordinate)
    samples = {}
    for slot in range(SEQUENCE_SLOTS):
        uv = g.custom("float2 uv = UV;" + uv_anim_code("uv", "A", "B", "C", "T") +
                      " if (Flip.z > 1.5) {"
                      " float frame = floor(fmod(T * Flip.w, Flip.z));"
                      " uv = (frac(uv) + float2(fmod(frame, Flip.x), floor(frame / Flip.x))) / Flip.xy; }"
                      " return uv;", FLOAT2, UV=texcoord, T=time,
                      A=g.vector4("Seq%dAnimA" % slot, (0, 0, 0, 0)), B=g.vector4("Seq%dAnimB" % slot, (0, 0, 0, 0)),
                      C=g.vector4("Seq%dAnimC" % slot, (0, 0, 0, 0)), Flip=g.vector4("Seq%dFlip" % slot, (1, 1, 1, 0)))
        sample = g.texture("SeqTex%d" % slot, "/Engine/EngineResources/WhiteSquareTexture", uvs=uv)
        if shared_samplers:
            # Runtime frame textures all use wrap addressing. One sampler leaves room for forward translucency on SM5.
            sample.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        samples["S%d" % slot] = (sample, "RGBA")
    pick = "float4 s[%d] = { %s }; int a = (int)(W.x + 0.5); int b = (int)(W.y + 0.5); return lerp(s[a], s[b], W.z);" % (
        SEQUENCE_SLOTS, ", ".join("S%d" % slot for slot in range(SEQUENCE_SLOTS)))
    return g.custom(pick, FLOAT4, W=clock, **samples)


def light_collection():
    """The lighting plan's parameter collection: made when it is missing, and given the values it lacks. The values
    it has keep their ids; a default that differs from the table's is put back to it."""
    collection = unreal.load_asset(LIGHT_COLLECTION) if editor_assets.does_asset_exist(LIGHT_COLLECTION) else None
    made = collection is None
    if made:
        folder, name = LIGHT_COLLECTION.rsplit("/", 1)
        collection = asset_tools.create_asset(name, folder, unreal.MaterialParameterCollection,
                                              unreal.MaterialParameterCollectionFactoryNew())
    values = list(collection.get_editor_property("scalar_parameters"))
    have = {str(value.get_editor_property("parameter_name")): value for value in values}
    changed = made
    for name, default in LIGHT_PARAMETERS:
        value = have.get(name)
        if value is None:
            value = unreal.CollectionScalarParameter()
            value.set_editor_property("parameter_name", name)
            value.set_editor_property("default_value", default)
            values.append(value)
            changed = True
        elif abs(value.get_editor_property("default_value") - default) > 1e-6:
            value.set_editor_property("default_value", default)
            changed = True
    if changed:
        collection.set_editor_property("scalar_parameters", values)
        editor_assets.save_asset(LIGHT_COLLECTION)
        unreal.log_warning("SCCT-IMPORT %s %s" % ("made" if made else "updated", LIGHT_COLLECTION))
    return collection


class Graph:
    """Small helpers over the material editing library: every node is laid out left of the last."""

    def __init__(self, path, rebuild_in_place=False):
        folder, name = path.rsplit("/", 1)
        self.path = path
        if rebuild_in_place and editor_assets.does_asset_exist(path):
            self.material = unreal.load_asset(path)
            lib.delete_all_material_expressions(self.material)
        else:
            if editor_assets.does_asset_exist(path):
                editor_assets.delete_asset(path)
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

    def vector4(self, name, default):
        """A vector parameter as all four of its channels (its plain output is only the colour's three)."""
        return (self.colour(name, default), "RGBA")

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

    def light(self, name):
        """A value of the lighting plan's parameter collection (LIGHT_PARAMETERS)."""
        node = self.node(unreal.MaterialExpressionCollectionParameter, collection=light_collection())
        node.set_editor_property("parameter_name", name)
        return node

    def static_switch(self, name, when_on, when_off):
        """A static switch parameter (off by default): an instance that turns it on compiles `when_on`, every other
        instance `when_off` alone. Each is a node or (node, output name)."""
        switch = self.node(unreal.MaterialExpressionStaticSwitchParameter, parameter_name=name, default_value=False)
        for source, pins in ((when_on, ("True", "A")), (when_off, ("False", "B"))):
            node, output = source if isinstance(source, tuple) else (source, "")
            if not any(lib.connect_material_expressions(node, output, switch, pin) for pin in pins):
                raise RuntimeError("could not connect static switch '%s' in %s" % (name, self.path))
        return switch

    def in_the_picture(self, light, amount=None):
        """`light` as a brightness in the finished picture instead of in the scene: divided by the exposure the
        eye has settled on (the engine's eye adaptation node). With legacy lighting, exposure varies over
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
        if not editor_assets.save_asset(self.path):
            raise RuntimeError("Could not save material " + self.path)
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


def pawn_light(g, normal, world_position):
    """Four ordinary lights from the simulation. Slots 3..35 match FScctPawnLighting."""
    active = g.node(unreal.MaterialExpressionScalarParameter, parameter_name="HasPawnRig", default_value=0.0,
                    use_custom_primitive_data=True, primitive_data_index=3)
    inputs = {}
    for i in range(4):
        for j, name in enumerate(("Place", "Colour")):
            key = name + str(i)
            param = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="PawnLamp" + key,
                           default_value=unreal.LinearColor(0, 0, 0, 0), use_custom_primitive_data=True,
                           primitive_data_index=4 + 8 * i + 4 * j)
            inputs[key] = (param, "RGBA")
    term = g.custom("""
        if (Active < 0.5 || Plan < 0.5) return 0;
        float4 places[4] = {Place0, Place1, Place2, Place3};
        float3 colours[4] = {Colour0.rgb, Colour1.rgb, Colour2.rgb, Colour3.rgb};
        float3 term = float3(Colour0.a, Colour1.a, Colour2.a);
        [unroll] for (int i = 0; i < 4; ++i) {
            float3 toward = places[i].xyz - P * places[i].w;
            term += colours[i] * saturate(dot(N, toward) * rsqrt(max(dot(toward, toward), 1e-6)));
        }
        return pow(clamp(term, 0.0, 2.0), 2.2);
        """, FLOAT3, Active=active, Plan=g.light("Plan"), N=normal, P=world_position, **inputs)
    # Alpha of the last lamp is independent of that lamp. Zero keeps ordinary surfaces and previews as authored.
    # On a spy body it is one plus the display-space illumination actually reaching the pawn.
    return term, (inputs["Colour3"][0], "A")


def build_surface(cosmetic=False, scrolling_overlay=False, translucent_sequence=False):
    path = SURFACE
    if cosmetic:
        path = "/Game/OpenSCCT/Materials/M_ScctCosmeticFinish"
    elif scrolling_overlay:
        path = SCROLLING_OVERLAY
    elif translucent_sequence:
        path = TRANSLUCENT_SEQUENCE
    g = Graph(path, rebuild_in_place=path == SURFACE)
    material = g.material
    if not cosmetic:
        # Legacy imported and runtime lit-translucent instances inherit this setting from the opaque master.
        material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    # Normals leave the graph in world space: the detail layer is projected along world axes.
    material.set_editor_property("tangent_space_normal", False)
    # One material serves level surfaces and characters.
    material.set_editor_property("used_with_skeletal_mesh", True)
    material.set_editor_property("used_with_nanite", True)

    # ---- the original texture, moved as the original moved it: its scrolling ("panner"), its oscillator or
    # rotator (UV_ANIM_CODE: AnimA/B/C, the converter's one modifier per layer, its static TexScaler and fixed
    # rotations being baked into the mesh's UVs already), then its flipbook (caustics, rain): BaseColor is then
    # the sheet of frames, played at the original's rate.
    time = g.node(unreal.MaterialExpressionTime)
    uv = g.custom("float2 uv = UV + frac(T * float2(U, V));" + uv_anim_code("uv", "A", "B", "C", "T") +
                  " if (Cols * Rows > 1.5) {"
                  " float frame = floor(fmod(T * Fps, max(Frames, 1.0)));"
                  " uv = (frac(uv) + float2(fmod(frame, Cols), floor(frame / Cols))) / float2(Cols, Rows); }"
                  " return uv;", FLOAT2,
                  UV=g.node(unreal.MaterialExpressionTextureCoordinate), T=time,
                  U=g.scalar("PanU", 0.0), V=g.scalar("PanV", 0.0),
                  A=g.vector4("AnimA", (0, 0, 0, 0)), B=g.vector4("AnimB", (0, 0, 0, 0)), C=g.vector4("AnimC", (0, 0, 0, 0)),
                  Cols=g.scalar("FlipColumns", 1.0), Rows=g.scalar("FlipRows", 1.0),
                  Fps=g.scalar("FlipRate", 0.0), Frames=g.scalar("FlipFrames", 1.0))
    sampled = g.texture("BaseColor", "/Engine/EngineMaterials/DefaultDiffuse", uvs=uv)
    # A MaterialSequence (screens, alarm signs, blinking lamps) replaces the texture with its timeline when the
    # instance turns "Sequence" on; every other instance compiles without it.
    if scrolling_overlay:
        # UE2 adds the moving layer in the stored sRGB space. The base picture and its lettering stay fixed.
        overlay_uv = g.custom("return UV + frac(T * Speed.xy);", FLOAT2, UV=uv, T=time,
                              Speed=g.vector4("OverlayAnimA", (0, 0, 0, 0)))
        overlay = g.texture("OverlayTexture", "/Engine/EngineResources/Black", uvs=overlay_uv)
        light_first = g.scalar("LightBeforeOverlay", 0.0)
        picked = g.custom("float3 both = sRGBToLinear(saturate(LinearToSrgb(Base.rgb) + LinearToSrgb(Layer)));"
                          " return float4(lerp(both, Base.rgb, LightFirst), Base.a);", FLOAT4,
                          Base=(sampled, "RGBA"), Layer=(overlay, "RGB"), LightFirst=light_first)
        picked.set_editor_property("include_file_paths", ["/Engine/Private/GammaCorrectionCommon.ush"])
    elif translucent_sequence:
        picked = sequence_colour(g, time, shared_samplers=True)
    else:
        picked = g.static_switch("Sequence", sequence_colour(g, time), (sampled, "RGBA"))
    base_rgb = g.custom("return P.rgb;", FLOAT3, P=picked)
    base = {"RGB": base_rgb, "G": g.custom("return P.g;", FLOAT1, P=picked), "A": g.custom("return P.a;", FLOAT1, P=picked)}
    tint = g.colour("Tint", (1, 1, 1, 1))

    # ---- the surface normal: the map derived from the original texture, turned into world space
    macro = g.texture("Normal", "/Engine/EngineMaterials/DefaultNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, uvs=uv)
    macro_strength = g.scalar("MacroNormal", 1.0)
    detail_strength = g.scalar("DetailNormalStrength", 0.0)
    rough_amount = g.scalar("DetailRoughness", 0.0)
    if path == SURFACE:
        # The profile describes a material's finish, independently of the colour printed or painted on it.
        # Earlier map imports have class-specific detail maps but no finish profile; use their existing class range.
        finish = g.vector4("BodycamFinish", (-1, 0, 1, 1))
        physical = g.custom("return (F.r >= 0 || Detail > 0) ? Active : 0;", FLOAT1,
                            F=finish, Detail=rough_amount, Active=g.light("BodycamSurfaces"))
        micro_scale = g.custom("return lerp(1.0, F.r < 0 ? 0.75 : F.a, Active);", FLOAT1, F=finish, Active=physical)
        macro_strength = g.custom("return S * lerp(1.0, F.r < 0 ? 0.65 : F.b, Active);", FLOAT1,
                                  S=macro_strength, F=finish, Active=physical)
        detail_strength = g.custom("return S * Gain;", FLOAT1, S=detail_strength, Gain=micro_scale)
    macro_scaled = g.custom("return normalize(float3(N.xy * S, N.z));", FLOAT3, N=(macro, "RGB"), S=macro_strength)
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

    detailed = g.custom(DETAIL_NORMAL_CODE, FLOAT3, N=facing, S=detail_strength,
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
    colour_amount = g.scalar("DetailColour", 0.0)
    if path == SURFACE:
        # A finish can suppress a misclassified legacy detail overlay (e.g. a painted wall with a wood set).
        occlusion_amount = g.custom("return A * Gain;", FLOAT1, A=occlusion_amount, Gain=micro_scale)
        colour_amount = g.custom("return A * Gain;", FLOAT1, A=colour_amount, Gain=micro_scale)
    colour = g.custom(
        "float fade = lerp(1.0, 0.35, saturate((Depth - 500.0) / 2500.0));"
        " float grain = clamp(1.0 + (D.b * 2.0 - 1.0) * Amount * fade, 0.45, 1.6);"
        " return Base * Tint * grain * lerp(1.0, D.r, AO * 0.5 * fade);", FLOAT3,
        Base=base["RGB"], Tint=tint, D=packed, Amount=colour_amount, AO=occlusion_amount,
        Depth=g.node(unreal.MaterialExpressionPixelDepth))
    pawn, spy_light = pawn_light(g, normal, world_position)
    # Real lights still shade the mesh per pixel. A spy wholly in cover must not pick up the room's reflection
    # environment. Fade in the engine lighting response through the dark end of the original light term.
    receiver = g.custom("return L < 1.0 ? 1.0 : smoothstep(0.0, 0.15, L - 1.0);", FLOAT1, L=spy_light)
    g.output(g.custom("return C * Receive;", FLOAT3, C=colour, Receive=receiver), unreal.MaterialProperty.MP_BASE_COLOR)

    # ---- roughness: the class sets the level (darker parts of the original read as rougher grime, brighter parts
    # as worn, smoother material); the set adds scratches, pores and smears around it.
    dark, bright = g.scalar("RoughnessDark", 0.85), g.scalar("RoughnessBright", 0.45)
    roughness = g.custom("return saturate(lerp(Dark, Bright, G) + (D.g - 0.5) * 2.0 * Amount);", FLOAT1,
                         Dark=dark, Bright=bright, G=base["G"], D=packed, Amount=rough_amount)
    metallic = g.scalar("Metallic", 0.0)
    if path == SURFACE:
        uniform_finish = g.custom("return F.r < 0 ? float2(0.5 * (Dark + Bright), M) : F.rg;", FLOAT2,
                                  F=finish, Dark=dark, Bright=bright, M=metallic)
        atlas = g.texture("BodycamFinishAtlas", "/Engine/EngineMaterials/DefaultDiffuse_TC_Masks",
                          unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, uvs=uv)
        atlas_finish = g.custom("return T.rg;", FLOAT2, T=(atlas, "RGB"))
        response = g.static_switch("UseBodycamFinishAtlas", atlas_finish, uniform_finish)
        # The packed surface roughness is centred on 0.5. Colour markings keep the finish of the substrate.
        roughness = g.custom("return lerp(R, clamp(F.x + (D.g - 0.5) * 1.6 * Amount * Gain, 0.08, 1.0), A);",
                             FLOAT1, R=roughness, F=response, D=packed, Amount=rough_amount, Gain=micro_scale, A=physical)
        metallic = g.custom("return lerp(M, F.y, A);", FLOAT1, M=metallic, F=response, A=physical)
    # Dry spy cloth uses its painted wear. Suppress the white dielectric reflection that makes dark fabric look wet,
    # including imported map outfits inheriting the surface master's default specular.
    roughness = g.custom("return L >= 1.0 ? max(R, 0.92) : R;", FLOAT1, L=spy_light, R=roughness)
    specular = g.custom("return L >= 1.0 ? min(S, 0.04) * Receive : S;", FLOAT1,
                        S=g.scalar("Specular", 0.5), L=spy_light, Receive=receiver)
    if cosmetic:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        coverage = (g.texture("FinishMask", "/Engine/EngineResources/WhiteSquareTexture"), "R")
        mirror = g.scalar("GoldMirror", 0.0)
        roughness = g.custom("return lerp(R, lerp(.16,.055,Gold), Mask);", FLOAT1, R=roughness, Gold=mirror, Mask=coverage)
        metallic = g.custom("return lerp(M, Gold, Mask);", FLOAT1, M=metallic, Gold=mirror, Mask=coverage)
    if path == SURFACE:
        from scct_weapon_finish import weapon_finish
        roughness, metallic = weapon_finish(g, roughness, metallic)
    g.output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(metallic, unreal.MaterialProperty.MP_METALLIC)
    g.output(specular, unreal.MaterialProperty.MP_SPECULAR)
    # (Its output is with the baked light below: the lighting plan gates it by that.)
    occlusion = g.custom("return lerp(1.0, D.r, AO);", FLOAT1, D=packed, AO=occlusion_amount)

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
    vertex_colour = g.node(unreal.MaterialExpressionVertexColor)
    # The baked light keeps only part of its colour: it is a fill under lamps that carry the same tint, and with
    # their bounces on top a tinted room turned into one flat hue.
    #
    # THE LIGHTING PLAN (scct.Light.Plan 1; LIGHT_PARAMETERS). The baked light is the world's light, whole: the
    # original drew a surface at min(1, texture x min(2, 2 x lightmap + 2 x vertex colour + ...)) in display values,
    # and under the plan's fixed exposure and tone curve a display value v is scene light v ^ 2.2. So the surface
    # gives off its colour times that term to the power 2.2 (the lightmap page is sampled as sRGB, so its stored
    # value is taken back out of the sample first), in the term's own colour and with no share held back. The
    # instance's BakedFill does not enter: a surface without a lightmap or baked vertex colours has a term of 0.
    # The fourth value is the surface's occlusion under the plan (below): how much bounced and sky light it takes.
    # With Plan 0 the first three are the released formula, to the letter, and the fourth is 1.
    baked_plan = g.custom(
        "float3 vertex = pow(saturate(VC.rgb) * 2.0, 2.2) * saturate(UseVC);"
        " float3 light = min(LM * 4.6, 4.6) + vertex;"
        " float3 released = lerp(dot(light, float3(0.2126, 0.7152, 0.0722)), light, 0.6) * Fill;"
        "\n#if MATERIAL_SHADINGMODEL_UNLIT || MATERIALBLENDING_ADDITIVE\n Boost = 0.0;\n#endif\n"
        " if (Plan < 0.5) return float4(released + pow(max(Boost, 0.0), 2.2), 1.0);"
        " float3 lm = max(LM, 0.0);"
        " float3 stored = float3(lm.r <= 0.0031308 ? lm.r * 12.92 : 1.055 * pow(lm.r, 1.0 / 2.4) - 0.055,"
        " lm.g <= 0.0031308 ? lm.g * 12.92 : 1.055 * pow(lm.g, 1.0 / 2.4) - 0.055,"
        " lm.b <= 0.0031308 ? lm.b * 12.92 : 1.055 * pow(lm.b, 1.0 / 2.4) - 0.055);"
        # Parts of a retail map have surface irradiance but no surviving world emitter.
        # Bound the recovered diffuse light in bright pools and keep authored black at zero.
        # The reference is already indirect illumination. Keep it out of Lumen's emissive cache.
        " if (Plan > 1.5) { float3 residual = 0;"
        "\n#if !LUMEN_CARD_CAPTURE && !RAYTRACINGSHADER && !MATERIALBLENDING_ADDITIVE && !MATERIAL_SHADINGMODEL_UNLIT\n"
        " float3 reference = 2.0 * (stored + saturate(VC.rgb) * (UseVC > 0.5 && UseVC < 1.5));"
        " float peak = max(reference.r, max(reference.g, reference.b));"
        " residual = reference * Gain / (1.0 + max(peak - 0.6, 0.0));"
        "\n#endif\n"
        " return float4(pow(clamp(residual + Ambient + Boost, 0.0, 2.0), 2.2), 1.0); }"
        " float3 term = Scale * (stored + saturate(VC.rgb) * saturate(UseVC)) + Ambient + Boost;"
        " term = min(term, 2.0);"
        " float level = dot(term, float3(0.3, 0.59, 0.11));"
        " term = max(lerp(level, term, Saturation) * Gain, 0.0);"
        " float gate = Bounce * smoothstep(Low, max(High, Low + 0.001), level);"
        " return float4(pow(term, 2.2), gate);", FLOAT4,
        LM=(lightmap, "RGB"), VC=vertex_colour, UseVC=baked_vertex,
        Fill=g.scalar("BakedFill", 0.0), Plan=g.light("Plan"), Scale=g.light("BakedScale"), Gain=g.light("BakedGain"),
        Saturation=g.light("BakedSaturation"), Ambient=g.light("Ambient"), Boost=g.light("Boost"),
        Bounce=g.light("BounceGate"), Low=g.light("BounceLow"), High=g.light("BounceHigh"))
    baked = g.custom("return B.rgb;", FLOAT3, B=baked_plan)
    # The occlusion the engine's bounce and sky light go by. Released: the detail layer's pores. Under the plan a
    # surface takes bounced and sky light only as far as the original lit it itself: none on a surface the original
    # has dark (or on anything without baked light: characters, movers), so no bounce lights what the original left
    # black, and the truly lit rooms keep the bounce that makes them look built.
    g.output(g.custom("return O * B.a;", FLOAT1, O=occlusion, B=baked_plan), unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    # The glow of a screen, a lamp glass or an additive flare is a brightness in the finished picture (see
    # Graph.in_the_picture): 1 is the texture as painted, whatever the exposure. The baked light is light in the
    # scene like any lamp's and stays that.
    # Painted additive meshes use vertex RGB as the entire light term. A zero at the edge of an
    # aquarium ripple must add nothing, even when the underlying animated texture is bright there.
    painted_additive = g.custom("#if MATERIALBLENDING_ADDITIVE\nreturn Kind > 1.5 ? 1.0 : 0.0;\n"
                                "#else\nreturn 0.0;\n#endif\n", FLOAT1, Kind=baked_vertex)
    glow = g.custom("if (Painted > 0.5) return Base * Tint * pow(saturate(VC.rgb) * 2.0, 2.2);"
                    " return Base * Tint * Whole + Lit * Mask * Strength;", FLOAT3,
                    Painted=painted_additive, VC=vertex_colour,
                    Base=base["RGB"], Tint=tint, Whole=g.scalar("Emissive", 0.0),
                    Lit=(self_lit, "RGB"), Mask=(self_mask, "A"), Strength=g.scalar("SelfIllum", 0.0))
    if scrolling_overlay:
        # A bare opaque Combiner lights stage 1 before adding stage 2 (e.g. the Prison oscilloscope trace).
        glow = g.custom("return Glow + Layer * LightFirst;", FLOAT3, Glow=glow,
                        Layer=(overlay, "RGB"), LightFirst=light_first)
    # ---- the least light. The original never drew a lit surface black: every zone had an ambient light, and its
    # darkest rooms were dim shapes, not a black screen. Real lights and real shadows have no such floor, and a room
    # whose lamps did not survive in the map file is pitch dark however far the eye opens. So every lit surface
    # gives off a trace of its own colour, a little stronger on what faces up and weaker in the detail layer's
    # pores, so a dark room keeps its forms and not only its outlines. It is light in the scene, far below any
    # lamp's: lost in a lit room, and what the eye finds once it has opened for a dark one. (Not a brightness in
    # the picture like the glows: Lumen gathers every surface's glow without the eye's exposure, and a whole
    # level glowing "the same at any exposure" lit itself up like a lamp.)
    # Under the lighting plan the floor goes (the collection's Floor is the share that stays): the original's dark
    # is black, and the plan's fixed exposure shows it so.
    least = g.custom("return Base * Tint * Floor * (0.55 + 0.45 * saturate(N.z * 0.5 + 0.5)) * AO * (Plan < 0.5 ? 1.0 : Share);", FLOAT3,
                     Base=base["RGB"], Tint=tint, Floor=g.scalar("AmbientFloor", 0.0), N=normal, AO=occlusion,
                     Plan=g.light("Plan"), Share=g.light("Floor"))
    baked_colour = g.custom("return Base * Tint;", FLOAT3, Base=base["RGB"], Tint=tint)
    if path == SURFACE:
        # The original baked term also lights the physical albedo. Never add a reflection or a minimum to dark rooms.
        baked_colour = g.custom("return lerp(Original, Surface, A);", FLOAT3, Original=baked_colour, Surface=colour, A=physical)
    emissive = g.custom("return Painted > 0.5 ? Glow : Glow + Least + LitColour * Baked + Base * Tint * Pawn;", FLOAT3,
                        Painted=painted_additive,
                        Glow=g.in_the_picture(glow, g.scalar("GlowInPicture", 1.0)), Least=least, Base=base["RGB"],
                        Tint=tint, Baked=baked, Pawn=pawn, LitColour=baked_colour)
    if cosmetic:
        # The Armoury's studio reflection follows the light reaching the suit when worn in a match.
        reflection = g.node(unreal.MaterialExpressionReflectionVectorWS)
        view = g.node(unreal.MaterialExpressionTransform,
                      transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                      transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_VIEW)
        g.link(reflection, view, "Input")
        fresnel = g.node(unreal.MaterialExpressionFresnel, exponent=4.0, base_reflect_fraction=0.0)
        sheen = g.custom("""float3 r = normalize(R);
            float level = saturate(dot(Lit,float3(1,1,1))/max(dot(C,float3(1,1,1)),.03));
            float shade = lerp(lerp(.3,1,level),1,Gold);
            if (SpyLight >= 1.0) shade = pow(saturate(SpyLight - 1.0), 2.2);
            float env = lerp(.3,1.25,smoothstep(-.45,.9,r.y)) + .85*exp(-pow((r.y-.12)*7,2));
            float streaks = .35*sin(r.x*7+r.y*2.5) + .2*sin(r.x*17-r.z*5)
                + .7*pow(saturate(dot(r,normalize(float3(-.7,.45,.4)))),6);
            env = max(env+Gold*streaks,.15);
            float spec = pow(saturate(dot(r,normalize(float3(.35,.8,.5)))),48);
            float spec2 = pow(saturate(dot(r,normalize(float3(-.6,.3,.7)))),16);
            float band = frac((UV.x+UV.y)*.5-T*.18);
            float glint = smoothstep(0,.035,band)*(1-smoothstep(.035,.09,band));
            float3 tint = lerp(float3(1,.93,.85),float3(1,.8,.36),Gold);
            float strength = lerp(.7,1,Gold);
            return saturate((C*env*lerp(.75,.95,Gold) + tint*(1.8*spec+.5*spec2+.6*Rim+.5*glint*Gold))*shade*strength);""",
            FLOAT3, R=view, C=base_rgb, Gold=mirror, T=time, UV=uv, Rim=fresnel, Lit=emissive, SpyLight=spy_light)
        emissive = g.custom("return E*(1-Mask*lerp(.525,.95,Gold)) + Mask*S;", FLOAT3,
                            E=emissive, Mask=coverage, Gold=mirror, S=g.in_the_picture(sheen))
    g.output(emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    alpha = g.custom("return Painted > 0.5 ? O : A * O;", FLOAT1, Painted=painted_additive,
                     A=base["A"], O=g.scalar("Opacity", 1.0))
    g.output(alpha, unreal.MaterialProperty.MP_OPACITY)

    # The mask: the texture's alpha (AlphaMask 0 ignores it: a body's texture keeps other things in its alpha),
    # less the pixels of the near fade's screen door. The pattern moves every frame so the anti-aliasing blends
    # it into an even transparency. Shadows and traced rays (reflections, bounce light) meet the whole shape.
    near = g.node(unreal.MaterialExpressionScalarParameter, parameter_name="NearFade", default_value=0.0,
                  use_custom_primitive_data=True, primitive_data_index=NEAR_FADE_SLOT)
    # HorizonCut (0 = off): a skyline panorama that has the original's painted sky above its city keeps only what
    # lies below this share of its texture's height, so the real sky shows above the city (import_map.py, the
    # skyline; map_skies.json names the panoramas). The edge is a screen door over 5% of the height, which the
    # anti-aliasing turns into a soft fade, so the city's glow melts into the sky instead of ending on a ruled line.
    # HorizonKey (0 = off) is for a panorama whose land is a black silhouette with the painted sky's glow running
    # down between its peaks to the texture's foot (MuseE's sunset behind the hills): no height cut parts the two,
    # so a pixel brighter than this (linear, its brightest channel) is dropped as well, screen-doored from half to
    # one and a half times it, and only the dark ridge stays against the real sky.
    whole = g.custom("float keep = 1.0;"
                     " if (Cut > 0.0) {"
                     " float2 p = P + 5.588238 * float(View.StateFrameIndexMod8);"
                     " float n = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));"
                     " float share = saturate((frac(UV.y) - Cut) / 0.05 + 0.5);"
                     " if (Key > 0.0) share *= 1.0 - saturate((max(max(C.r, C.g), C.b) - 0.5 * Key) / Key);"
                     " keep = n < share ? 1.0 : 0.0; }"
                     " return lerp(1.0, A, saturate(UseAlpha)) * keep;", FLOAT1,
                     A=alpha, UseAlpha=g.scalar("AlphaMask", 1.0), Cut=g.scalar("HorizonCut", 0.0),
                     Key=g.scalar("HorizonKey", 0.0), C=base["RGB"],
                     UV=g.node(unreal.MaterialExpressionTextureCoordinate),
                     P=(g.node(unreal.MaterialExpressionScreenPosition), "PixelPosition"))
    mask = g.custom("float gone = 1.0 - smoothstep(Start * %f, Start, Depth);"
                    " float2 p = P + 5.588238 * float(View.StateFrameIndexMod8);"
                    " float n = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));"
                    " return (Start > 0.0 && n < gone) ? 0.0 : Kept;" % NEAR_FADE_GONE, FLOAT1,
                    Kept=whole, Start=near, Depth=g.node(unreal.MaterialExpressionPixelDepth),
                    P=(g.node(unreal.MaterialExpressionScreenPosition), "PixelPosition"))
    in_shadow = g.node(unreal.MaterialExpressionShadowReplace)
    g.link(mask, in_shadow, "Default")
    g.link(whole, in_shadow, "Shadow")
    traced = g.node(unreal.MaterialExpressionRayTracingQualitySwitch)
    g.link(in_shadow, traced, "Normal")
    g.link(whole, traced, "RayTraced")
    g.output(traced, unreal.MaterialProperty.MP_OPACITY_MASK)
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


def build_modulate():
    g = Graph(MODULATE)
    g.material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MODULATE)
    g.material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # The D3D9 framebuffer blend is a multiplication. With Darken the factor is (1 - source);
    # with Modulate it is 2 * source. UE textures and the destination framebuffer are linear.
    base = g.texture("BaseColor", "/Engine/EngineResources/Black")
    vertex_light = g.node(unreal.MaterialExpressionScalarParameter, parameter_name="BakedVertexLight", default_value=0.0,
                          use_custom_primitive_data=True, primitive_data_index=BAKED_VERTEX_SLOT)
    # The framebuffer sees the output of the texture stage, after MODULATE2X with the mesh's baked light.
    # On the tutorial bridge its vertex colours are only a few percent: inverting the raw yellow diffuse
    # instead of the lit source would turn the pane nearly black.
    factor = g.custom("float3 light = UseVC > 0.5 ? 2.0 * VC : 1.0;"
                      " float3 c = saturate(LinearToSrgb(saturate(C)) * light);"
                      " return sRGBToLinear(Darken > 0.5 ? 1.0 - c : 2.0 * c);",
                      FLOAT3, C=(base, "RGB"), VC=g.node(unreal.MaterialExpressionVertexColor),
                      UseVC=vertex_light, Darken=g.scalar("Darken", 0.0))
    factor.set_editor_property("include_file_paths", ["/Engine/Private/GammaCorrectionCommon.ush"])
    g.output(factor, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


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
    build_surface(scrolling_overlay=True)
    build_surface(translucent_sequence=True)
    build_modulate()
    build_glass()
    build_water()
    build_sky()
