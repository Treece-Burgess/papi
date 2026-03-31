#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <papi.h>

#define CHECK_PAPI_CALLS(call) \
    do {  \
        int _status = (call);  \
        if (_status != PAPI_OK) {  \
            fprintf(stderr, "Error in call to %s: %s\n", #call, PAPI_strerror(_status)); \
            exit(EXIT_FAILURE); \
        }  \
    } while (0);

#define NUM_EDELAY_INIT_COMPS 6

int main(int argc, char **argv)
{
    int retval = PAPI_library_init(PAPI_VER_CURRENT);
    if (retval != PAPI_VER_CURRENT) {
        fprintf(stderr, "");
        exit(EXIT_FAILURE);
    }

    int i;
    for (i = 1; i < argc; i++) {
        int component_idx = PAPI_get_component_index(argv[i]);
        if (component_idx == PAPI_ENOCMP) {
            continue;
        }

        const PAPI_component_info_t *component_info;
        component_info = PAPI_get_component_info(component_idx);
        if (component_info == NULL) {
            fprintf(stderr, "");
            exit(EXIT_FAILURE);
        }

        if (component_info->disabled != PAPI_EDELAY_INIT) {
            fprintf(stderr, "");
            exit(EXIT_FAILURE);
        }

        if (component_info->num_native_events != -1) {
            fprintf(stderr, "");
            exit(EXIT_FAILURE);
        }

        int EventCode = 0 | PAPI_NATIVE_MASK, modifier = PAPI_ENUM_FIRST;
        retval = PAPI_enum_cmp_event(&EventCode, modifier, component_idx);
        if (retval != PAPI_OK) {
            fprintf(stderr, "");
            exit(EXIT_FAILURE);
        }

        if (component_info->disabled == PAPI_EDELAY_INIT) {
            fprintf(stderr, "Error at the second disabled check.\n");
            exit(EXIT_FAILURE);
        }

        if (component_info->num_native_events == -1) {
            fprintf(stderr, "num native events failed");
            exit(EXIT_FAILURE);
        }

    }

    return 0;
}
