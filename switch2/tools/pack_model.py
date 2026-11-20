#!/usr/bin/env python3
"""Run in Blender background mode to convert an owned glTF model to a PKForge .pfm."""

import argparse
from array import array
import math
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Matrix, Vector

HEADER = struct.Struct("<4s6H")
VERTEX = struct.Struct("<3f4B4B2h")
TRIANGLE = struct.Struct("<5H")
TRANSFORM = struct.Struct("<12f")
NO_TEXTURE = 65535


def rgb565(channels):
    r, g, b = (max(0, min(255, round(c * 255))) for c in channels)
    return max(1, (r >> 3) << 11 | (g >> 2) << 5 | b >> 3)


def srgb(linear):
    return 12.92 * linear if linear < 0.0031308 else 1.055 * linear ** (1 / 2.4) - 0.055


def base_color(material):
    if material is None:
        return 0xffff, None
    nodes = material.node_tree.nodes if material.use_nodes else ()
    shader = next((node for node in nodes if node.type == "BSDF_PRINCIPLED"), None)
    unlit = next((node for node in nodes if node.type == "EMISSION"), None)
    if shader:
        base = shader.inputs["Base Color"]
    elif unlit:
        base = unlit.inputs["Color"]
    else:
        return rgb565(map(srgb, material.diffuse_color[:3])), None
    if base.is_linked:
        source = base.links[0].from_node
        # Blender multiplies a glTF base texture by COLOR_0 via a Mix node.
        if source.type == "MIX" and source.blend_type == "MULTIPLY":
            source = next((link.from_node for socket in source.inputs for link in socket.links
                           if link.from_node.type == "TEX_IMAGE"), source)
        if source.type == "TEX_IMAGE":
            return 0xffff, source.image
    return rgb565(map(srgb, base.default_value[:3])), None


def texture_bytes(image):
    width, height = image.size
    if not min(width, height):
        raise ValueError(f"Missing texture data for {image.name}")
    source = image
    try:
        limit = 256 / max(width, height)
        if limit < 1:
            source = image.copy()
            width, height = (max(1, round(side * limit)) for side in (width, height))
            source.scale(width, height)
        rgba = array("f", [0]) * (width * height * 4)
        source.pixels.foreach_get(rgba)
        pixels = [0 if rgba[i + 3] < 0.5 else rgb565(map(srgb, rgba[i:i + 3]) if source.is_float else rgba[i:i + 3])
                  for i in range(0, len(rgba), 4)]
        return struct.pack(f"<HH{len(pixels)}H", width, height, *pixels)
    finally:
        if source != image:
            bpy.data.images.remove(source)


def add_key(table, key):
    if key not in table:
        table[key] = len(table)
    return table[key]


def weights_for(mesh, modifier, vertex, objects, joint_ids):
    influences = {}
    if modifier is not None:
        armature = modifier.object
        for membership in vertex.groups:
            group = mesh.vertex_groups[membership.group].name
            if membership.weight > 0 and group in armature.pose.bones:
                influences[(armature.name, group)] = membership.weight
    chosen = sorted(influences, key=influences.get, reverse=True)[:4]
    if not chosen:
        chosen = [(mesh.name, "")]
        influences[chosen[0]] = 1
    weight_sum = sum(influences[key] for key in chosen)
    weights = [math.floor(influences[key] / weight_sum * 255) for key in chosen]
    weights[0] += 255 - sum(weights)
    ids = [add_key(joint_ids, key) for key in chosen]
    for name, _ in chosen:
        objects[name] = bpy.data.objects[name]
    return ids + [0] * (4 - len(ids)), weights + [0] * (4 - len(weights))


def collect(meshes):
    vertices, triangles, textures = [], [], []
    vertex_ids, texture_ids, joints, objects = {}, {}, {}, {}
    for mesh in meshes:
        data = mesh.data
        data.calc_loop_triangles()
        uv = data.uv_layers.active
        modifier = next((mod for mod in mesh.modifiers if mod.type == "ARMATURE" and mod.object), None)
        materials = [base_color(slot.material) for slot in mesh.material_slots]
        for triangle in data.loop_triangles:
            color, image = materials[triangle.material_index] if triangle.material_index < len(materials) else (0xffff, None)
            if image is not None and uv is not None:
                if image.name not in texture_ids:
                    textures.append(texture_bytes(image))
                texture = add_key(texture_ids, image.name)
            else:
                texture = NO_TEXTURE
            corners = []
            for source_vertex, loop in zip(triangle.vertices, triangle.loops):
                uv_pair = tuple(max(-32768, min(32767, round(v * 4096))) for v in uv.data[loop].uv) if uv else (0, 0)
                key = mesh.name, source_vertex, uv_pair
                if key not in vertex_ids:
                    vertex = data.vertices[source_vertex]
                    ids, weights = weights_for(mesh, modifier, vertex, objects, joints)
                    vertices.append((mesh.matrix_world @ vertex.co, ids, weights, uv_pair))
                corners.append(add_key(vertex_ids, key))
            triangles.append((*corners, texture, color))
    limits = [(vertices, 16384, "vertices"), (triangles, 32768, "triangles"), (joints, 256, "joints"),
              (textures, 16, "textures")]
    for values, max_count, name in limits:
        if len(values) > max_count:
            raise ValueError(f"{len(values)} {name} exceeds the portable limit of {max_count}")
    if not vertices or not triangles:
        raise ValueError("No mesh triangles found")
    return vertices, triangles, textures, joints, objects


def frame_settings(args, scene, armatures):
    if args.action:
        if len(armatures) != 1:
            raise ValueError("--action needs exactly one imported armature")
        data = armatures[0].animation_data_create()
        track = data.nla_tracks.get(args.action)
        action = track.strips[0].action if track and len(track.strips) == 1 else bpy.data.actions.get(args.action)
        if action is None:
            raise ValueError(f"No armature action or glTF animation named {args.action!r}")
        for track in data.nla_tracks:
            track.mute = True
        data.action = action

    ranges = [tuple(data.action.frame_range) for ob in scene.objects
              if (data := ob.animation_data) and data.action]
    ranges.extend((strip.frame_start, strip.frame_end) for ob in scene.objects
                  if (data := ob.animation_data) for track in data.nla_tracks if not track.mute
                  for strip in track.strips if not strip.mute)
    start = args.start if args.start is not None else min((r[0] for r in ranges), default=0)
    end = args.end if args.end is not None else max((r[1] for r in ranges), default=start)
    if end < start:
        raise ValueError("End frame precedes the start frame")
    scene_fps = scene.render.fps / scene.render.fps_base
    count = max(1, round((end - start) * args.fps / scene_fps))
    if count > 240:
        raise ValueError("The selected loop exceeds 240 samples. Use --start and --end to trim it.")
    return start, end, count


def set_frame(scene, frame):
    base = math.floor(frame)
    scene.frame_set(base, subframe=frame - base)


def run(args):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(args.input.resolve()))
    scene = bpy.context.scene
    armatures = [obj for obj in scene.objects if obj.type == "ARMATURE"]
    helpers = {bone.custom_shape for obj in armatures for bone in obj.pose.bones}
    meshes = [obj for obj in scene.objects if obj.type == "MESH" and obj not in helpers]
    start, end, frames = frame_settings(args, scene, armatures)
    set_frame(scene, start)
    vertices, triangles, textures, joints, objects = collect(meshes)

    def normalization(positions):
        minimum = Vector(tuple(min(p[i] for p in positions) for i in range(3)))
        maximum = Vector(tuple(max(p[i] for p in positions) for i in range(3)))
        center = (minimum + maximum) * 0.5
        extent = max((p - center).length for p in positions)
        if extent <= 0 or not math.isfinite(extent):
            raise ValueError("The model has no usable dimensions")
        return Matrix.Scale(1 / extent, 4) @ Matrix.Translation(-center)

    bind_normalize = normalization([position for position, *_ in vertices])
    unnormalize = bind_normalize.inverted()
    inverse_rest = {}
    first_pose = []
    for name, bone in joints:
        obj = objects[name]
        rest = obj.matrix_world @ obj.data.bones[bone].matrix_local if bone else obj.matrix_world
        pose = obj.matrix_world @ obj.pose.bones[bone].matrix if bone else obj.matrix_world
        first_pose.append(pose @ rest.inverted())
        inverse_rest[name, bone] = rest.inverted() @ unnormalize
    # Imported skeletons can change scene scale or orientation in the first pose.
    # Fit the camera to visible skinned geometry while keeping bind vertices compact.
    posed = [sum(((first_pose[j] @ position) * (w / 255) for j, w in zip(ids, weights) if w),
                 Vector((0, 0, 0))) for position, ids, weights, _ in vertices]
    normalize = normalization(posed)

    packed = bytearray(HEADER.pack(b"PFM1", len(vertices), len(triangles), len(joints), frames, len(textures), args.fps))
    for position, ids, weights, uv in vertices:
        packed.extend(VERTEX.pack(*(bind_normalize @ position), *ids, *weights, *uv))
    for triangle in triangles:
        packed.extend(TRIANGLE.pack(*triangle))
    for texture in textures:
        packed.extend(texture)

    for frame in range(frames):
        set_frame(scene, start + frame * (end - start) / frames)
        for name, bone in joints:
            obj = objects[name]
            pose = obj.matrix_world @ obj.pose.bones[bone].matrix if bone else obj.matrix_world
            matrix = normalize @ pose @ inverse_rest[name, bone]
            values = [component for row in range(3) for component in matrix[row]]
            if any(not math.isfinite(v) or abs(v) > 64 for v in values):
                raise ValueError(f"Joint {bone or name} moves outside the supported transform range")
            packed.extend(TRANSFORM.pack(*values))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(packed)
    print(f"{args.output} — {len(vertices)} vertices, {len(joints)} joints, {frames} poses")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fps", type=int, default=24, choices=range(1, 61), metavar="1-60")
    parser.add_argument("--action")
    parser.add_argument("--start", type=float)
    parser.add_argument("--end", type=float)
    run(parser.parse_args(sys.argv[sys.argv.index("--") + 1:]))
