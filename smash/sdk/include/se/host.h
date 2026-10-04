#ifndef SALTYSD_SE_HOST_H
#define SALTYSD_SE_HOST_H

#include <se/hooks.h>
#include <se/targets.h>
#include <se/types.h>

#define SE_HOST_MAGIC 0x53454832u /* SEH2 */
#define SE_HOST_ABI_MAJOR 1u
#define SE_HOST_ABI_MINOR 0u

#define SE_CAP_LOG (1u << 0)
#define SE_CAP_BUILD_IDENTITY (1u << 1)
#define SE_CAP_TARGETS (1u << 2)
#define SE_CAP_ALLOC (1u << 3)
#define SE_CAP_FILES (1u << 4)
#define SE_CAP_DIAGNOSTICS (1u << 5)
#define SE_CAP_LIFECYCLE (1u << 6)
#define SE_CAP_HOOKS (1u << 7)
#define SE_CAP_RESIDENT_MODULES (1u << 8)

#define SE_HOST_IMPORT_GET_HOST 1u

typedef se_u32 se_package_handle;
typedef se_u32 se_file_handle;

typedef struct {
    se_u32 magic;
    se_u16 abi_major;
    se_u16 abi_minor;
    se_u32 structure_size;
    se_u32 capabilities_lo;
    se_u32 capabilities_hi;
} se_host_header;

typedef struct {
    se_u32 title_id;
    se_u16 game_major;
    se_u16 game_minor;
    se_u16 region;
    se_u16 reserved;
    se_u32 build_id;
    se_u32 registry_revision;
    se_u8 code_sha256[32];
} se_build_identity;

typedef struct se_host_v1 {
    se_host_header header;
    void (SE_CALL *log)(se_package_handle owner, se_u32 level,
                        const char *bytes, se_u32 length);
    int (SE_CALL *get_build_identity)(se_build_identity *out, se_u32 out_size);
    int (SE_CALL *resolve_target)(se_u32 target_id, se_target *out, se_u32 out_size);
    void *(SE_CALL *alloc)(se_package_handle owner, se_u32 size,
                           se_u32 alignment, se_u32 arena);
    int (SE_CALL *free)(se_package_handle owner, void *allocation);
    int (SE_CALL *open_read)(se_package_handle owner, const char *relative_path,
                             se_file_handle *out);
    int (SE_CALL *read)(se_file_handle file, se_u32 offset, void *buffer,
                        se_u32 size, se_u32 *read_size);
    void (SE_CALL *close)(se_file_handle file);
    int (SE_CALL *emit_diagnostic)(se_package_handle owner, se_u32 code,
                                   se_u32 detail_a, se_u32 detail_b);
    int (SE_CALL *install_hook)(se_package_handle owner,
                                const se_hook_request *request,
                                se_hook_result *result);
    int (SE_CALL *remove_hook)(se_package_handle owner, se_hook_handle hook);
} se_host_v1;

typedef const se_host_v1 *(SE_CALL *se_get_host_fn)(se_u32 requested_major);
typedef int (SE_CALL *se_init_fn)(const se_host_v1 *host, se_package_handle owner);
typedef void (SE_CALL *se_cro_loaded_fn)(se_package_handle owner,
                                         const char *name, se_u32 base);

#endif
