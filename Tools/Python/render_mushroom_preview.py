"""Render procedural mushroom (and vegetation) OBJ files into a PNG contact sheet (standard library only).

Workflow (no editor window needed):
  1. Export the meshes: in the editor console `sh.Mesh.ExportObj [seed]`, or headless:
       UnrealEditor-Cmd.exe <project>.uproject "-ExecCmds=sh.Mesh.ExportObj 3,QUIT_EDITOR" -unattended -nullrhi
     This writes Saved/MushroomDebug/<SpeciesId>_<stage>.obj (+ _far.obj for the far LOD).
  2. Render them with plain Python (outside Unreal):
       python Tools/Python/render_mushroom_preview.py Saved/MushroomDebug Docs/Media/procedural_mushrooms.png
     Add "_far" as a third argument to render the far LOD instead.

The grid has one row per species and the columns Young / Mature / Old, drawn at a common
scale per species so growth is visible. Software rasterizer with a z-buffer and Lambert
shading of the per-vertex sRGB colors; good enough to review shapes and colors.

Vegetation (M1d-2): `sh.Vegetation.ExportObj` writes Saved/VegetationDebug/<Key>.obj, then
       python Tools/Python/render_mushroom_preview.py Saved/VegetationDebug out.png --grid
draws one row per tree genus (Sapling, Pole, Mature..., Old at a common scale) and one row
for the understory (each tile at its own scale).

Like Unreal, it uses the left-handed convention (front-face normal = (C - A) x (B - A))
and culls back faces, so a mesh with inverted winding shows up as "inside out" here too.
Pass "--no-cull" to draw both sides.
"""

import math
import os
import struct
import sys
import zlib

TILE = 320
AZIMUTH = math.radians(35.0)
ELEVATION = math.radians(22.0)
LIGHT_DIRECTION = (-0.45, -0.35, 0.82)
BACKGROUND = (40, 44, 52)


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(c):
    c = max(0.0, min(1.0, c))
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def normalize(v):
    length = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2) or 1.0
    return (v[0] / length, v[1] / length, v[2] / length)


def load_obj(path):
    """Reads 'v x y z [r g b]' and triangular 'f a b c' lines."""
    vertices, colors, faces = [], [], []
    with open(path, encoding="utf-8") as file:
        for line in file:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "v":
                vertices.append(tuple(float(x) for x in parts[1:4]))
                colors.append(tuple(float(x) for x in parts[4:7]) if len(parts) >= 7 else (0.8, 0.8, 0.8))
            elif parts[0] == "f":
                faces.append(tuple(int(x.split("/")[0]) - 1 for x in parts[1:4]))
    return vertices, colors, faces


def to_view(p):
    """World (Z up) -> (screen x, depth, screen y) for a camera above, looking down by ELEVATION."""
    x, y, z = p
    ca, sa = math.cos(AZIMUTH), math.sin(AZIMUTH)
    x1, y1 = ca * x - sa * y, sa * x + ca * y
    ce, se = math.cos(ELEVATION), math.sin(ELEVATION)
    return x1, ce * y1 - se * z, se * y1 + ce * z


def render_tile(image, origin_x, origin_y, mesh, scale, center, cull=True):
    vertices, colors, faces = mesh
    depth_buffer = [[1e30] * TILE for _ in range(TILE)]
    light = normalize(LIGHT_DIRECTION)

    projected = []
    for v in vertices:
        sx, depth, sy = to_view((v[0] - center[0], v[1] - center[1], v[2] - center[2]))
        projected.append((TILE / 2 + sx * scale, TILE * 0.78 - sy * scale, depth))

    for a, b, c in faces:
        A, B, C = vertices[a], vertices[b], vertices[c]
        # Unreal convention (left-handed): front-face normal = (C - A) x (B - A).
        e1 = (B[0] - A[0], B[1] - A[1], B[2] - A[2])
        e2 = (C[0] - A[0], C[1] - A[1], C[2] - A[2])
        normal = normalize((
            e2[1] * e1[2] - e2[2] * e1[1],
            e2[2] * e1[0] - e2[0] * e1[2],
            e2[0] * e1[1] - e2[1] * e1[0],
        ))
        facing_camera = to_view(normal)[1] < 0
        if not facing_camera:
            if cull:
                continue  # back face: invisible in Unreal (single-sided material)
            normal = (-normal[0], -normal[1], -normal[2])
        lambert = sum(normal[i] * light[i] for i in range(3))
        shade = 0.32 + 0.75 * max(0.0, lambert)
        color = [(colors[a][i] + colors[b][i] + colors[c][i]) / 3 for i in range(3)]
        rgb = tuple(int(255 * linear_to_srgb(srgb_to_linear(ch) * shade)) for ch in color)

        (x0, y0, d0), (x1, y1, d1), (x2, y2, d2) = projected[a], projected[b], projected[c]
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9:
            continue
        min_x, max_x = max(0, int(min(x0, x1, x2))), min(TILE - 1, int(max(x0, x1, x2)) + 1)
        min_y, max_y = max(0, int(min(y0, y1, y2))), min(TILE - 1, int(max(y0, y1, y2)) + 1)
        for py in range(min_y, max_y + 1):
            for px in range(min_x, max_x + 1):
                fx, fy = px + 0.5, py + 0.5
                w0 = ((x1 - fx) * (y2 - fy) - (x2 - fx) * (y1 - fy)) / area
                w1 = ((x2 - fx) * (y0 - fy) - (x0 - fx) * (y2 - fy)) / area
                w2 = 1.0 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                depth = w0 * d0 + w1 * d1 + w2 * d2
                if depth < depth_buffer[py][px]:
                    depth_buffer[py][px] = depth
                    image[origin_y + py][origin_x + px] = rgb


def write_png(path, image):
    height, width = len(image), len(image[0])
    raw = b"".join(b"\x00" + bytes(channel for pixel in row for channel in pixel) for row in image)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as file:
        file.write(png)


def grid_key(name):
    """'Fagus_Mature_0' -> ('Fagus', column order); 'Bracken_1' -> ('~Understory', ...)."""
    parts = name.split("_")
    forms = {"Sapling": 0, "Pole": 1, "Mature": 2, "Old": 3}
    if len(parts) == 3 and parts[1] in forms:
        return parts[0], (forms[parts[1]], parts[1], int(parts[2]))
    return "~Understory", (0, parts[0], int(parts[-1]) if parts[-1].isdigit() else 0)


def render_grid(folder, output, cull):
    rows = {}
    for name in sorted(os.listdir(folder)):
        if name.endswith(".obj"):
            row, order = grid_key(name[:-4])
            rows.setdefault(row, []).append((order, name))
    row_names = sorted(rows)
    columns = max(len(items) for items in rows.values())
    image = [[BACKGROUND] * (TILE * columns) for _ in range(TILE * len(row_names))]
    for r, row in enumerate(row_names):
        items = sorted(rows[row])
        meshes = [load_obj(os.path.join(folder, name)) for _, name in items]

        def extent(mesh):
            xs, ys, zs = zip(*mesh[0])
            return max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs), 1e-3)

        common = max(extent(m) for m in meshes)
        for c, ((_, name), mesh) in enumerate(zip(items, meshes)):
            largest = extent(mesh) if row.startswith("~") else common
            xs, ys, _ = zip(*mesh[0])
            center = ((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, 0.0)
            render_tile(image, c * TILE, r * TILE, mesh, TILE * 0.62 / largest, center, cull)
            print(f"{name}: {len(mesh[2])} triangles")
    write_png(output, image)
    print("wrote", output)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cull = "--no-cull" not in sys.argv
    folder, output = args[0], args[1]
    if "--grid" in sys.argv:
        render_grid(folder, output, cull)
        return
    suffix = args[2] if len(args) > 2 else ""

    species = sorted({name.rsplit("_", 1)[0] for name in os.listdir(folder)
                      if name.endswith(".obj") and not name.endswith("_far.obj")})
    image = [[BACKGROUND] * (TILE * 3) for _ in range(TILE * len(species))]
    for row, species_id in enumerate(species):
        meshes = [load_obj(os.path.join(folder, f"{species_id}_{stage}{suffix}.obj")) for stage in range(3)]
        largest = 1e-3
        for vertices, _, _ in meshes:
            xs, ys, zs = zip(*vertices)
            largest = max(largest, max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
        scale = TILE * 0.62 / largest
        for stage, mesh in enumerate(meshes):
            xs, ys, _ = zip(*mesh[0])
            center = ((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, 0.0)
            render_tile(image, stage * TILE, row * TILE, mesh, scale, center, cull)
            print(f"{species_id} stage {stage}: {len(mesh[2])} triangles")
    write_png(output, image)
    print("wrote", output)


if __name__ == "__main__":
    main()
