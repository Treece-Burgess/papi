//-----------------------------------------------------------------------------
// This program must be compiled using a special makefile:
// make -f ROCM_SMI_Makefile rocm_smi_writeTests.out 
//-----------------------------------------------------------------------------
#define __HIP_PLATFORM_HCC__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "papi.h"
#include "papi_test.h"
#include "do_loops.h"
#include <hip/hip_runtime.h>
#include <unistd.h>
#include "rocm_smi.h"   // Need some enumerations.

#include "force_init.h"

// THIS MACRO EXITS if the papi call does not return PAPI_OK. Do not use for routines that
// return anything else; e.g. PAPI_num_components, PAPI_get_component_info, PAPI_library_init.
#define CALL_PAPI_OK(papi_routine)                                                        \
    do {                                                                                  \
        int _papiret = papi_routine;                                                      \
        if (_papiret != PAPI_OK) {                                                        \
            fprintf(stderr, "%s:%d macro: PAPI Error: function " #papi_routine " failed with ret=%d [%s].\n", \
                    __FILE__, __LINE__, _papiret, PAPI_strerror(_papiret));               \
            exit(-1);                                                                     \
        }                                                                                 \
    } while (0);

// Show help.
//-----------------------------------------------------------------------------
static void printUsage()
{
    printf("This program takes two rocm_smi native events from the command line,\n");
    printf("this event will be used with the PAPI interface to write.\n");
    printf("The results for this event will be output to the terminal.\n");
} // end routine.


//-----------------------------------------------------------------------------
// Interpret command line flags.
//-----------------------------------------------------------------------------
void parseCommandLineArgs(int argc, char *argv[])
{
    if(argc < 2) return;

    if((strcmp(argv[1], "--help") == 0) || 
       (strcmp(argv[1], "-help") == 0)  || 
       (strcmp(argv[1], "-h") == 0)) {
        printUsage();
        exit(0);
    }
} // end routine.

//-----------------------------------------------------------------------------
// Main program.
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    int devices, device, i = 0;
    //char str[64], EventName[PAPI_2MAX_STR_LEN];
    (void) device;
    //(void) str;

    /* a maximum of 5 rocm_smi events can be added */
    int eventCount = argc - 1;
    if (eventCount == 0) {
        fprintf(stderr, "No eventnames specified at the command line. A maximum of five events can be added on the command line.\n");
        fprintf(stderr, "Run './papi_native_avail' in your install directory to see available events.\n");
        fprintf(stderr, "Example: './rocm_smi_writeTests rocm_smi:::temp_emergency:device=0:sensor=0'.\n");
        test_skip(__FILE__, __LINE__, "", 0);
    }
    else if (eventCount > 5) {
        fprintf(stderr, "A maximum of five events can be added on the command line.\n");
        test_skip(__FILE__, __LINE__, "", 0); 
    }

    /* only options are for help */
    parseCommandLineArgs(argc, argv);

    // fprintf(stderr, "Setup PAPI counters internally (PAPI)\n");
    int EventSet = PAPI_NULL;
    int ret;
    int k, m, cid=-1;
    (void) m;

    /* PAPI Initialization */
    ret = PAPI_library_init(PAPI_VER_CURRENT);
    if(ret != PAPI_VER_CURRENT) {
        fprintf(stderr, "PAPI_library_init failed, ret=%i [%s]\n", 
            ret, PAPI_strerror(ret));
        exit(-1);
    }

    printf("PAPI version: %d.%d.%d\n", 
        PAPI_VERSION_MAJOR(PAPI_VERSION), 
        PAPI_VERSION_MINOR(PAPI_VERSION), 
        PAPI_VERSION_REVISION(PAPI_VERSION));
    fflush(stdout);

    // Find rocm_smi component index.
    k = PAPI_num_components();                                          // get number of components.
    for (i=0; i<k && cid<0; i++) {                                      // while not found,
        PAPI_component_info_t *aComponent = 
            (PAPI_component_info_t*) PAPI_get_component_info(i);        // get the component info.     
        if (aComponent == NULL) {                                       // if we failed,
            fprintf(stderr,  "PAPI_get_component_info(%i) failed, "
                "returned NULL. %i components reported.\n", i,k);
            exit(-1);    
        }

       if (strcmp("rocm_smi", aComponent->name) == 0) cid=i;            // If we found our match, record it.
    } // end search components.

    if (cid < 0) {                                                      // if no PCP component found,
        fprintf(stderr, "Failed to find rocm_smi component among %i "
            "reported components.\n", k);
        PAPI_shutdown();
        exit(-1); 
    }

    printf("Found ROCM_SMI Component at id %d\n", cid);

    // Add events at a GPU specific level ... eg rocm:::device=0:Whatever
    int eventsRead=0;
    (void) eventsRead;

   // Begin enumeration of all events.
    //long long *values = (long long *) calloc(eventCount, sizeof (long long));
    long long value = 0;
    std::string eventName;
    eventName = "rocm_smi:::NUMDevices";

    force_rocm_smi_init(cid);

    /* collect the total number of devices on the machine */
    CALL_PAPI_OK(PAPI_create_eventset(&EventSet)); 
    CALL_PAPI_OK(PAPI_assign_eventset_component(EventSet, cid)); 
    ret = PAPI_add_named_event(EventSet, eventName.c_str());  
    if (ret == PAPI_OK) {
        CALL_PAPI_OK(PAPI_start(EventSet));
        CALL_PAPI_OK(PAPI_stop(EventSet, &value));
        devices = value;
        printf("Found %i devices.\n", devices);
    } else {
        CALL_PAPI_OK(PAPI_cleanup_eventset(EventSet));          // Delete all events in set.
        CALL_PAPI_OK(PAPI_destroy_eventset(&EventSet));         // destroy the event set.
        fprintf(stderr, "Failed to add_event: %s, with error message: %s\n", eventName.c_str(), PAPI_strerror(ret));
        test_skip(__FILE__, __LINE__,"", 0);
    }

    /* cleanup the eventset to add the command line event */
    CALL_PAPI_OK(PAPI_cleanup_eventset(EventSet));

    /* add the command line events */
    for (i = 0; i < eventCount; i++) {
        eventName = argv[i + 1];
        printf("Event Names are: %s\n", eventName.c_str());
        ret = PAPI_add_named_event(EventSet, eventName.c_str());
        if (ret != PAPI_OK) {
            fprintf(stderr, "Failed to add event: %s, with error message: %s\n", eventName.c_str(), PAPI_strerror(ret));
            test_skip(__FILE__, __LINE__, "", 0);
        }
    }

    long long curmax[1];
    /* start counting */
    CALL_PAPI_OK(PAPI_start(EventSet));
    CALL_PAPI_OK(PAPI_stop(EventSet, curmax));
    printf("curmax: %lli\n", curmax[0] );

    curmax[0] = 50000;
    CALL_PAPI_OK(PAPI_start(EventSet));
    /* set counter*/
    ret = PAPI_write(EventSet, curmax);
    if (ret != PAPI_OK) {
        printf("Failed: %d\n", ret);
        exit(1);
    }

    /* read and output initial counter values */
    //printf("PAPI_read:\n");
    //CALL_PAPI_OK(PAPI_read(EventSet, values));
    //for (i = 0; i < eventCount; i++) {
    //    printf("%s counter values for PAPI_read: %lld\n", argv[i + 1], values[i]);
    //}
    //printf("\n");

    /* must call PAPI_stop or there will be a runtime error with PAPI_write */
    /* stop and output counter */
    //printf("PAPI_stop:\n");
    //CALL_PAPI_OK(PAPI_stop(EventSet, values));
    //for (i = 0; i < eventCount; i++) {
    //    printf("%s counter values for PAPI_stop: %lld\n", argv[i + 1], values[i]);
    //}
    //printf("\n"); 
  
    /* set values array for PAPI_write */
    //for (i = 0; i < eventCount; i++) {
    //    values[i] = 100;
    //} 

    /* write counter values and print to the terminal */
    //CALL_PAPI_OK(PAPI_write(EventSet, values));

    CALL_PAPI_OK(PAPI_stop(EventSet, &value));
    /* output reads after PAPI_write */
    for (i = 0; i < eventCount; i++) {
        printf("%s counter values after PAPI_write: %lld\n", argv[i + 1], value);
    } 

    /* do reads */
    //do_reads(10);

    /*
    CALL_PAPI_OK(PAPI_start(EventSet));
    ret = PAPI_write(EventSet, values);
    if ( ret != PAPI_OK ) {
        PAPI_stop(EventSet, values);                                // Must be stopped.
        PAPI_cleanup_eventset(EventSet);                            // Empty it.
        PAPI_destroy_eventset(&EventSet);                           // Release memory.
        fprintf(stderr, "PAPI_write failure returned %i, = %s.\n", ret, PAPI_strerror(ret));
    } else {
        printf("Call succeeded to set fan_speed to %d RPM.\n", 0);
    }
    */
    // Now try to read it. 
    //CALL_PAPI_OK(PAPI_stop(EventSet, values));
    //printf("After set, read-back of fan value is %lli.\n", values);

    CALL_PAPI_OK(PAPI_cleanup_eventset(EventSet));              // Delete all events in set.
    CALL_PAPI_OK(PAPI_destroy_eventset(&EventSet));             // destroy the event set.

    PAPI_shutdown();                                            // Returns no value.

    /* If we hit here everything ran as expected */
    test_pass( __FILE__ );

    return(0);                                                  // exit OK.
} // end MAIN.
