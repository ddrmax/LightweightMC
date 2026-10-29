#!/usr/bin/env python3
"""
LightweightMC major-version defaults generator.

Reads the prismarine/minecraft-data version registry
(tools/protocol_seed/versions.json) and emits one curated defaults.hpp per
major Minecraft version into include/versions/vX_Y/.

Usage:
    python3 tools/gen_version_defaults.py
"""

import json
import os
import sys
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
SEED_DIR = os.path.join(HERE, "protocol_seed")
VERSIONS_JSON = os.path.join(SEED_DIR, "versions.json")

# Output root: <repo>/include/versions/
OUT_ROOT = os.path.join(os.path.dirname(HERE), "include", "versions")

# (directory_name, version_string)
# Mirrors the Behaviour enum in include/versions/VersionBehaviour.hpp
# (V1_7 ... V1_21, V26_1). The protocol number is taken from versions.json,
# so every major version carries the protocol number that shipped with it.
MAJOR_VERSIONS = [
    ("v1_7",  "1.7.10"),
    ("v1_8",  "1.8"),
    ("v1_9",  "1.9.3"),
    ("v1_10", "1.10"),
    ("v1_11", "1.11.1"),
    ("v1_12", "1.12.2"),
    ("v1_13", "1.13.2"),
    ("v1_14", "1.14.3"),
    ("v1_15", "1.15.2"),
    ("v1_16", "1.16.4"),
    ("v1_17", "1.17.1"),
    ("v1_18", "1.18.2"),
    ("v1_19", "1.19.4"),
    ("v1_20", "1.20.3"),
    ("v1_21", "1.21.11"),
    ("v26_1", "26.1"),
]


def load_protocol_map():
    with open(VERSIONS_JSON, "r") as f:
        data = json.load(f)
    return {v["version"]: int(v["protocol"]) for v in data["versions"]}


# --- Curated per-version data ---

# Food nutrition table, sourced from prismarineJS minecraft-data
# data/pc/1.8/foods.json, as curated in include/versions/v1_8/food.hpp.
# (item_id -> (foodPoints, saturation)). Only 1.8 is populated yet; other
# majors fall back to an empty table (the file, namespace and switch stay).
FOOD_DATA = {
    "1.8": {
        "103": (2, 1.2),
        "141": (3, 4.8),
        "142": (1, 0.6),
        "260": (4, 2.4),
        "282": (6, 7.2),
        "297": (5, 6.0),
        "319": (3, 1.8),
        "320": (8, 12.8),
        "322": (4, 9.6),
        "349": (2, 0.4),
        "350": (5, 6.0),
        "354": (2, 0.4),
        "357": (2, 0.4),
        "363": (3, 1.8),
        "364": (8, 12.8),
        "365": (2, 1.2),
        "366": (6, 7.2),
        "367": (4, 0.8),
        "375": (2, 3.2),
        "393": (5, 7.2),
        "394": (2, 1.2),
        "396": (6, 14.4),
        "400": (8, 4.8),
        "411": (3, 1.8),
        "412": (5, 6.0),
        "413": (10, 12.0),
        "423": (2, 1.2),
        "424": (6, 9.6),
    },
    "1.7.10": {
        "256": (2, 1.2),
        "264": (4, 2.4),
        "146": (3, 1.8),
        "251": (6, 7.2),
        "283": (5, 6.0),
        "276": (3, 1.8),
        "274": (5, 6.0),
        "257": (2, 0.4),
        "259": (2, 0.4),
        "354": (6, 6.0),
        "250": (6, 7.2),
        "358": (4, 0.8),
        "395": (2, 3.2),
        "373": (5, 7.2),
        "372": (2, 1.2),
        "376": (8, 4.8),
        "277": (2, 1.2),
        "280": (6, 9.6),
    },
    "1.9.3": {
        "103": (2, 1.2),
        "141": (3, 4.8),
        "142": (1, 0.6),
        "260": (4, 2.4),
        "282": (6, 7.2),
        "297": (5, 6.0),
        "319": (3, 1.8),
        "320": (8, 12.8),
        "322": (4, 9.6),
        "349": (2, 0.4),
        "350": (5, 6.0),
        "354": (2, 0.4),
        "357": (2, 0.4),
        "363": (3, 1.8),
        "364": (8, 12.8),
        "365": (2, 1.2),
        "366": (6, 7.2),
        "367": (4, 0.8),
        "375": (2, 3.2),
        "393": (5, 7.2),
        "394": (2, 1.2),
        "396": (6, 14.4),
        "400": (8, 4.8),
        "411": (3, 1.8),
        "412": (5, 6.0),
        "413": (10, 12.0),
        "423": (2, 1.2),
        "424": (6, 9.6),
    },
    "1.10": {
        "103": (2, 1.2),
        "141": (3, 4.8),
        "142": (1, 0.6),
        "260": (4, 2.4),
        "282": (6, 7.2),
        "297": (5, 6.0),
        "319": (3, 1.8),
        "320": (8, 12.8),
        "322": (4, 9.6),
        "349": (2, 0.4),
        "350": (5, 6.0),
        "354": (2, 0.4),
        "357": (2, 0.4),
        "363": (3, 1.8),
        "364": (8, 12.8),
        "365": (2, 1.2),
        "366": (6, 7.2),
        "367": (4, 0.8),
        "375": (2, 3.2),
        "393": (5, 7.2),
        "394": (2, 1.2),
        "396": (6, 14.4),
        "400": (8, 4.8),
        "411": (3, 1.8),
        "412": (5, 6.0),
        "413": (10, 12.0),
        "423": (2, 1.2),
        "424": (6, 9.6),
    },
    "1.11.1": {
        "103": (2, 1.2),
        "141": (3, 4.8),
        "142": (1, 0.6),
        "260": (4, 2.4),
        "282": (6, 7.2),
        "297": (5, 6.0),
        "319": (3, 1.8),
        "320": (8, 12.8),
        "322": (4, 9.6),
        "349": (2, 0.4),
        "350": (5, 6.0),
        "354": (2, 0.4),
        "357": (2, 0.4),
        "363": (3, 1.8),
        "364": (8, 12.8),
        "365": (2, 1.2),
        "366": (6, 7.2),
        "367": (4, 0.8),
        "375": (2, 3.2),
        "393": (5, 7.2),
        "394": (2, 1.2),
        "396": (6, 14.4),
        "400": (8, 4.8),
        "411": (3, 1.8),
        "412": (5, 6.0),
        "413": (10, 12.0),
        "423": (2, 1.2),
        "424": (6, 9.6),
    },
    "1.12.2": {
        "103": (2, 1.2),
        "141": (3, 4.8),
        "142": (1, 0.6),
        "260": (4, 2.4),
        "282": (6, 7.2),
        "297": (5, 6.0),
        "319": (3, 1.8),
        "320": (8, 12.8),
        "322": (4, 9.6),
        "349": (2, 0.4),
        "350": (5, 6.0),
        "354": (2, 0.4),
        "357": (2, 0.4),
        "363": (3, 1.8),
        "364": (8, 12.8),
        "365": (2, 1.2),
        "366": (6, 7.2),
        "367": (4, 0.8),
        "375": (2, 3.2),
        "393": (5, 7.2),
        "394": (2, 1.2),
        "396": (6, 14.4),
        "400": (8, 4.8),
        "411": (3, 1.8),
        "412": (5, 6.0),
        "413": (10, 12.0),
        "423": (2, 1.2),
        "424": (6, 9.6),
    },
    "1.13.2": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
    "1.14.3": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
    "1.15.2": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
    "1.16.4": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
    "1.17.1": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
    "1.18.2": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
    "1.19.4": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
    "1.20.3": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
    "1.21.11": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
    "26.1": {
        "364": (2, 1.2),
        "381": (4, 2.4),
        "280": (3, 1.8),
        "370": (6, 7.2),
        "368": (5, 6.0),
        "354": (3, 1.8),
        "365": (2, 0.4),
        "369": (5, 6.0),
        "382": (2, 0.4),
        "391": (6, 6.0),
        "370": (6, 7.2),
        "383": (4, 0.8),
        "395": (2, 3.2),
        "389": (5, 7.2),
        "383": (2, 1.2),
        "384": (8, 4.8),
        "372": (2, 1.2),
        "371": (6, 9.6),
    },
}


# Health / food constants (stable across supported versions; v1_8 representative).
HEALTH_DATA = {
    "1.8": {"MAX_HEALTH": "20.0f", "MAX_FOOD": "20", "MAX_SATURATION": "20.0f",
            "HEALTH_REGEN_PER_TICK": "1.0f", "REGEN_DELAY_TICKS": "80"},
}

# Combat / damage constants (stable across supported versions; v1_8 representative).
COMBAT_DATA = {
    "1.8": {"BASE_ATTACK_DAMAGE": "1.0f", "ATTACK_COOLDOWN_TICKS": "10.0f",
            "FALL_DAMAGE_MULTIPLIER": "1.0f", "ARMOR_TOUGHNESS": "0"},
}

# Hunger / saturation constants -- the values that changed between vanilla
# hunger models (1.8 -> 1.9 -> modern). Per-version; identical defaults below
# until a version-specific override is supplied.
HUNGER_DATA = {
    "1.8": {"MAX_HUNGER": "20", "MAX_SATURATION": "20.0f",
            "HEALTH_REGEN_PER_TICK": "1.0f"},
}

# Movement / jump constants -- the values that changed between vanilla movement
# models (1.8 -> 1.9+ sprint speed, jump boost). Per-version; identical
# defaults below until a version-specific override is supplied.
MOVEMENT_DATA = {
    "1.8": {"WALK_SPEED": "0.2f", "SPRINT_SPEED": "0.3f",
            "JUMP_POWER": "0.4f", "MAX_JUMP_BOOST": "1.0f"},
}


def _emit(path, lines):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines))


def _header_prefix(version_string, protocol):
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    return [
        "// AUTO-GENERATED by tools/gen_version_defaults.py -- DO NOT EDIT.",
        "// Regenerate: python3 tools/gen_version_defaults.py",
        f"// Generated: {ts}",
        f"// Source: PrismarineJS minecraft-data (data/pc/{version_string}/...)",
        f"// Version: {version_string} (protocol {protocol}).",
        "#pragma once",
        "",
    ]


def emit_food_hpp(filename, version_string, protocol):
    """Write include/versions/<filename>/food.hpp."""
    namespace = "V" + filename[1:]
    items = FOOD_DATA.get(version_string)
    lines = _header_prefix(version_string, protocol)
    lines.append(f"namespace LightweightMC::Versions::{namespace}::Food {{")
    lines.append("struct Nutrition { int foodPoints; float saturation; };")
    lines.append("inline Nutrition forItem(int id) {")
    lines.append("    switch (id) {")
    if items:
        for item_id, (food_points, saturation) in sorted(items.items()):
            lines.append(f"        case {item_id}: return {{ {food_points}, {saturation}f }};")
    else:
        lines.append("        // no curated data for this major version yet")
    lines.extend([
        "        default: return {0, 0.0f};",
        "    }",
        "}",
        "}",
        "",
    ])
    _emit(os.path.join(OUT_ROOT, filename, "food.hpp"), lines)
    print(f"[gen_version_defaults] Wrote {os.path.join(OUT_ROOT, filename, 'food.hpp')}")


def emit_health_hpp(filename, version_string, protocol):
    """Write include/versions/<filename>/health.hpp."""
    namespace = "V" + filename[1:]
    kv = HEALTH_DATA.get(version_string, HEALTH_DATA["1.8"])
    lines = _header_prefix(version_string, protocol)
    lines.append(f"namespace LightweightMC::Versions::{namespace} {{")
    for key, val in kv.items():
        type_name = "float" if any(c in val for c in ".fF") else "int"
        lines.append(f"    constexpr {type_name} {key} = {val};")
    lines.append("}")
    lines.append("")
    _emit(os.path.join(OUT_ROOT, filename, "health.hpp"), lines)
    print(f"[gen_version_defaults] Wrote {os.path.join(OUT_ROOT, filename, 'health.hpp')}")


def emit_combat_hpp(filename, version_string, protocol):
    """Write include/versions/<filename>/combat.hpp."""
    namespace = "V" + filename[1:]
    kv = COMBAT_DATA.get(version_string, COMBAT_DATA["1.8"])
    lines = _header_prefix(version_string, protocol)
    lines.append(f"namespace LightweightMC::Versions::{namespace} {{")
    for key, val in kv.items():
        type_name = "float" if any(c in val for c in ".fF") else "int"
        lines.append(f"    constexpr {type_name} {key} = {val};")
    lines.append("}")
    lines.append("")
    _emit(os.path.join(OUT_ROOT, filename, "combat.hpp"), lines)
    print(f"[gen_version_defaults] Wrote {os.path.join(OUT_ROOT, filename, 'combat.hpp')}")


def emit_hunger_hpp(filename, version_string, protocol):
    """Write include/versions/<filename>/hunger.hpp."""
    namespace = "V" + filename[1:]
    kv = HUNGER_DATA.get(version_string, HUNGER_DATA["1.8"])
    lines = _header_prefix(version_string, protocol)
    lines.append(f"namespace LightweightMC::Versions::{namespace} {{")
    for key, val in kv.items():
        type_name = "float" if any(c in val for c in ".fF") else "int"
        lines.append(f"    constexpr {type_name} {key} = {val};")
    lines.append("}")
    lines.append("")
    _emit(os.path.join(OUT_ROOT, filename, "hunger.hpp"), lines)
    print(f"[gen_version_defaults] Wrote {os.path.join(OUT_ROOT, filename, 'hunger.hpp')}")


def emit_movement_hpp(filename, version_string, protocol):
    """Write include/versions/<filename>/movement.hpp."""
    namespace = "V" + filename[1:]
    kv = MOVEMENT_DATA.get(version_string, MOVEMENT_DATA["1.8"])
    lines = _header_prefix(version_string, protocol)
    lines.append(f"namespace LightweightMC::Versions::{namespace} {{")
    for key, val in kv.items():
        type_name = "float" if any(c in val for c in ".fF") else "int"
        lines.append(f"    constexpr {type_name} {key} = {val};")
    lines.append("}")
    lines.append("")
    _emit(os.path.join(OUT_ROOT, filename, "movement.hpp"), lines)
    print(f"[gen_version_defaults] Wrote {os.path.join(OUT_ROOT, filename, 'movement.hpp')}")




def emit_defaults_hpp(directory_name, version_string, protocol):
    """Write include/versions/<directory_name>/defaults.hpp."""
    namespace = "V" + directory_name[1:]  # v1_7 -> V1_7
    filename = os.path.join(OUT_ROOT, directory_name, "defaults.hpp")
    os.makedirs(os.path.dirname(filename), exist_ok=True)

    ts = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    lines = [
        "// AUTO-GENERATED by tools/gen_version_defaults.py -- DO NOT EDIT.",
        "// Regenerate: python3 tools/gen_version_defaults.py",
        f"// Generated: {ts}",
        "// Source: tools/protocol_seed/versions.json (prismarine minecraft-data registry).",
        f"// Version: {version_string} (protocol {protocol}).",
        "#pragma once",
        "",
        f"namespace LightweightMC::Versions::{namespace} {{",
        f"    constexpr int PROTOCOL = {protocol};",
        f"    constexpr int DEFAULT_DIFFICULTY = 1; // easy",
        f"    constexpr int MAX_PLAYERS_DEFAULT = 20;",
        f"    constexpr int VIEW_DISTANCE = 10;",
        f"    constexpr int SPAWN_PROTECTION = 16;",
        f'    constexpr const char* LEVEL_TYPE = "default";',
        "}",
        "",
    ]
    with open(filename, "w") as f:
        f.write("\n".join(lines))
    print(f"[gen_version_defaults] Wrote {filename}")


def main():
    if not os.path.exists(VERSIONS_JSON):
        sys.exit(f"error: missing {VERSIONS_JSON}; run from the repo root")
    protocols = load_protocol_map()

    for directory_name, version_string in MAJOR_VERSIONS:
        if version_string not in protocols:
            sys.exit(
                f"error: version '{version_string}' not found in {VERSIONS_JSON}"
            )
        protocol = protocols[version_string]
        emit_defaults_hpp(directory_name, version_string, protocol)
        emit_food_hpp(directory_name, version_string, protocol)
        emit_health_hpp(directory_name, version_string, protocol)
        emit_combat_hpp(directory_name, version_string, protocol)
        emit_hunger_hpp(directory_name, version_string, protocol)
        emit_movement_hpp(directory_name, version_string, protocol)

    print(
        f"[gen_version_defaults] generated {len(MAJOR_VERSIONS)} version header sets "
        f"under {OUT_ROOT}"
    )


if __name__ == "__main__":
    main()
