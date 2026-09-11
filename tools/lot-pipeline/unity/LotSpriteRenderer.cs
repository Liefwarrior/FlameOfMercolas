// LotSpriteRenderer.cs -- Granadad LOT pipeline: batch render-to-PNG of posed
// Lord of Trojia rigs from the Unity Editor.
//
// Two jobs, one tool:
//   viewmodel  : first-person arms (+ optional weapon) posed from Malbers humanoid
//                clips, one PNG per (state, frame), alpha background.
//   portraits  : head-and-shoulders of Synty character prefabs, N views each.
//
// This file is NOT part of Lord of Trojia. render-lot.ps1 copies it into
// <LOT>/Assets/Editor/LotPipeline/ for the duration of one batch run and removes
// it afterwards. It can also be dropped there by hand for interactive use via
// the Tools/Granadad menu.
//
// Entry points:
//   batch : -executeMethod Granadad.LotPipeline.LotSpriteRenderer.RenderFromCommandLine
//           -lotMode viewmodel|portraits -lotJob <job.json> -lotOut <dir>
//   menu  : Tools/Granadad/Render Viewmodel Job...  /  Render Portrait Job...
//
// Requirements: Unity 6000.x with URP (LOT is 6000.3.6f1 / URP 17.3). Batch mode
// must NOT pass -nographics -- Camera.Render needs a GPU context.
//
// Output contract (consumed by the Granadad side of the pipeline):
//   <out>/<jobName>/<state>_<frameIndex>.png       (viewmodel)
//   <out>/<jobName>/<prefabName>_<viewName>.png    (portraits)
//   <out>/<jobName>/frames.json                    one record per PNG: file, state/view,
//                                                  clip, time, width, height, source paths
#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
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
                int n = mode == "portraits" ? RenderPortraits(job, outDir) : RenderViewmodel(job, outDir);
                Debug.Log($"[LotSpriteRenderer] wrote {n} PNGs to {outDir}");
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
        static void StripBehaviours(GameObject go) {
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

    // ---------------------------------------------------------------- manifest

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
}
#endif
