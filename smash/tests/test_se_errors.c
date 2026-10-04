#include <assert.h>
#include <string.h>

#include "se/errors.h"

int main(void)
{
    assert(PLUGIN_OK == 0);
    assert(PLUGIN_NO_MEMORY == 17);
    assert(strcmp(se_error_text(PLUGIN_SIG_AMBIGUOUS),
                  "signature matched more than once") == 0);
    assert(strcmp(se_error_text((se_error)999), "unknown engine result") == 0);
    return 0;
}
