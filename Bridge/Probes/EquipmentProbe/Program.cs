using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.Core;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Equipment;
using OpenScct.UnrealBridge.Pose;

// A console check of the equipment feature: the catalog of weapon meshes, then a merc round and a spy round on a
// map with bots, with the weapon keys pressed on a timeline. Each second it prints what Unreal would read; every
// frame it checks the poses (unit rotations, the same frame read twice gives the same pose, no bone jumps) and
// counts what the reads allocate. A last short round goes through the exported function tables only, the way
// Unreal calls them.

string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 9;

BridgeSession.Configure(content);
Console.WriteLine($"struct sizes: BridgeEquipment {Unsafe.SizeOf<BridgeEquipment>()}, BridgeAttachment {Unsafe.SizeOf<BridgeAttachment>()}, " +
    $"BridgeFirstPersonPart {Unsafe.SizeOf<BridgeFirstPersonPart>()}, BridgeFirstPerson {Unsafe.SizeOf<BridgeFirstPerson>()}, " +
    $"BridgeEquipmentMesh {Unsafe.SizeOf<BridgeEquipmentMesh>()}, BridgeBone {Unsafe.SizeOf<BridgeBone>()}, EquipmentApi {Unsafe.SizeOf<EquipmentApi>()}");
if (!Probe.Table(content))
{
    return 1;
}

// The merc: torch, a burst, a reload, the laser (which puts the torch out), the scope.
var merc = new List<Step>
{
    new(1.0, "torch on", s => s.Command("GadTorcheSwitch", 0)),
    new(2.0, "fire held", s => { s.Command("Fire", 0); Probe.Hold(6, true); }),
    new(3.0, "fire released", _ => Probe.Hold(6, false)),
    new(3.4, "reload", _ => Probe.Hold(13, true)),
    new(3.6, "", _ => Probe.Hold(13, false)),
    new(5.6, "laser on", s => s.Command("GadLaserSwitch", 0)),
    new(6.6, "scope in", s => s.Command("SetScope", 1)),
    new(7.6, "scope out", s => s.Command("SetScope", 0)),
};
if (!Probe.Run(map, merc: true, seconds, merc))
{
    return 1;
}

// The spy: eye view, night vision, the pistol drawn with its laser, a shot, the laser off and on, the pistol put away.
var spy = new List<Step>
{
    new(0.6, "eye view, night vision", s => { s.Command("SetSpyFirstPerson", 1); s.Command("SwitchHeadset", 1); }),
    new(1.5, "draw (fire mode tapped)", _ => Probe.Hold(11, true)),
    new(1.6, "", _ => Probe.Hold(11, false)),
    new(3.5, "fire", s => { s.Command("Fire", 0); Probe.Hold(6, true); }),
    new(3.7, "", _ => Probe.Hold(6, false)),
    new(4.6, "laser off", s => s.Command("SwitchLaser", 0)),
    new(5.4, "laser on", s => s.Command("SwitchLaser", 0)),
    new(6.6, "holster (fire mode tapped)", _ => Probe.Hold(11, true)),
    new(6.7, "", _ => Probe.Hold(11, false)),
};
if (!Probe.Run(map, merc: false, seconds, spy))
{
    return 1;
}

return Probe.ThroughTables(content, map) ? 0 : 1;

internal sealed record Step(double At, string What, Action<BridgeSession> Do);

internal static unsafe class Probe
{
    private static readonly bool[] s_held = new bool[BridgeInput.ButtonCount];

    public static void Hold(int button, bool down) => s_held[button] = down;

    /// <summary>The exported table, called as Unreal calls it: the entries that work without a session.</summary>
    public static bool Table(string content)
    {
        EquipmentApi api = default;
        delegate* unmanaged<EquipmentApi*, int> getApi = &EquipmentExports.GetApi;
        int version = getApi(&api);
        var meshes = new BridgeEquipmentMesh[64];
        int count;
        fixed (BridgeEquipmentMesh* p = meshes)
        {
            count = api.GetCatalog(p, meshes.Length);
        }

        Console.WriteLine($"GetApi version {version} (table says {api.Version}); catalog through the table: {count} meshes");
        if (version != EquipmentExports.ApiVersion || count <= 0)
        {
            Console.WriteLine("FAILED: the table gave no catalog");
            return false;
        }

        // Every glb in the equipment packages against the catalog: a file the pose code cannot read is left out of it
        // without a word otherwise. Known: the first-person pistols of OpenScctSidearms animate nodes outside their
        // skin, which GlbCharacter refuses, so a merc holding a pistol has no view model until that is read.
        string chars = System.IO.Path.Combine(content, "shared", "chars");
        int unread = 0;
        foreach (string package in new[] { "SWeaponFirst", "SGoggle", "OpenScctSidearms", "OpenScctRifles" })
        {
            string folder = System.IO.Path.Combine(chars, package);
            if (!System.IO.Directory.Exists(folder))
            {
                continue;
            }

            foreach (string file in System.IO.Directory.GetFiles(folder, "*.glb"))
            {
                string asset = package + "." + System.IO.Path.GetFileNameWithoutExtension(file);
                if (EquipmentModel.Load(asset, System.IO.Path.Combine(content, "shared")) is null)
                {
                    unread++;
                    Console.WriteLine($"   NOT READ {asset}: {EquipmentModel.FailureOf(asset)}");
                }
            }
        }

        Console.WriteLine($"equipment mesh files the pose code could not read (not in the catalog, never drawn): {unread}");

        var info = new BridgeBoneInfo[128];
        char* text = stackalloc char[260];
        for (int i = 0; i < Math.Min(count, meshes.Length); i++)
        {
            BridgeEquipmentMesh m = meshes[i];
            api.GetName(m.Mesh, text, 260);
            string mesh = new(text);
            api.GetName(m.MeshPath, text, 260);
            string path = new(text);
            api.GetName(m.Class, text, 260);
            string cls = new(text);
            int bones;
            fixed (BridgeBoneInfo* b = info)
            {
                bones = api.GetMeshSkeleton(m.Mesh, b, info.Length);
            }

            BridgeBoneInfo first = info[0];
            string root = bones > 0 ? new string(first.Name) : "";
            Console.WriteLine($"   {mesh,-32} role {m.Role} class '{cls}' {path}: {m.BoneCount} bones (skeleton call {bones}, root '{root}'), {m.ClipCount} clips, " +
                $"FX {m.MuzzleBone} FX_DOUILLE {m.CasingBone} Laser {m.LaserBone}, rotOrigin yaw {m.RotOriginYaw:F0} scale ({m.ScaleX:F2},{m.ScaleY:F2},{m.ScaleZ:F2})");
            if (bones != m.BoneCount)
            {
                Console.WriteLine("FAILED: the skeleton call disagrees with the catalog");
                return false;
            }
        }

        return true;
    }

    /// <summary>
    /// A short merc round driven only through the exported function tables (the main one and the equipment one), as
    /// Unreal drives them: every per-frame entry is called through its pointer and checked against the others.
    /// </summary>
    public static bool ThroughTables(string content, string map)
    {
        BridgeApi bridge = default;
        EquipmentApi api = default;
        delegate* unmanaged<BridgeApi*, int, int> getBridge = &Exports.GetApiSized;
        delegate* unmanaged<EquipmentApi*, int> getApi = &EquipmentExports.GetApi;
        getBridge(&bridge, sizeof(BridgeApi));
        getApi(&api);
        Console.WriteLine();
        Console.WriteLine($"==== {map} as a merc through the exported tables only");
        var one = new BridgeEquipment[1];
        fixed (BridgeEquipment* p = one)
        {
            int before = api.GetEquipment(p, 1);
            BridgeFirstPerson none;
            int shownBefore = api.GetFirstPerson(&none);
            Console.WriteLine($"before a session: GetEquipment {before}, GetFirstPerson {shownBefore}");
            if (before != 0 || shownBefore != 0)
            {
                Console.WriteLine("FAILED: equipment without a session");
                return false;
            }
        }

        int started;
        fixed (char* c = content, m = map, mode = "Story")
        {
            started = bridge.Configure(c) != 0 ? bridge.StartLocal(m, mode, 1, 3) : 0;
        }

        if (started == 0)
        {
            Console.WriteLine("FAILED: the session did not start: " + LastError(bridge));
            return false;
        }

        var pawns = new BridgePawn[64];
        var equipment = new BridgeEquipment[64];
        var attachments = new BridgeAttachment[128];
        var bones = new BridgeBone[160];
        char* text = stackalloc char[260];
        int frames = 0, ticks = 0, shownFrames = 0, posedBones = 0, animated = 0, maxEquipped = 0, maxAttached = 0, shots = 0, torchFrames = 0;
        string weapon = "", viewMesh = "";
        bool torch = false, fired = false;
        var input = default(BridgeInput);
        var run = Stopwatch.StartNew();
        while (run.Elapsed.TotalSeconds < 4.0)
        {
            double t = run.Elapsed.TotalSeconds;
            if (!torch && t >= 1.0)
            {
                torch = true;
                fixed (char* name = "GadTorcheSwitch")
                {
                    bridge.Command(name, 0);
                }
            }

            if (!fired && t >= 1.5)
            {
                fired = true;
                fixed (char* name = "Fire")
                {
                    bridge.Command(name, 0);
                }
            }

            input.Buttons[6] = (byte)(t >= 1.5 && t < 2.5 ? 1 : 0);
            bridge.SetInput(&input);
            ticks += bridge.Update();
            if (bridge.GetState() != (int)BridgeState.Playing)
            {
                Console.WriteLine("FAILED: " + LastError(bridge));
                return false;
            }

            frames++;
            int pawnCount, equipped, attached, shown;
            BridgeFirstPerson view;
            fixed (BridgePawn* p = pawns)
            fixed (BridgeEquipment* e = equipment)
            fixed (BridgeAttachment* a = attachments)
            fixed (BridgeBone* b = bones)
            {
                pawnCount = bridge.GetPawns(p, pawns.Length);
                equipped = api.GetEquipment(e, equipment.Length);
                attached = api.GetAttachments(a, attachments.Length);
                shown = api.GetFirstPerson(&view);
                if (equipped != pawnCount)
                {
                    Console.WriteLine($"FAILED: {pawnCount} pawns but equipment for {equipped}");
                    return false;
                }

                for (int i = 0; i < attached; i++)
                {
                    if ((attachments[i].Flags & BridgeAttachmentFlags.Animated) != 0)
                    {
                        int n = api.GetAttachmentPose(attachments[i].ActorId, attachments[i].Slot, b, bones.Length);
                        if (n != attachments[i].BoneCount)
                        {
                            Console.WriteLine($"FAILED: GetAttachmentPose gave {n} bones, the attachment says {attachments[i].BoneCount}");
                            return false;
                        }

                        animated++;
                    }
                }

                if (shown != 0)
                {
                    int n = api.GetFirstPersonPose(0, b, bones.Length);
                    if (n != view.Part0.BoneCount || n == 0 || api.GetFirstPersonPose(view.PartCount, b, bones.Length) != 0)
                    {
                        Console.WriteLine($"FAILED: GetFirstPersonPose gave {n} bones, the part says {view.Part0.BoneCount}");
                        return false;
                    }

                    shownFrames++;
                    posedBones = n;
                    api.GetName(view.WeaponClass, text, 260);
                    weapon = new string(text);
                    api.GetName(view.Part0.MeshPath, text, 260);
                    viewMesh = new string(text);
                }
            }

            maxEquipped = Math.Max(maxEquipped, equipped);
            maxAttached = Math.Max(maxAttached, attached);
            for (int i = 0; i < equipped; i++)
            {
                if ((equipment[i].Flags & BridgeEquipmentFlags.FirstPerson) != 0)
                {
                    shots = equipment[i].ShotCount;
                    torchFrames += (equipment[i].Flags & BridgeEquipmentFlags.TorchOn) != 0 ? 1 : 0;
                }
            }

            Thread.Sleep(4);
        }

        bridge.Stop();
        int after;
        fixed (BridgeEquipment* p = one)
        {
            after = api.GetEquipment(p, 1);
        }

        Console.WriteLine($"done: {frames} frames, {ticks} ticks; at most {maxEquipped} pawns with equipment and {maxAttached} attachments; {animated} animated attachment poses read; " +
            $"first person shown in {shownFrames} frames ('{weapon}', {viewMesh}, {posedBones} bones); local shots {shots}; torch on in {torchFrames} frames; " +
            $"GetEquipment after Stop {after}");
        if (shownFrames == 0 || maxEquipped == 0 || maxAttached == 0 || shots == 0 || torchFrames == 0 || after != 0)
        {
            Console.WriteLine("FAILED: the tables did not show a merc with a torch who fired");
            return false;
        }

        return true;
    }

    private static string LastError(BridgeApi bridge)
    {
        char* text = stackalloc char[2048];
        bridge.LastError(text, 2048);
        return new string(text);
    }

    public static bool Run(string map, bool merc, double seconds, List<Step> steps)
    {
        Array.Clear(s_held);
        var load = Stopwatch.StartNew();
        using BridgeSession session = BridgeSession.StartLocal(map, "Story", merc, bots: 3);
        EquipmentFeature feature = EquipmentFeature.For(session);
        Console.WriteLine();
        Console.WriteLine($"==== {map} as a {(merc ? "merc" : "spy")}, loaded in {load.Elapsed.TotalSeconds:F1} s; shared content {feature.SharedRoot}");

        var pawns = new BridgePawn[64];
        var equipment = new BridgeEquipment[64];
        var attachments = new BridgeAttachment[128];
        var bones = new BridgeBone[160];
        var again = new BridgeBone[160];
        var lastPose = new BridgeBone[2][] { new BridgeBone[160], new BridgeBone[160] };
        var hadPose = new bool[2];
        var seenBones = new HashSet<(int Actor, int Slot, int Bone)>();
        float worstLength = 0f, worstStep = 0f, muzzleNear = float.MaxValue, muzzleFar = 0f;
        var firstPart = new BridgeBone[160];
        float gripGap = 0f;
        string worstAt = "";
        int frames = 0, shownFrames = 0, ticks = 0, next = 0, lastKind = -1, lastShots = 0, lastFlags = -1;
        int steadyFrames = 0, allocatingFrames = 0, eyeFrames = 0, eyeOnlyFrames = 0;
        long steadyBytes = 0;
        bool comparedBeam = false;
        double nextReport = 0, readMs = 0;
        var run = Stopwatch.StartNew();
        while (run.Elapsed.TotalSeconds < seconds)
        {
            double t = run.Elapsed.TotalSeconds;
            while (next < steps.Count && steps[next].At <= t)
            {
                if (steps[next].What.Length > 0)
                {
                    Console.WriteLine($"t={t:F2} >> {steps[next].What}");
                }

                steps[next++].Do(session);
            }

            var input = default(BridgeInput);
            for (int b = 0; b < BridgeInput.ButtonCount; b++)
            {
                input.Buttons[b] = (byte)(s_held[b] ? 1 : 0);
            }

            session.SetInput(in input);
            ticks += session.Update();
            if (session.Failure is { } failure)
            {
                Console.WriteLine("FAILED: " + failure);
                return false;
            }

            // Every frame, as Unreal would: the pawns, their equipment and attachments, the first-person view.
            frames++;
            long reading = Stopwatch.GetTimestamp();
            int pawnCount = session.GetPawns(pawns);
            for (int i = 0; i < pawnCount; i++)
            {
                // Unreal reads every pawn's pose each frame anyway; done first, what the pose code allocates (a body
                // clip read on its first play) stays out of the equipment's count below.
                session.GetPawnPose(pawns[i].ActorId, bones);
            }

            long allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
            int equipped = feature.GetEquipment(equipment);
            int attached = feature.GetAttachments(attachments);
            bool shown = feature.GetFirstPerson(out BridgeFirstPerson view);
            long allocated = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
            for (int part = 0; part < view.PartCount; part++)
            {
                allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
                int n = feature.GetFirstPersonPose(part, bones);
                allocated += GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
                if (feature.GetFirstPersonPose(part, again) != n || !bones.AsSpan(0, n).SequenceEqual(again.AsSpan(0, n)))
                {
                    Console.WriteLine("FAILED: a second read of the same frame gave another first-person pose");
                    return false;
                }

                BridgeFirstPersonPart p = part == 0 ? view.Part0 : view.Part1;
                if (n != p.BoneCount || n == 0)
                {
                    Console.WriteLine($"FAILED: first-person part {part} has {n} bones, its header says {p.BoneCount}");
                    return false;
                }

                for (int b = 0; b < n; b++)
                {
                    Quaternion q = Rotation(bones[b]);
                    worstLength = MathF.Max(worstLength, MathF.Abs(q.Length() - 1f));
                    if (hadPose[part] && lastKind == view.Kind)
                    {
                        float step = Angle(q, Rotation(lastPose[part][b]));
                        if (step > worstStep)
                        {
                            worstStep = step;
                            worstAt = $"t={t:F2} part {part} bone {b} '{BoneName(feature, p.Mesh, b)}'";
                        }
                    }

                    lastPose[part][b] = bones[b];
                }

                hadPose[part] = true;
                if (part == 0)
                {
                    bones.AsSpan(0, n).CopyTo(firstPart);
                }

                if (p.MuzzleBone >= 0)
                {
                    // The muzzle put together the way Unreal will: the part's place in the camera, then the pose
                    // down to the bone. It must land where the header says the muzzle is.
                    Vector3 muzzle = InCamera(feature, p, bones, p.MuzzleBone);
                    float off = Vector3.Distance(muzzle, new Vector3(p.Muzzle.Px, p.Muzzle.Py, p.Muzzle.Pz));
                    if (off > 1e-4f)
                    {
                        Console.WriteLine($"FAILED: the muzzle composed from the pose is {off * 1000f:F2} mm from the header's");
                        return false;
                    }

                    float distance = new Vector3(p.Muzzle.Px, p.Muzzle.Py, p.Muzzle.Pz).Length();
                    muzzleNear = MathF.Min(muzzleNear, distance);
                    muzzleFar = MathF.Max(muzzleFar, distance);
                }
            }

            if (view.Kind == BridgeFirstPersonKind.Spy)
            {
                // The pistol sits in the right hand of the fitted arms while gripped (the draw swings both together).
                Vector3 hand = InCamera(feature, view.Part0, firstPart, BoneIndex(feature, view.Part0.Mesh, "B R Hand"));
                gripGap = MathF.Max(gripGap, Vector3.Distance(hand, new Vector3(view.Part1.ModelToCamera.Px, view.Part1.ModelToCamera.Py, view.Part1.ModelToCamera.Pz)));
            }

            if (!shown)
            {
                hadPose[0] = hadPose[1] = false;
            }

            lastKind = view.Kind;
            shownFrames += shown ? 1 : 0;
            for (int i = 0; i < attached; i++)
            {
                BridgeAttachment a = attachments[i];
                worstLength = MathF.Max(worstLength, MathF.Abs(Rotation(a.ModelToBone).Length() - 1f));
                if ((a.Flags & BridgeAttachmentFlags.Animated) != 0)
                {
                    allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
                    int n = feature.GetAttachmentPose(a.ActorId, a.Slot, bones);
                    allocated += GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
                    if (n != a.BoneCount)
                    {
                        Console.WriteLine($"FAILED: attachment pose of #{a.ActorId} has {n} bones, the attachment says {a.BoneCount}");
                        return false;
                    }
                }

                // Every change of the bone a thing hangs on is worth a line (the draw, the holster, the other hand).
                if (seenBones.Add((a.ActorId, a.Slot, a.Bone)))
                {
                    Console.WriteLine($"t={t:F2} attach #{a.ActorId} slot {a.Slot}: {Describe(a)}");
                }
            }

            readMs += Stopwatch.GetElapsedTime(reading).TotalMilliseconds;

            // What the reads allocated. The first second loads models and gives names their ids; after it a frame's
            // reads should leave no garbage.
            if (t >= 1.0)
            {
                steadyFrames++;
                steadyBytes += allocated;
                allocatingFrames += allocated > 0 ? 1 : 0;
                if (allocated > 0)
                {
                    // Expected only where a view model or pistol clip plays for the first time (GlbClip reads its
                    // tracks on first use).
                    Console.WriteLine($"t={t:F2} the reads allocated {allocated} bytes");
                }
            }

            // A pawn seen from its own eyes must be flagged in every such frame, view model or not: count the frames
            // where the local pawn is flagged and the ones where a view model is drawn without the flag.
            for (int i = 0; i < equipped; i++)
            {
                if (IsLocal(pawns, pawnCount, equipment[i].ActorId))
                {
                    bool eyes = (equipment[i].Flags & BridgeEquipmentFlags.FirstPerson) != 0;
                    eyeFrames += eyes ? 1 : 0;
                    eyeOnlyFrames += eyes && !shown ? 1 : 0;
                    if (shown && !eyes)
                    {
                        Console.WriteLine("FAILED: a view model is drawn for a pawn not flagged FirstPerson");
                        return false;
                    }
                }
            }

            // The local pawn's shots and flag changes as they happen.
            for (int i = 0; i < equipped; i++)
            {
                BridgeEquipment e = equipment[i];
                if (!IsLocal(pawns, pawnCount, e.ActorId))
                {
                    continue;
                }

                if (!comparedBeam && (e.Flags & BridgeEquipmentFlags.LaserBeam) != 0 && session.Session.LocalPawn is SPAWNDEFENSE own)
                {
                    // The beam worked out for mercs the simulation places none for (from the drawn rifle's FX bone
                    // along the packed aim), run on the local merc, whose beam the simulation did place: the two
                    // must start near each other (the simulation's is pulled back 50 uu) and point the same way.
                    comparedBeam = true;
                    (FVector start, FVector end) = feature.DistantBeam(own);
                    var simStart = new Vector3(e.LaserStartX, e.LaserStartY, e.LaserStartZ);
                    var simEnd = new Vector3(e.LaserEndX, e.LaserEndY, e.LaserEndZ);
                    var ownStart = new Vector3(start.X, start.Y, start.Z);
                    var ownEnd = new Vector3(end.X, end.Y, end.Z);
                    float angle = MathF.Acos(Math.Clamp(Vector3.Dot(Vector3.Normalize(simEnd - simStart), Vector3.Normalize(ownEnd - ownStart)), -1f, 1f)) * (180f / MathF.PI);
                    Console.WriteLine($"t={t:F2} laser of #{e.ActorId}: simulation ({simStart.X:F0},{simStart.Y:F0},{simStart.Z:F0}) to ({simEnd.X:F0},{simEnd.Y:F0},{simEnd.Z:F0}); " +
                        $"distant-beam port ({ownStart.X:F0},{ownStart.Y:F0},{ownStart.Z:F0}) to ({ownEnd.X:F0},{ownEnd.Y:F0},{ownEnd.Z:F0}): " +
                        $"starts {Vector3.Distance(simStart, ownStart):F0} uu apart, ends {Vector3.Distance(simEnd, ownEnd):F0} uu apart, {angle:F1} deg between them; " +
                        $"pawn at ({own.Location.X:F0},{own.Location.Y:F0},{own.Location.Z:F0})");
                    if (Vector3.Distance(simStart, ownStart) > 120f || angle > 10f)
                    {
                        Console.WriteLine("FAILED: the distant beam does not follow the rifle");
                        return false;
                    }
                }

                if (e.ShotCount != lastShots || e.Flags != lastFlags)
                {
                    Console.WriteLine($"t={t:F2} local #{e.ActorId}: shots {e.ShotCount} clip {e.ClipAmmo}/{e.Ammo} loaded {e.WeaponLoaded} flags [{Flags(e.Flags)}]");
                    lastShots = e.ShotCount;
                    lastFlags = e.Flags;
                }
            }

            if (t >= nextReport)
            {
                nextReport += 1.5;
                Console.WriteLine($"t={t:F1} ticks={ticks} frames={frames} pawns={pawnCount} equipment={equipped} attachments={attached} reads {readMs / frames:F3} ms/frame");
                bool botShown = false;
                for (int i = 0; i < equipped; i++)
                {
                    BridgeEquipment e = equipment[i];
                    bool local = IsLocal(pawns, pawnCount, e.ActorId);
                    if (!local && botShown)
                    {
                        continue;
                    }

                    botShown |= !local;
                    Console.WriteLine($"   #{e.ActorId} {(local ? "local" : "bot")}: weapon '{Name(e.WeaponClass)}' loaded {e.WeaponLoaded} slot {e.ActiveWeaponSlot} shots {e.ShotCount} " +
                        $"clip {e.ClipAmmo}/{e.Ammo} rof {e.RateOfFire} gadget '{Name(e.SelectedGadgetClass)}' goggles {e.GoggleMode} cloak {e.Cloak:F2} scope {e.ScopeFov:F0} flags [{Flags(e.Flags)}]");
                    if (e.TorchBone >= 0)
                    {
                        Console.WriteLine($"      torch: bone {e.TorchBone} at ({e.TorchX:F0},{e.TorchY:F0},{e.TorchZ:F0}) pitch {e.TorchPitch:F1} yaw {e.TorchYaw:F1}, range {e.TorchRange:F0}, " +
                            $"spot {e.TorchSpotWidth:F0}x{e.TorchSpotHeight:F0} (half angle {e.TorchHalfAngle:F1}), colour ({e.TorchR:F3},{e.TorchG:F3},{e.TorchB:F3}) brightness {e.TorchBrightness:F0}");
                    }

                    if ((e.Flags & (BridgeEquipmentFlags.LaserBeam | BridgeEquipmentFlags.LaserDot)) != 0)
                    {
                        Console.WriteLine($"      laser: ({e.LaserStartX:F0},{e.LaserStartY:F0},{e.LaserStartZ:F0}) to ({e.LaserEndX:F0},{e.LaserEndY:F0},{e.LaserEndZ:F0})");
                    }

                    for (int a = 0; a < attached; a++)
                    {
                        if (attachments[a].ActorId == e.ActorId)
                        {
                            Console.WriteLine($"      slot {attachments[a].Slot}: {Describe(attachments[a])}");
                        }
                    }
                }

                if (shown)
                {
                    Console.WriteLine($"   first person kind {view.Kind} of #{view.ActorId}: weapon '{Name(view.WeaponClass)}' rof skin '{Name(view.RateOfFireSkin)}' shots {view.ShotCount} cloak {view.Cloak:F2}");
                    for (int part = 0; part < view.PartCount; part++)
                    {
                        BridgeFirstPersonPart p = part == 0 ? view.Part0 : view.Part1;
                        Console.WriteLine($"      part {part}: {Name(p.Mesh)} ({Name(p.MeshPath)}) {p.BoneCount} bones; skeleton in camera {Describe(p.ModelToCamera)}");
                        if (p.MuzzleBone >= 0)
                        {
                            Console.WriteLine($"         muzzle bone {p.MuzzleBone} in camera {Describe(p.Muzzle)}, barrel along {Axis(p.Muzzle)}");
                        }

                        if (p.LaserBone >= 0)
                        {
                            Console.WriteLine($"         laser bone {p.LaserBone} in camera {Describe(p.Laser)}");
                        }
                    }

                    if (view.Kind == BridgeFirstPersonKind.Merc)
                    {
                        ReadOnlySpan<ChannelPose> channels = feature.FirstPersonChannels;
                        for (int c = 0; c < channels.Length; c++)
                        {
                            if (channels[c].Clip is not null)
                            {
                                Console.WriteLine($"         channel {c}: '{channels[c].Sequence}' frame {channels[c].Frame:F3} clip frame {channels[c].ClipFrame:F3} alpha {channels[c].Alpha:F2}{(channels[c].Loop ? " loop" : "")}");
                            }
                        }
                    }
                    else
                    {
                        Console.WriteLine($"         support hand contact error {feature.SpySupportContactError * 1000f:F2} mm");
                    }
                }
                else
                {
                    Console.WriteLine("   first person: none");
                }
            }

            Thread.Sleep(4);
        }

        Console.WriteLine($"done: {frames} frames, first person shown in {shownFrames}; worst |q|-1 {worstLength:E1}; largest first-person bone turn between frames {worstStep:F2} deg ({worstAt}); " +
            $"muzzle {muzzleNear:F3} to {muzzleFar:F3} m from the eye");
        Console.WriteLine($"      reads after the first second: {steadyBytes} bytes allocated over {steadyFrames} frames ({allocatingFrames} frames allocated anything); " +
            $"local pawn flagged FirstPerson in {eyeFrames} frames, {eyeOnlyFrames} of them with no view model drawn");
        if (!merc)
        {
            Console.WriteLine($"      the spy's right hand and the pistol's grip are at most {gripGap * 1000f:F3} mm apart in the eye view");
            if (gripGap > 1e-3f)
            {
                Console.WriteLine("FAILED: the pistol is not in the fitted hand");
                return false;
            }
        }

        if (worstLength > 1e-3f)
        {
            Console.WriteLine("FAILED: a rotation is not unit length");
            return false;
        }

        if (shownFrames == 0)
        {
            Console.WriteLine("FAILED: the first-person view was never shown");
            return false;
        }

        return true;
    }

    private static bool IsLocal(BridgePawn[] pawns, int count, int actorId)
    {
        for (int i = 0; i < count; i++)
        {
            if (pawns[i].ActorId == actorId)
            {
                return (pawns[i].Flags & BridgePawnFlags.Local) != 0;
            }
        }

        return false;
    }

    private static string Name(int id) => EquipmentNames.Text(id);

    private static int BoneIndex(EquipmentFeature feature, int mesh, string bone) =>
        EquipmentModel.Load(Name(mesh), feature.SharedRoot)?.Character.FindBone(bone) ?? -1;

    /// <summary>Where a bone of a first-person part is in the camera's frame, from the part's header and its pose.</summary>
    private static Vector3 InCamera(EquipmentFeature feature, BridgeFirstPersonPart part, BridgeBone[] pose, int bone)
    {
        if (bone < 0 || EquipmentModel.Load(Name(part.Mesh), feature.SharedRoot) is not { } model)
        {
            return new Vector3(float.NaN);
        }

        Xform x = Local(pose[bone]);
        for (int parent = model.Character.Parents[bone]; parent >= 0; parent = model.Character.Parents[parent])
        {
            x = Local(pose[parent]) * x;
        }

        return (Local(part.ModelToCamera) * x).Origin;
    }

    private static Xform Local(BridgeBone b) =>
        Xform.FromTrs(new Vector3(b.Px, b.Py, b.Pz), new Quaternion(b.Qx, b.Qy, b.Qz, b.Qw), new Vector3(b.Sx, b.Sy, b.Sz));

    private static string BoneName(EquipmentFeature feature, int mesh, int bone) =>
        EquipmentModel.Load(Name(mesh), feature.SharedRoot) is { } model && bone < model.Character.BoneCount ? model.Character.BoneNames[bone] : "?";

    private static string Describe(BridgeAttachment a) =>
        $"{Name(a.Mesh)} ({Name(a.Class)}, {(a.MeshPath == 0 ? "NO FILE" : Name(a.MeshPath))}) on '{Name(a.Bone)}' (bone {a.BoneIndex}) " +
        $"rel ({a.RelX:F0},{a.RelY:F0},{a.RelZ:F0}) rot ({a.RelPitch:F0},{a.RelYaw:F0},{a.RelRoll:F0}) skin '{Name(a.Skin)}' flags {a.Flags}; to bone {Describe(a.ModelToBone)}";

    private static string Describe(BridgeBone b) =>
        $"p=({b.Px:F3},{b.Py:F3},{b.Pz:F3}) q=({b.Qx:F3},{b.Qy:F3},{b.Qz:F3},{b.Qw:F3}) s=({b.Sx:F2},{b.Sy:F2},{b.Sz:F2})";

    /// <summary>The bone's X axis in the frame it is given in.</summary>
    private static string Axis(BridgeBone b)
    {
        Vector3 x = Vector3.Transform(Vector3.UnitX, Rotation(b));
        return $"({x.X:F3},{x.Y:F3},{x.Z:F3})";
    }

    private static Quaternion Rotation(BridgeBone b) => new(b.Qx, b.Qy, b.Qz, b.Qw);

    private static float Angle(Quaternion a, Quaternion b) =>
        2f * MathF.Acos(MathF.Min(1f, MathF.Abs(Quaternion.Dot(a, b)))) * (180f / MathF.PI);

    private static string Flags(int flags)
    {
        string[] names = ["Firing", "Reloading", "LaserOn", "TorchOn", "InScope", "GogglesGlow", "Wet", "StickySkinned", "WeaponInHand",
            "FirstPerson", "TorchCorona", "TorchFollowsBone", "LaserBeam", "LaserDot", "GogglesDown", "Merc", "Binoculars", "TorchInCamera"];
        var on = new List<string>();
        for (int i = 0; i < names.Length; i++)
        {
            if ((flags & (1 << i)) != 0)
            {
                on.Add(names[i]);
            }
        }

        return string.Join(' ', on);
    }
}
