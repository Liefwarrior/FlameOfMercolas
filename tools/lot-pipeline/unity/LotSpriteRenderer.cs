// LotSpriteRenderer.cs -- Granadad LOT pipeline: batch export of Lord of Trojia
// rigs, props and clips from the Unity Editor.
//
// Four jobs, one tool:
//   viewmodel  : first-person arms (+ optional weapon) posed from Malbers humanoid
//                clips, one PNG per (state, frame), alpha background.
//   portraits  : head-and-shoulders of Synty character prefabs, N views each.
//   gltf       : STATIC prefabs (walls, props, weapons) -> .gltf + .bin + the pack's
//                atlas PNG beside them (glTFast, com.unity.cloud.gltfast, already in LOT).
//   glb        : ANIMATED humanoids -> one .glb per rig carrying its whole clip set
//                baked onto the rig's own bones (UnityGLTF, org.khronos.unitygltf;
//                render-lot.ps1 adds it to LOT's Packages/manifest.json for the run).
//
// This file is NOT part of Lord of Trojia. render-lot.ps1 copies it (with its
// asmdef) into <LOT>/Assets/Editor/LotPipeline/ for the duration of one batch run
// and removes it afterwards. It can also be dropped there by hand for interactive
// use via the Tools/Granadad menu.
//
// Entry points:
//   batch : -executeMethod Granadad.LotPipeline.LotSpriteRenderer.RenderFromCommandLine
//           -lotMode viewmodel|portraits|gltf|glb -lotJob <job.json> -lotOut <dir>
//   menu  : Tools/Granadad/Render Viewmodel Job... / Render Portrait Job... /
//           Export Static glTF Job... / Export Rig GLB Job...
//
// Requirements: Unity 6000.x with URP (LOT is 6000.3.6f1 / URP 17.3). Batch mode
// must NOT pass -nographics -- Camera.Render and the texture blits need a GPU context.
// The LOT_GLTFAST / LOT_UNITYGLTF defines come from Granadad.LotPipeline.Editor.asmdef
// (versionDefines on the two packages); without the asmdef the 3D modes fail loudly.
//
// Output contract (consumed by the Granadad side of the pipeline):
//   <out>/<jobName>/<state>_<frameIndex>.png       (viewmodel)
//   <out>/<jobName>/<prefabName>_<viewName>.png    (portraits)
//   <out>/<jobName>/frames.json                    one record per PNG
//   <out>/<subdir>/<pack>/<prefab>.gltf + .bin     (gltf; subdir default "static")
//   <out>/<subdir>/<pack>/<atlas>.png              one copy per pack directory
//   <out>/<subdir>/<rig>.glb                       (glb; subdir default "characters")
//   <out>/<subdir>/manifest.json                   one record per exported asset:
//                                                  source prefab, file, tris, bounds,
//                                                  materials/textures, clip order
//   <out>/<subdir>/contact-sheet.png (+ .json)     row-major thumbnails, names in the json
//
// Coordinate note for the C++ side: glTF is right-handed, Unity left-handed; both
// exporters mirror X. 1 Unity unit = 1 m = 1 tile. Bone names survive (Root, Hips,
// Spine_01.., Hand_R ...) so sockets can be found by name. Clip order in a .glb is
// the job's clip order (index = the C++ enum), and the names are the job's names.
#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using UnityEditor;
using UnityEditor.Animations;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
#if LOT_GLTFAST
using GLTFast;
using GLTFast.Export;
#endif
#if LOT_UNITYGLTF
using UnityGLTF;
#endif
using Object = UnityEngine.Object;

namespace Granadad.LotPipeline {

    // ---------------------------------------------------------------- job schema
    // JsonUtility: [Serializable] classes, arrays not dictionaries, float[3] for vectors.

    [Serializable] public class CameraSpec {
        public int width = 512;
        public int height = 384;
        public float fov = 65f;
        public float nearClip = 0.02f;
        public float farClip = 6f;
        // viewmodel: offset from the head bone in character-local space (x right, y up, z forward)
        public float[] eyeOffset = { 0f, 0.04f, 0.06f };
        public float pitchDeg = 8f;      // positive = look down
        // portraits: camera sits this far in front of the head bone, looking at it
        public float distance = 0.9f;
        public float[] targetOffset = { 0f, 0.02f, 0f };
        public int msaa = 1;             // 1 = off. Keep 1 for a clean alpha edge.
    }

    [Serializable] public class LightSpec {
        public float[] euler = { 50f, -30f, 0f };
        public float intensity = 1.15f;
        public float ambient = 0.45f;
        public float[] color = { 1f, 0.97f, 0.9f };
    }

    [Serializable] public class WeaponSpec {
        public string prefab = "";                       // empty = bare hands
        public string bone = "RightHand";                // HumanBodyBones name
        public string boneNameFallback = "Hand_R";       // hierarchy search if the avatar map fails
        public float[] localPos = { 0f, 0f, 0f };
        public float[] localEuler = { 0f, 0f, 0f };
        public float localScale = 1f;
    }

    [Serializable] public class StateSpec {
        public string name;          // idle, charge, swing, hard_swing, block, cast, hit ...
        public string clip;          // asset path of the FBX/.anim holding the clip
        public string clipName;      // clip name inside the FBX (empty = first non-preview clip)
        public float[] times;        // normalised 0..1 of clip length, one PNG each
    }

    [Serializable] public class ViewSpec {
        public string name;          // front, left34, right34, down ...
        public float yawDeg;         // character root yaw (positive = character turns to its left)
        public float pitchDeg;       // camera pitch (positive = camera looks down at the face)
    }

    [Serializable] public class ViewmodelJob {
        public string jobName = "viewmodel-fists";
        public string rigPrefab;                  // e.g. Synty preset prefab
        public string avatarModel;                // FBX whose Humanoid Avatar drives the rig (empty = use rig's own)
        public string[] keepRenderers;            // name substrings; renderers not matching are hidden
        public WeaponSpec weapon = new WeaponSpec();
        public CameraSpec camera = new CameraSpec();
        public LightSpec light = new LightSpec();
        public StateSpec[] states;
        public bool mirrorX = false;              // flip the frame horizontally (left-handed viewmodel)
    }

    [Serializable] public class PortraitJob {
        public string jobName = "portraits";
        public string[] prefabs;                  // explicit prefab asset paths, and/or:
        public string prefabFolder = "";          // folder to enumerate prefabs from
        public string prefabFilter = "";          // substring filter on prefab name
        public int limit = 0;                     // 0 = all
        public string avatarModel;
        public string[] keepRenderers;
        public string poseClip = "";              // optional clip to pose the body (else prefab bind pose)
        public string poseClipName = "";
        public float poseTime = 0f;
        public CameraSpec camera = new CameraSpec { width = 128, height = 160, fov = 28f, distance = 0.85f };
        public LightSpec light = new LightSpec();
        public ViewSpec[] views;
    }

    // gltf: one set per output subfolder. names resolve as <folder>/<name>.prefab;
    // prefabs are explicit asset paths. Every prefab is named -- no globbing, so the
    // job file IS the list of what ships.
    [Serializable] public class PrefabSet {
        public string pack;                       // output subfolder under <subdir>/ (one atlas copy per pack folder)
        public string role = "world";             // world | prop | weapon -- manifest only, the C++ placer reads it
        public string folder = "";                // asset folder the names resolve against
        public string[] names;                    // prefab names without .prefab
        public string[] prefabs;                  // and/or explicit asset paths
        public IEnumerable<string> Resolve() {
            foreach (var n in names ?? new string[0]) yield return (folder.TrimEnd('/') + "/" + n + ".prefab");
            foreach (var p in prefabs ?? new string[0]) yield return p;
        }
    }

    [Serializable] public class StaticExportJob {
        public string jobName = "docks-3d-static";
        public string subdir = "static";
        public PrefabSet[] sets;
        public int sheetCell = 128;               // contact-sheet thumbnail size (0 = no sheet)
        public int sheetColumns = 12;
        public CameraSpec camera = new CameraSpec();
        public LightSpec light = new LightSpec();
    }

    [Serializable] public class ClipSpec {
        public string name;          // the glTF animation name AND its index in job order
        public string clip;          // asset path of the FBX/.anim holding the clip
        public string clipName;      // clip name inside the FBX (empty = first non-preview clip)
    }

    // glb: one rig = one .glb. clips picks from the job's clip library by name, in the
    // order given (empty = the whole library in library order); clipOverrides replaces
    // the library outright (the viewmodel arms use their own state-named set).
    [Serializable] public class RigSpec {
        public string name;                       // output file stem: <subdir>/<name>.glb
        public string role = "actor";             // actor | viewmodel -- manifest only
        public string rigPrefab;
        public string avatarModel;
        public string[] keepRenderers;
        public WeaponSpec weapon = new WeaponSpec();
        public string[] clips;
        public ClipSpec[] clipOverrides;
    }

    [Serializable] public class RigExportJob {
        public string jobName = "docks-3d-rigs";
        public string subdir = "characters";
        public ClipSpec[] clips;
        public RigSpec[] rigs;
        public int sheetCell = 256;
        public int sheetColumns = 6;
        public CameraSpec camera = new CameraSpec();
        public LightSpec light = new LightSpec();
    }

    // ---------------------------------------------------------------- entry points

    public static class LotSpriteRenderer {

        [MenuItem("Tools/Granadad/Render Viewmodel Job...")]
        public static void MenuViewmodel() {
            var job = EditorUtility.OpenFilePanel("Viewmodel job JSON", "", "json");
            if (string.IsNullOrEmpty(job)) return;
            var outDir = EditorUtility.OpenFolderPanel("Output directory", "", "");
            if (string.IsNullOrEmpty(outDir)) return;
            RenderViewmodel(job, outDir);
        }

        [MenuItem("Tools/Granadad/Render Portrait Job...")]
        public static void MenuPortraits() {
            var job = EditorUtility.OpenFilePanel("Portrait job JSON", "", "json");
            if (string.IsNullOrEmpty(job)) return;
            var outDir = EditorUtility.OpenFolderPanel("Output directory", "", "");
            if (string.IsNullOrEmpty(outDir)) return;
            RenderPortraits(job, outDir);
        }

        [MenuItem("Tools/Granadad/Export Static glTF Job...")]
        public static void MenuStatic() {
            var job = EditorUtility.OpenFilePanel("Static glTF job JSON", "", "json");
            if (string.IsNullOrEmpty(job)) return;
            var outDir = EditorUtility.OpenFolderPanel("Output root (content/art/lot-3d)", "", "");
            if (string.IsNullOrEmpty(outDir)) return;
            ExportStatic(job, outDir);
        }

        [MenuItem("Tools/Granadad/Export Rig GLB Job...")]
        public static void MenuRigs() {
            var job = EditorUtility.OpenFilePanel("Rig GLB job JSON", "", "json");
            if (string.IsNullOrEmpty(job)) return;
            var outDir = EditorUtility.OpenFolderPanel("Output root (content/art/lot-3d)", "", "");
            if (string.IsNullOrEmpty(outDir)) return;
            ExportRigs(job, outDir);
        }

        // Command-line entry. Exit code 0 on success, 2 on any failure (render-lot.ps1
        // reads it). Exits the editor itself, batch or windowed: the runner launches a
        // windowed editor when the licence has no headless entitlement, and -quit alone
        // would still route through the editor's own shutdown prompts.
        public static void RenderFromCommandLine() {
            int code = 0;
            try {
                var mode = Arg("-lotMode", "viewmodel");
                var job = Arg("-lotJob", null);
                var outDir = Arg("-lotOut", null);
                if (job == null || outDir == null)
                    throw new ArgumentException("need -lotJob <job.json> and -lotOut <dir>");
                int n;
                switch (mode) {
                    case "portraits": n = RenderPortraits(job, outDir); break;
                    case "gltf":      n = ExportStatic(job, outDir); break;
                    case "glb":       n = ExportRigs(job, outDir); break;
                    default:          n = RenderViewmodel(job, outDir); break;
                }
                Debug.Log($"[LotSpriteRenderer] wrote {n} outputs ({mode}) to {outDir}");
            } catch (Exception e) {
                Debug.LogError("[LotSpriteRenderer] FAILED: " + e);
                code = 2;
            }
            EditorApplication.Exit(code);
        }

        static string Arg(string key, string fallback) {
            var a = Environment.GetCommandLineArgs();
            for (int i = 0; i < a.Length - 1; i++) if (a[i] == key) return a[i + 1];
            return fallback;
        }

        // ---------------------------------------------------------------- viewmodel

        public static int RenderViewmodel(string jobPath, string outRoot) {
            var job = JsonUtility.FromJson<ViewmodelJob>(File.ReadAllText(jobPath));
            if (job.states == null || job.states.Length == 0) throw new ArgumentException("job.states is empty");
            var outDir = Path.Combine(outRoot, job.jobName);
            Directory.CreateDirectory(outDir);

            var scene = new RenderScene(job.camera, job.light);
            var manifest = new ManifestWriter(job.jobName, jobPath);
            int written = 0;
            try {
                var rig = scene.SpawnRig(job.rigPrefab, job.avatarModel, job.keepRenderers);
                var weaponInfo = "";
                if (!string.IsNullOrEmpty(job.weapon.prefab)) {
                    scene.AttachWeapon(rig, job.weapon);
                    weaponInfo = job.weapon.prefab;
                }

                AnimationMode.StartAnimationMode();
                foreach (var st in job.states) {
                    var clip = LoadClip(st.clip, st.clipName);
                    if (clip == null) throw new FileNotFoundException($"clip '{st.clipName}' in {st.clip}");
                    var times = (st.times == null || st.times.Length == 0) ? new[] { 0f } : st.times;
                    for (int i = 0; i < times.Length; i++) {
                        float t = Mathf.Clamp01(times[i]) * clip.length;
                        AnimationMode.BeginSampling();
                        AnimationMode.SampleAnimationClip(rig.root, clip, t);
                        AnimationMode.EndSampling();
                        scene.PlaceViewmodelCamera(rig, job.camera);
                        var file = $"{st.name}_{i}.png";
                        scene.Capture(Path.Combine(outDir, file), job.camera, job.mirrorX);
                        manifest.Add(file, st.name, i, st.clip, clip.name, t, clip.length,
                                     job.camera.width, job.camera.height, job.rigPrefab, weaponInfo);
                        written++;
                    }
                }
            } finally {
                if (AnimationMode.InAnimationMode()) AnimationMode.StopAnimationMode();
                scene.Dispose();
            }
            manifest.Write(Path.Combine(outDir, "frames.json"));
            return written;
        }

        // ---------------------------------------------------------------- portraits

        public static int RenderPortraits(string jobPath, string outRoot) {
            var job = JsonUtility.FromJson<PortraitJob>(File.ReadAllText(jobPath));
            var prefabs = new List<string>(job.prefabs ?? new string[0]);
            if (!string.IsNullOrEmpty(job.prefabFolder)) {
                var guids = AssetDatabase.FindAssets("t:Prefab", new[] { job.prefabFolder });
                var paths = guids.Select(AssetDatabase.GUIDToAssetPath)
                                 .Where(p => string.IsNullOrEmpty(job.prefabFilter) ||
                                             Path.GetFileName(p).IndexOf(job.prefabFilter, StringComparison.OrdinalIgnoreCase) >= 0)
                                 .OrderBy(p => p, new NaturalComparer()).ToList();
                prefabs.AddRange(paths);
            }
            if (job.limit > 0 && prefabs.Count > job.limit) prefabs = prefabs.Take(job.limit).ToList();
            if (prefabs.Count == 0) throw new ArgumentException("no prefabs to render");
            var views = (job.views == null || job.views.Length == 0)
                ? new[] { new ViewSpec { name = "front" } } : job.views;

            var outDir = Path.Combine(outRoot, job.jobName);
            Directory.CreateDirectory(outDir);
            var scene = new RenderScene(job.camera, job.light);
            var manifest = new ManifestWriter(job.jobName, jobPath);
            int written = 0;
            try {
                AnimationClip pose = string.IsNullOrEmpty(job.poseClip) ? null : LoadClip(job.poseClip, job.poseClipName);
                if (pose != null) AnimationMode.StartAnimationMode();
                foreach (var prefabPath in prefabs) {
                    var rig = scene.SpawnRig(prefabPath, job.avatarModel, job.keepRenderers);
                    if (pose != null) {
                        AnimationMode.BeginSampling();
                        AnimationMode.SampleAnimationClip(rig.root, pose, Mathf.Clamp01(job.poseTime) * pose.length);
                        AnimationMode.EndSampling();
                    }
                    var baseName = Path.GetFileNameWithoutExtension(prefabPath);
                    foreach (var v in views) {
                        rig.root.transform.rotation = Quaternion.Euler(0f, v.yawDeg, 0f);
                        scene.PlacePortraitCamera(rig, job.camera, v.pitchDeg);
                        var file = $"{baseName}_{v.name}.png";
                        scene.Capture(Path.Combine(outDir, file), job.camera, false);
                        manifest.Add(file, v.name, 0, job.poseClip, pose != null ? pose.name : "", job.poseTime, 0f,
                                     job.camera.width, job.camera.height, prefabPath, "");
                        written++;
                    }
                    Object.DestroyImmediate(rig.root);
                }
            } finally {
                if (AnimationMode.InAnimationMode()) AnimationMode.StopAnimationMode();
                scene.Dispose();
            }
            manifest.Write(Path.Combine(outDir, "frames.json"));
            return written;
        }

        // ---------------------------------------------------------------- gltf (static, glTFast)

        // Thumbnail view directions (Unity space): props from front-right-above, rigs from the front.
        static readonly Vector3 kPropViewDir = new Vector3(1f, 0.8f, -1.2f);
        static readonly Vector3 kRigViewDir = new Vector3(0.25f, 0.25f, -1f);

        public static int ExportStatic(string jobPath, string outRoot) {
#if !LOT_GLTFAST
            throw new InvalidOperationException("gltf mode needs glTFast (com.unity.cloud.gltfast) and the LOT_GLTFAST define from Granadad.LotPipeline.Editor.asmdef -- run via render-lot.ps1");
#else
            var job = JsonUtility.FromJson<StaticExportJob>(File.ReadAllText(jobPath));
            if (job.sets == null || job.sets.Length == 0) throw new ArgumentException("job.sets is empty");
            var entries = new List<KeyValuePair<PrefabSet, string>>();
            foreach (var set in job.sets) {
                if (string.IsNullOrEmpty(set.pack)) throw new ArgumentException("a set has no pack name");
                foreach (var p in set.Resolve()) entries.Add(new KeyValuePair<PrefabSet, string>(set, p));
            }
            var missing = entries.Where(e => AssetDatabase.LoadAssetAtPath<GameObject>(e.Value) == null).Select(e => e.Value).ToList();
            if (missing.Count > 0) throw new FileNotFoundException("prefabs not found:\n  " + string.Join("\n  ", missing));
            var dupes = entries.GroupBy(e => Path.GetFileNameWithoutExtension(e.Value) + "@" + e.Key.pack).Where(g => g.Count() > 1).Select(g => g.Key).ToList();
            if (dupes.Count > 0) throw new ArgumentException("duplicate prefab names in one pack folder: " + string.Join(", ", dupes));

            var outDir = Path.Combine(outRoot, job.subdir);
            Directory.CreateDirectory(outDir);
            var scene = new RenderScene(job.camera, job.light);
            var sheet = job.sheetCell > 0 ? new ContactSheet(job.sheetCell, job.sheetColumns, entries.Count) : null;
            var manifest = new ExportManifest(job.jobName, jobPath, "gltf");
            var flattener = new MaterialFlattener();
            int written = 0;
            try {
                foreach (var entry in entries) {
                    var set = entry.Key; var path = entry.Value;
                    var name = Path.GetFileNameWithoutExtension(path);
                    var go = SpawnStatic(path, flattener);
                    var dir = Path.Combine(outDir, set.pack);
                    Directory.CreateDirectory(dir);
                    var file = Path.Combine(dir, name + ".gltf");

                    var settings = new ExportSettings {
                        Format = GltfFormat.Json,                          // .gltf + .bin: the atlas is written ONCE per pack dir, not embedded per prefab
                        ImageDestination = ImageDestination.SeparateFile,
                        FileConflictResolution = FileConflictResolution.Overwrite,
                        ComponentMask = ComponentType.Mesh,                // no lights, no cameras
                        Deterministic = true,
                    };
                    var export = new GameObjectExport(settings, new GameObjectExportSettings { OnlyActiveInHierarchy = true, DisabledComponents = false });
                    if (!export.AddScene(new[] { go }, name)) throw new InvalidOperationException("glTFast refused " + path);
                    bool ok = SyncRunner.Run(() => export.SaveToFileAndDispose(file));
                    if (!ok || !File.Exists(file)) throw new IOException("glTFast export failed for " + path + " (see log)");

                    var stats = MeshStats.Of(go);
                    var mats = MaterialFlattener.Used(go).ToList();
                    if (sheet != null) sheet.Add(scene.RenderThumb(stats.bounds, job.sheetCell, kPropViewDir), name);
                    manifest.AddStatic(set, path, name, file, outDir, stats, mats);
                    Object.DestroyImmediate(go);
                    written++;
                }
            } finally {
                scene.Dispose();
                flattener.Dispose();
            }
            if (sheet != null) sheet.Write(Path.Combine(outDir, "contact-sheet.png"));
            manifest.Write(Path.Combine(outDir, "manifest.json"));
            return written;
#endif
        }

        // A static prefab as the export sees it: unpacked, no behaviours, LOD0 only,
        // every material flattened to URP/Lit so the exporter finds the atlas.
        static GameObject SpawnStatic(string prefabPath, MaterialFlattener flattener) {
            var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(prefabPath);
            if (prefab == null) throw new FileNotFoundException("prefab: " + prefabPath);
            var go = (GameObject)PrefabUtility.InstantiatePrefab(prefab);
            PrefabUtility.UnpackPrefabInstance(go, PrefabUnpackMode.Completely, InteractionMode.AutomatedAction);
            go.name = Path.GetFileNameWithoutExtension(prefabPath);
            go.transform.position = Vector3.zero;
            go.transform.rotation = Quaternion.identity;
            go.transform.localScale = Vector3.one;
            RenderScene.StripBehaviours(go);
            foreach (var lod in go.GetComponentsInChildren<LODGroup>(true)) {
                var lods = lod.GetLODs();
                for (int i = 1; i < lods.Length; i++)
                    foreach (var r in lods[i].renderers) if (r != null) r.gameObject.SetActive(false);
                Object.DestroyImmediate(lod);
            }
            foreach (var t in go.GetComponentsInChildren<Transform>(true))
                if (System.Text.RegularExpressions.Regex.IsMatch(t.name, "_LOD[1-9]$")) t.gameObject.SetActive(false);
            flattener.Flatten(go);
            return go;
        }

        // ---------------------------------------------------------------- glb (animated rigs, UnityGLTF)

        public static int ExportRigs(string jobPath, string outRoot) {
#if !LOT_UNITYGLTF
            throw new InvalidOperationException("glb mode needs UnityGLTF (org.khronos.unitygltf) and the LOT_UNITYGLTF define from Granadad.LotPipeline.Editor.asmdef -- render-lot.ps1 -Mode glb adds the package to LOT's Packages/manifest.json for the run");
#else
            var job = JsonUtility.FromJson<RigExportJob>(File.ReadAllText(jobPath));
            if (job.rigs == null || job.rigs.Length == 0) throw new ArgumentException("job.rigs is empty");
            var outDir = Path.Combine(outRoot, job.subdir);
            Directory.CreateDirectory(outDir);
            var scene = new RenderScene(job.camera, job.light);
            var sheet = job.sheetCell > 0 ? new ContactSheet(job.sheetCell, job.sheetColumns, job.rigs.Length) : null;
            var manifest = new ExportManifest(job.jobName, jobPath, "glb");
            var flattener = new MaterialFlattener();
            int written = 0;
            try {
                foreach (var spec in job.rigs) {
                    if (string.IsNullOrEmpty(spec.name)) throw new ArgumentException("a rig has no name");
                    var clipSpecs = SelectClips(job, spec);
                    if (clipSpecs.Count == 0) throw new ArgumentException("rig " + spec.name + " has no clips");

                    var rig = scene.SpawnRig(spec.rigPrefab, spec.avatarModel, spec.keepRenderers);
                    if (rig.animator.avatar == null || !rig.animator.avatar.isValid || !rig.animator.avatar.isHuman)
                        throw new InvalidOperationException("rig " + spec.name + ": no valid Humanoid avatar -- humanoid clips cannot be baked (set avatarModel)");
                    var weaponInfo = "";
                    if (spec.weapon != null && !string.IsNullOrEmpty(spec.weapon.prefab)) {
                        scene.AttachWeapon(rig, spec.weapon);
                        weaponInfo = spec.weapon.prefab;
                    }
                    flattener.Flatten(rig.root);
                    var skin = SkinCombiner.Combine(rig.root);

                    // One in-memory controller, one state per clip, in job order; the exporter
                    // puts the default state first and takes the rest in controller order.
                    var clips = new List<AnimationClip>();
                    var clipRecords = new List<ExportManifest.ClipRecord>();
                    foreach (var cs in clipSpecs) {
                        var src = LoadClip(cs.clip, cs.clipName);
                        if (src == null) throw new FileNotFoundException($"clip '{cs.clipName}' in {cs.clip}");
                        var copy = Object.Instantiate(src);
                        copy.name = cs.name;
                        clips.Add(copy);
                        clipRecords.Add(new ExportManifest.ClipRecord {
                            name = cs.name, index = clipRecords.Count, source = cs.clip, clipName = src.name,
                            length = src.length, frames = Mathf.Max(1, Mathf.CeilToInt(src.length * 30f)) + 1, loop = src.isLooping,
                        });
                    }
                    var controller = BuildController(spec.name, clips);
                    rig.animator.runtimeAnimatorController = controller;
                    rig.animator.enabled = true;

                    var settings = ScriptableObject.CreateInstance<GLTFSettings>();
                    settings.ExportAnimations = true;
                    settings.ExportDisabledGameObjects = false;
                    settings.ExportNames = true;
                    settings.ExportFullPath = false;
                    settings.BakeAnimationSpeed = false;
                    settings.UniqueAnimationNames = false;
                    settings.TryExportTexturesFromDisk = false;
                    settings.ExportVertexColors = false;
                    settings.BlendShapeExportProperties = GLTFSettings.BlendShapeExportPropertyFlags.None;
                    settings.UseMainCameraVisibility = false;
                    // Plain node TRS animation only: raylib reads that, not KHR_animation_pointer.
                    foreach (var p in settings.ExportPlugins)
                        if (p != null && (p.GetType().Name.Contains("AnimationPointer") || p.GetType().Name.Contains("Interactivity") || p.GetType().Name.Contains("VisualScripting")))
                            p.Enabled = false;
                    var ctx = new ExportContext(settings);
                    var exporter = new GLTFSceneExporter(rig.root.transform, ctx);
                    exporter.SaveGLB(outDir, spec.name);
                    var file = Path.Combine(outDir, spec.name + ".glb");
                    if (!File.Exists(file)) throw new IOException("UnityGLTF wrote no file for " + spec.name);

                    // Proof frame: the first clip's first frame, framed on the skinned body.
                    Bounds bounds;
                    AnimationMode.StartAnimationMode();
                    try {
                        AnimationMode.BeginSampling();
                        AnimationMode.SampleAnimationClip(rig.root, clips[0], 0f);
                        AnimationMode.EndSampling();
                        var baked = new Mesh();
                        skin.body.BakeMesh(baked);
                        bounds = baked.bounds;
                        Object.DestroyImmediate(baked);
                        if (sheet != null) sheet.Add(scene.RenderThumb(bounds, job.sheetCell, kRigViewDir), spec.name);
                    } finally {
                        AnimationMode.StopAnimationMode();
                    }
                    manifest.AddRig(spec, file, outDir, skin, clipRecords, weaponInfo, bounds);

                    Object.DestroyImmediate(rig.root);
                    Object.DestroyImmediate(controller);
                    foreach (var c in clips) Object.DestroyImmediate(c);
                    Object.DestroyImmediate(settings);
                    written++;
                }
            } finally {
                if (AnimationMode.InAnimationMode()) AnimationMode.StopAnimationMode();
                scene.Dispose();
                flattener.Dispose();
            }
            if (sheet != null) sheet.Write(Path.Combine(outDir, "contact-sheet.png"));
            manifest.Write(Path.Combine(outDir, "manifest.json"));
            return written;
#endif
        }

        static List<ClipSpec> SelectClips(RigExportJob job, RigSpec spec) {
            if (spec.clipOverrides != null && spec.clipOverrides.Length > 0) return spec.clipOverrides.ToList();
            var library = (job.clips ?? new ClipSpec[0]).ToList();
            if (spec.clips == null || spec.clips.Length == 0) return library;
            var picked = new List<ClipSpec>();
            foreach (var n in spec.clips) {
                var c = library.FirstOrDefault(x => x.name == n);
                if (c == null) throw new ArgumentException($"rig {spec.name}: clip '{n}' is not in job.clips");
                picked.Add(c);
            }
            return picked;
        }

        static AnimatorController BuildController(string name, IList<AnimationClip> clips) {
            var controller = new AnimatorController { name = name + "_clips" };
            controller.AddLayer("Base");
            var sm = controller.layers[0].stateMachine;
            AnimatorState first = null;
            foreach (var clip in clips) {
                var st = sm.AddState(clip.name);
                st.motion = clip;
                if (first == null) first = st;
            }
            if (first != null) sm.defaultState = first;
            return controller;
        }

        // ---------------------------------------------------------------- helpers

        public static AnimationClip LoadClip(string assetPath, string clipName) {
            if (assetPath.EndsWith(".anim", StringComparison.OrdinalIgnoreCase))
                return AssetDatabase.LoadAssetAtPath<AnimationClip>(assetPath);
            var clips = AssetDatabase.LoadAllAssetsAtPath(assetPath).OfType<AnimationClip>()
                                     .Where(c => !c.name.StartsWith("__preview__")).ToList();
            if (clips.Count == 0) return null;
            if (string.IsNullOrEmpty(clipName)) return clips[0];
            return clips.FirstOrDefault(c => c.name == clipName)
                ?? clips.FirstOrDefault(c => c.name.Equals(clipName, StringComparison.OrdinalIgnoreCase));
        }

        public static Avatar LoadAvatar(string modelPath) {
            if (string.IsNullOrEmpty(modelPath)) return null;
            return AssetDatabase.LoadAllAssetsAtPath(modelPath).OfType<Avatar>().FirstOrDefault();
        }

        class NaturalComparer : IComparer<string> {
            public int Compare(string a, string b) {
                // Preset_2 before Preset_10: compare trailing integers when the stems match.
                var ra = Split(a); var rb = Split(b);
                int c = string.Compare(ra.Item1, rb.Item1, StringComparison.OrdinalIgnoreCase);
                return c != 0 ? c : ra.Item2.CompareTo(rb.Item2);
            }
            static Tuple<string, int> Split(string s) {
                var stem = Path.GetFileNameWithoutExtension(s);
                int i = stem.Length; while (i > 0 && char.IsDigit(stem[i - 1])) i--;
                int n = i < stem.Length ? int.Parse(stem.Substring(i)) : -1;
                return Tuple.Create(Path.GetDirectoryName(s) + "/" + stem.Substring(0, i), n);
            }
        }
    }

    // ---------------------------------------------------------------- the rig

    public class Rig {
        public GameObject root;
        public Animator animator;
        public Transform head;
        public Transform Bone(HumanBodyBones b, string fallbackName) {
            var t = animator != null && animator.avatar != null && animator.avatar.isValid ? animator.GetBoneTransform(b) : null;
            if (t == null && !string.IsNullOrEmpty(fallbackName)) t = FindDeep(root.transform, fallbackName);
            return t;
        }
        public static Transform FindDeep(Transform t, string name) {
            if (t.name == name) return t;
            for (int i = 0; i < t.childCount; i++) { var r = FindDeep(t.GetChild(i), name); if (r != null) return r; }
            return null;
        }
    }

    // ---------------------------------------------------------------- the scene

    public class RenderScene : IDisposable {
        readonly Camera cam;
        readonly Light sun;
        readonly string previousScenePath;   // what the editor had open; restored on Dispose
        RenderTexture rt;
        Texture2D readback;

        public RenderScene(CameraSpec cs, LightSpec ls) {
            previousScenePath = UnityEngine.SceneManagement.SceneManager.GetActiveScene().path;
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            RenderSettings.ambientMode = AmbientMode.Flat;
            RenderSettings.ambientLight = new Color(ls.color[0], ls.color[1], ls.color[2]) * ls.ambient;
            RenderSettings.fog = false;

            sun = new GameObject("LotSun").AddComponent<Light>();
            sun.type = LightType.Directional;
            sun.intensity = ls.intensity;
            sun.color = new Color(ls.color[0], ls.color[1], ls.color[2]);
            sun.shadows = LightShadows.None;
            sun.transform.rotation = Quaternion.Euler(ls.euler[0], ls.euler[1], ls.euler[2]);

            cam = new GameObject("LotCamera").AddComponent<Camera>();
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = new Color(0f, 0f, 0f, 0f);
            cam.allowHDR = false;
            cam.allowMSAA = cs.msaa > 1;
            cam.nearClipPlane = cs.nearClip;
            cam.farClipPlane = cs.farClip;
            cam.fieldOfView = cs.fov;
            var urp = cam.GetUniversalAdditionalCameraData();
            if (urp != null) {
                urp.renderType = CameraRenderType.Base;
                urp.renderPostProcessing = false;
                urp.antialiasing = AntialiasingMode.None;
                urp.renderShadows = false;
                urp.dithering = false;
            }
            rt = new RenderTexture(cs.width, cs.height, 24, RenderTextureFormat.ARGB32) { antiAliasing = Mathf.Max(1, cs.msaa) };
            rt.Create();
            readback = new Texture2D(cs.width, cs.height, TextureFormat.RGBA32, false);
        }

        public Rig SpawnRig(string prefabPath, string avatarModel, string[] keepRenderers) {
            var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(prefabPath);
            if (prefab == null) throw new FileNotFoundException("rig prefab: " + prefabPath);
            var go = (GameObject)PrefabUtility.InstantiatePrefab(prefab);
            PrefabUtility.UnpackPrefabInstance(go, PrefabUnpackMode.Completely, InteractionMode.AutomatedAction);
            go.transform.position = Vector3.zero;
            go.transform.rotation = Quaternion.identity;

            // Strip every behaviour the pack shipped on the prefab (randomizers, controllers, AI).
            StripBehaviours(go);

            // Synty preset prefabs already carry an Animator with a valid Humanoid avatar
            // (Chr_FantasyHero_Preset_*: Root/Hips/.../Hand_R, 719 SkinnedMeshRenderers).
            // avatarModel is the FALLBACK for rigs that arrive without one (a raw FBX,
            // a stripped prefab); never override a valid avatar with a guessed one.
            var animator = go.GetComponent<Animator>() ?? go.AddComponent<Animator>();
            if (animator.avatar == null || !animator.avatar.isValid) {
                var avatar = LotSpriteRenderer.LoadAvatar(avatarModel);
                if (avatar != null) animator.avatar = avatar;
                else Debug.LogWarning("[LotSpriteRenderer] no valid avatar on " + prefabPath + " and none in " + avatarModel + " -- humanoid clips will not retarget");
            }
            animator.runtimeAnimatorController = null;
            animator.applyRootMotion = false;
            animator.cullingMode = AnimatorCullingMode.AlwaysAnimate;

            // Keep only renderers whose GameObject was active in the prefab AND whose
            // name contains one of keepRenderers. Everything else is hidden, never destroyed
            // (bones may live under hidden parts).
            if (keepRenderers != null && keepRenderers.Length > 0) {
                foreach (var r in go.GetComponentsInChildren<Renderer>(true)) {
                    bool keep = r.gameObject.activeInHierarchy &&
                                keepRenderers.Any(k => r.gameObject.name.IndexOf(k, StringComparison.OrdinalIgnoreCase) >= 0);
                    r.enabled = keep;
                }
            }
            var rig = new Rig { root = go, animator = animator };
            rig.head = rig.Bone(HumanBodyBones.Head, "Head");
            if (rig.head == null) throw new InvalidOperationException("no Head bone on " + prefabPath + " (avatar invalid and no transform named 'Head')");
            return rig;
        }

        public void AttachWeapon(Rig rig, WeaponSpec w) {
            var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(w.prefab);
            if (prefab == null) throw new FileNotFoundException("weapon prefab: " + w.prefab);
            var bone = (HumanBodyBones)Enum.Parse(typeof(HumanBodyBones), w.bone);
            var hand = rig.Bone(bone, w.boneNameFallback);
            if (hand == null) throw new InvalidOperationException("no bone " + w.bone + "/" + w.boneNameFallback);
            var wep = (GameObject)PrefabUtility.InstantiatePrefab(prefab);
            PrefabUtility.UnpackPrefabInstance(wep, PrefabUnpackMode.Completely, InteractionMode.AutomatedAction);
            StripBehaviours(wep);
            wep.transform.SetParent(hand, false);
            wep.transform.localPosition = new Vector3(w.localPos[0], w.localPos[1], w.localPos[2]);
            wep.transform.localRotation = Quaternion.Euler(w.localEuler[0], w.localEuler[1], w.localEuler[2]);
            wep.transform.localScale = Vector3.one * w.localScale;
        }

        // Two passes: a script guarded by [RequireComponent] refuses to go before its
        // dependant does, so anything that survives pass one falls in pass two.
        public static void StripBehaviours(GameObject go) {
            for (int pass = 0; pass < 2; pass++)
                foreach (var mb in go.GetComponentsInChildren<MonoBehaviour>(true)) {
                    if (mb == null) continue;
                    try { Object.DestroyImmediate(mb); } catch (Exception) { /* retry next pass */ }
                }
        }

        // First-person: camera sits at the head bone, offset in CHARACTER space so the
        // head animation cannot shake the frame; looks along the character's forward
        // with a fixed downward pitch. The arms move; the camera does not.
        public void PlaceViewmodelCamera(Rig rig, CameraSpec cs) {
            var root = rig.root.transform;
            var eye = rig.head.position + root.right * cs.eyeOffset[0] + root.up * cs.eyeOffset[1] + root.forward * cs.eyeOffset[2];
            cam.transform.position = eye;
            cam.transform.rotation = Quaternion.LookRotation(root.forward, Vector3.up) * Quaternion.Euler(cs.pitchDeg, 0f, 0f);
            cam.fieldOfView = cs.fov;
        }

        // Portrait: camera in front of the face at cs.distance, looking at head + targetOffset.
        public void PlacePortraitCamera(Rig rig, CameraSpec cs, float pitchDeg) {
            var root = rig.root.transform;
            var target = rig.head.position + root.right * cs.targetOffset[0] + Vector3.up * cs.targetOffset[1] + root.forward * cs.targetOffset[2];
            var back = Quaternion.AngleAxis(-pitchDeg, root.right) * root.forward;   // lift the camera to look down
            cam.transform.position = target + back * cs.distance;
            cam.transform.rotation = Quaternion.LookRotation(target - cam.transform.position, Vector3.up);
            cam.fieldOfView = cs.fov;
        }

        public void Capture(string path, CameraSpec cs, bool mirrorX) {
            cam.targetTexture = rt;
            cam.Render();
            var prev = RenderTexture.active;
            RenderTexture.active = rt;
            readback.ReadPixels(new Rect(0, 0, cs.width, cs.height), 0, 0);
            readback.Apply();
            RenderTexture.active = prev;
            cam.targetTexture = null;
            if (mirrorX) {
                var px = readback.GetPixels32();
                int w = cs.width, h = cs.height;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w / 2; x++) {
                        int a = y * w + x, b = y * w + (w - 1 - x);
                        var tmp = px[a]; px[a] = px[b]; px[b] = tmp;
                    }
                readback.SetPixels32(px);
                readback.Apply();
            }
            File.WriteAllBytes(path, readback.EncodeToPNG());
        }

        // Contact-sheet thumbnail: a square frame of `bounds` (world space, the object is
        // at the origin) from direction `dir`, opaque dark background. Returns a fresh
        // Texture2D the caller owns.
        public Texture2D RenderThumb(Bounds bounds, int size, Vector3 dir) {
            const float fov = 30f;
            float radius = Mathf.Max(bounds.extents.magnitude, 0.05f);
            float dist = radius / Mathf.Sin(fov * 0.5f * Mathf.Deg2Rad) * 1.05f;
            var prevPos = cam.transform.position; var prevRot = cam.transform.rotation;
            float prevFov = cam.fieldOfView, prevNear = cam.nearClipPlane, prevFar = cam.farClipPlane;
            var prevBg = cam.backgroundColor;
            cam.fieldOfView = fov;
            cam.transform.position = bounds.center + dir.normalized * dist;
            cam.transform.rotation = Quaternion.LookRotation(bounds.center - cam.transform.position, Vector3.up);
            cam.nearClipPlane = Mathf.Max(0.01f, dist - radius * 2f);
            cam.farClipPlane = dist + radius * 2f;
            cam.backgroundColor = new Color(0.11f, 0.11f, 0.13f, 1f);
            var thumbRt = RenderTexture.GetTemporary(size, size, 24, RenderTextureFormat.ARGB32);
            var tex = new Texture2D(size, size, TextureFormat.RGBA32, false);
            cam.targetTexture = thumbRt;
            cam.Render();
            var prev = RenderTexture.active;
            RenderTexture.active = thumbRt;
            tex.ReadPixels(new Rect(0, 0, size, size), 0, 0);
            tex.Apply();
            RenderTexture.active = prev;
            cam.targetTexture = null;
            RenderTexture.ReleaseTemporary(thumbRt);
            cam.transform.position = prevPos; cam.transform.rotation = prevRot;
            cam.fieldOfView = prevFov; cam.nearClipPlane = prevNear; cam.farClipPlane = prevFar;
            cam.backgroundColor = prevBg;
            return tex;
        }

        public void Dispose() {
            if (cam != null) { cam.targetTexture = null; Object.DestroyImmediate(cam.gameObject); }
            if (sun != null) Object.DestroyImmediate(sun.gameObject);
            if (rt != null) { rt.Release(); Object.DestroyImmediate(rt); rt = null; }
            if (readback != null) { Object.DestroyImmediate(readback); readback = null; }
            // Never save anything into LOT. Drop the render scene and put the editor
            // back on the scene it had open (a windowed run remembers its last scene),
            // or on a fresh empty one when it had none.
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            if (!string.IsNullOrEmpty(previousScenePath) && File.Exists(previousScenePath))
                EditorSceneManager.OpenScene(previousScenePath, OpenSceneMode.Single);
        }
    }

    // ---------------------------------------------------------------- materials

    // Synty materials sit on the packs' own shader graphs (Generic_Basic: _Albedo_Map,
    // POLYGON_CustomCharacters: _Texture_Map + tint masks). Neither exporter knows those
    // property names, so every material is swapped -- on the scene instance only -- for a
    // URP/Lit clone of the same name carrying the atlas in _BaseMap, white base colour,
    // metallic 0, smoothness 0. The glTF gets baseColorTexture = the atlas and nothing
    // else, which is what a flat-shaded raylib pass wants. FantasyHero's per-preset
    // tints (skin/hair/cloth colours through masks) are NOT baked: those presets export
    // with the plain atlas colours.
    public class MaterialFlattener : IDisposable {
        static readonly string[] kAlbedoProps = { "_Albedo_Map", "_Texture_Map", "_BaseMap", "_MainTex", "_Albedo", "_Texture", "_Diffuse" };
        static readonly string[] kNotAlbedo = { "normal", "mask", "emis", "metal", "occlu", "smooth", "rough", "bump", "height" };
        readonly Dictionary<Material, Material> cache = new Dictionary<Material, Material>();
        readonly Shader lit;
        readonly Material fallback;

        public MaterialFlattener() {
            lit = Shader.Find("Universal Render Pipeline/Lit");
            if (lit == null) throw new InvalidOperationException("Universal Render Pipeline/Lit not found -- is this a URP project?");
            fallback = new Material(lit) { name = "Missing" };
            fallback.SetColor("_BaseColor", Color.magenta);
        }

        public static Texture2D Albedo(Material src) {
            foreach (var p in kAlbedoProps)
                if (src.HasProperty(p) && src.GetTexture(p) is Texture2D t) return t;
            foreach (var p in src.GetTexturePropertyNames()) {
                var lower = p.ToLowerInvariant();
                if (kNotAlbedo.Any(lower.Contains)) continue;
                if (src.GetTexture(p) is Texture2D t) return t;
            }
            return null;
        }

        public Material Flatten(Material src) {
            if (src == null) return fallback;
            if (cache.TryGetValue(src, out var done)) return done;
            var m = new Material(lit) { name = src.name };
            var tex = Albedo(src);
            m.SetTexture("_BaseMap", tex);
            m.SetColor("_BaseColor", Color.white);
            m.SetFloat("_Metallic", 0f);
            m.SetFloat("_Smoothness", 0f);
            bool transparent = src.renderQueue >= 3000 ||
                               (src.HasProperty("_Surface") && src.GetFloat("_Surface") > 0.5f) ||
                               src.name.IndexOf("Water", StringComparison.OrdinalIgnoreCase) >= 0 ||
                               src.name.IndexOf("Glass", StringComparison.OrdinalIgnoreCase) >= 0;
            if (transparent) {
                m.SetFloat("_Surface", 1f);
                m.SetFloat("_Blend", 0f);
                m.SetOverrideTag("RenderType", "Transparent");
                m.EnableKeyword("_SURFACE_TYPE_TRANSPARENT");
                m.renderQueue = 3000;
                m.SetColor("_BaseColor", new Color(1f, 1f, 1f, 0.7f));
            }
            if (tex == null) Debug.LogWarning("[LotSpriteRenderer] no albedo texture found on material " + src.name + " (" + src.shader.name + ")");
            cache[src] = m;
            return m;
        }

        public void Flatten(GameObject go) {
            foreach (var r in go.GetComponentsInChildren<Renderer>(true))
                r.sharedMaterials = r.sharedMaterials.Select(Flatten).ToArray();
        }

        // Materials on renderers that actually export (active, enabled), in first-seen order.
        public static IEnumerable<Material> Used(GameObject go) {
            var seen = new HashSet<Material>();
            foreach (var r in go.GetComponentsInChildren<Renderer>(true)) {
                if (!r.enabled || !r.gameObject.activeInHierarchy) continue;
                foreach (var m in r.sharedMaterials) if (m != null && seen.Add(m)) yield return m;
            }
        }

        public void Dispose() {
            foreach (var m in cache.Values) if (m != null) Object.DestroyImmediate(m);
            cache.Clear();
            if (fallback != null) Object.DestroyImmediate(fallback);
        }
    }

    // ---------------------------------------------------------------- skin combiner

    // raylib loads ONE skin per model (skins[0]) and reads every primitive's JOINTS_0
    // against it, so a rig must leave here as a single SkinnedMeshRenderer over one
    // bone list. Synty presets are dozens of SkinnedMeshRenderers (the modular parts,
    // all skinned to the same skeleton in the same bind space) plus rigid MeshRenderers
    // parented to bones (sword holder, pouch, the viewmodel weapon). Everything active
    // is merged: skinned parts keep their weights (remapped to the union bone list),
    // rigid parts are re-expressed in their bone's bind space and weighted 100% to it.
    // Submeshes are grouped by material; the source renderers (and any hidden ones)
    // are removed so the glb carries only the skeleton and the body.
    public class CombinedSkin {
        public int bones;
        public int vertices;
        public int triangles;
        public List<Material> materials = new List<Material>();
        public SkinnedMeshRenderer body;
        public string rootBone;
    }

    public static class SkinCombiner {
        public static CombinedSkin Combine(GameObject root) {
            var smrs = root.GetComponentsInChildren<SkinnedMeshRenderer>(true)
                .Where(r => r.enabled && r.gameObject.activeInHierarchy && r.sharedMesh != null && r.bones != null && r.bones.Length > 0)
                .ToList();
            var mrs = root.GetComponentsInChildren<MeshRenderer>(true)
                .Where(r => r.enabled && r.gameObject.activeInHierarchy)
                .Where(r => { var mf = r.GetComponent<MeshFilter>(); return mf != null && mf.sharedMesh != null; })
                .ToList();
            if (smrs.Count == 0) throw new InvalidOperationException("no active SkinnedMeshRenderer under " + root.name + " -- check keepRenderers");

            var bones = new List<Transform>();
            var boneIndex = new Dictionary<Transform, int>();
            var bind = new List<Matrix4x4>();
            foreach (var smr in smrs) {
                var bp = smr.sharedMesh.bindposes;
                for (int i = 0; i < smr.bones.Length; i++) {
                    var b = smr.bones[i];
                    if (b == null) continue;
                    var pose = i < bp.Length ? bp[i] : b.worldToLocalMatrix * root.transform.localToWorldMatrix;
                    if (boneIndex.TryGetValue(b, out var idx)) {
                        if (!Approximately(pose, bind[idx]))
                            Debug.LogWarning($"[LotSpriteRenderer] bind pose of {b.name} differs between {smr.name} and an earlier part; keeping the first");
                        continue;
                    }
                    boneIndex[b] = bones.Count;
                    bones.Add(b);
                    bind.Add(pose);
                }
            }

            var verts = new List<Vector3>();
            var norms = new List<Vector3>();
            var uvs = new List<Vector2>();
            var weights = new List<BoneWeight>();
            var tris = new Dictionary<Material, List<int>>();
            var matOrder = new List<Material>();
            void AddTris(Material m, int[] t, int baseIndex) {
                if (!tris.TryGetValue(m, out var l)) { l = new List<int>(); tris[m] = l; matOrder.Add(m); }
                foreach (var i in t) l.Add(i + baseIndex);
            }
            Material MatAt(Material[] mats, int sub) => mats.Length > 0 ? mats[Mathf.Min(sub, mats.Length - 1)] : null;

            foreach (var smr in smrs) {
                var mesh = smr.sharedMesh;
                int baseIndex = verts.Count;
                var v = mesh.vertices; var n = mesh.normals; var uv = mesh.uv; var bw = mesh.boneWeights;
                var map = smr.bones.Select(b => b != null && boneIndex.TryGetValue(b, out var k) ? k : 0).ToArray();
                verts.AddRange(v);
                norms.AddRange(n.Length == v.Length ? n : Enumerable.Repeat(Vector3.up, v.Length));
                uvs.AddRange(uv.Length == v.Length ? uv : Enumerable.Repeat(Vector2.zero, v.Length));
                if (bw.Length == v.Length) foreach (var w in bw) weights.Add(Remap(w, map));
                else for (int i = 0; i < v.Length; i++) weights.Add(new BoneWeight { boneIndex0 = map.Length > 0 ? map[0] : 0, weight0 = 1f });
                var mats = smr.sharedMaterials;
                for (int s = 0; s < mesh.subMeshCount; s++) AddTris(MatAt(mats, s), mesh.GetTriangles(s), baseIndex);
            }
            foreach (var mr in mrs) {
                var mesh = mr.GetComponent<MeshFilter>().sharedMesh;
                var bone = mr.transform;
                while (bone != null && !boneIndex.ContainsKey(bone)) bone = bone.parent;
                if (bone == null) { Debug.LogWarning("[LotSpriteRenderer] rigid mesh not under a bone, skipped: " + mr.name); continue; }
                int bi = boneIndex[bone];
                int baseIndex = verts.Count;
                var m = bind[bi].inverse * bone.worldToLocalMatrix * mr.transform.localToWorldMatrix;
                var v = mesh.vertices; var n = mesh.normals; var uv = mesh.uv;
                for (int i = 0; i < v.Length; i++) {
                    verts.Add(m.MultiplyPoint3x4(v[i]));
                    norms.Add(i < n.Length ? m.MultiplyVector(n[i]).normalized : Vector3.up);
                    uvs.Add(i < uv.Length ? uv[i] : Vector2.zero);
                    weights.Add(new BoneWeight { boneIndex0 = bi, weight0 = 1f });
                }
                var mats = mr.sharedMaterials;
                for (int s = 0; s < mesh.subMeshCount; s++) AddTris(MatAt(mats, s), mesh.GetTriangles(s), baseIndex);
            }

            var combined = new Mesh { name = root.name + "_body" };
            combined.indexFormat = verts.Count > 65000 ? IndexFormat.UInt32 : IndexFormat.UInt16;
            combined.SetVertices(verts);
            combined.SetNormals(norms);
            combined.SetUVs(0, uvs);
            combined.boneWeights = weights.ToArray();
            combined.bindposes = bind.ToArray();
            combined.subMeshCount = matOrder.Count;
            for (int s = 0; s < matOrder.Count; s++) combined.SetTriangles(tris[matOrder[s]], s);
            combined.RecalculateBounds();

            // Drop every source renderer (active or hidden) and then every empty leaf
            // outside the skeleton, so the glb is skeleton + Body and nothing else. The
            // skeleton subtree (the top-level ancestor of the first bone, e.g. "Root") is
            // never pruned: the Humanoid avatar maps bones the parts may not weight
            // (toes, finger tips) and the retarget needs every one of them present.
            foreach (var r in root.GetComponentsInChildren<Renderer>(true).ToList()) {
                if (r == null) continue;
                var go = r.gameObject;
                var mf = go.GetComponent<MeshFilter>();
                Object.DestroyImmediate(r);
                if (mf != null) Object.DestroyImmediate(mf);
            }
            var skeletonRoot = bones[0];
            while (skeletonRoot.parent != null && skeletonRoot.parent != root.transform) skeletonRoot = skeletonRoot.parent;
            PruneEmpty(root.transform, skeletonRoot);

            var bodyGo = new GameObject("Body");
            bodyGo.transform.SetParent(root.transform, false);
            var body = bodyGo.AddComponent<SkinnedMeshRenderer>();
            body.sharedMesh = combined;
            body.bones = bones.ToArray();
            body.rootBone = bones[0];
            body.sharedMaterials = matOrder.ToArray();
            body.localBounds = combined.bounds;
            body.updateWhenOffscreen = true;
            return new CombinedSkin {
                bones = bones.Count, vertices = verts.Count, triangles = tris.Values.Sum(l => l.Count) / 3,
                materials = matOrder, body = body, rootBone = bones[0].name,
            };
        }

        static BoneWeight Remap(BoneWeight w, int[] map) {
            int M(int i) => i >= 0 && i < map.Length ? map[i] : 0;
            return new BoneWeight {
                boneIndex0 = M(w.boneIndex0), weight0 = w.weight0,
                boneIndex1 = M(w.boneIndex1), weight1 = w.weight1,
                boneIndex2 = M(w.boneIndex2), weight2 = w.weight2,
                boneIndex3 = M(w.boneIndex3), weight3 = w.weight3,
            };
        }

        static bool Approximately(Matrix4x4 a, Matrix4x4 b) {
            for (int i = 0; i < 16; i++) if (Mathf.Abs(a[i] - b[i]) > 1e-4f) return false;
            return true;
        }

        // Bottom-up: a transform with no other components and no children goes, unless it
        // is the skeleton or lives inside it.
        static void PruneEmpty(Transform t, Transform skeletonRoot) {
            if (t == skeletonRoot) return;
            for (int i = t.childCount - 1; i >= 0; i--) PruneEmpty(t.GetChild(i), skeletonRoot);
            if (t.parent == null) return;
            if (t.childCount == 0 && t.GetComponents<Component>().Length == 1) Object.DestroyImmediate(t.gameObject);
        }
    }

    // ---------------------------------------------------------------- mesh stats

    public struct MeshStats {
        public int triangles;
        public int vertices;
        public Bounds bounds;       // world space; the object sits at the origin
        public static MeshStats Of(GameObject go) {
            var s = new MeshStats();
            bool any = false;
            foreach (var r in go.GetComponentsInChildren<Renderer>(true)) {
                if (!r.enabled || !r.gameObject.activeInHierarchy) continue;
                Mesh mesh = null;
                if (r is SkinnedMeshRenderer smr) mesh = smr.sharedMesh;
                else { var mf = r.GetComponent<MeshFilter>(); if (mf != null) mesh = mf.sharedMesh; }
                if (mesh == null) continue;
                s.vertices += mesh.vertexCount;
                for (int i = 0; i < mesh.subMeshCount; i++) s.triangles += (int)(mesh.GetIndexCount(i) / 3);
                if (!any) { s.bounds = r.bounds; any = true; } else s.bounds.Encapsulate(r.bounds);
            }
            return s;
        }
    }

    // ---------------------------------------------------------------- contact sheet

    public class ContactSheet {
        readonly int cell, cols, rows;
        readonly Texture2D tex;
        readonly List<string> names = new List<string>();
        int n;

        public ContactSheet(int cell, int cols, int count) {
            this.cell = Mathf.Max(16, cell);
            this.cols = Mathf.Max(1, cols);
            rows = Mathf.Max(1, (Mathf.Max(1, count) + this.cols - 1) / this.cols);
            tex = new Texture2D(this.cols * this.cell, rows * this.cell, TextureFormat.RGBA32, false);
            var fill = new Color32(24, 24, 28, 255);
            tex.SetPixels32(Enumerable.Repeat(fill, tex.width * tex.height).ToArray());
        }

        // Takes ownership of `thumb` (cell x cell). Extra cells past the planned count are dropped.
        public void Add(Texture2D thumb, string name) {
            try {
                int col = n % cols, row = n / cols;
                if (row < rows && thumb.width == cell && thumb.height == cell)
                    tex.SetPixels32(col * cell, tex.height - (row + 1) * cell, cell, cell, thumb.GetPixels32());
                names.Add(name);
                n++;
            } finally {
                Object.DestroyImmediate(thumb);
            }
        }

        public void Write(string path) {
            tex.Apply();
            File.WriteAllBytes(path, tex.EncodeToPNG());
            var sb = new StringBuilder();
            sb.Append("{\n  \"cell\": ").Append(cell).Append(", \"columns\": ").Append(cols).Append(", \"rows\": ").Append(rows).Append(",\n  \"cells\": [");
            sb.Append(string.Join(", ", names.Select(x => "\"" + ExportManifest.E(x) + "\"")));
            sb.Append("]\n}\n");
            File.WriteAllText(Path.ChangeExtension(path, ".json"), sb.ToString());
            Object.DestroyImmediate(tex);
        }
    }

    // ---------------------------------------------------------------- sync runner

    // glTFast's export is async; the Editor's own menu entry pumps it on the main
    // thread with an exclusive SynchronizationContext, and that helper is internal.
    // Same idea here: every continuation posts back to this queue, which we drain
    // until the task ends, so nothing blocks waiting for a main thread we hold.
    static class SyncRunner {
        public static T Run<T>(Func<Task<T>> task) {
            var old = SynchronizationContext.Current;
            var ctx = new ExclusiveContext();
            SynchronizationContext.SetSynchronizationContext(ctx);
            T result = default;
            try {
                ctx.Post(async _ => {
                    try { result = await task(); }
                    catch (Exception e) { ctx.Error = e; }
                    finally { ctx.End(); }
                }, null);
                ctx.Loop();
            } finally {
                SynchronizationContext.SetSynchronizationContext(old);
            }
            if (ctx.Error != null) throw new InvalidOperationException("async export failed: " + ctx.Error.Message, ctx.Error);
            return result;
        }

        class ExclusiveContext : SynchronizationContext {
            readonly Queue<KeyValuePair<SendOrPostCallback, object>> queue = new Queue<KeyValuePair<SendOrPostCallback, object>>();
            readonly AutoResetEvent pending = new AutoResetEvent(false);
            bool done;
            public Exception Error;
            public override void Send(SendOrPostCallback d, object state) => throw new NotSupportedException("Send on the exclusive context");
            public override void Post(SendOrPostCallback d, object state) {
                lock (queue) queue.Enqueue(new KeyValuePair<SendOrPostCallback, object>(d, state));
                pending.Set();
            }
            public void End() => Post(_ => done = true, null);
            public void Loop() {
                while (!done) {
                    KeyValuePair<SendOrPostCallback, object> item;
                    bool has;
                    lock (queue) { has = queue.Count > 0; item = has ? queue.Dequeue() : default; }
                    if (has) item.Key(item.Value);
                    else pending.WaitOne();
                }
            }
            public override SynchronizationContext CreateCopy() => this;
        }
    }

    // ---------------------------------------------------------------- manifests

    public class ManifestWriter {
        readonly string jobName, jobPath;
        readonly List<string> records = new List<string>();
        public ManifestWriter(string jobName, string jobPath) { this.jobName = jobName; this.jobPath = jobPath; }
        public void Add(string file, string state, int frame, string clipAsset, string clipName, float time, float clipLength,
                        int w, int h, string rig, string weapon) {
            records.Add("    {" +
                $"\"file\": \"{E(file)}\", \"state\": \"{E(state)}\", \"frame\": {frame}, " +
                $"\"clipAsset\": \"{E(clipAsset)}\", \"clipName\": \"{E(clipName)}\", " +
                $"\"time\": {time.ToString("0.####", System.Globalization.CultureInfo.InvariantCulture)}, " +
                $"\"clipLength\": {clipLength.ToString("0.####", System.Globalization.CultureInfo.InvariantCulture)}, " +
                $"\"width\": {w}, \"height\": {h}, \"rig\": \"{E(rig)}\", \"weapon\": \"{E(weapon)}\"}}");
        }
        public void Write(string path) {
            var sb = new StringBuilder();
            sb.Append("{\n  \"schemaVersion\": 1,\n");
            sb.Append($"  \"provenance\": \"Rendered by tools/lot-pipeline/unity/LotSpriteRenderer.cs from Lord of Trojia assets (licensed, not redistributable) - rerun render-lot.ps1; do not hand-edit\",\n");
            sb.Append($"  \"jobName\": \"{E(jobName)}\",\n  \"job\": \"{E(jobPath)}\",\n");
            sb.Append($"  \"unity\": \"{E(Application.unityVersion)}\",\n");
            sb.Append("  \"frames\": [\n");
            sb.Append(string.Join(",\n", records));
            sb.Append("\n  ]\n}\n");
            File.WriteAllText(path, sb.ToString());
        }
        static string E(string s) => (s ?? "").Replace("\\", "/").Replace("\"", "\\\"");
    }

    // One record per exported 3D asset. Hand-built JSON (JsonUtility cannot write
    // nested lists); lot3d-manifest.py reads it, adds sha256s and writes the ledgers.
    public class ExportManifest {
        public class ClipRecord { public string name; public int index; public string source; public string clipName; public float length; public int frames; public bool loop; }
        readonly string jobName, jobPath, mode;
        readonly List<string> records = new List<string>();
        public ExportManifest(string jobName, string jobPath, string mode) { this.jobName = jobName; this.jobPath = jobPath; this.mode = mode; }

        public void AddStatic(PrefabSet set, string prefabPath, string name, string gltfPath, string outDir, MeshStats stats, List<Material> mats) {
            var rel = Rel(outDir, gltfPath);
            var binPath = Path.ChangeExtension(gltfPath, ".bin");
            long bytes = new FileInfo(gltfPath).Length + (File.Exists(binPath) ? new FileInfo(binPath).Length : 0);
            var sb = new StringBuilder();
            sb.Append("    {");
            sb.Append($"\"name\": \"{E(name)}\", \"pack\": \"{E(set.pack)}\", \"role\": \"{E(set.role)}\", \"source\": \"{E(prefabPath)}\", ");
            sb.Append($"\"file\": \"{E(rel)}\", \"bin\": \"{E(Path.ChangeExtension(rel, ".bin"))}\", \"bytes\": {bytes}, ");
            sb.Append($"\"triangles\": {stats.triangles}, \"vertices\": {stats.vertices}, ");
            sb.Append(BoundsJson(stats.bounds)).Append(", ");
            sb.Append(MaterialsJson(mats));
            sb.Append("}");
            records.Add(sb.ToString());
        }

        public void AddRig(RigSpec spec, string glbPath, string outDir, CombinedSkin skin, List<ClipRecord> clips, string weapon, Bounds bounds) {
            var rel = Rel(outDir, glbPath);
            var sb = new StringBuilder();
            sb.Append("    {");
            sb.Append($"\"name\": \"{E(spec.name)}\", \"role\": \"{E(spec.role)}\", \"source\": \"{E(spec.rigPrefab)}\", \"avatarModel\": \"{E(spec.avatarModel)}\", \"weapon\": \"{E(weapon)}\", ");
            sb.Append($"\"file\": \"{E(rel)}\", \"bytes\": {new FileInfo(glbPath).Length}, ");
            sb.Append($"\"bones\": {skin.bones}, \"rootBone\": \"{E(skin.rootBone)}\", \"triangles\": {skin.triangles}, \"vertices\": {skin.vertices}, ");
            sb.Append(BoundsJson(bounds)).Append(", ");
            sb.Append(MaterialsJson(skin.materials)).Append(", ");
            sb.Append("\"clips\": [");
            sb.Append(string.Join(", ", clips.Select(c =>
                $"{{\"index\": {c.index}, \"name\": \"{E(c.name)}\", \"source\": \"{E(c.source)}\", \"clipName\": \"{E(c.clipName)}\", " +
                $"\"length\": {F(c.length)}, \"frames\": {c.frames}, \"loop\": {(c.loop ? "true" : "false")}}}")));
            sb.Append("]}");
            records.Add(sb.ToString());
        }

        // Unity-space bounds and the same box in glTF space (both exporters mirror X).
        static string BoundsJson(Bounds b) {
            var mn = b.min; var mx = b.max;
            return $"\"boundsUnity\": {{\"min\": [{F(mn.x)}, {F(mn.y)}, {F(mn.z)}], \"max\": [{F(mx.x)}, {F(mx.y)}, {F(mx.z)}]}}, " +
                   $"\"boundsGltf\": {{\"min\": [{F(-mx.x)}, {F(mn.y)}, {F(mn.z)}], \"max\": [{F(-mn.x)}, {F(mx.y)}, {F(mx.z)}]}}";
        }

        static string MaterialsJson(List<Material> mats) {
            var ms = mats.Select(m => {
                var t = m != null ? m.GetTexture("_BaseMap") : null;
                var texPath = t != null ? AssetDatabase.GetAssetPath(t) : "";
                return $"{{\"name\": \"{E(m != null ? m.name : "")}\", \"texture\": \"{E(texPath)}\"}}";
            });
            return "\"materials\": [" + string.Join(", ", ms) + "]";
        }

        public void Write(string path) {
            var sb = new StringBuilder();
            sb.Append("{\n  \"schemaVersion\": 1,\n");
            sb.Append("  \"provenance\": \"Exported by tools/lot-pipeline/unity/LotSpriteRenderer.cs from Lord of Trojia assets (Synty / Malbers: licensed, not redistributable) - rerun render-lot.ps1; do not hand-edit\",\n");
            sb.Append($"  \"mode\": \"{E(mode)}\",\n  \"jobName\": \"{E(jobName)}\",\n  \"job\": \"{E(jobPath)}\",\n");
            sb.Append($"  \"unity\": \"{E(Application.unityVersion)}\",\n");
            sb.Append("  \"coordinates\": \"glTF right-handed Y-up, metres; X mirrored from Unity; 1 unit = 1 tile\",\n");
            sb.Append("  \"assets\": [\n");
            sb.Append(string.Join(",\n", records));
            sb.Append("\n  ]\n}\n");
            File.WriteAllText(path, sb.ToString());
        }

        static string Rel(string root, string path) {
            var r = Path.GetFullPath(root).TrimEnd('\\', '/') + Path.DirectorySeparatorChar;
            var p = Path.GetFullPath(path);
            return (p.StartsWith(r, StringComparison.OrdinalIgnoreCase) ? p.Substring(r.Length) : p).Replace('\\', '/');
        }
        static string F(float v) => v.ToString("0.####", System.Globalization.CultureInfo.InvariantCulture);
        public static string E(string s) => (s ?? "").Replace("\\", "/").Replace("\"", "\\\"");
    }
}
#endif
