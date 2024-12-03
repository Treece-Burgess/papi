/****************************/
/* THIS IS OPEN SOURCE CODE */
/****************************/

/**
 * @file    power_report_rocm.cpp
 * CVS:     $Id$
 * @author Tony Castaldo (tonycastaldo@icl.utk.edu)
 * @author Treece Burgess (tburgess@icl.utk.edu) (updated in Winter 2024 to streamline test for GitHub CI;
 *                                                changes tested on Dopamine at ICL with an AMD EPYC 7413)
 *
 * @brief

 * This file reads power limits using ROCM_SMI and writes them
 * periodically to an output file.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include "papi.h"
#include "papi_test.h"

#include "force_init.h"

#define dprintf if (0) printf /* debug printf; change to (1) to enable. */

// --------- GLOBALS -----------
#define NUM_EVENTS 32               /* Max number of GPUs on a node this code can handle. */
int  Interval = 100;                /* set to read every 100 milliseconds */
int  Duration = 5;                  /* set to run for a total of 5 seconds*/
int  DeviceCount = 0;
long long set_powercap[NUM_EVENTS]; 
double ValueScale = 1000000.;       /* Reports are in millionths of watts. */ 

int CTL_Z = 0;                      /* No SIGTSTP signalled yet. */

void cbSignal_SIGTSTP(int signalNumber) {
   (void) signalNumber;                // No warning about unused.
   CTL_Z = 1;                          // Indicate it was received.
} // end signal handler.

/* obtain the number of AMD devices on the machine */
void rocmGetDeviceCount(long long *deviceCount) 
{
    int EventSet = PAPI_NULL;
    int retval, devCntEventCode;

    retval = PAPI_event_name_to_code("rocm_smi:::NUMDevices", &devCntEventCode);
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_event_name_to_code failure", retval);
    }

    retval = PAPI_create_eventset( &EventSet );
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_create_eventset failure", retval);
    }

    retval = PAPI_add_event(EventSet, devCntEventCode);     // Add the event in.
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_add_event failure", retval);
    }

    retval = PAPI_start(EventSet);                          // Start the event set.
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_start failure", retval);
    }

    retval = PAPI_stop(EventSet, deviceCount);               // Stop and get value.
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_stop failure", retval);
    }

    PAPI_cleanup_eventset(EventSet);                        // get rid of this set.
} // end Get Devices.

// Host function
int main( int argc, char** argv )
{

    int retval, i, j;
    int EventSet = PAPI_NULL;
    long long values[NUM_EVENTS];                       // For reading either limit or current power.
    char LimitEventName[NUM_EVENTS][PAPI_MAX_STR_LEN];
    char PowerEventName[NUM_EVENTS][PAPI_MAX_STR_LEN];
    char minEventName[NUM_EVENTS][PAPI_MAX_STR_LEN];
    char maxEventName[NUM_EVENTS][PAPI_MAX_STR_LEN];
    int powerEvents[NUM_EVENTS];                        // PAPI codes for current power events.
    int limitEvents[NUM_EVENTS];                        // PAPI codes for power limit setting.
    int minEvents[NUM_EVENTS];
    int maxEvents[NUM_EVENTS];
    long long minSetting[NUM_EVENTS];
    long long maxSetting[NUM_EVENTS];
    long long OrigLimitFound[NUM_EVENTS];               // original limit read from device.
    int PowerEventCount = 0, LimitEventCount = 0, minEventCount = 0, maxEventCount = 0;
    const PAPI_component_info_t *cmpinfo;
    char event_name[PAPI_MAX_STR_LEN];
    signal(SIGTSTP, cbSignal_SIGTSTP);                  // register the signal handler for CTL_Z.

    /* PAPI Initialization */
    retval = PAPI_library_init( PAPI_VER_CURRENT );
    if( retval != PAPI_VER_CURRENT ) {
        fprintf(stderr, "PAPI_library_init failure returned %i [%s].\n", retval, PAPI_strerror(retval));
        exit(-1);
    }

    printf( "PAPI_VERSION : %4d %6d %7d\n",
            PAPI_VERSION_MAJOR( PAPI_VERSION ),
            PAPI_VERSION_MINOR( PAPI_VERSION ),
            PAPI_VERSION_REVISION( PAPI_VERSION ) );

    int numcmp = PAPI_num_components();

    /* search for the rocm_smi component */ 
    int cid = 0;
    for (cid=0; cid<numcmp; cid++) {
        cmpinfo = PAPI_get_component_info(cid);
        if (cmpinfo == NULL) {
            fprintf(stderr, "PAPI error: PAPI reports %d components, but PAPI_get_component_info(%d) returns NULL pointer.\n", numcmp, cid); 
            test_fail( __FILE__, __LINE__,"PAPI_get_component_info failed\n",-1 );
        } else {
            if ( strstr( cmpinfo->name, "rocm_smi" ) ) break;
        }
    }

    if (cid==numcmp) {
        fprintf(stderr, "ROCM_SMI PAPI Component was not found.\n");       
        exit(-1);
    }

    force_rocm_smi_init(cid);

    if (cmpinfo->disabled) {
        fprintf(stderr, "ROCM_SMI PAPI Component is disabled.\n");
        exit(-1);
    }

    long long llDC;
    rocmGetDeviceCount( &llDC);
    DeviceCount = (int) llDC;
    printf("AMD Device Count: %d.\n", DeviceCount);
    
    if (DeviceCount < 1) {
        fprintf(stderr, "There are no GPUs to manage.\n");
        exit(-1);
    } 

    if (DeviceCount > NUM_EVENTS) {
        fprintf(stderr, "There are %i GPUs found; this code cannot handle more than %i.\n", DeviceCount, NUM_EVENTS);
        exit(-1);
    } 

    // Scan events to find rocm power events.
    int code = PAPI_NATIVE_MASK;
    int ii=0;
    char *ss;
    int did;
    int event_modifier = PAPI_ENUM_FIRST;
    for ( ii=0; ii<cmpinfo->num_native_events; ii++ ) {
        retval = PAPI_enum_cmp_event( &code, event_modifier, cid );
        event_modifier = PAPI_ENUM_EVENTS;
        if ( retval != PAPI_OK ) test_fail( __FILE__, __LINE__, "PAPI_event_code_to_name", retval );

        /* convert rocm_smi event to code */
        retval = PAPI_event_code_to_name( code, event_name );
        if (retval != PAPI_OK) {
            fprintf(stderr, "PAPI_event_code_to_name failure on line %d returned %i [%s].\n", __LINE__, retval, PAPI_strerror(retval));
            exit(1);
        }

        /* get the device id for indexing */
        ss = strstr(event_name, "device=");                       // Look for the device id.
        if (ss == NULL) continue;                                 // Not a valid name.
        did = atoi(ss+7);                                         // convert it.
        if (did >= DeviceCount) continue;                         // Invalid device count.

        /* current average power consumption in microwatts */
        ss = strstr(event_name, "power_average:");
        if (ss != NULL) {
            strncpy(PowerEventName[did], event_name, PAPI_MAX_STR_LEN);
            PowerEventName[did][PAPI_MAX_STR_LEN-1]=0;
            PowerEventCount++;
            continue;
        }

        /* power cap in microwatts, read/write, between min/max */
        ss = strstr(event_name, "power_cap:");
        if (ss != NULL) {
            strncpy(LimitEventName[did], event_name, PAPI_MAX_STR_LEN);
            LimitEventName[did][PAPI_MAX_STR_LEN-1]=0;
            LimitEventCount++;
            continue;
        }

        /* power cap Minimum settable value, in microwatts */
        ss = strstr(event_name, "power_cap_range_min:");
        if (ss != NULL) {
            strncpy(minEventName[did], event_name, PAPI_MAX_STR_LEN);
            minEventName[did][PAPI_MAX_STR_LEN-1]=0;
            minEventCount++;
            continue;
        }

        /* power cap maximim settable value, in microwatts */
        ss = strstr(event_name, "power_cap_range_max");
        if (ss != NULL) {
            strncpy(maxEventName[did], event_name, PAPI_MAX_STR_LEN);
            maxEventName[did][PAPI_MAX_STR_LEN-1]=0;
            maxEventCount++;
            continue;
        }
    } // end of for each event. 


    if (PowerEventCount != DeviceCount || 
        LimitEventCount != DeviceCount ||
          minEventCount != DeviceCount ||
          maxEventCount != DeviceCount) {
        fprintf(stderr, "Too few ROCM_SMI events found; %d devices, %i PowerEvents, %i LimitEvents, %i maxEvents, %i minEvents. Aborting\n",
                DeviceCount, PowerEventCount, LimitEventCount, minEventCount, maxEventCount);
        exit(-1);
    }

    /* for each device, convert the rocm_smi power native event names to an event code */
    for(i=0; i < DeviceCount; i++) {
        /* power_average */
        retval = PAPI_event_name_to_code( ( char * )PowerEventName[i], &powerEvents[i] );
        if( retval != PAPI_OK ) {
            test_fail(__FILE__, __LINE__, "PAPI_event_name_to_code failure", retval);
        }

        /* power_cap */
        retval = PAPI_event_name_to_code( ( char * )LimitEventName[i], &limitEvents[i] );
        if( retval != PAPI_OK ) {
            test_fail(__FILE__, __LINE__, "PAPI_event_name_to_code failure", retval);
        }

        /* power_cap_range_min */
        retval = PAPI_event_name_to_code( ( char * )minEventName[i], &minEvents[i] );
        if( retval != PAPI_OK ) {
            test_fail(__FILE__, __LINE__, "PAPI_event_name_to_code failure", retval);
        }

        /* power_cap_range_max */
        retval = PAPI_event_name_to_code( ( char * )maxEventName[i], &maxEvents[i] );
        if( retval != PAPI_OK ) {
            test_fail(__FILE__, __LINE__, "PAPI_event_name_to_code failure", retval);
        }
    }

    retval = PAPI_create_eventset( &EventSet );
    if( retval != PAPI_OK ) {
        test_fail(__FILE__, __LINE__, "PAPI_create_eventset failure", retval);
    }

    /* get the minimum values we can set each device to */
    retval = PAPI_add_events(EventSet, minEvents, DeviceCount);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "PAPI_add_events (minEvents) failure returned %i [%s].\n", retval, PAPI_strerror(retval));
        exit(-1); 
    }

    retval = PAPI_start(EventSet);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "PAPI_start failure returned %i [%s].\n", retval, PAPI_strerror(retval));
        exit(-1); 
    }

    /* stop to get the minimum settable power value */
    retval = PAPI_stop(EventSet, minSetting);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "%i: PAPI_stop failed, returned %i [%s].\n", __LINE__, retval, PAPI_strerror(retval));
        exit(-1); 
    }

    /* cleanup eventset to be used to get maximum values */
    retval = PAPI_cleanup_eventset(EventSet);
    if (retval != PAPI_OK) {
        fprintf(stderr, "%i: PAPI_cleanup_eventset failed, returned %i [%s].\n", __LINE__, retval, PAPI_strerror(retval));
        exit(-1); 
    }
    
    /* get the maximum values we can set each device to */
    retval = PAPI_add_events(EventSet, maxEvents, DeviceCount);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "PAPI_add_events (maxEvents) failure returned %i [%s].\n", retval, PAPI_strerror(retval));
        exit(-1); 
    }

    retval = PAPI_start(EventSet);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "PAPI_start failure returned %i [%s].\n", retval, PAPI_strerror(retval));
        exit(-1); 
    }

    /* stop to get the maximum settable power value */
    retval = PAPI_stop(EventSet, maxSetting);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "%i: PAPI_stop failed, returned %i [%s].\n", __LINE__, retval, PAPI_strerror(retval));
        exit(-1); 
    }

    /* print out the values for the minimum and maximum we can set each device to */
    /* set desired powercap */
    for (i = 0; i < DeviceCount; i++) {
        printf("Device %i: MinSetting=%.6f, MaxSetting=%.6f.\n", i, (minSetting[i]/ValueScale), (maxSetting[i])/ValueScale);
        set_powercap[i] = 50000000;
        printf("Setting Device %i to have a power cap of %lld.\n", i, set_powercap[i]);
    }

    // check to see if user settings are in range.
    retval = 0;                                             // count violations.

    for (i=0; i<DeviceCount; i++) {
        if (set_powercap[i] < minSetting[i] ||
            set_powercap[i] > maxSetting[i]) {
            fprintf(stderr, "User Power Limit of %lld is out of range for device %i; min=%.6f, max=%.6f.\n", 
                set_powercap[i], i, (minSetting[i]/ValueScale), (maxSetting[i]/ValueScale));
            retval++;                                       // increase violations.
        }
    }

    // exit if any violations.
    if (retval > 0) {
        exit(-1); 
    }

    // Go ahead and read settings, all at once.
    retval = PAPI_cleanup_eventset(EventSet);
    if (retval != PAPI_OK) {
        fprintf(stderr, "%i: PAPI_cleanup_eventset failed, returned %i [%s].\n", __LINE__, retval, PAPI_strerror(retval));
        exit(-1);
    }

    retval = PAPI_add_events(EventSet, limitEvents, DeviceCount);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "PAPI_add_events failure returned %i [%s].\n", retval, PAPI_strerror(retval));
        exit(-1); 
    }

    retval = PAPI_start(EventSet);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "PAPI_start failure returned %i [%s].\n", retval, PAPI_strerror(retval));
        exit(-1); 
    }

    // Read values.
    retval = PAPI_stop(EventSet, OrigLimitFound);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "PAPI_read failed, returned %i [%s].\n", retval, PAPI_strerror(retval));
        exit(-1); 
    }

    PAPI_cleanup_eventset(EventSet);

    /* set the powercap in microwatts */
    for (i=0; i<DeviceCount; i++) {
        printf("Original Power Limit Read: %.6f for %s.\n", (OrigLimitFound[i]/ValueScale), LimitEventName[i]);
        printf("Attempting to set Power Limit for device %i to %.6f.\n", i, (set_powercap[i]/ValueScale));
        retval = PAPI_add_event(EventSet, limitEvents[i]);
        if ( retval != PAPI_OK ) {
            fprintf(stderr, "PAPI_add_event failure returned %i [%s].\n", retval, PAPI_strerror(retval));
            exit(-1); 
        }

        retval = PAPI_start(EventSet);
        if ( retval != PAPI_OK ) {
            fprintf(stderr, "PAPI_start failure returned %i [%s].\n", retval, PAPI_strerror(retval));
            exit(-1); 
        }

        // Try to write user value.
        retval = PAPI_write(EventSet, &set_powercap[i]);
        if (retval != PAPI_OK) {
            test_fail(__FILE__, __LINE__, "PAPI_write failure", retval);
            fprintf(stderr, "PAPI_write(User Limit) device %i failed, returned %i [%s]. May require sudo status.\n", i, retval, PAPI_strerror(retval));
            exit(-1); 
        }

        // Check result.
        retval = PAPI_stop(EventSet, values);
        if ( retval != PAPI_OK ) {
            fprintf(stderr, "%i: PAPI_stop failed, returned %i [%s].\n", __LINE__, retval, PAPI_strerror(retval));
            exit(-1); 
        }
        
        printf("User Limit %.6f set, readback new Limit: %.6f for %s.\n", (set_powercap[i]/ValueScale), (values[0]/ValueScale), LimitEventName[i]);
        PAPI_cleanup_eventset(EventSet);

    } // end handling setting of user Limits.

    // Eventset is cleaned up. Add all power reading events.
    retval = PAPI_add_events(EventSet, powerEvents, DeviceCount);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "PAPI_add_events failure (for power reading events) returned %i [%s].\n", retval, PAPI_strerror(retval));
        PAPI_cleanup_eventset(EventSet);
        PAPI_destroy_eventset(&EventSet);
        exit(-1); 
    }

    retval = PAPI_start(EventSet);
    if( retval != PAPI_OK ) {
        fprintf(stderr, "PAPI_start failure (for power reading events) returned %i [%s].\n", retval, PAPI_strerror(retval));
        PAPI_cleanup_eventset(EventSet);
        PAPI_destroy_eventset(&EventSet);
        exit(-1); 
    }

    //--------------------------------------------------------------------------
    // Main part of program, the reading loop.
    //--------------------------------------------------------------------------
    int runCount = 0;
    long long t1, t2;
    double elapsedSec;
    t1 = PAPI_get_real_nsec();                                  // Get the start time.
    while (CTL_Z == 0) {                                        // While I havent received a CTL-Z;
        usleep(Interval*1000);                                  // .. Wait (Interval given in mS, function arg is uS).
        if (CTL_Z) break;                                       // .. CTL-Z may have interrupted usleep.
        t2 = PAPI_get_real_nsec();                              // .. Find end time.
        PAPI_read(EventSet, values);                            // .. Read instantaneous power consumption.
        elapsedSec = ((double) (t2-t1))/1.e09;                  // .. convert elapsed nanoseconds to seconds.
        printf("%.6f", elapsedSec);                             // .. print the time elapsed in seconds
        for (i=0; i<DeviceCount; i++) {                         // .. for each device
            printf("\t%.6f", (values[i]/ValueScale));           // .... print out a value
        }
        printf("\n");                                           /* Finish the line */

        runCount++;                                             // Count a run.
        if (Duration > 0 && elapsedSec >= Duration) break;  // Exit if time is up.
    }

    if (CTL_Z) fprintf(stderr, "Received CTL_Z signal (SIGTSTP).\n");
    else       fprintf(stderr, "Runtime duration of %i seconds expired.\n", Duration);

    retval = PAPI_stop(EventSet, values);
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_stop failure", retval);
    }

    fprintf(stderr, "Total reads: %i.\n", runCount);

    /* cleanup event set */
    retval = PAPI_cleanup_eventset(EventSet);
    if (retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_cleanup_eventset failure", retval);
    }

    /* destroy the eventset  */
    retval = PAPI_destroy_eventset(&EventSet);
    if (retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_destroy_eventset failure", retval);
    }

    /* if we make it here everything ran successfully */
    PAPI_shutdown();
    test_pass(__FILE__);

    return 0;
} // end main.
