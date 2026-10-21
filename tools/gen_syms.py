"""Writes syms/sm64.us.syms.toml and syms/sm64.us.datasyms.toml from the decomp's US ELF with N64Recomp's own
context dump (--dump-context), the way sonicdcer's MarioKart64RecompSyms and Starfox64RecompSyms were made.

The ELF is tools/build_decomp.sh's sm64.us.mdebug.elf: the decomp's own link again, with each object's .mdebug kept
(sm64.ld discards it), so N64Recomp finds the C files' static functions there (use_mdebug): IDO puts no symbol for a
static function in an object's symbol table. N64Recomp reads each section's VRAM from the ELF's section headers and
its ROM offset from the load address sm64.ld gives every segment (AT(__romPos)), so main, engine and goddard come out
at the offsets the ROM has them at.

The decomp's assembly (asm/entry.s, asm/decompress.s, libultra's lib/asm) declares its functions with glabel, which
gives a symbol no type and no size, and N64Recomp only takes a sized function symbol as a function. Each global the
link map lists in a .text of a code section gets a size here (function_sizes): up to the next global symbol or the
end of its file's .text. The static functions' sizes are filled in after the dump (fill_static_sizes).

Usage: py -3 -I tools/gen_syms.py <N64Recomp> <elf> <map>
"""
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# The decomp's entry_point (asm/entry.s), where the boot code jumps after it copies 1 MB of main from ROM 0x1000.
ENTRYPOINT = 0x80246000
SHF_EXECINSTR = 0x4
STT_OBJECT, STT_FUNC, STT_SECTION, STT_FILE = 1, 2, 3, 4
STB_GLOBAL = 1


def read_elf(path):
    """The section headers (name, flags, address, size) and symbols (name, value, size, type, bind, section) of a
    32-bit big-endian ELF."""
    data = Path(path).read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
        sys.exit(f"{path}: not a 32-bit big-endian ELF")
    e_shoff, = struct.unpack_from(">I", data, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    headers = [struct.unpack_from(">IIIIIIIIII", data, e_shoff + i * e_shentsize) for i in range(e_shnum)]

    def string(table, offset):
        start = headers[table][4] + offset
        return data[start:data.index(b"\0", start)].decode()

    sections = [(string(e_shstrndx, h[0]), h[2], h[3], h[5]) for h in headers]
    symbols = []
    for h in headers:
        if h[1] != 2:  # SHT_SYMTAB
            continue
        for i in range(h[5] // 16):
            st_name, st_value, st_size, st_info, _, st_shndx = struct.unpack_from(">IIIBBH", data, h[4] + i * 16)
            symbols.append((string(h[6], st_name), st_value, st_size, st_info & 0xF, st_info >> 4, st_shndx))
    return sections, symbols


def text_inputs(map_path):
    """Every input .text section the link map lists, as (start, end, object, the global symbols it defines)."""
    inputs = []
    lines = Path(map_path).read_text(errors="replace").splitlines()
    i = 0
    while i < len(lines):
        m = re.match(r"^ \.text\S*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", lines[i])
        if not m and re.match(r"^ \.text\S*$", lines[i]) and i + 1 < len(lines):
            i += 1
            m = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", lines[i])
        i += 1
        if not m:
            continue
        start, size = int(m.group(1), 16) & 0xFFFFFFFF, int(m.group(2), 16)
        names = []
        while i < len(lines) and (sym := re.match(r"^\s+0x[0-9a-f]+\s+(\w+)$", lines[i])):
            names.append(sym.group(1))
            i += 1
        if size:
            inputs.append((start, start + size, m.group(3), names))
    return sorted(inputs)


def asm_function_sizes(sections, symbols, inputs):
    """The glabel functions in the code sections, each sized to the next global symbol or its .text's end."""
    sizes = {}
    for index, (_, flags, address, size) in enumerate(sections):
        if not flags & SHF_EXECINSTR or address < 0x80000000:
            continue
        in_section = [s for s in symbols if s[5] == index and s[3] not in (STT_SECTION, STT_FILE)]
        bounds = sorted({s[1] for s in in_section if s[4] == STB_GLOBAL or (s[3] == STT_FUNC and s[2])})
        by_name = {s[0]: s for s in in_section if s[4] == STB_GLOBAL}
        for start, end, obj, names in inputs:
            # lib/rsp.o's .text is the RSP microcodes the game uploads, data to the CPU.
            if not address <= start < address + size or obj.endswith("/rsp.o"):
                continue
            for name in names:
                sym = by_name.get(name)
                if sym is None or sym[2] or sym[3] == STT_FUNC:
                    continue
                sizes[name] = min([b for b in bounds if b > sym[1]] + [end]) - sym[1]
    return sizes


def fill_static_sizes(dump, inputs):
    """N64Recomp's .mdebug reader keeps a static function's size only when its end record comes right after it, and in
    the decomp's -g objects a function's parameters and locals come between, so the dump has those at size 0. Each
    gets the room up to the next function or the end of its file's .text, which is where IDO put it."""
    starts = sorted({int(v, 16) for v in re.findall(r"vram = (0x[0-9A-F]+), size", dump)})

    def sized(m):
        vram = int(m.group(2), 16)
        end = next((e for s, e, _, _ in inputs if s <= vram < e), None)
        if end is None:
            sys.exit(f"{m.group(1)} at 0x{vram:08X} is in no .text the map lists")
        end = min([s for s in starts if s > vram] + [end])
        return f'{{ name = "{m.group(1)}", vram = {m.group(2)}, size = 0x{end - vram:X} }}'

    return re.sub(r'\{ name = "(\w+)", vram = (0x[0-9A-F]+), size = 0x0 \}', sized, dump)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip().splitlines()[-1])
    n64recomp, elf, map_path = (Path(a).resolve() for a in sys.argv[1:])
    sections, symbols = read_elf(elf)
    inputs = text_inputs(map_path)
    sizes = asm_function_sizes(sections, symbols, inputs)
    entries = "".join(f'    {{ name = "{n}", size = 0x{s:X} }},\n' for n, s in sorted(sizes.items()))
    with tempfile.TemporaryDirectory() as tmp:
        config = Path(tmp) / "context.toml"
        config.write_text(
            "[input]\n"
            f"entrypoint = 0x{ENTRYPOINT:08X}\n"
            f'elf_path = "{elf.as_posix()}"\n'
            'output_func_path = "out"\n'
            # The decomp's bss sections are named <segment>.noload (sm64.ld).
            'bss_section_suffix = ".noload"\n'
            "use_mdebug = true\n"
            f"function_sizes = [\n{entries}]\n")
        run = subprocess.run([str(n64recomp), config.name, "--dump-context"], cwd=tmp, capture_output=True, text=True)
        if run.returncode != 0:
            sys.exit((run.stdout + run.stderr)[-3000:])
        out = ROOT / "syms"
        out.mkdir(exist_ok=True)
        dump = fill_static_sizes((Path(tmp) / "dump.toml").read_text(), inputs)
        (out / "sm64.us.syms.toml").write_text(dump, newline="\n")
        (out / "sm64.us.datasyms.toml").write_text((Path(tmp) / "data_dump.toml").read_text(), newline="\n")
    print(f"{len(sizes)} assembly functions sized; wrote syms/sm64.us.syms.toml and syms/sm64.us.datasyms.toml")


if __name__ == "__main__":
    main()
