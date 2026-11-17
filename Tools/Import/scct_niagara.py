"""The project's Niagara systems, built from code so they can be rebuilt and reviewed as text.

Runs inside the editor's Python, without a window. Niagara has no classic Python authoring API; the systems are
built through the engine's Niagara toolset (Engine/Plugins/Experimental/Toolsets/NiagaraToolsets, enabled by the
AllToolsets plugin of this project): its tools take and return JSON and are called through the toolset registry.

  NS_ScctSprites  every sprite emitter of the original maps (steam, bubbles, sparks, drips, dust, flames). One
                  system for all of them: what differs per emitter (rate, lifetime, start box, velocity, size and
                  colour over life, spin, flipbook cells) is a user parameter the map importer sets on each placed
                  component, and the look (texture, blend) is the component's material.

The motion follows the original engine's own rules (OpenSCCT spec/native/emitters.md), which is why it is written
as a handful of expressions on the particle attributes instead of stock force modules: a pick is uniform inside a
box, velocity gains `Acceleration x dt` and loses `VelocityLoss x velocity x dt`, size and colour follow up to four
key frames over the particle's life.
"""
import json

import unreal

EFFECTS = "/Game/OpenSCCT/Effects"
SPRITES = EFFECTS + "/NS_ScctSprites"

TOOLSET = "NiagaraToolsets.NiagaraToolset_System"
MODULES = "/Niagara/Modules"
TEMPLATE_EMITTER = "/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"

FLOAT = "/Script/Niagara.NiagaraFloat"
VEC2 = "/Script/CoreUObject.Vector2f"
VEC3 = "/Script/CoreUObject.Vector3f"
VEC4 = "/Script/CoreUObject.Vector4f"
COLOUR = "/Script/CoreUObject.LinearColor"
POSITION = "/Script/Niagara.NiagaraPosition"
MATERIAL = "/Script/Engine.MaterialInterface"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def tool(name, arguments, toolset=TOOLSET):
    """Calls one tool of a toolset and returns its decoded result. A failed call stops the build."""
    result = unreal.ToolsetRegistry.execute_tool(toolset, name, json.dumps(arguments))
    if result.error:
        raise RuntimeError("%s failed: %s" % (name, result.error))
    return json.loads(result.value).get("returnValue") if result.value else None


def ref(path):
    """An asset or type reference as the tools take it."""
    return {"refPath": path if "." in path.rsplit("/", 1)[-1] or path.startswith("/Script/") else path + "." + path.rsplit("/", 1)[-1]}


def typed(type_path):
    return {"classStructOrEnum": {"refPath": type_path}}


def value_of(type_path, value):
    """A literal of a Niagara type in the tools' form. Vectors and colours are given as tuples."""
    if type_path == MATERIAL:
        return {"struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_VariableValue_Object"},
                "value": {"objectClass": {"refPath": MATERIAL}, "object": ref(value)}}
    if type_path == FLOAT:
        body = {"value": float(value)}
    elif type_path == COLOUR:
        body = dict(zip("rgba", (float(v) for v in value)))
    else:
        body = dict(zip("xyzw", (float(v) for v in value)))
    return {"struct": {"refPath": type_path}, "value": body}


# The user parameters of NS_ScctSprites: name, type, default, what it is. Times are seconds, lengths are world
# units, everything is in the emitter's own frame (the importer turns the frame, see scct_fx.py).
SPRITE_PARAMETERS = [
    ("Rate", FLOAT, 10.0, "particles per second while the emitter spawns"),
    ("SpawnWindow", FLOAT, 1.0e9, "seconds from the start of each loop in which particles spawn (a burst: short)"),
    ("LoopDuration", FLOAT, 1.0e9, "seconds after which a burst emitter starts again"),
    ("LifeMin", FLOAT, 4.0, "shortest particle life"),
    ("LifeMax", FLOAT, 4.0, "longest particle life"),
    ("BoxMin", VEC3, (0, 0, 0), "start box, low corner"),
    ("BoxMax", VEC3, (0, 0, 0), "start box, high corner"),
    ("VelMin", VEC3, (0, 0, 0), "start velocity, low corner"),
    ("VelMax", VEC3, (0, 0, 0), "start velocity, high corner"),
    ("Radial", VEC2, (0, 0), "speed added away from the emitter, least and most"),
    ("Accel", VEC3, (0, 0, 0), "constant acceleration"),
    ("VelLoss", VEC3, (0, 0, 0), "share of each velocity axis lost per second"),
    ("SizeMin", VEC2, (100, 100), "sprite half size at birth, least (x across, y up)"),
    ("SizeMax", VEC2, (100, 100), "sprite half size at birth, most"),
    ("SizeUniform", FLOAT, 1.0, "1: the sprite is square (y follows x)"),
    ("SizeTimes", VEC4, (0, 1, 1, 1), "size key frames: share of the life, rising"),
    ("SizeValues", VEC4, (1, 1, 1, 1), "size key frames: scale"),
    ("SizeRepeats", FLOAT, 1.0, "how many times the size curve plays over the life"),
    ("SizeDecay", FLOAT, 0.0, "1: size is 1 / (age + 1) instead of the curve (the original's default scale)"),
    ("ColorTimes", VEC4, (0, 1, 1, 1), "colour key frames: share of the life, rising"),
    ("Color0", COLOUR, (1, 1, 1, 1), "colour key 0"),
    ("Color1", COLOUR, (1, 1, 1, 1), "colour key 1"),
    ("Color2", COLOUR, (1, 1, 1, 1), "colour key 2"),
    ("Color3", COLOUR, (1, 1, 1, 1), "colour key 3"),
    ("ColorRepeats", FLOAT, 1.0, "how many times the colour curve plays over the life"),
    ("Tint", COLOUR, (1, 1, 1, 1), "multiplies every particle (the placed emitter's own colour and strength)"),
    ("FadeIn", FLOAT, 0.0, "seconds over which a particle fades in (0: none)"),
    ("FadeOut", FLOAT, 1.0e9, "age in seconds at which a particle starts to fade out"),
    ("Spin", VEC4, (0, 0, 0, 0), "start angle least and most, turns per second least and most (in turns)"),
    ("Cells", VEC2, (1, 0), "flipbook cells in the texture; 0 = each particle keeps a random cell, 1 = cells play over the life"),
]


def expression(text):
    return {"struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"}, "value": {"hlslExpression": text}}


def linked(name, type_path):
    return {"struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"},
            "value": {"linkedVariable": {"name": name, "type": typed(type_path)}}}


def choice(enum_path, entry):
    """A value of one of Niagara's own enum assets, whose entries have generated names."""
    name = enum_path.rsplit("/", 1)[-1]
    return {"struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"},
            "value": {"enum": {"refPath": "%s.%s" % (enum_path, name)}, "enumName": "%s::NewEnumerator%d" % (name, entry)}}


def curve4(times, values, x):
    """Four key frames read at x: straight lines between the keys, the end values held outside them."""
    def step(a, b):
        return "saturate((%s - %s.%s) / max(%s.%s - %s.%s, 1e-4))" % (x, times, a, times, b, times, a)
    v = values if isinstance(values, (list, tuple)) else ["%s.%s" % (values, c) for c in "xyzw"]
    return "lerp(%s, lerp(%s, lerp(%s, %s, %s), %s), %s)" % (v[0], v[1], v[2], v[3], step("z", "w"), step("y", "z"), step("x", "y"))


class Emitter:
    """One emitter of a system being built: adds modules and sets their inputs by name."""

    def __init__(self, system_path, name):
        self.system = ref(system_path)
        self.name = name
        tool("AddEmitter", {"system": self.system, "templateEmitter": ref(TEMPLATE_EMITTER), "emitterName": name})

    def item(self, script="", module="", inputs=(), renderer=-1):
        return {"system": self.system, "emitterName": self.name, "scriptName": script, "moduleName": module,
                "rendererIndex": renderer, "inputNameStack": list(inputs)}

    def add_module(self, script, asset):
        return tool("AddModule", {"moduleLocationRef": self.item(script), "moduleAsset": ref(asset)})["moduleName"]

    def set_input(self, script, module, name, data):
        tool("SetStackInputData", {"stackInputRef": self.item(script, module, [name]), "inputData": data})

    def assign(self, script, assignments):
        """A "set parameters" module: each (attribute, type, expression) is computed and stored, in order."""
        entries = [{"variable": {"name": name, "type": typed(type_path)},
                    "defaultValue": value_of(type_path, 0.0 if type_path == FLOAT else (0, 0, 0, 0))}
                   for name, type_path, _ in assignments]
        module = tool("AddSetParametersModule", {"moduleLocationRef": self.item(script), "parameters": entries})["moduleName"]
        for name, _, text in assignments:
            self.set_input(script, module, name, expression(text))
        return module

    def properties(self, **values):
        tool("SetEmitterData", {"emitter": self.item(), "emitterData": {"propertyValues": json.dumps(values)}})

    def renderer(self, material_parameter, **values):
        """Sets properties of the emitter's renderer, and binds its material to a user parameter so every placed
        component can bring its own (a component's ordinary material slots do not reach Niagara renderers)."""
        item = self.item(renderer=0)
        current = json.loads(tool("GetRendererData", {"rendererRef": item})["propertyValues"])
        binding = current["MaterialUserParamBinding"]
        binding["parameter"]["name"] = "User." + material_parameter
        values["MaterialUserParamBinding"] = binding
        tool("SetRendererData", {"renderer": item, "rendererData": {"propertyValues": json.dumps(values)}})


def new_system(path, parameters):
    folder, name = path.rsplit("/", 1)
    if editor_assets.does_asset_exist(path):
        editor_assets.delete_asset(path)
    asset_tools.create_asset(name, folder, unreal.NiagaraSystem, unreal.NiagaraSystemFactoryNew())
    tool("AddUserVariables", {"system": ref(path), "variablesToAdd": [
        {"name": "User." + name, "type": typed(type_path), "defaultValue": value_of(type_path, default), "description": text}
        for name, type_path, default, text in parameters]})


def finish_system(path):
    """Saves (which waits for the compile); a script that does not compile stops the build with its messages."""
    editor_assets.save_asset(path)
    result = unreal.ToolsetRegistry.execute_tool(TOOLSET, "GetSystemCompileState", json.dumps({"system": ref(path)}))
    state = json.loads(result.value).get("returnValue", {}) if result.value else {}
    problems = ["%s %s: %s" % (script.get("emitterName"), script.get("scriptName"), event.get("message"))
                for script in state.get("scripts", []) for event in script.get("compileEvents", [])
                if event.get("severity") in ("Error", "Warning")]
    log("%s compile: %s" % (path, state.get("aggregateStatus", result.error or "not reported")))
    for problem in problems[:20]:
        log("  " + problem)
    if state.get("bHasErrors"):
        raise RuntimeError("Niagara system does not compile: " + path)
    log("made " + path)


# How the sprites of an emitter lie: User.Kind picks one of the system's three emitters (the others spawn nothing).
SPRITE_KINDS = [
    ("Facing", {}),                                                               # 0: toward the camera
    ("Aligned", {"Alignment": "VelocityAligned", "FacingMode": "FaceCamera"}),    # 1: along their motion (sparks, drips)
    ("Flat", {"Alignment": "Unaligned", "FacingMode": "CustomFacingVector"}),     # 2: in a fixed plane (rings on water)
]


def build_sprites(material_path):
    """NS_ScctSprites: three emitters with the same motion that differ only in how the sprite lies. A placed
    emitter uses one of them."""
    parameters = SPRITE_PARAMETERS + [
        ("Kind", FLOAT, 0.0, "0: sprites face the camera, 1: lie along their velocity, 2: lie flat across User.Normal"),
        ("Normal", VEC3, (0, 0, 1), "what flat sprites face"),
        ("Material", MATERIAL, material_path, "the sprites' material: an instance of M_ScctParticleLit or M_ScctParticleGlow"),
    ]
    new_system(SPRITES, parameters)
    for index, (name, facing) in enumerate(SPRITE_KINDS):
        emitter = Emitter(SPRITES, name)
        # In the component's own frame: the importer bakes the emitter's rotation rules into the placed actor.
        emitter.properties(bLocalSpace=True, bDeterminism=False)

        # ---- when particles spawn: at User.Rate inside the spawn window of each loop
        state = "EmitterUpdateScript"
        # The emitter runs its own endless loop (the template follows the system's): a burst emitter starts again
        # every User.LoopDuration.
        emitter.set_input(state, "EmitterState", "Life Cycle Mode", choice("/Niagara/Enums/ENiagaraEmitterLifeCycleMode", 1))
        emitter.set_input(state, "EmitterState", "Loop Behavior", choice("/Niagara/Enums/ENiagara_EmitterStateOptions", 0))
        emitter.set_input(state, "EmitterState", "Loop Duration", linked("User.LoopDuration", FLOAT))
        spawn = emitter.add_module(state, MODULES + "/Emitter/SpawnRate")
        emitter.set_input(state, spawn, "SpawnRate", expression(
            "User.Rate * step(abs(User.Kind - %d.0), 0.5) * step(Emitter.LoopedAge, User.SpawnWindow)" % index))

        # ---- how a particle starts (spec/native/emitters.md rule 6)
        born = "ParticleSpawnScript"
        emitter.assign(born, [
            ("Particles.Lifetime", FLOAT, "lerp(User.LifeMin, User.LifeMax, rand(1.0))"),
            ("Particles.ScctStart", VEC3, "lerp(User.BoxMin, User.BoxMax, float3(rand(1.0), rand(1.0), rand(1.0)))"),
            ("Particles.ScctSeed", VEC4, "float4(rand(1.0), rand(1.0), rand(1.0), rand(1.0))"),
        ])
        emitter.assign(born, [
            ("Particles.Position", POSITION, "Particles.Position + Particles.ScctStart"),
            ("Particles.Velocity", VEC3,
             "lerp(User.VelMin, User.VelMax, float3(rand(1.0), rand(1.0), rand(1.0)))"
             " + normalize(Particles.ScctStart + float3(0.0, 0.0, 1e-4)) * lerp(User.Radial.x, User.Radial.y, rand(1.0))"),
            # Half sizes at birth; y follows x for square sprites.
            ("Particles.ScctSize", VEC2,
             "lerp(User.SizeMin, User.SizeMax, lerp(float2(Particles.ScctSeed.x, Particles.ScctSeed.y),"
             " float2(Particles.ScctSeed.x, Particles.ScctSeed.x), saturate(User.SizeUniform)))"),
            # x: start angle, y: turns per second (either way round), both in turns.
            ("Particles.ScctSpin", VEC2,
             "float2(lerp(User.Spin.x, User.Spin.y, Particles.ScctSeed.z),"
             " lerp(User.Spin.z, User.Spin.w, Particles.ScctSeed.w) * sign(rand(1.0) - 0.5))"),
        ])

        # ---- how it lives (rules 9 and 11 to 15). ParticleState ages it and ends its life.
        alive = "ParticleUpdateScript"
        emitter.assign(alive, [
            ("Particles.Velocity", VEC3,
             "(Particles.Velocity + User.Accel * Engine.DeltaTime) * saturate(1.0 - User.VelLoss * Engine.DeltaTime)"),
        ])
        size_x = "frac(min(Particles.NormalizedAge, 0.9999) * max(User.SizeRepeats, 1.0))"
        colour_x = "frac(min(Particles.NormalizedAge, 0.9999) * max(User.ColorRepeats, 1.0))"
        age = "(Particles.NormalizedAge * Particles.Lifetime)"
        fade = ("saturate(%s / max(User.FadeIn, 1e-4)) * saturate(1.0 - (%s - User.FadeOut) / max(Particles.Lifetime - User.FadeOut, 1e-4))"
                % (age, age))
        cell = ("lerp(floor(Particles.ScctSeed.y * User.Cells.x), floor(min(Particles.NormalizedAge, 0.9999) * User.Cells.x),"
                " saturate(User.Cells.y))")
        emitter.assign(alive, [
            ("Particles.Position", POSITION, "Particles.Position + Particles.Velocity * Engine.DeltaTime"),
            ("Particles.SpriteSize", VEC2,
             "Particles.ScctSize * 2.0 * lerp(%s, 1.0 / (%s + 1.0), saturate(User.SizeDecay))"
             % (curve4("User.SizeTimes", "User.SizeValues", size_x), age)),
            ("Particles.Color", COLOUR,
             "%s * User.Tint * float4(1.0, 1.0, 1.0, %s)"
             % (curve4("User.ColorTimes", ["User.Color0", "User.Color1", "User.Color2", "User.Color3"], colour_x), fade)),
            ("Particles.SpriteRotation", FLOAT, "(Particles.ScctSpin.x + Particles.ScctSpin.y * %s) * 360.0" % age),
            # The material reads the flipbook cell from here (x) and a per-particle random number (y).
            ("Particles.DynamicMaterialParameter", VEC4, "float4(%s, Particles.ScctSeed.w, 0.0, 0.0)" % cell),
        ])

        if name == "Flat":
            emitter.assign(alive, [("Particles.SpriteFacing", VEC3, "User.Normal")])
        look = {"Material": ref(material_path), "SortMode": "ViewDepth", "bCastShadows": False}
        look.update(facing)
        emitter.renderer("Material", **look)
    finish_system(SPRITES)


def build_all():
    import scct_effects
    build_sprites(scct_effects.PARTICLE_GLOW)
