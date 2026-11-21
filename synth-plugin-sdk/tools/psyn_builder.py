#!/usr/bin/env python3
"""
Polyend Community Edition .psyn Binary Packager (psyn_builder.py)

Links or converts an ARM Cortex-M7 ELF image (linked at base address 0x00000000
with -Wl,--emit-relocs) into a self-contained relocatable .psyn binary for SD card
/Synths/*.psyn.

Supports:
  - R_ARM_ABS32 (2) and R_ARM_TARGET1 (38)
  - R_ARM_THM_MOVW_ABS_NC (47) and R_ARM_THM_MOVT_ABS (48)
  - .init_array C++ static constructors
"""

import argparse
import struct
import sys

PSYN_MAGIC = 0x4E595350  # "PSYN"
PSYN_API_VERSION = 1

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_RELA = 4
SHT_NOBITS = 8
SHT_REL = 9
SHT_INIT_ARRAY = 14
SHF_ALLOC = 0x2

R_ARM_NONE = 0
R_ARM_ABS32 = 2
R_ARM_REL32 = 3
R_ARM_THM_CALL = 10
R_ARM_THM_JUMP24 = 30
R_ARM_TARGET1 = 38
R_ARM_V4BX = 40
R_ARM_PREL31 = 42
R_ARM_THM_MOVW_ABS_NC = 47
R_ARM_THM_MOVT_ABS = 48
R_ARM_THM_JUMP19 = 51

RELOC_TAG_ABS32 = 0x00000000
RELOC_TAG_MOVW = 0x40000000
RELOC_TAG_MOVT = 0x80000000


def align_up(val: int, align: int = 8) -> int:
    return (val + align - 1) & ~(align - 1)


def decode_thumb_movw_movt_imm16(hw1: int, hw2: int) -> int:
    imm4 = hw1 & 0x000F
    i = (hw1 >> 10) & 0x1
    imm3 = (hw2 >> 12) & 0x7
    imm8 = hw2 & 0x00FF
    return (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8


def parse_elf_and_pack(
    elf_path: str,
    out_path: str,
    code_name: str,
    display_name: str,
    patch_folder: str,
    type_id_wave: int,
    type_id_play_tracker: int,
    default_voices: int,
    max_voices: int,
    output_channels: int,
    renders_aux_sends: int,
    has_sample_slots: int = 0,
    audio_input: int = 0,
    supports_idle_sleep: int = 0,
    has_voice_tick_idle: int = 0,
) -> None:
    with open(elf_path, "rb") as f:
        elf = f.read()

    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise RuntimeError(f"{elf_path}: Not a 32-bit little-endian ELF file")

    (
        e_type,
        e_machine,
        e_version,
        e_entry,
        e_phoff,
        e_shoff,
        e_flags,
        e_ehsize,
        e_phentsize,
        e_phnum,
        e_shentsize,
        e_shnum,
        e_shstrndx,
    ) = struct.unpack_from("<HHIIIIIHHHHHH", elf, 16)

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sh = struct.unpack_from("<IIIIIIIIII", elf, off)
        sections.append(
            {
                "index": i,
                "name_off": sh[0],
                "type": sh[1],
                "flags": sh[2],
                "addr": sh[3],
                "offset": sh[4],
                "size": sh[5],
                "link": sh[6],
                "info": sh[7],
                "addralign": sh[8],
                "entsize": sh[9],
            }
        )

    shstrtab = elf[
        sections[e_shstrndx]["offset"] : sections[e_shstrndx]["offset"]
        + sections[e_shstrndx]["size"]
    ]
    for s in sections:
        end = shstrtab.find(b"\x00", s["name_off"])
        s["name"] = shstrtab[s["name_off"] : end].decode("ascii", errors="replace")

    sec_by_name = {s["name"]: s for s in sections}

    # Determine total memory span of allocated sections
    alloc_sections = [
        s
        for s in sections
        if (s["flags"] & SHF_ALLOC)
        and s["size"] > 0
        and not s["name"].startswith(".ARM.exidx")
    ]
    if not alloc_sections:
        raise RuntimeError("No SHF_ALLOC sections found in ELF")

    code_sec = sec_by_name.get(".text")
    rodata_sec = sec_by_name.get(".rodata")
    data_sec = sec_by_name.get(".data")
    bss_sec = sec_by_name.get(".bss")
    init_array_sec = sec_by_name.get(".init_array")

    code_size = align_up(code_sec["addr"] + code_sec["size"], 8) if code_sec else 0
    rodata_end = (
        align_up(rodata_sec["addr"] + rodata_sec["size"], 8)
        if (rodata_sec and rodata_sec["size"] > 0)
        else code_size
    )
    rodata_size = max(0, rodata_end - code_size)

    data_end = rodata_end
    for sec in (init_array_sec, data_sec):
        if sec and sec["size"] > 0:
            data_end = max(data_end, align_up(sec["addr"] + sec["size"], 8))
    data_size = max(0, data_end - rodata_end)

    bss_end = data_end
    if bss_sec and bss_sec["size"] > 0:
        bss_end = max(bss_end, align_up(bss_sec["addr"] + bss_sec["size"], 8))
    bss_size = max(0, bss_end - data_end)

    loaded_image_size = code_size + rodata_size + data_size
    total_mem_size = loaded_image_size + bss_size

    image = bytearray(loaded_image_size)
    alloc_sec_indices = set()
    for s in sections:
        if (
            (s["flags"] & SHF_ALLOC)
            and s["type"] in (SHT_PROGBITS, SHT_INIT_ARRAY)
            and s["size"] > 0
            and not s["name"].startswith(".ARM.exidx")
        ):
            start = s["addr"]
            end = start + s["size"]
            if end > loaded_image_size:
                image.extend(b"\x00" * (align_up(end, 8) - len(image)))
                data_size = len(image) - (code_size + rodata_size)
                loaded_image_size = len(image)
                total_mem_size = loaded_image_size + bss_size
            image[start:end] = elf[s["offset"] : s["offset"] + s["size"]]
            alloc_sec_indices.add(s["index"])
        elif (s["flags"] & SHF_ALLOC) and s["type"] == SHT_NOBITS and s["size"] > 0:
            alloc_sec_indices.add(s["index"])

    # Parse symbol table to locate psyn_plugin_entry and check SHN_UNDEF symbols
    symtab_sec = next((s for s in sections if s["type"] == SHT_SYMTAB), None)
    if not symtab_sec:
        raise RuntimeError("No .symtab found in ELF")
    strtab_sec = sections[symtab_sec["link"]]
    strtab = elf[strtab_sec["offset"] : strtab_sec["offset"] + strtab_sec["size"]]

    symbols = []
    num_syms = symtab_sec["size"] // 16
    entry_offset = None
    for i in range(num_syms):
        soff = symtab_sec["offset"] + i * 16
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
            "<IIIBBH", elf, soff
        )
        end = strtab.find(b"\x00", st_name)
        name = strtab[st_name:end].decode("ascii", errors="replace")
        symbols.append(
            {
                "name": name,
                "value": st_value,
                "size": st_size,
                "info": st_info,
                "shndx": st_shndx,
            }
        )
        if name == "psyn_plugin_entry":
            entry_offset = st_value | 1  # Ensure Thumb bit is set

    if entry_offset is None:
        raise RuntimeError("psyn_plugin_entry symbol not found in ELF")

    # Collect relocations in allocated sections
    reloc_entries = []
    seen_abs32 = set()
    seen_movw_movt = set()

    for s in sections:
        if s["type"] not in (SHT_REL, SHT_RELA):
            continue
        target_sec_idx = s["info"]
        if target_sec_idx not in alloc_sec_indices:
            continue

        ent_size = 8 if s["type"] == SHT_REL else 12
        count = s["size"] // ent_size
        for i in range(count):
            roff = s["offset"] + i * ent_size
            r_offset, r_info = struct.unpack_from("<II", elf, roff)
            r_type = r_info & 0xFF
            r_sym = r_info >> 8

            if r_type == R_ARM_NONE or r_type in (
                R_ARM_THM_CALL,
                R_ARM_THM_JUMP24,
                R_ARM_THM_JUMP19,
                R_ARM_REL32,
                R_ARM_PREL31,
                R_ARM_V4BX,
            ):
                continue

            sym = symbols[r_sym] if r_sym < len(symbols) else None
            if sym and sym["shndx"] == 0:
                # Weak undefined symbol resolved to 0 (NULL) - do NOT relocate
                continue

            if r_type in (R_ARM_ABS32, R_ARM_TARGET1):
                if r_offset + 4 <= len(image) and r_offset not in seen_abs32:
                    seen_abs32.add(r_offset)
                    reloc_entries.append(RELOC_TAG_ABS32 | r_offset)
            elif r_type in (R_ARM_THM_MOVW_ABS_NC, R_ARM_THM_MOVT_ABS):
                if r_offset + 4 <= len(image) and (r_offset, r_type) not in seen_movw_movt:
                    seen_movw_movt.add((r_offset, r_type))
                    tag = RELOC_TAG_MOVW if r_type == R_ARM_THM_MOVW_ABS_NC else RELOC_TAG_MOVT
                    target_addr = sym["value"] if sym else 0
                    if r_type == R_ARM_THM_MOVW_ABS_NC:
                        hw1, hw2 = struct.unpack_from("<HH", image, r_offset)
                        imm16 = decode_thumb_movw_movt_imm16(hw1, hw2)
                        target_addr = (target_addr & 0xFFFF0000) | imm16
                    reloc_entries.append(tag | r_offset)
                    reloc_entries.append(target_addr & 0xFFFFFFFF)
            else:
                raise RuntimeError(
                    f"Unsupported ELF relocation type {r_type} at offset {hex(r_offset)} "
                    f"(symbol: {sym['name'] if sym else '?'})"
                )

    init_array_offset = init_array_sec["addr"] if (init_array_sec and init_array_sec["size"] > 0) else 0
    init_array_count = (init_array_sec["size"] // 4) if (init_array_sec and init_array_sec["size"] > 0) else 0

    flags = (
        (0x0001 if has_sample_slots else 0)
        | (0x0002 if supports_idle_sleep else 0)
        | (0x0004 if has_voice_tick_idle else 0)
        | (0x0008 if audio_input else 0)
    )
    header = struct.pack(
        "<IHHHH8s24s16sBBBBIIIIIIII8I",
        PSYN_MAGIC,
        PSYN_API_VERSION,
        flags,
        type_id_wave,
        type_id_play_tracker,
        code_name.encode("ascii")[:7].ljust(8, b"\x00"),
        display_name.encode("ascii")[:23].ljust(24, b"\x00"),
        patch_folder.encode("ascii")[:15].ljust(16, b"\x00"),
        default_voices,
        max_voices,
        output_channels,
        renders_aux_sends,
        code_size,
        rodata_size,
        data_size,
        bss_size,
        entry_offset,
        init_array_offset,
        init_array_count,
        len(reloc_entries),
        0, 0, 0, 0, 0, 0, 0, 0,
    )
    assert len(header) == 128

    reloc_bytes = struct.pack(f"<{len(reloc_entries)}I", *reloc_entries)

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(image)
        f.write(reloc_bytes)

    print(
        f"[psyn_builder] Built {out_path}: code={code_size}B rodata={rodata_size}B "
        f"data={data_size}B bss={bss_size}B relocs={len(reloc_entries)} "
        f"total_file={len(header) + len(image) + len(reloc_bytes)}B "
        f"ram_footprint={total_mem_size}B"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack ARM ELF into Polyend .psyn plugin")
    parser.add_argument("--elf", required=True, help="Input ELF file linked at 0x0 with --emit-relocs")
    parser.add_argument("--out", required=True, help="Output .psyn file")
    parser.add_argument("--code-name", required=True, help="Short engine name (e.g. ACD, CHIP)")
    parser.add_argument("--display-name", required=True, help="Display name (e.g. Chiptune)")
    parser.add_argument("--patch-folder", default="", help="Patch folder name (defaults to code-name)")
    parser.add_argument("--type-id-wave", type=int, default=0, help="InstrumentTypeId on Polyend Synth")
    parser.add_argument("--type-id-play-tracker", type=int, default=0, help="InstrumentTypeId on Play/Tracker")
    parser.add_argument("--default-voices", type=int, default=4, help="Default polyphony")
    parser.add_argument("--max-voices", type=int, default=8, help="Max polyphony")
    parser.add_argument("--output-channels", type=int, default=2, help="1=Mono, 2=Stereo")
    parser.add_argument("--renders-aux", type=int, default=0, help="1 if engine writes aux sends")
    parser.add_argument("--has-sample-slots", type=int, default=0, help="1 if engine uses sample slots")
    parser.add_argument("--audio-input", type=int, default=0, help="1 if engine consumes host audio input")
    parser.add_argument(
        "--supports-idle-sleep",
        type=int,
        default=0,
        help="1 if engine supports Voice Sleep (idle voice sleep when On)",
    )
    parser.add_argument(
        "--has-voice-tick-idle",
        type=int,
        default=0,
        help="1 if PsynPluginApi appends voice_tick_idle",
    )
    args = parser.parse_args()

    patch_folder = args.patch_folder if args.patch_folder else args.code_name
    parse_elf_and_pack(
        elf_path=args.elf,
        out_path=args.out,
        code_name=args.code_name,
        display_name=args.display_name,
        patch_folder=patch_folder,
        type_id_wave=args.type_id_wave,
        type_id_play_tracker=args.type_id_play_tracker,
        default_voices=args.default_voices,
        max_voices=args.max_voices,
        output_channels=args.output_channels,
        renders_aux_sends=args.renders_aux,
        has_sample_slots=args.has_sample_slots,
        audio_input=args.audio_input,
        supports_idle_sleep=args.supports_idle_sleep,
        has_voice_tick_idle=args.has_voice_tick_idle,
    )


if __name__ == "__main__":
    main()
