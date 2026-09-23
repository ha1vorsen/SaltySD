from __future__ import print_function
import struct, sys, zlib, os
def b2str(bytes):
    return "".join(map(chr, bytes))
    
def r32(data, pos):
    try:
        return struct.unpack('<I', data.encode('latin-1')[pos:pos+4])[0]
    except:
        return struct.unpack('<I', data[pos:pos+4])[0]

def arm_branch_target(data, pos):
    word = r32(data, pos)
    if ((word >> 25) & 0x7) != 0x5:
        raise ValueError("expected ARM branch at file offset " + hex(pos))
    imm = word & 0x00FFFFFF
    if imm & 0x00800000:
        imm -= 0x01000000
    return 0x100000 + pos + 8 + (imm << 2)

crc_sig = b2str([0xD0, 0x10, 0xD0, 0xE1, 0x00, 0x20, 0xE0, 0xE3, 0x00, 0x00, 0x51, 0xE3, 0x30, 0x30, 0x9F, 0x15])
mount_sdmc_sig = b2str([0x38, 0x40, 0x2D, 0xE9, 0x00, 0x40, 0xA0, 0xE1, 0x09, 0x10, 0xA0, 0xE3, 0x0D, 0x00, 0xA0, 0xE1])
unmount_path_sig = b2str([0x30, 0x40, 0x2D, 0xE9, 0x0C, 0xD0, 0x4D, 0xE2, 0x00, 0x20, 0xA0, 0xE3, 0x0D, 0x30, 0xA0, 0xE1])
IFile_Init_sig = b2str([0x10, 0x40, 0x2D, 0xE9, 0x00, 0x40, 0xA0, 0xE1, 0x54, 0x00, 0x9F, 0xE5, 0x00, 0x00, 0x84, 0xE5, 0x1C, 0x00, 0xA0, 0xE3])
IFile_Open_sig = b2str([0xF0, 0x47, 0x2D, 0xE9, 0x86, 0xDF, 0x4D, 0xE2, 0x02, 0x40, 0xA0, 0xE1, 0x01, 0x60, 0xA0, 0xE1, 0x04, 0x50, 0x90, 0xE5])
IFile_GetSize_sig = b2str([0x30, 0x40, 0x2D, 0xE9, 0x0C, 0xD0, 0x4D, 0xE2, 0x04, 0x40, 0x90, 0xE5])
IFile_Read_sig = b2str([0xF8, 0x40, 0x2D, 0xE9, 0x03, 0x50, 0xA0, 0xE1, 0x01, 0x70, 0xA0, 0xE1, 0x04, 0x40, 0x90, 0xE5, 0x02, 0x60, 0xA0, 0xE1])
IFile_Close_sig = b2str([0x70, 0x40, 0x2D, 0xE9, 0x00, 0x60, 0xA0, 0xE1, 0x04, 0x40, 0x90, 0xE5, 0x44, 0x00, 0x9F, 0xE5, 0x00, 0x00, 0x54, 0xE3, 0x00, 0x00, 0x86, 0xE5, 0x0C, 0x00, 0x00, 0x0A, 0x04, 0x50, 0xA0, 0xE1])
strcat_sig = b2str([0x42, 0x1E, 0x52, 0x1C, 0x13, 0x78, 0x00, 0x2B, 0xFB, 0xD1, 0x0B, 0x78, 0x49, 0x1C, 0x13, 0x70, 0x52, 0x1C, 0x00, 0x2B])
strcpy_sig = b2str([0x01, 0x30, 0x80, 0xE1, 0x00, 0x20, 0xA0, 0xE1, 0x03, 0x00, 0x13, 0xE3, 0x50, 0xC0, 0x9F, 0x05, 0x04, 0xE0, 0x2D, 0xE5, 0x09, 0x00, 0x00, 0x1A, 0x04, 0x30, 0x91, 0xE4, 0xF3, 0xEF, 0x6C, 0xE6])
strlen_sig = b2str([0x01, 0x30, 0x80, 0xE2, 0x03, 0x00, 0x00, 0xEA, 0x01, 0x10, 0xD0, 0xE4, 0x00, 0x00, 0x51, 0xE3, 0x03, 0x00, 0x40, 0x00, 0x1E, 0xFF, 0x2F, 0x01, 0x03, 0x00, 0x10, 0xE3, 0x38, 0x20, 0x9F, 0x05])
resalloc_sig = b2str([0xFF, 0x4F, 0x2D, 0xE9, 0x44, 0xD0, 0x4D, 0xE2, 0x01, 0x30, 0xA0, 0xE1, 0x00, 0x40, 0xA0, 0xE1])
path_str_sig = b2str([0xF0, 0x41, 0x2D, 0xE9, 0x20, 0xD0, 0x4D, 0xE2, 0x00, 0x80, 0xA0, 0xE3, 0xB0, 0x20, 0xD1, 0xE1])
res_deallocate_sig = b2str([0x00, 0x00, 0xA0, 0xE1, 0x00, 0x00, 0x51, 0xE3, 0x1E, 0xFF, 0x2F, 0x01])
idk_sig = b2str([0x00, 0x20, 0x91, 0xE5, 0x02, 0x28, 0xA0, 0xE1, 0x22, 0x28, 0xB0, 0xE1, 0x0C, 0x00, 0x00, 0x0A])
referenced_by_ls_init_sig = b2str([0x03, 0x00, 0x2D, 0xE9, 0x00, 0x10, 0xA0, 0xE3, 0xB0, 0x10, 0xC0, 0xE1, 0xB4, 0x20, 0xDD, 0xE1])
read_dtls_sig = b2str([0xF0, 0x47, 0x2D, 0xE9, 0x00, 0x60, 0xA0, 0xE1, 0x8A, 0xDF, 0x4D, 0xE2, 0x01, 0x40, 0xA0, 0xE1])
liballoc_sig = b2str([0x70, 0x40, 0x2D, 0xE9, 0x00, 0x40, 0xA0, 0xE1, 0x38, 0x60, 0x9F, 0xE5, 0x04, 0x50, 0xA0, 0xE3, 0x00, 0x10, 0xA0, 0xE1, 0x05, 0x20, 0xA0, 0xE1, 0x0C, 0x00, 0x96, 0xE5])
libdealloc_sig = b2str([0x00, 0x00, 0x50, 0xE3, 0x03, 0x00, 0x00, 0x0A, 0x00, 0x10, 0xA0, 0xE1, 0x08, 0x00, 0x9F, 0xE5, 0x0C, 0x00, 0x90, 0xE5])
memcpy_sig = b2str([0x03, 0x00, 0x52, 0xE3, 0x17, 0x00, 0x00, 0x9A, 0x03, 0xC0, 0x10, 0xE2, 0x08, 0x00, 0x00, 0x0A])
memmove_sig = b2str([0x03, 0x00, 0x52, 0xE3, 0x02, 0x00, 0x80, 0xE0, 0x02, 0x10, 0x81, 0xE0])
memclr_sig = b2str([0x00, 0x20, 0xA0, 0xE3, 0x04, 0x00, 0x51, 0xE3, 0x07, 0x00, 0x00, 0x3A])
strcmp_sig = b2str([0x03, 0x00, 0x10, 0xE3, 0x03, 0x00, 0x11, 0x03, 0x1E, 0x00, 0x00, 0x1A])
crit_this_sig = b2str([0x90, 0x00, 0x9F, 0xE5, 0x10, 0x40, 0x2D, 0xE9])
crit_init_sig = b2str([0x00, 0x00, 0xA0, 0xE3, 0x04, 0x00, 0x84, 0xE5, 0x08, 0x00, 0x84, 0xE5, 0x10, 0x80, 0xBD, 0xE8]) #-0xC
crit_enter_sig = b2str([0x10, 0x40, 0x2D, 0xE9, 0x00, 0x40, 0xA0, 0xE1, 0x70, 0x0F, 0x1D, 0xEE, 0x04, 0x10, 0x94, 0xE5, 0x01, 0x00, 0x50, 0xE1, 0x03, 0x00, 0x00, 0x0A])
crit_leave_sig = b2str([0x08, 0x10, 0x90, 0xE5, 0x01, 0x10, 0x51, 0xE2, 0x08, 0x10, 0x80, 0xE5, 0x02, 0x00, 0x00, 0x1A, 0x00, 0x10, 0xA0, 0xE3, 0x04, 0x10, 0x80, 0xE5, 0x0C, 0x70, 0xFF, 0xEA])
vsnprintf_sig = b2str([0x7C, 0xB5, 0x15, 0x00, 0x0C, 0x00, 0x1A, 0x00, 0x00, 0x29, 0x00, 0x90, 0x01, 0xD0, 0x00, 0x19, 0x40, 0x1E, 0x08, 0x4B, 0x7B, 0x44, 0x69, 0x46, 0x01, 0x90, 0x28, 0x00, 0xFC, 0xF1])
get_rf_struct_sig = b2str([0x00, 0x20, 0xA0, 0xE1, 0x00, 0x00, 0x90, 0xE5, 0x00, 0x08, 0xA0, 0xE1, 0x20, 0x08, 0xB0, 0xE1])

OpenDirectory_sig = b2str([0xF0, 0x40, 0x2D, 0xE9, 0x00, 0x50, 0xA0, 0xE1, 0x14, 0xD0, 0x4D, 0xE2, 0x01, 0x70, 0xA0, 0xE1, 0x01, 0x00, 0xA0, 0xE1])
ReadDirectory_sig = b2str([0x00, 0x00, 0x50, 0xE3, 0x00, 0x00, 0x51, 0x13, 0x3C, 0x00, 0x9F, 0x05, 0x1E, 0xFF, 0x2F, 0x01, 0xF0, 0x41, 0x2D, 0xE9])
CloseDirectory_sig = b2str([0x00, 0x00, 0x50, 0xE3, 0x02, 0x00, 0x00, 0x0A, 0x00, 0x10, 0x90, 0xE5, 0x04, 0x10, 0x91, 0xE5, 0x11, 0xFF, 0x2F, 0xE1, 0x1E, 0xFF, 0x2F, 0xE1])

resalloc_sig_legacy = b2str([0xF0, 0x4F, 0x2D, 0xE9, 0x34, 0xD0, 0x4D, 0xE2, 0x01, 0x80, 0xA0, 0xE1, 0x00, 0x50, 0xA0, 0xE1])
idk_sig_legacy = b2str([0x04, 0x10, 0x91, 0xE5, 0x00, 0x00, 0x51, 0xE3, 0x0B, 0x10, 0xD1, 0x15, 0x00, 0x00, 0x51, 0x13])
read_dtls_sig_legacy = b2str([0xF0, 0x41, 0x2D, 0xE9, 0x01, 0x40, 0xA0, 0xE1, 0x00, 0x70, 0xA0, 0xE1, 0x08, 0x50, 0x90, 0xE5])

cro_load_sig = b2str([0xF0, 0x41, 0x2D, 0xE9, 0x00, 0x50, 0x90, 0xE5, 0x18, 0x00, 0x85, 0xE2])
cro_file_size_sig = b2str([0x00, 0xF0, 0x20, 0xE3, 0xF1, 0xFF, 0xFF, 0x0A, 0x08, 0x00, 0x90, 0xE5])
cro_load_object_sig = b2str([0x7C, 0x40, 0x9F, 0xE5, 0x00, 0x00, 0x94, 0xE5, 0x00, 0x00, 0x50, 0xE3, 0x05, 0x00, 0x00, 0x1A])
cro_fighter_new_sig = b2str([0x10, 0x40, 0x2D, 0xE9, 0xB0, 0x22, 0xD0, 0xE5, 0x00, 0x40, 0xA0, 0xE1])
menu_state_apply_sig = b2str([
    0x14, 0x10, 0xA0, 0xE3, 0x00, 0x00, 0xA0, 0xE1,
    0x40, 0x20, 0x90, 0xE5, 0x02, 0x11, 0x80, 0xE7,
    0x01, 0x10, 0xA0, 0xE3, 0x44, 0x10, 0xC0, 0xE5,
    0x1E, 0xFF, 0x2F, 0xE1,
])
cro_unk_1_sig = b2str([0x38, 0x20, 0x9F, 0xE5, 0x14, 0x10, 0x90, 0xE5, 0x00, 0x20, 0x80, 0xE5])
cro_proj_sig = b2str([0x65, 0x6E, 0x65, 0x6D, 0x79, 0x00, 0x00, 0x00, 0x6D, 0x6F, 0x64, 0x65])
cro_cro_name_sig = b2str([0x30, 0x10, 0x9F, 0xE5, 0x3A, 0x00, 0x50, 0xE3, 0x3C, 0x00, 0x91, 0x05])
cro_raw_free_sig = b2str([0x00, 0x00, 0x50, 0xE3, 0x1E, 0xFF, 0x2F, 0x01, 0x70, 0x40, 0x2D, 0xE9, 0x00, 0x40, 0xA0, 0xE1, 0xB2, 0x5E, 0xFE, 0xEB, 0x04, 0x50, 0xA0, 0xE1, 0x00, 0x60, 0xA0, 0xE1])
cro_queue_submit_sig = b2str([0x00, 0x00, 0x90, 0xE5, 0x20, 0x00, 0x90, 0xE5, 0x00, 0x00, 0xA0, 0xE1, 0xF0, 0x41, 0x2D, 0xE9, 0x00, 0x40, 0xA0, 0xE1])
cro_menu_name_sig = b2str([0x6D, 0x65, 0x6E, 0x75, 0x2F, 0x6D, 0x65, 0x6E, 0x75, 0x00])
cro_minigame_name_sig = b2str([0x6D, 0x69, 0x6E, 0x69, 0x67, 0x61, 0x6D, 0x65, 0x2F, 0x6D, 0x69, 0x6E, 0x69, 0x67, 0x61, 0x6D, 0x65, 0x00])
sprintf_sig = b2str([0x0F, 0x00, 0x2D, 0xE9, 0x10, 0x40, 0x2D, 0xE9, 0x00, 0x40, 0xA0, 0xE1, 0x0C, 0x20, 0x9D, 0xE5])
get_fighter_data_sig = b2str([0x01, 0x10, 0x81, 0xE2, 0x10, 0x10, 0x80, 0xE5, 0x1E, 0xFF, 0x2F, 0xE1, 0x41, 0x00, 0x51, 0xE3]) # TODO: This includes a fighter count in the sig, 0x41
get_fighter_specializer_sig = b2str([0x00, 0x00, 0x80, 0x3F, 0x41, 0x00, 0x51, 0xE3, 0x01, 0xF1, 0x9F, 0x37, 0xC2, 0x00, 0x00, 0xEA])
get_weapon_data_sig = b2str([0xC2, 0x00, 0x51, 0xE3, 0x0F, 0x03, 0x00, 0x0A, 0xC4, 0x00, 0x00, 0xCA, 0xC2, 0x00, 0x51, 0xE3]) # Has 0xC2 weapon count
get_weapon_specializer_sig = b2str([0xC2, 0x00, 0x51, 0xE3, 0x10, 0x40, 0x2D, 0xE9, 0x0F, 0x03, 0x00, 0x0A, 0xC4, 0x00, 0x00, 0xCA]) #Has 0xC2 weapon count
weapon_data_default_sig = b2str([0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
weapon_specializer_default_sig = b2str([0x38, 0x00, 0x9F, 0xE5, 0x00, 0x00, 0x90, 0xE5, 0x01, 0x00, 0x10, 0xE3])
weapon_specializer_thing4_sig = b2str([0x01, 0x68, 0x00, 0x29, 0x01, 0xD0, 0x00, 0x20])

gsp_state_sig = b2str([0x04, 0x00, 0x9F, 0xE5, 0xD2, 0x00, 0xD0, 0xE1, 0x1E, 0xFF, 0x2F, 0xE1])
gsp_state_tail = b2str([0x04, 0x00, 0x9F, 0xE5, 0x1C, 0x00, 0x90, 0xE5, 0x1E, 0xFF, 0x2F, 0xE1])
hid_object_sig = b2str([0x08, 0x00, 0x9F, 0xE5, 0x08, 0x10, 0x90, 0xE5, 0x04, 0x00, 0x9F, 0xE5])

applet_state_sig = b2str([0x04, 0x00, 0x9F, 0xE5, 0xD9, 0x00, 0xD0, 0xE1, 0x1E, 0xFF, 0x2F, 0xE1])
applet_state_tail = b2str([0x04, 0x00, 0x9F, 0xE5, 0x0B, 0x00, 0xD0, 0xE5, 0x1E, 0xFF, 0x2F, 0xE1])
restart_app_sig = b2str([0x38, 0x40, 0x2D, 0xE9, 0x00, 0x40, 0xA0, 0xE1, 0x00, 0x00, 0xA0, 0xE3, 0x00, 0x20, 0xE0, 0xE3, 0x00, 0x00, 0x8D, 0xE5, 0x01, 0x50, 0xA0, 0xE1, 0x02, 0x30, 0xA0, 0xE1, 0x02, 0x00, 0xA0, 0xE3])

def find_applet_state():
    at = f.find(applet_state_sig)
    while at != -1:
        if f[at+0x10:at+0x1C] == applet_state_tail:
            return r32(f, at+0xC)
        at = f.find(applet_state_sig, at+1)
    return 0

def find_gsp_state():
    at = f.find(gsp_state_sig)
    while at != -1:
        if f[at+0x10:at+0x1C] == gsp_state_tail:
            return r32(f, at+0xC)
        at = f.find(gsp_state_sig, at+1)
    return 0

# Make this compatible with Python 2 and 3
try:
    f = open(sys.argv[1], 'r', encoding='latin-1', newline="").read()
except TypeError:
    f = open(sys.argv[1], 'rb').read()

menu_state_apply_offs = f.find(menu_state_apply_sig)
if (menu_state_apply_offs < 0 or
        f.find(menu_state_apply_sig, menu_state_apply_offs + 1) >= 0):
    raise ValueError("menu state apply function is not a unique match")

# menu.cro imports the entry at +8
menu_state_apply_addr = menu_state_apply_offs + 0x100000
menu_hook_site_addr = menu_state_apply_addr + 8
menu_hook_word = r32(f, menu_hook_site_addr - 0x100000)
if menu_hook_word != 0xE5902040:
    raise ValueError("menu export has an unknown displaced instruction")
    
os.makedirs('build', exist_ok=True)
common = open('build/common.asm','w')
common_armips = open('build/common.armips.asm','w')

# Print to stderr a helpful message in case someone tries to patch the Demo.
# This will also stop the Makefile
if f.find(mount_sdmc_sig) == -1:
    print("mount_sdmc does not exist!\nIs your code.bin from a retail copy of Smash, and not the demo?\n", file=sys.stderr)
    exit(1)  

print(".equ mount_sdmc, \t\t\t\t" + hex(f.find(mount_sdmc_sig)+0x100000), file=common)
print(".equ unmount_path, \t\t\t\t" + hex(f.find(unmount_path_sig)+0x100000), file=common)
print(".equ IFile_Init, \t\t\t\t" + hex(f.find(IFile_Init_sig)+0x100000), file=common)
print(".equ IFile_Open, \t\t\t\t" + hex(f.find(IFile_Open_sig)+0x100000), file=common)
print(".equ IFile_GetSize, \t\t\t" + hex(f.find(IFile_GetSize_sig)+0x100000), file=common)
print(".equ IFile_Read, \t\t\t\t" + hex(f.find(IFile_Read_sig)+0x100000), file=common)
print(".equ IFile_Close, \t\t\t\t" + hex(f.find(IFile_Close_sig)+0x100000), file=common)
print(".equ OpenDirectory, \t\t\t" + hex(f.find(OpenDirectory_sig)+0x100000), file=common)
print(".equ ReadDirectory, \t\t\t" + hex(f.find(ReadDirectory_sig)+0x100000), file=common)
print(".equ CloseDirectory, \t\t\t" + hex(f.find(CloseDirectory_sig)+0x100000), file=common)
print(".equ strcat, \t\t\t\t\t" + hex(f.find(strcat_sig)+0x100000), file=common)
print(".equ strcpy, \t\t\t\t\t" + hex(f.find(strcpy_sig)+0x100000), file=common)
print(".equ strlen, \t\t\t\t\t" + hex(f.find(strlen_sig)+0x100000), file=common)
print(".equ resalloc, \t\t\t\t\t" + hex(f.find(resalloc_sig_legacy if f.find(resalloc_sig) == -1 else resalloc_sig)+0x100000), file=common)  
print(".equ path_str, \t\t\t\t\t" + hex(f.find(path_str_sig)+0x100000), file=common)
print(".equ res_deallocate, \t\t\t" + hex(f.find(res_deallocate_sig)+0x100000+0x4), file=common)
print(".equ idk, \t\t\t\t\t\t" + hex(f.find(idk_sig_legacy if f.find(idk_sig) == -1 else idk_sig)+0x100000), file=common) 
print(".equ referenced_by_ls_init, \t" + hex(f.find(referenced_by_ls_init_sig)+0x100000), file=common)
print(".equ read_dtls, \t\t\t\t" + hex(f.find(read_dtls_sig_legacy if f.find(read_dtls_sig) == -1 else read_dtls_sig)+0x100000-0x4), file=common)
print(".equ liballoc, \t\t\t\t\t" + hex(f.find(liballoc_sig)+0x100000), file=common)
print(".equ libdealloc, \t\t\t\t" + hex(f.find(libdealloc_sig)+0x100000), file=common)
print(".equ memcpy, \t\t\t\t\t" + hex(f.find(memcpy_sig)+0x100000), file=common)
print(".equ memmove, \t\t\t\t\t" + hex(f.find(memmove_sig)+0x100000-0xC), file=common)
print(".equ memclr, \t\t\t\t\t" + hex(f.find(memclr_sig)+0x100000), file=common)
print(".equ strcmp, \t\t\t\t\t" + hex(f.find(strcmp_sig)+0x100000), file=common)
print(".equ crit_this, \t\t\t\t" + hex(f.find(crit_this_sig)+0x100000), file=common)
print(".equ crit_init, \t\t\t\t" + hex(f.find(crit_init_sig)+0x100000-0xC), file=common)
print(".equ crit_enter, \t\t\t\t" + hex(f.find(crit_enter_sig)+0x100000), file=common)
print(".equ crit_leave, \t\t\t\t" + hex(f.find(crit_leave_sig)+0x100000), file=common)
print(".equ crc, \t\t\t\t\t\t" + hex(f.find(crc_sig)+0x100000), file=common)
print(".equ vsnprintf, \t\t\t\t" + hex(f.find(vsnprintf_sig)+0x100000+1), file=common)
print(".equ get_rf_struct, \t\t\t" + hex(f.find(get_rf_struct_sig)+0x100000), file=common)
if(r32(f,f.find(path_str_sig)-4) == 0x0):
    print(".equ something_resource_lock, \t" + hex(r32(f,f.find(path_str_sig)-8)) + "\n", file=common)
else:
    print(".equ something_resource_lock, \t" + hex(r32(f,f.find(path_str_sig)-4)) + "\n", file=common)

print("build/common.asm generated successfully!")

print("mount_sdmc equ (" + hex(f.find(mount_sdmc_sig)+0x100000) + ")", file=common_armips)
print("unmount_path equ (" + hex(f.find(unmount_path_sig)+0x100000) + ")", file=common_armips)
print("IFile_Init equ (" + hex(f.find(IFile_Init_sig)+0x100000) + ")", file=common_armips)
print("IFile_Open equ (" + hex(f.find(IFile_Open_sig)+0x100000) + ")", file=common_armips)
print("IFile_GetSize equ (" + hex(f.find(IFile_GetSize_sig)+0x100000) + ")", file=common_armips)
print("IFile_Read equ (" + hex(f.find(IFile_Read_sig)+0x100000) + ")", file=common_armips)
print("IFile_Close equ (" + hex(f.find(IFile_Close_sig)+0x100000) + ")", file=common_armips)
print("OpenDirectory equ (" + hex(f.find(OpenDirectory_sig)+0x100000) + ")", file=common_armips)
print("ReadDirectory equ (" + hex(f.find(ReadDirectory_sig)+0x100000) + ")", file=common_armips)
print("CloseDirectory equ (" + hex(f.find(CloseDirectory_sig)+0x100000) + ")", file=common_armips)
print("strcat equ (" + hex(f.find(strcat_sig)+0x100000) + ")", file=common_armips)
print("strcpy equ (" + hex(f.find(strcpy_sig)+0x100000) + ")", file=common_armips)
print("strlen equ (" + hex(f.find(strlen_sig)+0x100000) + ")", file=common_armips)
print("resalloc equ (" + hex(f.find(resalloc_sig_legacy if f.find(resalloc_sig) == -1 else resalloc_sig)+0x100000) + ")", file=common_armips)  
print("path_str equ (" + hex(f.find(path_str_sig)+0x100000) + ")", file=common_armips)
print("res_deallocate equ (" + hex(f.find(res_deallocate_sig)+0x100000+0x4), file=common_armips)
print("idk equ (" + hex(f.find(idk_sig_legacy if f.find(idk_sig) == -1 else idk_sig)+0x100000) + ")", file=common_armips) 
print("referenced_by_ls_init equ (" + hex(f.find(referenced_by_ls_init_sig)+0x100000) + ")", file=common_armips)
print("read_dtls equ (" + hex(f.find(read_dtls_sig_legacy if f.find(read_dtls_sig) == -1 else read_dtls_sig)+0x100000-0x4), file=common_armips)
print("liballoc equ (" + hex(f.find(liballoc_sig)+0x100000) + ")", file=common_armips)
print("libdealloc equ (" + hex(f.find(libdealloc_sig)+0x100000) + ")", file=common_armips)
print("memcpy equ (" + hex(f.find(memcpy_sig)+0x100000) + ")", file=common_armips)
print("memmove equ (" + hex(f.find(memmove_sig)+0x100000-0xC), file=common_armips)
print("memclr equ (" + hex(f.find(memclr_sig)+0x100000) + ")", file=common_armips)
print("strcmp equ (" + hex(f.find(strcmp_sig)+0x100000) + ")", file=common_armips)
print("crit_this equ (" + hex(f.find(crit_this_sig)+0x100000) + ")", file=common_armips)
print("crit_init equ (" + hex(f.find(crit_init_sig)+0x100000-0xC), file=common_armips)
print("crit_enter equ (" + hex(f.find(crit_enter_sig)+0x100000) + ")", file=common_armips)
print("crit_leave equ (" + hex(f.find(crit_leave_sig)+0x100000) + ")", file=common_armips)
print("crc equ (" + hex(f.find(crc_sig)+0x100000) + ")", file=common_armips)
print("vsnprintf equ (" + hex(f.find(vsnprintf_sig)+0x100000+1), file=common_armips)
print("get_rf_struct equ (" + hex(f.find(get_rf_struct_sig)+0x100000) + ")", file=common_armips)
if(r32(f,f.find(path_str_sig)-4) == 0x0):
    print("something_resource_lock equ (" + hex(r32(f,f.find(path_str_sig)-8)) + ")\n", file=common_armips)
else:
    print("something_resource_lock equ (" + hex(r32(f,f.find(path_str_sig)-4)) + ")\n", file=common_armips)

print("cro_load_hook_loc_2 equ (" + hex(f.find(cro_load_sig)+4+0x100000) + ")", file=common_armips)
print("cro_msg_hook_loc_2 equ (" + hex(f.find(cro_load_sig)+4+0x78+0x100000) + ")", file=common_armips)
print("cro_post_hook_loc_2 equ (" + hex(f.find(cro_load_sig)+4+0xD0+0x100000) + ")", file=common_armips)
print("cro_load_hook_loc equ (" + hex(f.index(cro_load_sig, f.find(cro_load_sig)+len(cro_load_sig))+4+0x100000) + ")", file=common_armips)
print("cro_msg_hook_loc equ (" + hex(f.index(cro_load_sig, f.find(cro_load_sig)+len(cro_load_sig))+4+0x78+0x100000) + ")", file=common_armips)
print("cro_post_hook_loc equ (" + hex(f.index(cro_load_sig, f.find(cro_load_sig)+len(cro_load_sig))+4+0xFC+0x100000) + ")", file=common_armips)
print("cro_file_size_hook_loc equ (" + hex(f.find(cro_file_size_sig)+0x100000) + ")", file=common_armips)
print("cro_file_hook_loc equ (" + hex(f.find(cro_file_size_sig)+0xA8+0x100000) + ")", file=common_armips)
print("cro_raw_alloc equ (" + hex(arm_branch_target(f, f.find(cro_file_size_sig)+0x1C)) + ")", file=common_armips)
print("cro_load_setup equ (" + hex(arm_branch_target(f, f.find(cro_file_size_sig)+0x98)) + ")", file=common_armips)
print("cro_raw_free equ (" + hex(f.find(cro_raw_free_sig)+0x100000) + ")", file=common_armips)
print("cro_queue_submit equ (" + hex(f.find(cro_queue_submit_sig)+0x100000) + ")", file=common_armips)
print("cro_request_free equ (" + hex(arm_branch_target(f, f.find(cro_file_size_sig)+0x1BC)) + ")", file=common_armips)

_menu_name = f.find(cro_menu_name_sig)
_minigame_name = f.find(cro_minigame_name_sig)
if _menu_name == -1 or f.find(cro_menu_name_sig, _menu_name + 1) != -1:
    raise ValueError("expected exactly one menu/menu CRO caller string")
if _minigame_name == -1 or f.find(cro_minigame_name_sig, _minigame_name + 1) != -1:
    raise ValueError("expected exactly one minigame/minigame CRO caller string")
_menu_panic_call = _menu_name - 0x4
_minigame_load_call = _minigame_name - 0x28
if (r32(f, _minigame_load_call - 0x8) != 0xE3A01000 or
        r32(f, _minigame_load_call - 0x4) != 0xE28F0024 or
        r32(f, _minigame_load_call + 0x4) != 0xE584006C):
    raise ValueError("minigame CRO caller no longer matches the audited load/store sequence")
print("cro_minigame_load_hook_loc equ (" + hex(_minigame_load_call+0x100000) + ")", file=common_armips)
print("cro_blocking_load equ (" + hex(arm_branch_target(f, _minigame_load_call)) + ")", file=common_armips)
print("cro_failure_panic equ (" + hex(arm_branch_target(f, _menu_panic_call)) + ")", file=common_armips)
print("cro_load_object_adj_loc equ (" + hex(f.find(cro_load_object_sig)+0x10+0x100000) + ")", file=common_armips)
print("cro_load_object equ (" + hex(r32(f, f.find(cro_load_object_sig)+0x84)) + ")", file=common_armips)
print("cro_fighter_new equ (" + hex(f.find(cro_fighter_new_sig)+0x100000) + ")", file=common_armips)
print("menu_hook_site equ (" + hex(menu_hook_site_addr) + ")", file=common_armips)
print("cro_unk_1 equ (" + hex(f.find(cro_unk_1_sig)+0x100000) + ")", file=common_armips)
print("projectile_table_ptr equ (" + hex(f.find(cro_proj_sig)-0xC+0x100000) + ")", file=common_armips)
print("projectile_prefix_table_ptr equ (" + hex(f.find(cro_proj_sig)-0x8+0x100000) + ")", file=common_armips)
print("cro_cro_name equ (" + hex(f.find(cro_cro_name_sig)+0x100000) + ")", file=common_armips)
print("character_id_to_lowercase equ (" + hex(f.find(cro_cro_name_sig)+0x3c+0x100000) + ")", file=common_armips)
print("sprintf equ (" + hex(f.find(sprintf_sig)+0x100000) + ")", file=common_armips)
_fighter_data = f.find(get_fighter_data_sig) + 0xC
_fighter_data_va = _fighter_data + 0x100000
_fighter_data_cases = _fighter_data_va + 0x110
_fighter_data_default = _fighter_data_va + 0x318
if (r32(f, _fighter_data) != 0xE3510041 or
        r32(f, _fighter_data + 0x4) != 0x379FF101 or
        arm_branch_target(f, _fighter_data + 0x8) != _fighter_data_default):
    raise ValueError("get_fighter_data no longer has the audited range/default dispatch")
for _fighter_id in range(0x41):
    if r32(f, _fighter_data + 0xC + _fighter_id * 4) != _fighter_data_cases + _fighter_id * 8:
        raise ValueError("get_fighter_data case table is not the audited 0x41-entry layout")
print("get_fighter_data equ (" + hex(_fighter_data_va) + ")", file=common_armips)
print("get_fighter_data_stock_cases equ (" + hex(_fighter_data_cases) + ")", file=common_armips)
print("get_fighter_data_stock_default equ (" + hex(_fighter_data_default) + ")", file=common_armips)
print("get_fighter_specializer equ (" + hex(f.find(get_fighter_specializer_sig)+0x4+0x100000) + ")", file=common_armips)
print("get_weapon_data equ (" + hex(f.find(get_weapon_data_sig)+0x100000) + ")", file=common_armips)
print("get_weapon_specializer equ (" + hex(f.find(get_weapon_specializer_sig)+0x100000) + ")", file=common_armips)

# Save the original weapon-specializer branches
_wspec = f.find(get_weapon_specializer_sig)
print("get_weapon_specializer_eq equ (" + hex(arm_branch_target(f, _wspec+0x8)) + ")", file=common_armips)
print("get_weapon_specializer_gt equ (" + hex(arm_branch_target(f, _wspec+0xC)) + ")", file=common_armips)
print("weapon_data_default equ (" + hex(f.find(weapon_data_default_sig)+0x14+0x100000) + ")", file=common_armips)
print("weapon_specializer_default_case equ (" + hex(f.find(weapon_specializer_default_sig)+0x100000) + ")", file=common_armips)
print("weapon_specializer_thing1 equ (" + hex(r32(f, f.find(weapon_specializer_default_sig)+0x40)) + ")", file=common_armips)
print("weapon_specializer_thing2 equ (" + hex(r32(f, f.find(weapon_specializer_default_sig)+0x44)) + ")", file=common_armips)
print("weapon_specializer_thing3 equ (" + hex(r32(f, f.find(weapon_specializer_default_sig)+0x48)) + ")", file=common_armips)
print("weapon_specializer_thing4 equ (" + hex(f.find(weapon_specializer_thing4_sig)+0x100000) + ")", file=common_armips)

print("build/common.armips.asm generated successfully!")

common = open('build/common.h','w')
print("#define mount_sdmc_ADDR " + hex(f.find(mount_sdmc_sig)+0x100000), file=common)
print("#define unmount_path_ADDR " + hex(f.find(unmount_path_sig)+0x100000), file=common)
print("#define IFile_Init_ADDR " + hex(f.find(IFile_Init_sig)+0x100000), file=common)
print("#define IFile_Open_ADDR " + hex(f.find(IFile_Open_sig)+0x100000), file=common)
print("#define IFile_GetSize_ADDR " + hex(f.find(IFile_GetSize_sig)+0x100000), file=common)
print("#define IFile_Read_ADDR " + hex(f.find(IFile_Read_sig)+0x100000), file=common)
print("#define IFile_Close_ADDR " + hex(f.find(IFile_Close_sig)+0x100000), file=common)
print("#define OpenDirectory_ADDR " + hex(f.find(OpenDirectory_sig)+0x100000), file=common)
print("#define ReadDirectory_ADDR " + hex(f.find(ReadDirectory_sig)+0x100000), file=common)
print("#define CloseDirectory_ADDR " + hex(f.find(CloseDirectory_sig)+0x100000), file=common)

print("#define strcat_ADDR " + hex(f.find(strcat_sig)+0x100000), file=common)
print("#define strcpy_ADDR " + hex(f.find(strcpy_sig)+0x100000), file=common)
print("#define strlen_ADDR " + hex(f.find(strlen_sig)+0x100000), file=common)
print("#define resalloc_ADDR " + hex(f.find(resalloc_sig_legacy if f.find(resalloc_sig) == -1 else resalloc_sig)+0x100000), file=common)  
print("#define path_str_ADDR " + hex(f.find(path_str_sig)+0x100000), file=common)
print("#define res_deallocate_ADDR " + hex(f.find(res_deallocate_sig)+0x100000+0x4), file=common)
print("#define idk_ADDR " + hex(f.find(idk_sig_legacy if f.find(idk_sig) == -1 else idk_sig)+0x100000), file=common) 
print("#define referenced_by_ls_init_ADDR " + hex(f.find(referenced_by_ls_init_sig)+0x100000), file=common)
print("#define read_dtls_ADDR " + hex(f.find(read_dtls_sig_legacy if f.find(read_dtls_sig) == -1 else read_dtls_sig)+0x100000-0x4), file=common)
print("#define liballoc_ADDR " + hex(f.find(liballoc_sig)+0x100000), file=common)
print("#define libdealloc_ADDR " + hex(f.find(libdealloc_sig)+0x100000), file=common)
print("#define memcpy_ADDR " + hex(f.find(memcpy_sig)+0x100000), file=common)
print("#define memmove_ADDR " + hex(f.find(memmove_sig)+0x100000-0xC), file=common)
print("#define memclr_ADDR " + hex(f.find(memclr_sig)+0x100000), file=common)
print("#define strcmp_ADDR " + hex(f.find(strcmp_sig)+0x100000), file=common)
print("#define crit_this_ADDR " + hex(f.find(crit_this_sig)+0x100000), file=common)
print("#define crit_init_ADDR " + hex(f.find(crit_init_sig)+0x100000-0xC), file=common)
print("#define crit_enter_ADDR " + hex(f.find(crit_enter_sig)+0x100000), file=common)
print("#define crit_leave_ADDR " + hex(f.find(crit_leave_sig)+0x100000), file=common)
print("#define crc_ADDR " + hex(f.find(crc_sig)+0x100000), file=common)
print("#define vsnprintf_ADDR " + hex(f.find(vsnprintf_sig)+0x100000+1), file=common)
print("#define get_rf_struct_ADDR " + hex(f.find(get_rf_struct_sig)+0x100000), file=common)
print("#define cro_fighter_new_ADDR " + hex(f.find(cro_fighter_new_sig)+0x100000), file=common)
print("#define menu_hook_site_ADDR " + hex(menu_hook_site_addr), file=common)
print("#define gsp_state_ADDR " + hex(find_gsp_state()), file=common)
print("#define hid_object_ADDR " + hex(r32(f, f.find(hid_object_sig)+0x14)), file=common)
print("#define applet_state_ADDR " + hex(find_applet_state()), file=common)
print("#define restart_app_ADDR " + hex(f.find(restart_app_sig)+0x100000), file=common)
if(r32(f,f.find(path_str_sig)-4) == 0x0):
    print("#define something_resource_lock_ADDR " + hex(r32(f,f.find(path_str_sig)-8)) + "\n", file=common)
else:
    print("#define something_resource_lock_ADDR " + hex(r32(f,f.find(path_str_sig)-4)) + "\n", file=common)
    
print("build/common.h generated successfully!")
