"""The project's effect materials, built from code like the surface masters (scct_materials.py).

Runs inside the editor's Python. import_effects.py builds these and the Niagara systems (scct_niagara.py);
import_map.py (scct_fx.py) makes the instances a map needs.

  M_ScctParticleLit    smoke, steam, dust, spray: translucent sprites lit by the room's lights, so steam is dark
                       in a dark room and glows in a lamp's beam. The original drew these unlit.
  M_ScctParticleGlow   sparks, flames, bubbles, flares: sprites that add their own light to the picture.
  M_ScctCorona         the halo around a lamp: a camera-facing card that fades when the lamp is hidden.
  M_ScctLightFunction  what shapes a light: the rectangle and picture ("cookie") of the original's projector
                       lights, caustics, and the pulse and flicker of its animated lights.
  M_ScctProjector      a picture projected onto surfaces as a decal: signs, graffiti, rain splashes.
  M_ScctGlowCard       the original's painted light: beam and flare cards modelled into the level.
"""
import unreal

from scct_materials import FLOAT1, FLOAT2, FLOAT3, Graph

EFFECTS = "/Game/OpenSCCT/Effects"
PARTICLE_LIT = EFFECTS + "/M_ScctParticleLit"
PARTICLE_GLOW = EFFECTS + "/M_ScctParticleGlow"
CORONA = EFFECTS + "/M_ScctCorona"
LIGHT_FUNCTION = EFFECTS + "/M_ScctLightFunction"
PROJECTOR = EFFECTS + "/M_ScctProjector"
GLOW_CARD = EFFECTS + "/M_ScctGlowCard"
RAIN_STREAK = EFFECTS + "/T_RainStreak"

# Custom primitive data slots of a corona card (set per placed card by scct_fx.py).
CORONA_COLOUR_SLOT = 0     # 0..2: colour
CORONA_MIN_SLOT = 3        # smallest half size, world units
CORONA_MAX_SLOT = 4        # largest half size
CORONA_SCALE_SLOT = 5      # half size per unit of the original's distance factor
CORONA_REACH_SLOT = 6      # how far away the halo is still drawn (0: any distance)

# The cell of a flipbook texture. The original counts particle cells down a column first
# (spec/native/emitters.md rule 20). The particle hands its cell over in the first dynamic parameter.
CELL_UV_CODE = """
float cell = floor(Cell + 0.5);
float column = floor(cell / max(Rows, 1.0));
float row = cell - column * max(Rows, 1.0);
return (UV + float2(column, row)) / float2(max(Columns, 1.0), max(Rows, 1.0));"""

# A sheet of frames played over time, as the surface material does it (frames run along the rows).
FLIPBOOK_UV_CODE = """
float2 uv = UV;
if (Cols * Rows > 1.5) {
    float frame = floor(fmod(T * Fps, max(Frames, 1.0)));
    uv = (frac(uv) + float2(fmod(frame, Cols), floor(frame / Cols))) / float2(Cols, Rows);
}
return uv;"""


def primitive_data(g, name, slot, default=0.0):
    return g.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default,
                  use_custom_primitive_data=True, primitive_data_index=slot)


# ---------------------------------------------------------------------------------------------------------------
# Particles
# ---------------------------------------------------------------------------------------------------------------

def particle_inputs(g):
    """What both particle materials share: the texture (one flipbook cell of it), the particle's colour and the
    soft edge where a sprite meets a wall or comes close to the camera."""
    dynamic = g.node(unreal.MaterialExpressionDynamicParameter)
    uv = g.custom(CELL_UV_CODE, FLOAT2, UV=g.node(unreal.MaterialExpressionTextureCoordinate), Cell=dynamic,
                  Columns=g.scalar("Columns", 1.0), Rows=g.scalar("Rows", 1.0))
    texture = g.texture("Texture", "/Engine/EngineResources/WhiteSquareTexture", uvs=uv)
    colour = g.node(unreal.MaterialExpressionParticleColor)
    # A sprite cut by a wall shows a hard line; fading it out over the last few units hides the cut.
    soft = g.node(unreal.MaterialExpressionDepthFade, fade_distance_default=12.0)
    g.link(g.scalar("SoftEdge", 12.0), soft, "FadeDistance")
    # Close to the eye a sprite would fill the screen and pop as the camera passes through it.
    near = g.custom("return saturate((Depth - 8.0) / 40.0);", FLOAT1, Depth=g.node(unreal.MaterialExpressionPixelDepth))
    return texture, colour, soft, near


def build_particle_lit():
    g = Graph(PARTICLE_LIT)
    material = g.material
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Lit from the light volume around it, the same from every side: right for something without a surface.
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_niagara_sprites", True)
    texture, colour, soft, near = particle_inputs(g)
    base = g.custom("return T * C;", FLOAT3, T=(texture, "RGB"), C=(colour, "RGB"))
    g.output(base, unreal.MaterialProperty.MP_BASE_COLOR)
    # A little light of its own keeps a puff readable in a black room (the original drew it at full brightness).
    g.output(g.custom("return B * E;", FLOAT3, B=base, E=g.scalar("Emissive", 0.04)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.output(g.custom("return saturate(T * C * O) * S * N;", FLOAT1, T=(texture, "A"), C=(colour, "A"), O=g.scalar("Opacity", 1.0), S=soft, N=near),
             unreal.MaterialProperty.MP_OPACITY)
    return g.finish()


def build_particle_glow():
    g = Graph(PARTICLE_GLOW)
    material = g.material
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_niagara_sprites", True)
    texture, colour, soft, near = particle_inputs(g)
    # The original adds texture x colour to the frame; a fading particle lowers its colour. AlphaWeight is 1 for
    # textures whose shape is in their alpha channel (their colour alone would add a square).
    glow = g.custom("return T * C * CA * lerp(1.0, TA, saturate(W)) * B * S * N;", FLOAT3,
                    T=(texture, "RGB"), TA=(texture, "A"), C=(colour, "RGB"), CA=(colour, "A"),
                    W=g.scalar("AlphaWeight", 0.0), B=g.scalar("Brightness", 1.0), S=soft, N=near)
    g.output(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


# ---------------------------------------------------------------------------------------------------------------
# Coronas
# ---------------------------------------------------------------------------------------------------------------

# The card is any flat mesh with 0..1 texture coordinates; its corners are placed here, facing the camera. The
# size is the original's (docs/RENDERING.md, Coronas): it grows with distance up to a largest size, so a far lamp
# keeps a visible halo and a near one does not fill the screen.
CORONA_CORNER_CODE = """
float3 toCard = Centre - Camera;
float distanceFactor = clamp(0.001 * length(toCard), 0.01, 10.0);
float halfSize = clamp(distanceFactor * Scale, Least, Most);
float3 corner = Centre + (ResolvedView.ViewRight * (UV.x - 0.5) + ResolvedView.ViewUp * (0.5 - UV.y)) * 2.0 * halfSize;
return corner - Position;"""

# Where the lamp itself is on screen, and how far away (x, y: screen position 0..1, z: depth along the view).
CORONA_CENTRE_CODE = """
float3 toCard = Centre - Camera;
float depth = dot(toCard, ResolvedView.ViewForward);
float2 screen = float2(dot(toCard, ResolvedView.ViewRight) * ResolvedView.ViewToClip[0][0],
                       dot(toCard, ResolvedView.ViewUp) * ResolvedView.ViewToClip[1][1]) / max(depth, 1.0);
return float3(screen * float2(0.5, -0.5) + 0.5, depth);"""

# The halo shows while the lamp is in sight: the scene depth at the lamp and at four spots around it is compared
# with the lamp's own distance, with the original's tolerance (the lamp's housing must not hide its own halo).
# Five samples make it fade as the lamp passes behind an edge instead of switching.
CORONA_SIGHT_CODE = """
float limit = C.z - max(16.0, 0.08 * C.z);
float seen = (step(limit, D0) + step(limit, D1) + step(limit, D2) + step(limit, D3) + step(limit, D4)) / 5.0;
float inFront = step(1.0, C.z);
float near = saturate((C.z - 20.0) / 60.0);
float reach = Reach > 0.0 ? saturate((Reach - C.z) / (0.15 * Reach)) : 1.0;
return seen * inFront * near * reach;"""


def build_corona():
    g = Graph(CORONA)
    material = g.material
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    # Drawn over the lamp's own housing; whether the lamp is in sight is decided by the samples below.
    material.set_editor_property("disable_depth_test", True)

    centre = g.node(unreal.MaterialExpressionObjectPositionWS)
    camera = g.node(unreal.MaterialExpressionCameraPositionWS)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    corner = g.custom(CORONA_CORNER_CODE, FLOAT3, Centre=centre, Camera=camera, UV=uv,
                      Position=g.node(unreal.MaterialExpressionWorldPosition),
                      Scale=primitive_data(g, "Scale", CORONA_SCALE_SLOT, 512.0),
                      Least=primitive_data(g, "Least", CORONA_MIN_SLOT, 20.0),
                      Most=primitive_data(g, "Most", CORONA_MAX_SLOT, 40.0))
    g.output(corner, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    on_screen = g.custom(CORONA_CENTRE_CODE, FLOAT3, Centre=centre, Camera=camera)
    depths = []
    for dx, dy in ((0.0, 0.0), (0.004, 0.0), (-0.004, 0.0), (0.0, 0.007), (0.0, -0.007)):
        spot = g.custom("return C.xy + float2(%f, %f);" % (dx, dy), FLOAT2, C=on_screen)
        depth = g.node(unreal.MaterialExpressionSceneDepth, input_mode=unreal.MaterialSceneAttributeInputMode.COORDINATES)
        g.link(spot, depth, "Input")
        depths.append(depth)
    sight = g.custom(CORONA_SIGHT_CODE, FLOAT1, C=on_screen, D0=depths[0], D1=depths[1], D2=depths[2], D3=depths[3], D4=depths[4],
                     Reach=primitive_data(g, "Reach", CORONA_REACH_SLOT, 0.0))

    texture = g.texture("Texture", "/Engine/EngineResources/WhiteSquareTexture")
    colour = g.custom("return float3(R, G, B);", FLOAT3,
                      R=primitive_data(g, "ColourR", CORONA_COLOUR_SLOT, 1.0),
                      G=primitive_data(g, "ColourG", CORONA_COLOUR_SLOT + 1, 1.0),
                      B=primitive_data(g, "ColourB", CORONA_COLOUR_SLOT + 2, 1.0))
    glow = g.custom("return T * C * S * B;", FLOAT3, T=(texture, "RGB"), C=colour, S=sight, B=g.scalar("Brightness", 1.0))
    g.output(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


# ---------------------------------------------------------------------------------------------------------------
# Light functions
# ---------------------------------------------------------------------------------------------------------------

# A spot light's function is laid over the square around its cone (0..1 both ways at any distance). The
# original's projector lights are rectangles: RectU and RectV say how many times the cone's square is wider than
# the rectangle's half width and half height (0: no rectangle, the whole cone). Inside the rectangle the picture
# is the cookie texture, or without one the original's default: a soft round spot.
LIGHT_SHAPE_CODE = """
float2 p = float2((UV.y * 2.0 - 1.0) * RectU, (UV.x * 2.0 - 1.0) * RectV);
float inside = (RectU + RectV) > 0.0 ? step(abs(p.x), 1.0) * step(abs(p.y), 1.0) : 1.0;
float round = 1.0 - smoothstep(0.55, 1.0, length(p));
float edge = saturate((1.0 - max(abs(p.x), abs(p.y))) * 12.0);
return float3(p * float2(0.5, -0.5) + 0.5, inside * lerp(lerp(1.0, edge, step(0.001, RectU + RectV)), round, saturate(Round)));"""

# The original's light types (FDynamicLight::Update, docs/RENDERING.md "Light types"): 2 pulse, 7 subtle pulse,
# 3 blink (so fast it reads as noise), 4 flicker (a fresh random value each frame, dark below one half).
LIGHT_TYPE_CODE = """
float angle = T * Rate * 6.2831853 + Phase;
float tick = floor(T * 30.0) + Phase;
float random = frac(sin(tick * 12.9898) * 43758.5453);
if (Type > 6.5) return 0.9 + 0.09 * sin(angle);
if (Type > 3.5) return random < 0.5 ? 0.0 : random;
if (Type > 2.5) return step(0.5, random);
if (Type > 1.5) return 0.6 + 0.39 * sin(angle);
return 1.0;"""


def build_light_function():
    g = Graph(LIGHT_FUNCTION)
    material = g.material
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    time = g.node(unreal.MaterialExpressionTime)
    shape = g.custom(LIGHT_SHAPE_CODE, FLOAT3, UV=g.node(unreal.MaterialExpressionTextureCoordinate),
                     RectU=g.scalar("RectU", 0.0), RectV=g.scalar("RectV", 0.0), Round=g.scalar("Round", 0.0))
    cookie_uv = g.custom("return S.xy * float2(TileU, TileV);", FLOAT2, S=shape, TileU=g.scalar("TileU", 1.0), TileV=g.scalar("TileV", 1.0))
    frame_uv = g.custom(FLIPBOOK_UV_CODE, FLOAT2, UV=cookie_uv, T=time,
                        Cols=g.scalar("FlipColumns", 1.0), Rows=g.scalar("FlipRows", 1.0),
                        Fps=g.scalar("FlipRate", 0.0), Frames=g.scalar("FlipFrames", 1.0))
    cookie = g.texture("Cookie", "/Engine/EngineResources/WhiteSquareTexture", uvs=frame_uv)
    pulse = g.custom(LIGHT_TYPE_CODE, FLOAT1, T=time, Type=g.scalar("LightType", 1.0), Rate=g.scalar("Rate", 1.0), Phase=g.scalar("Phase", 0.0))
    # CookieGain lifts dark pictures (caustics are mostly black): a light function can only take light away.
    light = g.custom("float grey = dot(C, float3(0.3, 0.59, 0.11)); return saturate(lerp(1.0, grey * Gain, saturate(W)) * S.z) * P;", FLOAT1,
                     C=(cookie, "RGB"), W=g.scalar("CookieWeight", 0.0), Gain=g.scalar("CookieGain", 1.0), S=shape, P=pulse)
    g.output(light, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


# ---------------------------------------------------------------------------------------------------------------
# Projected pictures
# ---------------------------------------------------------------------------------------------------------------

# A decal's box has its picture across local Y and Z. The original lays u along the projector's Y and texture
# row 0 toward its +Z (docs/RENDERING.md, Projectors); FlipU and FlipV turn it where the two disagree.
PROJECTOR_UV_CODE = """
float2 uv = float2(lerp(UV.x, 1.0 - UV.x, saturate(FlipU)), lerp(UV.y, 1.0 - UV.y, saturate(FlipV)));
return uv * float2(TileU, TileV);"""


def build_projector():
    g = Graph(PROJECTOR)
    material = g.material
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    uv = g.custom(PROJECTOR_UV_CODE, FLOAT2, UV=g.node(unreal.MaterialExpressionTextureCoordinate),
                  FlipU=g.scalar("FlipU", 0.0), FlipV=g.scalar("FlipV", 0.0), TileU=g.scalar("TileU", 1.0), TileV=g.scalar("TileV", 1.0))
    frame_uv = g.custom(FLIPBOOK_UV_CODE, FLOAT2, UV=uv, T=g.node(unreal.MaterialExpressionTime),
                        Cols=g.scalar("FlipColumns", 1.0), Rows=g.scalar("FlipRows", 1.0),
                        Fps=g.scalar("FlipRate", 0.0), Frames=g.scalar("FlipFrames", 1.0))
    texture = g.texture("Texture", "/Engine/EngineResources/WhiteSquareTexture", uvs=frame_uv)
    # A picture with an alpha channel covers by its alpha (signs). One the original added to the frame (chalk
    # drawings, rain splashes) covers by its brightness and is drawn in its own colour at full strength.
    by_brightness = g.scalar("OpacityFromBrightness", 0.0)
    colour = g.custom("float peak = max(max(T.r, T.g), max(T.b, 1e-3)); return lerp(T, T / peak, saturate(L)) * Tint;", FLOAT3,
                      T=(texture, "RGB"), L=by_brightness, Tint=g.colour("Tint", (1, 1, 1, 1)))
    g.output(colour, unreal.MaterialProperty.MP_BASE_COLOR)
    opacity = g.custom("float peak = max(max(T.r, T.g), T.b); return saturate(lerp(A, peak * Gain, saturate(L)) * O);", FLOAT1,
                       T=(texture, "RGB"), A=(texture, "A"), L=by_brightness, Gain=g.scalar("BrightnessGain", 1.0), O=g.scalar("Opacity", 1.0))
    g.output(opacity, unreal.MaterialProperty.MP_OPACITY)
    g.output(g.scalar("Roughness", 0.7), unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(g.custom("return C * E;", FLOAT3, C=colour, E=g.scalar("Emissive", 0.0)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


# ---------------------------------------------------------------------------------------------------------------
# Glow cards
# ---------------------------------------------------------------------------------------------------------------

def build_glow_card():
    """Beam and flare cards: geometry the original's artists placed and drew additively to fake a shaft of light
    or a lamp's flare. The surface material draws them as glowing solids; here they add a soft light instead,
    shaped by the mesh's vertex colours (the artists' own fade along a beam, stored at half strength), fading
    where the card meets a wall, comes close to the eye or is seen edge-on."""
    g = Graph(GLOW_CARD)
    material = g.material
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    uv = g.custom("return UV + T * float2(U, V);", FLOAT2, UV=g.node(unreal.MaterialExpressionTextureCoordinate),
                  T=g.node(unreal.MaterialExpressionTime), U=g.scalar("PanU", 0.0), V=g.scalar("PanV", 0.0))
    texture = g.texture("BaseColor", "/Engine/EngineResources/WhiteSquareTexture", uvs=uv)
    soft = g.node(unreal.MaterialExpressionDepthFade, fade_distance_default=30.0)
    near = g.custom("return saturate((Depth - 20.0) / 120.0);", FLOAT1, Depth=g.node(unreal.MaterialExpressionPixelDepth))
    facing = g.custom("return saturate(abs(dot(normalize(N), normalize(C))) * 2.5);", FLOAT1,
                      N=g.node(unreal.MaterialExpressionVertexNormalWS), C=g.node(unreal.MaterialExpressionCameraVectorWS))
    glow = g.custom("return T * saturate(VC * 2.0) * B * S * N * lerp(1.0, F, saturate(E));", FLOAT3,
                    T=(texture, "RGB"), VC=g.node(unreal.MaterialExpressionVertexColor), B=g.scalar("Brightness", 0.2),
                    S=soft, N=near, F=facing, E=g.scalar("EdgeFade", 1.0))
    g.output(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


def build_all():
    build_particle_lit()
    build_particle_glow()
    build_corona()
    build_light_function()
    build_projector()
    build_glow_card()
