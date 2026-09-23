import struct, sys, zlib, os, re
def b2str(bytes):
    return "".join(map(chr, bytes))

def r32(data, pos):
    try:
        return struct.unpack('<I', data.encode('latin-1')[pos:pos+4])[0]
    except:
        return struct.unpack('<I', data[pos:pos+4])[0]

def insertreplace(data, to_insert, addr):
    try:
        return data[0:addr] + to_insert + data[addr+len(to_insert):]
    except:
        return data[0:addr] + bytes(to_insert, encoding='latin-1') + data[addr+len(to_insert):]

def readbytes(path):
    try:
        return open(path, 'r', encoding='latin-1', newline="").read().encode('latin-1')
    except TypeError:
        return open(path, 'rb').read()

def require_match(data, signature, label):
    address = data.find(signature)
    if address < 0:
        raise RuntimeError("Couldn't find %s; refusing to patch this code.bin." % label)
    return address

def arm_b(site, target):
    distance = target - (site + 8)
    if distance & 3 or distance < -0x2000000 or distance >= 0x2000000:
        raise RuntimeError("ARM branch target is out of range or unaligned")
    return struct.pack('<I', 0xEA000000 | ((distance >> 2) & 0x00FFFFFF))

rf_sig = b2str([0x02, 0x10, 0xD0, 0xE5, 0x04, 0x00, 0x51, 0xE3, 0x05, 0x00, 0x00, 0x3A, 0x0C, 0x10, 0x90, 0xE5, 0x04, 0x00, 0x90, 0xE5, 0x00, 0x10, 0x41, 0xE0])
rf_alloc_sig = b2str([0x1C, 0x00, 0x90, 0xE5, 0x7F, 0x00, 0x80, 0xE2, 0x7F, 0x10, 0xC0, 0xE3, 0xE8, 0x06, 0x9D, 0xE5])
ls_sig = b2str([0x00, 0x50, 0xA0, 0xE1, 0x44, 0x00, 0x9D, 0xE5, 0x00, 0x40, 0xA0, 0xE3, 0x00, 0x00, 0x50, 0xE3])
ls_alloc_sig = b2str([0x44, 0x00, 0x9D, 0xE5, 0x80, 0x20, 0xA0, 0xE3, 0x00, 0x02, 0xA0, 0xE1, 0x7F, 0x00, 0x80, 0xE2])
thread_sig = b2str([0x08, 0xD0, 0x4D, 0xE2, 0x00, 0x60, 0xA0, 0xE1, 0x04, 0x00, 0x92, 0xE5])
norm_sig = b2str([0x05, 0x20, 0xA0, 0xE1, 0x07, 0x10, 0xA0, 0xE1, 0x06, 0x00, 0xA0, 0xE1, 0x03, 0x00, 0x00, 0x9A])
bgm_sig = "%s/snd_bgm_%s.nus3bank"

bgm_site = [0xe1a03000, 0xe59f20b0, 0xe28f10b0, 0xe1a00005]
bgm_tail = [0xe28dd008, 0xe8bd8070]
bgm_hook_offs = -0xC0
bgm_tail_offs = 0x8C

# Code the hooks reach with a bl or a b lives in the dead per-fighter stub array.
ISLAND_OFFS = 0x3C4
ISLAND_SIZE = 0x400
ISLAND_SDBGM = 0x1E0
ISLAND_HOOKS = 0x220
BASE = 0x100000

def armips_value(name):
    src = open('common.armips.asm', encoding='latin-1').read()
    pattern = r"^%s equ \((0x[0-9a-fA-F]+)\)$" % re.escape(name)
    match = re.search(pattern, src, re.MULTILINE)
    if not match:
        raise RuntimeError("common.armips.asm has no %s; run scan.py first." % name)
    return int(match.group(1), 16)

def island_base():
    return armips_value("cro_fighter_new") - BASE + ISLAND_OFFS

#Make this compatible with Python 2 and 3
try:
    f = open(sys.argv[1], 'r', encoding='latin-1', newline="").read()
except TypeError:
    f = open(sys.argv[1], 'rb').read()

rf_alloc = readbytes("bin/incalloc.bin")
rf_hook = readbytes("bin/hookresource.bin")
#ls_alloc = readbytes("bin/inclsalloc.bin")
#ls_hook = readbytes("bin/hookls.bin")
thread_hook = readbytes("bin/hookthread.bin")
norm_hook = readbytes("bin/hooknorm.bin")
sdbgm = readbytes("bin/island_sdbgm.bin")

bgm_str_addr = f.find(bgm_sig)
bgm_hook_addr = bgm_str_addr + bgm_hook_offs

rf_hook_addr = require_match(f, rf_sig, "resource-file hook")
rf_alloc_addr = require_match(f, rf_alloc_sig, "resource-file allocation hook")
ls_hook_addr = require_match(f, ls_sig, "LS hook") + 4
ls_alloc_addr = require_match(f, ls_alloc_sig, "LS allocation hook")
thread_hook_addr = require_match(f, thread_sig, "thread loader hook")
norm_hook_addr = require_match(f, norm_sig, "normal loader hook")
menu_hook_site_addr = armips_value("menu_hook_site") - BASE
menu_hook_word = r32(f, menu_hook_site_addr)
if menu_hook_word != 0xE5902040:
    raise RuntimeError("Menu export has an unknown displaced instruction")

sdbgm_addr = island_base() + ISLAND_SDBGM
menu_tramp_addr = island_base() + ISLAND_HOOKS + 0xC

if ISLAND_SDBGM + len(sdbgm) > ISLAND_HOOKS:
    raise RuntimeError("The BGM payload does not fit in the island.")

# Just convert f to bytes now that we're done searching things.
try:
    f = f.encode('latin-1')
except:
    f = f

w = open(os.path.splitext(sys.argv[1])[0]+"_saltysd.bin", 'w+b')
f = insertreplace(f,rf_hook,rf_hook_addr)
f = insertreplace(f,rf_alloc,rf_alloc_addr)
#f = insertreplace(f,ls_hook,ls_hook_addr)
#f = insertreplace(f,ls_alloc,ls_alloc_addr)
f = insertreplace(f,thread_hook,thread_hook_addr)
f = insertreplace(f,norm_hook,norm_hook_addr)

f = insertreplace(f, arm_b(menu_hook_site_addr, menu_tramp_addr),
                  menu_hook_site_addr)

if r32(f, norm_hook_addr-0x58+3) & 0xFF != 0x9A:
    print("It seems Shiny Quagsire was wrong to assume this address shift would always work.")
    w.write(f)
    exit(0)

f = insertreplace(f,b2str([0xEA]),norm_hook_addr-0x58+3)

f = insertreplace(f,sdbgm,sdbgm_addr)

def words_match(addr, words):
    return all(r32(f, addr + i*4) == word for i, word in enumerate(words))

# A version whose path builder isn't the one island_sdbgm.s was written against
# loses BGM override rather than taking a branch into the middle of something
# else. Pre-update versions (1.0.1, Demo) land here too.
if(bgm_str_addr < 0 or not words_match(bgm_hook_addr, bgm_site)
   or not words_match(bgm_hook_addr+bgm_tail_offs, bgm_tail)):
    print("The BGM path builder isn't the one this hook was written against.\nSound override is not supported with this version.")
    w.write(f)
    exit(0)

bl_offs = ((sdbgm_addr - bgm_hook_addr) - 0x8) >> 2
f = insertreplace(f,struct.pack('<I', 0xEB000000 | (bl_offs & 0xFFFFFF)),bgm_hook_addr)
w.write(f)
