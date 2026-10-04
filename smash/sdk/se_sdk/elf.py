from io import BytesIO
import struct

import elftools
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile


REQUIRED_PYELFTOOLS = "0.33"
if elftools.__version__ != REQUIRED_PYELFTOOLS:
    raise RuntimeError(
        f"pyelftools {REQUIRED_PYELFTOOLS} is required; found {elftools.__version__}. "
        "Run sdk/tools/bootstrap_deps.py."
    )


class ElfFormatError(ValueError):
    pass


def _align4(value):
    return (value + 3) & ~3


def _notes(data, index, owner):
    notes = {}
    at = 0
    while at + 12 <= len(data):
        namesz, descsz, kind = struct.unpack_from("<III", data, at)
        name_at = at + 12
        desc_at = name_at + _align4(namesz)
        if desc_at > len(data) or descsz > len(data) - desc_at:
            raise ElfFormatError(f"note in segment {index} is damaged")
        if data[name_at:name_at + namesz] == owner:
            if kind in notes:
                raise ElfFormatError(f"more than one SaltySD note of type {kind}")
            notes[kind] = data[desc_at:desc_at + descsz]
        at = desc_at + _align4(descsz)
    return notes


def read(data, note_owner):
    try:
        elf = ELFFile(BytesIO(data))
    except (ELFError, OSError, struct.error, TypeError, ValueError) as exc:
        if len(data) < 4 or data[:4] != b"\x7fELF":
            raise ElfFormatError("not an ELF file") from exc
        raise ElfFormatError(f"ELF header is damaged: {exc}") from exc
    if elf.elfclass != 32 or not elf.little_endian:
        raise ElfFormatError("not a 32-bit little-endian ELF")
    if elf.header["e_machine"] != "EM_ARM":
        raise ElfFormatError("not an ARM ELF")
    if elf.header["e_type"] != "ET_EXEC":
        raise ElfFormatError("not an executable ELF; link it (ET_EXEC) instead of passing an object file")

    segments = []
    notes = {}
    try:
        for index, segment in enumerate(elf.iter_segments()):
            header = segment.header
            offset = int(header["p_offset"])
            filesz = int(header["p_filesz"])
            memsz = int(header["p_memsz"])
            vaddr = int(header["p_vaddr"])
            if offset > len(data) or filesz > len(data) - offset:
                raise ElfFormatError(f"segment {index} runs past the end of the file")
            payload = data[offset:offset + filesz]
            if header["p_type"] == "PT_LOAD":
                if filesz != memsz:
                    raise ElfFormatError(
                        f"segment at 0x{vaddr:08X} has {memsz - filesz} bytes of .bss; "
                        "SEA 1.2 can only write bytes that are in the file"
                    )
                segments.append((vaddr, payload))
            elif header["p_type"] == "PT_NOTE":
                for kind, value in _notes(payload, index, note_owner).items():
                    if kind in notes:
                        raise ElfFormatError(f"more than one SaltySD note of type {kind}")
                    notes[kind] = value
    except (ELFError, OSError, struct.error) as exc:
        raise ElfFormatError(f"program headers are missing or damaged: {exc}") from exc
    return segments, notes
