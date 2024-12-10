/****************************/
/* THIS IS OPEN SOURCE CODE */
/****************************/

/**
 * @author  Treece Burgess (tburgess@icl.utk.edu)
 *
 * Test case for the rocm_smi component.
 * For GitHub CI and terminal use.
 *
 * Tested on Dopamine at ICL in winter 2024 with two
 * AMD MI210.
 *
 * @brief
 *   List the event code and event name for all 
 *   available rocm_smi events on the current 
 *   machine.
 */

#include <stdio.h>

#include "papi.h"
#include "papi_test.h"

int main(int argc, char **argv) 
{
    int retval, event_cnt = 0, EventCode, cidx;
    char EventName[PAPI_2MAX_STR_LEN];

    /* determine if we quiet output */
    tests_quiet(argc, argv);

    /* initialize the PAPI library */
    retval = PAPI_library_init(PAPI_VER_CURRENT);
    if (retval != PAPI_VER_CURRENT) {
        test_fail(__FILE__, __LINE__, "PAPI_library_init", retval);
    }

    /* get the rocm_smi component index */
    cidx = PAPI_get_component_index("rocm_smi");
    if (cidx < 0) {
        test_fail(__FILE__, __LINE__, "PAPI_get_component_index failed for rocm_smi", cidx);
    }

    if (!TESTS_QUIET) { 
        printf("Component index for rocm_smi: %d\n", cidx);
    }   

    int modifier = PAPI_ENUM_FIRST;
    EventCode = PAPI_NATIVE_MASK;
    retval = PAPI_enum_cmp_event(&EventCode, modifier, cidx);
    if (retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_enum_cmp_event", retval);
    }

    /* enumerate through all rocm_smi events found on the current machine */ 
    modifier = PAPI_ENUM_EVENTS;
    do {
        /* print output header  */
        if (event_cnt == 0 && !TESTS_QUIET) {
            printf("%s %s", "Event code", "Event name\n");
        }

        retval = PAPI_event_code_to_name(EventCode, EventName);
        if (retval != PAPI_OK) {
            test_fail(__FILE__, __LINE__, "PAPI_event_code_to_name", retval);
        }

        if (!TESTS_QUIET) {
            printf("%d %s\n", EventCode, EventName);
        }

        /* increment rocm_smi event count */
        event_cnt++;
    } while(PAPI_enum_cmp_event(&EventCode, modifier, cidx) == PAPI_OK);

    if (!TESTS_QUIET) {
        printf("Total number of events for rocm_smi: %d\n", event_cnt);
    }

    PAPI_shutdown();

    /* if we make it here everything ran succesfully */
    test_pass(__FILE__);

    return PAPI_OK;
}
