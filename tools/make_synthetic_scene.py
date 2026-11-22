#!/usr/bin/env python3
"""Generate a small synthetic 3D Gaussian Splatting scene as a standard 3DGS .ply.

The scene is a floor with a few coloured objects (cubes, a sphere, a pillar), written with the
same vertex properties the original 3DGS trainer outputs (x, y, z, f_dc_*, opacity, scale_*, rot_*),
so it loads in this viewer and in any other 3DGS viewer.

Usage: python3 tools/make_synthetic_scene.py assets/synthetic_room.ply [--seed 0]
"""
import argparse

import numpy as np

SH_C0 = 0.28209479177387814


def rgb_to_dc(rgb):
    return (np.asarray(rgb, dtype=np.float32) - 0.5) / SH_C0


def inv_sigmoid(x):
    return np.log(x / (1.0 - x))


def surface_points_box(rng, center, half, n):
    """Uniform points on the surface of an axis-aligned box."""
    half = np.asarray(half, dtype=np.float32)
    areas = np.array([half[1] * half[2], half[0] * half[2], half[0] * half[1]] * 2)
    face = rng.choice(6, size=n, p=areas / areas.sum())
    pts = rng.uniform(-1, 1, size=(n, 3)).astype(np.float32)
    axis = face % 3
    sign = np.where(face < 3, 1.0, -1.0)
    pts[np.arange(n), axis] = sign
    return center + pts * half


def surface_points_sphere(rng, center, radius, n):
    v = rng.normal(size=(n, 3)).astype(np.float32)
    v /= np.linalg.norm(v, axis=1, keepdims=True)
    return center + v * radius


def make_object(rng, pts, rgb, splat_size, color_noise=0.04):
    n = len(pts)
    col = np.clip(np.asarray(rgb) + rng.normal(0, color_noise, size=(n, 3)), 0.02, 0.98)
    return {
        "xyz": pts.astype(np.float32),
        "rgb": col.astype(np.float32),
        "scale": np.full((n, 3), splat_size, dtype=np.float32) * rng.uniform(0.7, 1.3, size=(n, 3)).astype(np.float32),
        "opacity": rng.uniform(0.75, 0.98, size=n).astype(np.float32),
    }


def build_scene(rng):
    objs = []
    # Floor: checkerboard-ish tiles, flat Gaussians
    n_floor = 60000
    fx = rng.uniform(-4, 4, n_floor)
    fz = rng.uniform(-4, 4, n_floor)
    floor = np.stack([fx, np.zeros(n_floor), fz], axis=1).astype(np.float32)
    checker = ((np.floor(fx) + np.floor(fz)) % 2 == 0)[:, None]
    floor_rgb = np.where(checker, [0.78, 0.76, 0.72], [0.45, 0.43, 0.40])
    f = make_object(rng, floor, [0, 0, 0], 0.035, color_noise=0.0)
    f["rgb"] = np.clip(floor_rgb + rng.normal(0, 0.02, (n_floor, 3)), 0, 1).astype(np.float32)
    f["scale"][:, 1] = 0.004  # flatten in y
    objs.append(f)

    # Objects on the floor
    objs.append(make_object(rng, surface_points_box(rng, np.array([-1.5, 0.5, -1.0]), [0.5, 0.5, 0.5], 18000),
                            [0.85, 0.20, 0.18], 0.03))  # red cube
    objs.append(make_object(rng, surface_points_box(rng, np.array([1.6, 0.35, 0.8]), [0.6, 0.35, 0.4], 18000),
                            [0.20, 0.45, 0.85], 0.03))  # blue box
    objs.append(make_object(rng, surface_points_sphere(rng, np.array([0.2, 0.6, -1.8]), 0.6, 16000),
                            [0.95, 0.80, 0.20], 0.03))  # yellow ball
    objs.append(make_object(rng, surface_points_box(rng, np.array([-2.2, 1.0, 2.0]), [0.25, 1.0, 0.25], 12000),
                            [0.25, 0.70, 0.35], 0.028))  # green pillar
    objs.append(make_object(rng, surface_points_box(rng, np.array([0.5, 0.2, 1.8]), [0.3, 0.2, 0.3], 8000),
                            [0.60, 0.30, 0.75], 0.025))  # small purple box

    return {k: np.concatenate([o[k] for o in objs]) for k in objs[0]}


def write_3dgs_ply(path, scene):
    n = len(scene["xyz"])
    props = ["x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
             "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"]
    data = np.zeros((n, len(props)), dtype=np.float32)
    data[:, 0:3] = scene["xyz"]
    data[:, 6:9] = rgb_to_dc(scene["rgb"])
    data[:, 9] = inv_sigmoid(scene["opacity"])
    data[:, 10:13] = np.log(scene["scale"])
    data[:, 13] = 1.0  # identity quaternion (w, x, y, z)
    header = "ply\nformat binary_little_endian 1.0\n"
    header += f"element vertex {n}\n" + "".join(f"property float {p}\n" for p in props) + "end_header\n"
    with open(path, "wb") as fh:
        fh.write(header.encode("ascii"))
        fh.write(data.astype("<f4").tobytes())
    print(f"wrote {n} gaussians to {path}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    write_3dgs_ply(args.out, build_scene(np.random.default_rng(args.seed)))
