from dataclasses import dataclass
from importlib.metadata import version

import capstone
from capstone import CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_LITTLE_ENDIAN, CS_MODE_THUMB, Cs


REQUIRED_CAPSTONE = "5.0.9"
INSTALLED_CAPSTONE = version("capstone")
if INSTALLED_CAPSTONE != REQUIRED_CAPSTONE:
    raise RuntimeError(
        f"Capstone {REQUIRED_CAPSTONE} is required; found {INSTALLED_CAPSTONE}. "
        "Run sdk/tools/bootstrap_deps.py."
    )


class ArmDecodeError(ValueError):
    pass


@dataclass(frozen=True)
class Instruction:
    address: int
    size: int
    mnemonic: str
    operands: str
    bytes: bytes


def disassemble(code, address, mode="arm"):
    if mode not in ("arm", "thumb"):
        raise ArmDecodeError(f"unsupported instruction mode {mode!r}")
    decoder = Cs(CS_ARCH_ARM,
                 (CS_MODE_ARM if mode == "arm" else CS_MODE_THUMB) |
                 CS_MODE_LITTLE_ENDIAN)
    decoded = tuple(Instruction(item.address, item.size, item.mnemonic, item.op_str,
                                bytes(item.bytes))
                    for item in decoder.disasm(bytes(code), address))
    if not decoded or sum(item.size for item in decoded) != len(code):
        consumed = sum(item.size for item in decoded)
        raise ArmDecodeError(f"decoded {consumed} of {len(code)} bytes at 0x{address:08X}")
    return decoded
