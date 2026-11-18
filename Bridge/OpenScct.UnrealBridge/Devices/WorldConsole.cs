using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using OpenScct.Core;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Pose;
using OpenScct.UnrealBridge.Pose.OutfitArms;

namespace OpenScct.UnrealBridge.Devices;

// Loose PS2 maps supply a skinned console GLB and clips. Use its actual key times and bind matrices
// so the same driver works whether the map was cooked in the editor or built at runtime.
internal sealed class WorldConsole
{
    private readonly WorldState _world;
    private readonly SComputerObjectiveTrigger _actor;
    private readonly int _owner;
    private readonly GlbCharacter _rig;
    private readonly BindMesh _mesh;
    private readonly List<int>[] _corners;
    private readonly int[] _pictures;
    private readonly string _folder;
    private readonly Dictionary<int, int> _skinPictures = [];
    private readonly Matrix4x4[] _global, _moves;
    private readonly Vector3[] _positions, _normals, _translation, _scale;
    private readonly Quaternion[] _rotation;
    private readonly bool[] _posed;
    private int _serial = -1, _finished = -1, _revision;
    private float _sampled = -1, _frame;
    private double _lastTime;
    private GlbClip? _clip;

    public static WorldConsole? Load(WorldState world, string render, string id, SComputerObjectiveTrigger actor)
    {
        string[] parts = actor.Mesh.Path!.Split('.', 2);
        if (parts.Length != 2 || Array.Exists(parts, s => s.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || s is "." or "..")) return null;
        string folder = Path.Combine(render, "chars", parts[0]);
        string file = Path.Combine(folder, parts[1] + ".glb");
        return File.Exists(file) ? new WorldConsole(world, actor, world.Intern(id), file, folder) : null;
    }

    private WorldConsole(WorldState world, SComputerObjectiveTrigger actor, int owner, string path, string folder)
    {
        (_world, _actor, _owner) = (world, actor, owner);
        _folder = folder;
        _rig = GlbCharacter.Load(path);
        _mesh = BindMesh.Load(path);
        _translation = new Vector3[_rig.BoneCount];
        _rotation = new Quaternion[_rig.BoneCount];
        _scale = new Vector3[_rig.BoneCount];
        _global = new Matrix4x4[_rig.BoneCount];
        _posed = new bool[_rig.BoneCount];
        _moves = new Matrix4x4[_mesh.Skeleton.Length];
        _positions = new Vector3[_mesh.Positions.Length];
        _normals = new Vector3[_positions.Length];
        _corners = new List<int>[_mesh.Materials.Length];
        _pictures = new int[_corners.Length];
        for (int i = 0; i < _corners.Length; i++)
        {
            _corners[i] = [];
            if (_mesh.Images[i] is { } uri)
            {
                string pic = Path.GetFullPath(Path.Combine(folder, uri));
                if (pic.StartsWith(Path.GetFullPath(folder) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                    _pictures[i] = world.Intern(pic);
            }
        }
        for (int t = 0; t < _mesh.TriangleCount; t++)
        {
            var section = _corners[_mesh.TriangleMaterials[t]];
            for (int c = 0; c < 3; c++) section.Add(_mesh.Corners[t * 3 + c]);
        }
    }

    private int SkinPicture(string? skin, int original)
    {
        int id = _world.Intern(skin);
        if (id == 0 || string.IsNullOrEmpty(skin)) return original;
        if (!_skinPictures.TryGetValue(id, out int result))
        {
            string[] names = skin.Split('.');
            string file = names[0] + "_" + names[^1] + ".png";
            string path = Path.Combine(_folder, "textures", file);
            result = file.IndexOfAny(Path.GetInvalidFileNameChars()) < 0 && File.Exists(path) ? _world.Intern(path) : 0;
            _skinPictures[id] = result;
        }
        return result != 0 ? result : original;
    }

    public void Draw(List<GeometryRow> rows, List<GeometryVertex> vertices, double now)
    {
        float dt = (float)Math.Max(0, now - _lastTime);
        _lastTime = now;
        if (_actor.bHidden || _actor.bDeleteMe) return;
        if (_serial != _actor.Ps2AnimationSerial)
        {
            _serial = _actor.Ps2AnimationSerial;
            _clip = _rig.Find(_actor.Ps2AnimationSequence.ToString());
            _frame = Math.Clamp(_actor.Ps2AnimationStartFrame, 0, 1);
            _sampled = -1;
            dt = 0;
        }
        float rate = _actor.Ps2AnimationRate * (_actor.Ps2AnimationBackward ? -1f : 1f);
        if (_clip is { Length: > 0 } clip)
        {
            _frame = Math.Clamp(_frame + dt * rate / clip.Length, 0, 1);
            if (_finished != _serial && (rate > 0 && _frame == 1 || rate < 0 && _frame == 0))
            {
                _finished = _serial;
                _actor.Ps2AnimationFinished(_serial);
            }
        }
        if (_sampled != _frame)
        {
            Pose();
            _sampled = _frame;
            _revision++;
        }
        for (int s = 0; s < _corners.Length; s++)
        {
            if (_corners[s].Count == 0) continue;
            int first = vertices.Count;
            foreach (int v in _corners[s])
            {
                var p = _positions[v]; var n = _normals[v]; var uv = _mesh.Uvs[v];
                vertices.Add(new(p.X, p.Y, p.Z, n.X, n.Y, n.Z, uv.X, uv.Y));
            }
            string? skin = s < _actor.Skins.Count ? _actor.Skins[s].Path : null;
            rows.Add(new GeometryRow { Kind = 4, Owner = _owner, Part = s, First = first, Count = vertices.Count - first,
                Material = _world.Intern(skin), Picture = SkinPicture(skin, _pictures[s]),
                Location = _actor.Location, Rotation = _actor.Rotation, Scale = _actor.DrawScale3D * _actor.DrawScale, Phase = _revision });
        }
    }

    private Matrix4x4 Global(int bone)
    {
        if (!_posed[bone])
        {
            var local = Matrix4x4.CreateScale(_scale[bone]) * Matrix4x4.CreateFromQuaternion(_rotation[bone]) * Matrix4x4.CreateTranslation(_translation[bone]);
            _global[bone] = _rig.Parents[bone] < 0 ? local : local * Global(_rig.Parents[bone]);
            _posed[bone] = true;
        }
        return _global[bone];
    }

    private void Pose()
    {
        _rig.RestPosition.CopyTo(_translation, 0);
        _rig.RestRotation.CopyTo(_rotation, 0);
        _rig.RestScale.CopyTo(_scale, 0);
        Array.Clear(_posed);
        if (_clip is { } clip)
        {
            double time = _frame * clip.Length;
            for (int i = 0; i < clip.Tracks.Length; i++)
            {
                var track = clip.Tracks[i];
                switch (track.Type)
                {
                    case GlbTrackType.Position: _translation[track.Bone] = clip.Position(i, time); break;
                    case GlbTrackType.Rotation: _rotation[track.Bone] = clip.Rotation(i, time); break;
                    case GlbTrackType.Scale: _scale[track.Bone] = clip.Scale(i, time); break;
                }
            }
        }
        for (int j = 0; j < _moves.Length; j++)
            _moves[j] = _mesh.Skeleton[j].InverseBind * Global(j);
        Basis3 basis = Basis3.FromScale(_rig.MeshScale) * RenderSpace.Rotation(_rig.RotOrigin);
        for (int i = 0; i < _positions.Length; i++)
        {
            Vector3 p = default, n = default;
            float total = 0;
            for (int w = i * 4; w < i * 4 + 4; w++)
            {
                float weight = _mesh.Weights[w];
                if (weight <= 0) continue;
                var move = _moves[_mesh.Joints[w]];
                p += Vector3.Transform(_mesh.Positions[i], move) * weight;
                n += Vector3.TransformNormal(_mesh.Normals[i], move) * weight;
                total += weight;
            }
            p = total > 0 ? p / total : _mesh.Positions[i];
            n = total > 0 ? n : _mesh.Normals[i];
            p = basis * ((_rig.SkeletonFrame.Basis * p) + _rig.SkeletonFrame.Origin) * 100;
            n = basis * (_rig.SkeletonFrame.Basis * n);
            if (n.LengthSquared() > 0) n = Vector3.Normalize(n);
            _positions[i] = new(p.X, p.Z, p.Y);
            _normals[i] = new(n.X, n.Z, n.Y);
        }
    }
}
