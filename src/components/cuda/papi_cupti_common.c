/**
 * @file    papi_cupti_common.c
 *
 * @author  Treece Burgess tburgess@icl.utk.edu (updated in 2024, redesigned to add device qualifier support.)
 * @author  Anustuv Pal    anustuv@icl.utk.edu
 */

#include <dlfcn.h>
#include <link.h>
#include <libgen.h>
#include <papi.h>
#include "papi_memory.h"

#include "cupti_config.h"
#include "papi_cupti_common.h"

static void *dl_drv, *dl_rt;

const char *linked_cudart_path;
const char *error_string;

void *dl_cupti;

unsigned int _cuda_lock;

/* cuda driver function pointers */
CUresult ( *cuCtxGetCurrentPtr ) (CUcontext *);
CUresult ( *cuCtxSetCurrentPtr ) (CUcontext);
CUresult ( *cuCtxDestroyPtr ) (CUcontext);
CUresult ( *cuCtxCreatePtr ) (CUcontext *pctx, unsigned int flags, CUdevice dev);
CUresult ( *cuCtxGetDevicePtr ) (CUdevice *);
CUresult ( *cuDeviceGetPtr ) (CUdevice *, int);
CUresult ( *cuDeviceGetCountPtr ) (int *);
CUresult ( *cuDeviceGetNamePtr ) (char *, int, CUdevice);
CUresult ( *cuDevicePrimaryCtxRetainPtr ) (CUcontext *pctx, CUdevice);
CUresult ( *cuDevicePrimaryCtxReleasePtr ) (CUdevice);
CUresult ( *cuInitPtr ) (unsigned int);
CUresult ( *cuGetErrorStringPtr ) (CUresult error, const char** pStr);
CUresult ( *cuCtxPopCurrentPtr ) (CUcontext * pctx);
CUresult ( *cuCtxPushCurrentPtr ) (CUcontext pctx);
CUresult ( *cuCtxSynchronizePtr ) ();
CUresult ( *cuDeviceGetAttributePtr ) (int *, CUdevice_attribute, CUdevice);

/* cuda runtime function pointers */
cudaError_t ( *cudaGetDeviceCountPtr ) (int *);
cudaError_t ( *cudaGetDevicePtr ) (int *);
const char *( *cudaGetErrorStringPtr ) (cudaError_t);
cudaError_t ( *cudaSetDevicePtr ) (int);
cudaError_t ( *cudaGetDevicePropertiesPtr ) (struct cudaDeviceProp* prop, int  device);
cudaError_t ( *cudaDeviceGetAttributePtr ) (int *value, enum cudaDeviceAttr attr, int device);
cudaError_t ( *cudaFreePtr ) (void *);
cudaError_t ( *cudaDriverGetVersionPtr ) (int *);
cudaError_t ( *cudaRuntimeGetVersionPtr ) (int *);

/* cupti function pointer */
CUptiResult ( *cuptiGetVersionPtr ) (uint32_t* );

/**@class load_cuda_sym
 * @brief Search for libcuda.so.
 */
static int load_cuda_sym(void)
{
    dl_drv = dlopen("libcuda.so", RTLD_NOW | RTLD_GLOBAL);
    if (!dl_drv) {
        ERRDBG("Loading installed libcuda.so failed. Check that cuda drivers are installed.\n");
        goto fn_fail;
    }

    cuCtxSetCurrentPtr           = DLSYM_AND_CHECK(dl_drv, "cuCtxSetCurrent");
    cuCtxGetCurrentPtr           = DLSYM_AND_CHECK(dl_drv, "cuCtxGetCurrent");
    cuCtxDestroyPtr              = DLSYM_AND_CHECK(dl_drv, "cuCtxDestroy");
    cuCtxCreatePtr               = DLSYM_AND_CHECK(dl_drv, "cuCtxCreate");
    cuCtxGetDevicePtr            = DLSYM_AND_CHECK(dl_drv, "cuCtxGetDevice");
    cuDeviceGetPtr               = DLSYM_AND_CHECK(dl_drv, "cuDeviceGet");
    cuDeviceGetCountPtr          = DLSYM_AND_CHECK(dl_drv, "cuDeviceGetCount");
    cuDeviceGetNamePtr           = DLSYM_AND_CHECK(dl_drv, "cuDeviceGetName");
    cuDevicePrimaryCtxRetainPtr  = DLSYM_AND_CHECK(dl_drv, "cuDevicePrimaryCtxRetain");
    cuDevicePrimaryCtxReleasePtr = DLSYM_AND_CHECK(dl_drv, "cuDevicePrimaryCtxRelease");
    cuInitPtr                    = DLSYM_AND_CHECK(dl_drv, "cuInit");
    cuGetErrorStringPtr          = DLSYM_AND_CHECK(dl_drv, "cuGetErrorString");
    cuCtxPopCurrentPtr           = DLSYM_AND_CHECK(dl_drv, "cuCtxPopCurrent");
    cuCtxPushCurrentPtr          = DLSYM_AND_CHECK(dl_drv, "cuCtxPushCurrent");
    cuCtxSynchronizePtr          = DLSYM_AND_CHECK(dl_drv, "cuCtxSynchronize");
    cuDeviceGetAttributePtr      = DLSYM_AND_CHECK(dl_drv, "cuDeviceGetAttribute");

    Dl_info info;
    dladdr(cuCtxSetCurrentPtr, &info);
    LOGDBG("CUDA driver library loaded from %s\n", info.dli_fname);
    return PAPI_OK;
fn_fail:
    return PAPI_EMISC;
}

static int unload_cuda_sym(void)
{
    if (dl_drv) {
        dlclose(dl_drv);
        dl_drv = NULL;
    }
    cuCtxSetCurrentPtr           = NULL;
    cuCtxGetCurrentPtr           = NULL;
    cuCtxDestroyPtr              = NULL;
    cuCtxCreatePtr               = NULL;
    cuCtxGetDevicePtr            = NULL;
    cuDeviceGetPtr               = NULL;
    cuDeviceGetCountPtr          = NULL;
    cuDeviceGetNamePtr           = NULL;
    cuDevicePrimaryCtxRetainPtr  = NULL;
    cuDevicePrimaryCtxReleasePtr = NULL;
    cuInitPtr                    = NULL;
    cuGetErrorStringPtr          = NULL;
    cuCtxPopCurrentPtr           = NULL;
    cuCtxPushCurrentPtr          = NULL;
    cuCtxSynchronizePtr          = NULL;
    cuDeviceGetAttributePtr      = NULL;
    return PAPI_OK;
}

void *cuptic_load_dynamic_syms(const char *parent_path, const char *dlname, const char *search_subpaths[])
{
    void *dl = NULL;
    char lookup_path[PATH_MAX];
    char *found_files[CUPTIU_MAX_FILES];
    int i, count;
    for (i = 0; search_subpaths[i] != NULL; i++) {
        sprintf(lookup_path, search_subpaths[i], parent_path, dlname);
        dl = dlopen(lookup_path, RTLD_NOW | RTLD_GLOBAL);
        if (dl) {
            return dl;
        }
    }
    count = cuptiu_files_search_in_path(dlname, parent_path, found_files);
    for (i = 0; i < count; i++) {
        dl = dlopen(found_files[i], RTLD_NOW | RTLD_GLOBAL);
        if (dl) {
            break;
        }
    }
    for (i = 0; i < count; i++) {
        papi_free(found_files[i]);
    }
    return dl;
}

/**@class load_cudart_sym
 * @brief Search for libcudart.so. Order of search is outlined below.
 *
 * 1. If a user sets PAPI_CUDA_RUNTIME, this will take precedent over
 *    the options listed below to be searched.
 * 2. If we fail to collect libcudart.so from PAPI_CUDA_RUNTIME or it is not set,
 *    we will search the path defined with PAPI_CUDA_ROOT; as this is supposed to always be set.
 * 3. If we fail to collect libcudart.so from steps 1 and 2, then we will search the linux
 *    default directories listed by /etc/ld.so.conf. As a note, updating the LD_LIBRARY_PATH is
 *    advised for this option.
 * 4. We use dlopen to search for libcudart.so.
 *    If this fails, then we failed to find libcudart.so
 */
static int load_cudart_sym(void)
{
    char dlname[] = "libcudart.so";
    char lookup_path[PATH_MAX];

    /* search PAPI_CUDA_RUNTIME for libcudart.so (takes precedent over PAPI_CUDA_ROOT) */
    char *papi_cuda_runtime = getenv("PAPI_CUDA_RUNTIME");
    if (papi_cuda_runtime) {
        sprintf(lookup_path, "%s/%s", papi_cuda_runtime, dlname);
        dl_rt = dlopen(lookup_path, RTLD_NOW | RTLD_GLOBAL);
    }

    const char *standard_paths[] = {
        "%s/lib64/%s",
        NULL,
    };

    /* search PAPI_CUDA_ROOT for libcudart.so */
    char *papi_cuda_root = getenv("PAPI_CUDA_ROOT");
    if (papi_cuda_root && !dl_rt) {
        dl_rt = cuptic_load_dynamic_syms(papi_cuda_root, dlname, standard_paths);
    }

    /* search linux default directories for libcudart.so */
    if (linked_cudart_path && !dl_rt) {
        dl_rt = cuptic_load_dynamic_syms(linked_cudart_path, dlname, standard_paths);
    }

    /* last ditch effort to find libcudart.so */
    if (!dl_rt) {
        dl_rt = dlopen(dlname, RTLD_NOW | RTLD_GLOBAL);
        if (!dl_rt) {
            ERRDBG("Loading libcudart.so failed. Try setting PAPI_CUDA_ROOT\n");
            goto fn_fail;
        }
    }

    cudaGetDevicePtr           = DLSYM_AND_CHECK(dl_rt, "cudaGetDevice");
    cudaGetDeviceCountPtr      = DLSYM_AND_CHECK(dl_rt, "cudaGetDeviceCount");
    cudaGetDevicePropertiesPtr = DLSYM_AND_CHECK(dl_rt, "cudaGetDeviceProperties");
    cudaGetErrorStringPtr      = DLSYM_AND_CHECK(dl_rt, "cudaGetErrorString");
    cudaDeviceGetAttributePtr  = DLSYM_AND_CHECK(dl_rt, "cudaDeviceGetAttribute");
    cudaSetDevicePtr           = DLSYM_AND_CHECK(dl_rt, "cudaSetDevice");
    cudaFreePtr                = DLSYM_AND_CHECK(dl_rt, "cudaFree");
    cudaDriverGetVersionPtr    = DLSYM_AND_CHECK(dl_rt, "cudaDriverGetVersion");
    cudaRuntimeGetVersionPtr   = DLSYM_AND_CHECK(dl_rt, "cudaRuntimeGetVersion");

    Dl_info info;
    dladdr(cudaGetDevicePtr, &info);
    LOGDBG("CUDA runtime library loaded from %s\n", info.dli_fname);
    return PAPI_OK;
fn_fail:
    return PAPI_EMISC;
}

static int unload_cudart_sym(void)
{
    if (dl_rt) {
        dlclose(dl_rt);
        dl_rt = NULL;
    }
    cudaGetDevicePtr           = NULL;
    cudaGetDeviceCountPtr      = NULL;
    cudaGetDevicePropertiesPtr = NULL;
    cudaGetErrorStringPtr      = NULL;
    cudaDeviceGetAttributePtr  = NULL;
    cudaSetDevicePtr           = NULL;
    cudaFreePtr                = NULL;
    cudaDriverGetVersionPtr    = NULL;
    cudaRuntimeGetVersionPtr   = NULL;
    return PAPI_OK;
}

/**@class load_cupti_common_sym
 * @brief Search for libcupti.so. Order of search is outlined below.
 *
 * 1. If a user sets PAPI_CUDA_CUPTI, this will take precedent over
 *    the options listed below to be searched.
 * 2. If we fail to collect libcupti.so from PAPI_CUDA_CUPTI or it is not set,
 *    we will search the path defined with PAPI_CUDA_ROOT; as this is supposed to always be set.
 * 3. If we fail to collect libcupti.so from steps 1 and 2, then we will search the linux
 *    default directories listed by /etc/ld.so.conf. As a note, updating the LD_LIBRARY_PATH is
 *    advised for this option.
 * 4. We use dlopen to search for libcupti.so.
 *    If this fails, then we failed to find libcupti.so
 */
static int load_cupti_common_sym(void)
{
    char dlname[] = "libcupti.so";
    char lookup_path[PATH_MAX];

    /* search PAPI_CUDA_CUPTI for libcupti.so (takes precedent over PAPI_CUDA_ROOT) */
    char *papi_cuda_cupti = getenv("PAPI_CUDA_CUPTI");
    if (papi_cuda_cupti) {
        sprintf(lookup_path, "%s/%s", papi_cuda_cupti, dlname);
        dl_cupti = dlopen(lookup_path, RTLD_NOW | RTLD_GLOBAL);
    }

    const char *standard_paths[] = {
        "%s/extras/CUPTI/lib64/%s",
        "%s/lib64/%s",
        NULL,
    };

    /* search PAPI_CUDA_ROOT for libcupti.so */
    char *papi_cuda_root = getenv("PAPI_CUDA_ROOT");
    if (papi_cuda_root && !dl_cupti) {
        dl_cupti = cuptic_load_dynamic_syms(papi_cuda_root, dlname, standard_paths);
    }

    /* search linux default directories for libcupti.so */
    if (linked_cudart_path && !dl_cupti) {
        dl_cupti = cuptic_load_dynamic_syms(linked_cudart_path, dlname, standard_paths);
    }

    /* last ditch effort to find libcupti.so */
    if (!dl_cupti) {
        dl_cupti = dlopen(dlname, RTLD_NOW | RTLD_GLOBAL);
        if (!dl_cupti) {
            ERRDBG("Loading libcupti.so failed. Try setting PAPI_CUDA_ROOT\n");
            goto fn_fail;
        }
    }

    cuptiGetVersionPtr = DLSYM_AND_CHECK(dl_cupti, "cuptiGetVersion");

    Dl_info info;
    dladdr(cuptiGetVersionPtr, &info);
    LOGDBG("CUPTI library loaded from %s\n", info.dli_fname);
    return PAPI_OK;
fn_fail:
    return PAPI_EMISC;
}

static int unload_cupti_common_sym(void)
{
    if (dl_cupti) {
        dlclose(dl_cupti);
        dl_cupti = NULL;
    }
    cuptiGetVersionPtr = NULL;
    return PAPI_OK;
}

static int util_load_cuda_sym(void)
{
    int papi_errno;
    papi_errno = load_cuda_sym();
    papi_errno += load_cudart_sym();
    papi_errno += load_cupti_common_sym();
    if (papi_errno != PAPI_OK) {
        return PAPI_EMISC;
    }
    else
        return PAPI_OK;
}

static void unload_linked_cudart_path(void)
{
    if (linked_cudart_path) {
        papi_free((void*) linked_cudart_path);
        linked_cudart_path = NULL;
    }
}

int cuptic_shutdown(void)
{
    unload_cuda_sym();
    unload_cudart_sym();
    unload_cupti_common_sym();
    unload_linked_cudart_path();
    return PAPI_OK;
}

static int util_dylib_cu_runtime_version(void)
{
    int runtimeVersion;
    cudaArtCheckErrors(cudaRuntimeGetVersionPtr(&runtimeVersion), return PAPI_EMISC);
    return runtimeVersion;
}

static int util_dylib_cupti_version(void)
{
    unsigned int cuptiVersion;
    cuptiCheckErrors(cuptiGetVersionPtr(&cuptiVersion), return PAPI_EMISC);
    return cuptiVersion;
}


/** @class cuptic_device_get_count
  * @brief Get total number of gpus on the machine that are compute
  *        capable..
  * @param *num_gpus 
  *    Collect the total number of gpus.
*/
int cuptic_device_get_count(int *num_gpus)
{
    cudaError_t cuda_err;

    /* find the total number of compute-capable devices */
    cuda_err = cudaGetDeviceCountPtr(num_gpus);
    if (cuda_err != cudaSuccess) {
        cuptic_err_set_last(cudaGetErrorStringPtr(cuda_err));
        return PAPI_EMISC;
    }
    return PAPI_OK;
}

static int get_gpu_compute_capability(int dev_num, int *cc)
{
    int cc_major, cc_minor;
    cudaError_t cuda_errno;
    cuda_errno = cudaDeviceGetAttributePtr(&cc_major, cudaDevAttrComputeCapabilityMajor, dev_num);
    if (cuda_errno != cudaSuccess) {
        cuptic_err_set_last(cudaGetErrorStringPtr(cuda_errno));
        return PAPI_EMISC;
    }
    cuda_errno = cudaDeviceGetAttributePtr(&cc_minor, cudaDevAttrComputeCapabilityMinor, dev_num);
    if (cuda_errno != cudaSuccess) {
        cuptic_err_set_last(cudaGetErrorStringPtr(cuda_errno));
        return PAPI_EMISC;
    }
    *cc = cc_major * 10 + cc_minor;
    return PAPI_OK;
}
typedef enum {GPU_COLLECTION_UNKNOWN, GPU_COLLECTION_ALL_PERF, GPU_COLLECTION_MIXED, GPU_COLLECTION_ALL_EVENTS, GPU_COLLECTION_ALL_CC70} gpu_collection_e;

static int util_gpu_collection_kind(gpu_collection_e *coll_kind)
{
    int papi_errno = PAPI_OK;
    static gpu_collection_e kind = GPU_COLLECTION_UNKNOWN;
    if (kind != GPU_COLLECTION_UNKNOWN) {
        goto fn_exit;
    }

    int total_gpus;
    papi_errno = cuptic_device_get_count(&total_gpus);
    if (papi_errno != PAPI_OK) {
        goto fn_exit;
    }

    int i, cc;
    int count_gte_cc70 = 0, count_eq_cc70 = 0, count_lte_cc70 = 0;
    for (i=0; i<total_gpus; i++) {
        papi_errno = get_gpu_compute_capability(i, &cc);
        if (papi_errno != PAPI_OK) {
            return papi_errno;
        }
        if (cc >= 70) {
            ++count_gte_cc70;
        }
        if (cc == 70) {
            ++count_eq_cc70;
        }
        if (cc <= 70) {
            ++count_lte_cc70;
        }
    }

    // All devices detected are cc >= 7.0.
    // Therefore use Perfworks API.
    if (count_gte_cc70 == total_gpus) {
        kind = GPU_COLLECTION_ALL_PERF;
        goto fn_exit;
    }
    // All devices detected are cc = 7.0.
    // Therefore Events API or Perfworks API could be used.
    else if (count_eq_cc70 == total_gpus) {
        kind = GPU_COLLECTION_ALL_CC70;
        goto fn_exit;
    }
    // All devices detected are <= 7.0.
    // Therefore use Events API.
    else if (count_lte_cc70 == total_gpus) {
        kind = GPU_COLLECTION_ALL_EVENTS;
        goto fn_exit;
    }
    // Devices detected have mixed compute capabilities.
    else {
        kind = GPU_COLLECTION_MIXED;
        goto fn_exit;
    }

fn_exit:
    *coll_kind = kind;
    return papi_errno;
}

/** @class cuptic_err_set_last
  * @brief For the last error, set an error message.
  * @param *error_str
  *    Error message to be set.
*/
int cuptic_err_set_last(const char *error_str)
{
    error_string = error_str;
    return PAPI_OK;
}

/** @class cuptic_err_get_last
  * @brief Get the last error message set.
  * @param **error_str
  *    Error message to be returned.
*/
int cuptic_err_get_last(const char **error_str)
{
    *error_str = error_string;
    return PAPI_OK;
}

static int dl_iterate_phdr_cb(struct dl_phdr_info *info, __attribute__((unused)) size_t size, __attribute__((unused)) void *data)
{
    const char *library_name = "libcudart.so";
    char *library_path = strdup(info->dlpi_name);

    if (library_path != NULL && strstr(library_path, library_name) != NULL) {
        linked_cudart_path = strdup(dirname(dirname((char *) library_path)));
    }

    free(library_path);
    return PAPI_OK;
}

static int get_user_cudart_path(void)
{
    dl_iterate_phdr(dl_iterate_phdr_cb, NULL);
    if (NULL == linked_cudart_path) {
        return PAPI_EMISC;
    }
    return PAPI_OK;
}

int cuptic_init(void)
{
    int papi_errno;
    char *PAPI_CUDA_API = getenv("PAPI_CUDA_API");

    papi_errno = get_user_cudart_path();
    if (papi_errno == PAPI_OK) {
        LOGDBG("Linked cudart root: %s\n", linked_cudart_path);
    }
    else {
        LOGDBG("Target application not linked with cuda runtime libraries.\n");
    }
    papi_errno = util_load_cuda_sym();
    if (papi_errno != PAPI_OK) {
        cuptic_err_set_last("Unable to load CUDA library functions.");
        goto fn_exit;
    }

    gpu_collection_e kind;
    papi_errno = util_gpu_collection_kind(&kind);
    if (papi_errno != PAPI_OK) {
        goto fn_exit;
    }

    if (kind == GPU_COLLECTION_MIXED) {
        // Default API is Perfworks (CC >= 7.0), users do not need to set PAPI_CUDA_API for this
        if (PAPI_CUDA_API == NULL) {
            cuptic_err_set_last("System includes multiple compute capabilities: <7.0, =7.0, >7.0."
                                " Only support for CC >=7.0 enabled."); 
        }
        // User set PAPI_CUDA_API; therefore, we use the Events API
        else {
            printf("Using Events API.\n");
            cuptic_err_set_last("System includes multiple compute capabilities: <7.0, =7.0, >7.0."
                                " Only support for CC <=7.0 enabled.");

        }
        papi_errno = PAPI_PARTIAL;
        goto fn_exit;
    }

fn_exit:
    return papi_errno;
}

int cuptic_determine_runtime_api(void) 
{
    static int cupti_api = -1;
    if (cupti_api != -1) {
        goto fn_exit;
    }

    char *PAPI_CUDA_API = getenv("PAPI_CUDA_API");

    gpu_collection_e gpus_kind;
    int papi_errno = util_gpu_collection_kind(&gpus_kind);
    if (papi_errno != PAPI_OK) {
        goto fn_exit;
    }

    // For the Perfworks API to be operational in the Cuda component,
    // users must link with a Cuda toolkit version that has a CUPTI version >= 13.
    unsigned int cuptiVersion = util_dylib_cupti_version();
    if (!(cuptiVersion >= CUPTI_PROFILER_API_MIN_SUPPORTED_VERSION) && PAPI_CUDA_API == NULL) {
        cupti_api = 0;
        goto fn_exit;
    }
    //TODO: Once the Events API is added back in, add a similar check as above

    switch (gpus_kind) {
        // All devices have CC's <= 7.0
        // Must use Events API
        case GPU_COLLECTION_ALL_EVENTS:
            //TODO: Once the Events API is re-implemented, make sure to add a version check as
            //      seen in GPU_COLLECTION_ALL_PERF.
            cupti_api = API_EVENTS;
            break;
        // All devices have CC's >= 7.0
        // Must use Perfworks API
        case GPU_COLLECTION_ALL_PERF:
            if (cuptiVersion >= CUPTI_PROFILER_API_MIN_SUPPORTED_VERSION)
                cupti_api = API_PERFWORKS;
            break;
        // ALL devices have CC's = 7.0
        // Perfworks or Events API can be used
        case GPU_COLLECTION_ALL_CC70:
        // Devices are mixed with CC's > 7.0, CC's = 7.0, and CC's < 7.0
        // Default will be to use Perfworks API, user can change this by setting PAPI_CUDA_API.
        case GPU_COLLECTION_MIXED:
            if (cuptiVersion >= CUPTI_PROFILER_API_MIN_SUPPORTED_VERSION
                && PAPI_CUDA_API == NULL) {
                cupti_api = API_PERFWORKS;
            }
            // TODO: Add check for CUPTI version once Events API is added back.
            else if (PAPI_CUDA_API != NULL) {
                int result = strcasecmp(PAPI_CUDA_API, "EVENTS");
                if (result == 0)
                    cupti_api = API_EVENTS;
            }
            break;
        default:
            goto fn_exit;
    }

fn_exit:
    return cupti_api;
}

struct cuptic_info {
    CUcontext ctx;
};

/** @class cuptic_ctxarr_create
  * @brief Allocate memory for pinfo.
  * @param *pinfo
  *    Instance of a struct that holds read count, running, cuptic_t
  *    and cuptip_gpu_state_t.
*/
int cuptic_ctxarr_create(cuptic_info_t *pinfo)
{
    COMPDBG("Entering.\n");
    int total_gpus, papi_errno;

    /* retrieve total number of compute-capable devices */
    papi_errno = cuptic_device_get_count(&total_gpus);
    if (papi_errno != PAPI_OK) {
        return PAPI_EMISC;
    }
  
    /* allocate memory */ 
    *pinfo = (cuptic_info_t) papi_calloc (total_gpus, sizeof(*pinfo));
    if (*pinfo == NULL) {
        return PAPI_ENOMEM;
    }

    return PAPI_OK;
}


/** @class cuptic_ctxarr_update_current
  * @brief Updating the current Cuda context.
  * @param info
  *    Struct that contains a Cuda context, that can be indexed into based
  *    on device id.
  * @param evt_dev_id
  *    Device id from an appended device qualifier (e.g. :device=#).
*/
int cuptic_ctxarr_update_current(cuptic_info_t info, int evt_dev_id)
{
    CUcontext pctx;
    CUresult cuda_err;
    CUdevice dev_id;

    // If a Cuda context already exists, get it
    cuda_err = cuCtxGetCurrentPtr(&pctx);
    if (cuda_err != CUDA_SUCCESS) {
        return PAPI_EMISC;
    }

    // Get the Device ID for the existing Cuda context
    if (pctx != NULL) {
        cuda_err = cuCtxGetDevicePtr(&dev_id);
        if (cuda_err != CUDA_SUCCESS) {
            return PAPI_EMISC;
        }
     }

    // A context is not stored for the :device=# qualifier
    if (info[evt_dev_id].ctx == NULL) {
        // Cuda context was not found or a user did not provide an appropriate Cuda context for the
        // device qualifier id that was supplied
        if (pctx == NULL || dev_id != evt_dev_id) {
            // If multiple devices are found on the machine, then we need to call cudaSetDevice
            SUBDBG("A Cuda context was not found. Therefore, one is created for device: %d\n", evt_dev_id);
            cudaArtCheckErrors(cudaSetDevicePtr(evt_dev_id), return PAPI_EMISC);
            cudaArtCheckErrors(cudaFreePtr(0), return PAPI_EMISC);

            cudaCheckErrors(cuCtxGetCurrentPtr(&info[evt_dev_id].ctx), return PAPI_EMISC);
            cudaCheckErrors(cuCtxPopCurrentPtr(&info[evt_dev_id].ctx), return PAPI_EMISC);
        }
        // Cuda context was found
        else {
            SUBDBG("A cuda context was found for device: %d\n", evt_dev_id);
            cudaCheckErrors(cuCtxGetCurrentPtr(&info[evt_dev_id].ctx), return PAPI_EMISC);
        }
    }
    // If the Cuda context has changed for a device keep the first one seen, but output a warning
    else if (pctx != NULL){
        if (evt_dev_id == dev_id) {
            if (info[dev_id].ctx != pctx) {
                ERRDBG("Warning: cuda context for device %d has changed from %p to %p\n", dev_id, info[dev_id].ctx, pctx);
            }
        }
    }

    return PAPI_OK;
}

int cuptic_ctxarr_get_ctx(cuptic_info_t info, int gpu_idx, CUcontext *ctx)
{
    *ctx = info[gpu_idx].ctx;
    return PAPI_OK;
}

int cuptic_ctxarr_destroy(cuptic_info_t *pinfo)
{
    papi_free(*pinfo);
    *pinfo = NULL;
    return PAPI_OK;
}

/* Functions based on bitmasking to detect gpu exclusivity */
typedef int64_t gpu_occupancy_t;
static gpu_occupancy_t global_gpu_bitmask;

static int event_name_get_gpuid(const char *name, int *gpuid)
{
    int papi_errno = PAPI_OK;
    char *token;
    char *copy = strdup(name);

    token = strtok(copy, "=");
    if (token == NULL) {
        goto fn_fail;
    }
    token = strtok(NULL, "\0");
    if (token == NULL) {
        goto fn_fail;
    }
    *gpuid = strtol(token, NULL, 10);

fn_exit:
    papi_free(copy);
    return papi_errno;
fn_fail:
    papi_errno = PAPI_EINVAL;
    goto fn_exit;
}

static int _devmask_events_get(cuptiu_event_table_t *evt_table, gpu_occupancy_t *bitmask)
{
    int papi_errno = PAPI_OK, gpu_id;
    long i;
    gpu_occupancy_t acq_mask = 0;
    cuptiu_event_t *evt_rec;
    for (i = 0; i < evt_table->count; i++) {
        acq_mask |= (1 << evt_table->cuda_devs[i]);
    }
    *bitmask = acq_mask;
fn_exit:
    return papi_errno;
}

int cuptic_device_acquire(cuptiu_event_table_t *evt_table)
{
    gpu_occupancy_t bitmask;
    int papi_errno = _devmask_events_get(evt_table, &bitmask);
    if (papi_errno != PAPI_OK) {
        return papi_errno;
    }
    if (bitmask & global_gpu_bitmask) {
        return PAPI_ECNFLCT;
    }
    _papi_hwi_lock(_cuda_lock);
    global_gpu_bitmask |= bitmask;
    _papi_hwi_unlock(_cuda_lock);
    return PAPI_OK;
}

int cuptic_device_release(cuptiu_event_table_t *evt_table)
{
    gpu_occupancy_t bitmask;
    int papi_errno = _devmask_events_get(evt_table, &bitmask);
    if (papi_errno != PAPI_OK) {
        return papi_errno;
    }
    if ((bitmask & global_gpu_bitmask) != bitmask) {
        return PAPI_EMISC;
    }
    _papi_hwi_lock(_cuda_lock);
    global_gpu_bitmask ^= bitmask;
    _papi_hwi_unlock(_cuda_lock);
    return PAPI_OK;
}

/** @class cuptiu_dev_set
  * @brief For a Cuda native event, set the device ID.
  *
  * @param *bitmap
  *   Device map.
  * @param i
  *   Device ID.
*/
int cuptiu_dev_set(cuptiu_bitmap_t *bitmap, int i)
{
    *bitmap |= (1ULL << i);
    return PAPI_OK;
}

/** @class cuptiu_dev_check
  * @brief For a Cuda native event, check for a valid device ID.
  *
  * @param *bitmap
  *   Device map.
  * @param i
  *   Device ID.
*/
int cuptiu_dev_check(cuptiu_bitmap_t bitmap, int i)
{
    return (bitmap & (1ULL << i));
}
