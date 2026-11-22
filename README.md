# SplatDiff Viewer

Change detection between two 3D Gaussian Splatting scans, built on top of
[Gaussian-Splat-Editor](https://github.com/jason9075/Gaussian-Splat-Editor) by Jason Kuan (MIT licence).

## Milestone 1: scan A / scan B

```bash
make build                     # builds splatdiff, splatdiff_edit and splatdiff_tests
make test                      # unit tests for the covariance maths and edits
python3 tools/make_synthetic_scene.py assets/synthetic_room.ply
./splatdiff assets/synthetic_room.ply                       # scan B starts as a copy of A
./splatdiff assets/synthetic_room.ply --scan-b assets/scanB_demo.ply --show-b
```

* **Tab** switches between scan A (before) and scan B (after).
* The **SplatDiff** panel edits scan B with the selection box: move/rotate, delete, duplicate (real
  changes), and lighting change, jitter, split, prune (re-scan effects that must *not* be detected).
* **Save B** writes `scanB.ply` plus `scanB.edits.json`, the ground truth for the change detector.

Scripted test pairs (same edits, no GUI):

```bash
./build/splatdiff_edit assets/synthetic_room.ply assets/scanB_demo.ply --seed 42 \
  --delete 0.2 0.62 -1.8  0.65 0.6 0.65 \
  --move -1.5 0.535 -1.0  0.55 0.515 0.55  1.2 0 0.4  35 \
  --duplicate 0.5 0.215 1.8  0.35 0.195 0.35  0 0 1.4 \
  --colour 1.15 1.05 0.85 0.03 --jitter 0.005 0.05
```

Boxes are `centre(x y z) half-size(x y z)` in file coordinates. Covariances are transformed as
Sigma' = L Sigma L^T, where L is the linear part of the edit.

## Milestone 2: change detection

```bash
./build/splatdiff_detect assets/synthetic_room.ply assets/scanB_demo.ply           # regions + ground-truth score
./build/splatdiff_detect assets/synthetic_room.ply assets/scanB_demo.ply --bench   # serial vs OpenMP
tools/run_experiments.sh                                                            # every experiment, CSVs in assets/tests
./splatdiff assets/synthetic_room.ply --scan-b assets/scanB_demo.ply --show-b --no-box --camera 1.5 5.5 6.5 -0.3 0 0
```

Pipeline (`src/ChangeDetect.cpp`), colour never used:

1. **Splat** each scan into a voxel grid: Phi(v) = sum a_i exp(-1/2 d^T Sigma_i^-1 d), with a half-voxel
   low-pass filter Sigma + s^2 I (mass preserving) so thin Gaussians are not missed, then occupancy
   O = 1 - exp(-Phi).
2. **Compare** with a 1-voxel tolerance: added if O_B - max_neighbourhood(O_A) > tau, removed if the reverse.
3. **Regions**: 26-connected components, typed added / removed, and removed + added pairs with a similar size
   and D1 shape histogram are reported as one moved object.

Execution modes: serial, OpenMP with atomic adds, OpenMP with per-thread private grids + reduction. The
viewer shows region boxes (red removed, green added, orange/yellow moved from/to) and scores them against the
ground-truth edit log.

---

## Original project README

# Gaussian Splat Editor

Gaussian Splat Editor is an OpenGL tool designed for rendering Gaussian Splat .ply files and allowing users to remove unwanted splats.

## Example Video

[![Example Video](./imgs/gs.gif)](https://youtu.be/_gb3ja7yWPo?si=VWrYYp2CCI9ugnqk)

## Features

- **Render Gaussian Splat .ply Files**: Visualize complex point cloud data using Gaussian splatting.
- **Selective Splat Removal**: Easily remove splats that are not needed or are incorrectly placed.
- **Real-time Editing**: Utilize OpenGL for fast rendering and interactive manipulation.

## Build Instructions

### Requirements

- **CMake** 3.10 or higher
- **OpenGL** development libraries
- **SDL2** for window management and input
- **GLEW** for handling OpenGL extensions (macOS only)
- **glm** for mathematics
- **nlohmann_json** for JSON parsing
- **imgui** for GUI rendering
- **ImGuiFileDialog** for file selection

### Installation

To build the project, follow these steps:

```bash
# Active nix environment
nix develop -c zsh

# If you need to clean the build directory, use the -r option
bash ./build.sh -r

# Build the project
bash ./build.sh

# Run the application
./splatdiff assets/synthetic_room.ply
```

## Ref:

This project is inspired by and builds upon the work of several existing projects:

[antimatter15's webgl viewer](https://github.com/antimatter15/splat):Use texture to send info in shader.

[kishimisu's webgl viewer](https://github.com/kishimisu/Gaussian-Splatting-WebGL):Nice implements, draw mutiple instance to generate gaussian vertex.

[hyperlogic's opengl viewer](https://github.com/hyperlogic/splatapult):Use Geometry shader to generate gaussian vertex.
