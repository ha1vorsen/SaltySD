#include "errors.h"

const char *se_error_text(se_error error)
{
    static const char *const messages[] = {
        [PLUGIN_OK] = "ready",
        [PLUGIN_READ] = "package could not be read",
        [PLUGIN_TOO_BIG] = "package exceeds the size limit",
        [PLUGIN_EMPTY] = "folder has no SEA package",
        [PLUGIN_NOT_ELF] = "package is not a supported ARM ELF",
        [PLUGIN_BAD_SEGMENT] = "package has an invalid load segment",
        [PLUGIN_NO_ORIGINAL] = "original-byte record is missing or invalid",
        [PLUGIN_MISMATCH] = "target bytes differ from the package baseline",
        [PLUGIN_CONFLICT] = "patch range conflicts with another owner",
        [PLUGIN_FULL] = "engine capacity is exhausted",
        [PLUGIN_INCOMPATIBLE] = "SEA version or feature is unsupported",
        [PLUGIN_MULTIPLE] = "folder contains more than one SEA package",
        [PLUGIN_NO_SIGNATURE] = "signature or placement record is invalid",
        [PLUGIN_SIG_NOT_FOUND] = "signature was not found",
        [PLUGIN_SIG_AMBIGUOUS] = "signature matched more than once",
        [PLUGIN_BAD_FIXUP] = "fix-up record is invalid",
        [PLUGIN_BAD_TARGET] = "CRO target record is invalid",
        [PLUGIN_NO_MEMORY] = "package retention memory is unavailable",
        [SE_ERROR_INVALID_ID] = "package ID is invalid",
        [SE_ERROR_DUPLICATE_ID] = "package ID is duplicated",
        [SE_ERROR_MISSING_DEPENDENCY] = "required dependency is missing",
        [SE_ERROR_DEPENDENCY_VERSION] = "dependency version is incompatible",
        [SE_ERROR_DEPENDENCY_REFUSED] = "required dependency was refused",
        [SE_ERROR_DEPENDENCY_CYCLE] = "dependency or order cycle was found",
        [SE_ERROR_EXPLICIT_CONFLICT] = "packages declare a conflict",
        [SE_ERROR_STATIC_COLLISION] = "static patch ranges overlap",
        [SE_ERROR_WRONG_BUILD] = "package has no unique variant for this build",
        [SE_ERROR_FILE_CHANGED] = "package changed between plan and commit",
        [SE_ERROR_ABI_UNSUPPORTED] = "host ABI range is incompatible",
        [SE_ERROR_CAPABILITY_UNSUPPORTED] = "required host capability is unavailable",
        [SE_ERROR_HOOK_UNSUPPORTED] = "hook mode or target kind is unsupported",
        [SE_ERROR_HOOK_CONFLICT] = "hook target already has an owner",
        [SE_ERROR_HOOK_TARGET_CHANGED] = "hook target changed outside the manager",
        [SE_ERROR_HOOK_FULL] = "managed hook table is exhausted",
    };
    unsigned int value = (unsigned int)error;
    if (value >= sizeof(messages) / sizeof(messages[0]) || !messages[value])
        return "unknown engine result";
    return messages[value];
}
