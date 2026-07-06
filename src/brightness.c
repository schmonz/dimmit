#include "brightness.h"
#include "platform/ddc/abstraction.h"
#include <stdlib.h>

int brightness_enumerate(brightness_source **out, int *count) {
    /* Phase 1 has a single provider. Phase 2 concatenates internal providers. */
    return ddc_enumerate_sources(out, count);
}

void brightness_free(brightness_source *sources, int count) {
    if (!sources) return;
    for (int i = 0; i < count; i++) {
        if (sources[i].ops && sources[i].ops->close) sources[i].ops->close(sources[i].ctx);
    }
    free(sources);
}
