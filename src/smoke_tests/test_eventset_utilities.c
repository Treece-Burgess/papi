// Library headers
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// PAPI headers
#include <papi.h>

#define CHECK_PAPI_CALLS(call) \
    do {  \
        int _status = (call);  \
        if (_status != PAPI_OK) {  \
            fprintf(stderr, "Error in call to %s: %s\n", #call, PAPI_strerror(_status)); \
            exit(EXIT_FAILURE); \
        }  \
    } while (0);

int main( int argc, char **argv )
{
    int number_of_components = argc - 1;
    char **component_names = argv + 1;

    int retval = PAPI_library_init( PAPI_VER_CURRENT );
    if ( retval != PAPI_VER_CURRENT ) {
        printf("ERROR: PAPI_library_init: %d: %s\n", retval, PAPI_strerror(retval));
        exit(EXIT_FAILURE);
    }

    int i;
    for (i = 0; i < number_of_components; i++) {
        int EventSet = PAPI_NULL;
        CHECK_PAPI_CALLS( PAPI_create_eventset (&EventSet) );
        if (EventSet != 0) {
            fprintf(stderr, "Failed.");
            exit(EXIT_FAILURE);
        }

        int cidx = PAPI_get_component_index( component_names[i] );
        if (cidx < 0) {
            fprintf(stderr, "Error in call to PAPI_get_component_index: %s\n", PAPI_strerror(cidx));
            exit(EXIT_FAILURE);
        }

        int eventCode = 0 | PAPI_NATIVE_MASK;
        int modifier = PAPI_ENUM_FIRST;
        CHECK_PAPI_CALLS( PAPI_enum_cmp_event(&eventCode, modifier, cidx) );

        CHECK_PAPI_CALLS( PAPI_add_event(EventSet, eventCode) );

        int event, number_of_events = 0;
        CHECK_PAPI_CALLS( PAPI_list_events(EventSet, &event,  &number_of_events) );
        if (number_of_events != 1) {
            fprintf(stderr, "%d events are in the eventset. Expected 1.\n", number_of_events);
            exit(EXIT_FAILURE);
        }

        CHECK_PAPI_CALLS( PAPI_list_events(EventSet, &event, &number_of_events) );
        if (event != eventCode) {
            fprintf(stderr, "Eventcode (%x) from PAPI_list_events does not match eventcode (%x) from PAPI_enum_cmp_event.\n", event, eventCode);
            exit(EXIT_FAILURE);
        }

        CHECK_PAPI_CALLS( PAPI_cleanup_eventset(EventSet) );
        CHECK_PAPI_CALLS( PAPI_destroy_eventset(&EventSet) );
    }

    PAPI_shutdown();
    return 0;
}
