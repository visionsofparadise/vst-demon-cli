"""Structural validator for a VST3 .vstpreset file.

Parses the header (VST3 magic, version, 32-char ASCII class id, chunk-list offset)
and the trailing chunk list ('List' id + Comp/Cont entries), asserting the container
is well-formed. Does not load the plugin; this is a pure format check.

Usage:
    check_preset_format.py <path.vstpreset>

Exit 0 when structurally valid, nonzero (with a message on stderr) otherwise.

Format (public.sdk/source/vst/vstpresetfile.h):
    HEADER      'VST3' (4) | version int32 (4) | class id ASCII (32) | chunk-list offset int64 (8)
    DATA AREA   chunk data 1..n
    CHUNK LIST  'List' (4) | entry count int32 (4) | [ id (4) | offset int64 (8) | size int64 (8) ]*
"""
import struct
import sys

HEADER_SIZE = 4 + 4 + 32 + 8


def fail(msg):
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) != 2:
        fail("usage: check_preset_format.py <path.vstpreset>")

    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()

    size = len(data)
    if size < HEADER_SIZE:
        fail(f"file too small ({size} bytes) to hold a {HEADER_SIZE}-byte header")

    magic = data[0:4]
    if magic != b"VST3":
        fail(f"bad magic: expected b'VST3', got {magic!r}")

    (version,) = struct.unpack_from("<i", data, 4)

    class_id = data[8:40]
    try:
        class_id_str = class_id.decode("ascii")
    except UnicodeDecodeError:
        fail(f"class id is not ASCII: {class_id!r}")
    if len(class_id_str) != 32:
        fail(f"class id is not 32 chars: {len(class_id_str)}")
    if not all(c in "0123456789ABCDEFabcdef" for c in class_id_str):
        fail(f"class id is not 32 hex chars: {class_id_str!r}")

    (list_offset,) = struct.unpack_from("<q", data, 40)
    if list_offset < HEADER_SIZE or list_offset > size:
        fail(f"chunk-list offset {list_offset} out of range (file size {size})")

    list_id = data[list_offset:list_offset + 4]
    if list_id != b"List":
        fail(f"chunk list id: expected b'List' at offset {list_offset}, got {list_id!r}")

    (entry_count,) = struct.unpack_from("<i", data, list_offset + 4)
    if entry_count < 1 or entry_count > 128:
        fail(f"implausible entry count: {entry_count}")

    entries = {}
    pos = list_offset + 8
    entry_struct = struct.Struct("<4sqq")
    for i in range(entry_count):
        if pos + entry_struct.size > size:
            fail(f"chunk list entry {i} runs past EOF")
        cid, offset, csize = entry_struct.unpack_from(data, pos)
        pos += entry_struct.size
        cid_str = cid.decode("ascii", "replace")
        if offset < HEADER_SIZE or offset + csize > size:
            fail(f"chunk '{cid_str}' data range [{offset}, {offset + csize}) out of file ({size})")
        entries[cid_str] = (offset, csize)

    if "Comp" not in entries:
        fail(f"missing 'Comp' (component state) chunk; entries: {sorted(entries)}")
    # 'Cont' (controller state) is optional in the format but expected for editor plugins.

    comp_off, comp_size = entries["Comp"]
    cont = entries.get("Cont")
    print(
        f"OK: {path}\n"
        f"  magic=VST3 version={version} class_id={class_id_str}\n"
        f"  chunk-list offset={list_offset} entries={entry_count}\n"
        f"  Comp offset={comp_off} size={comp_size}"
        + (f"  Cont offset={cont[0]} size={cont[1]}" if cont else "  (no Cont chunk)")
    )
    sys.exit(0)


if __name__ == "__main__":
    main()
