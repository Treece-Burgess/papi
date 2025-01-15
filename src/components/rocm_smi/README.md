# ROCM_SMI Component

The `rocm_smi` (System Management Interface) component exposes hardware management
counters and controls for AMD GPUs, such as power consumption, fan speed and
temperature readings; it also allows capping of power consumption.

* [Enabling the ROCM_SMI Component](#enabling-the-rocm_smi-component)
* [Environment Variables](#environment-variables)
* [Known Limitations](#known-limitations)
* [FAQ](#faq)
***
## Enabling the ROCM_SMI Component

To enable reading or writing of `rocm_smi` counters, the user needs to link
against a PAPI library that was configured with the `rocm_smi` component enabled.
As an example the following command: `./configure --with-components="rocm_smi"`
is sufficient to enable the component.

Typically, the utility `papi_component_avail` (available in `papi/src/utils/papi_component_avail`) will display the compiled in components to the user. If the `rocm_smi` component was successfully compiled in then it will show underneath the **Active components:** section; otherwise, the component is disabled and a disabled reason will show. 

## Environment Variables

For the `rocm_smi` component to be functional, PAPI requires one environment variable: `PAPI_ROCMSMI_ROOT`. This environment variable must be set for both compile and run time. As a note, `PAPI_ROCMSMI_ROOT` must be set to find the subdirectory `rocm_smi`. The location for the `rocm_smi` subdirectory can vary between ROCm versions. See below for examples.

For ROCm versions >= 6.0:

```bash
export PAPI_ROCMSMI_ROOT=/opt/rocm/include/rocm_smi
```

Here we expect the following standard headers to be found: `kfd_ioctl.h` and `rocm_smi.h`.


For ROCm versions < 6.0:

```bash
export PAPI_ROCMSMI_ROOT=/opt/rocm/rocm_smi
```

Here we expect the following standard directories to be found:
```bash
PAPI_ROCMSMI_ROOT/lib
PAPI_ROCMSMI_ROOT/include/rocm_smi
```

## Known Limitations

* Only sets of metrics and events that can be gathered in a single pass are supported.

* Although AMD metrics may be floating point, all values are recast and returned as long long integers.

    The binary image of a `double` is intact; but users must recast to `double` for display purposes.

***
## FAQ

1. [Unusual installations](#unusual-installations)

## Unusual installations
For the `rocm_smi` component to be operational, it must find the dynamic library `librocm_smi64.so`.
This is normally found in the directory `/opt/rocm/rocm_smi/lib` for ROCm versions < 6.0 or the 
directory `/opt/rocm/lib` for ROCm versions >= 6.0. Along with these two locations, `librocm_smi64.so`
could be found in one of the Linux default directories listed by `/etc/ld.so.conf` usually `/usr/lib64`, `/lib64`,
`/usr/lib` and `/lib`. 

If the library is not found (or is not functional) then the component will be listed as "disabled" 
with a reason explaining the problem. If the dynamic library was not found, it is not in the expected places. 

The system will search the directories listed in **LD\_LIBRARY\_PATH**, separated by colons `:`.
This can be se using export: 

    export LD_LIBRARY_PATH=/WhereALibraryCanBeFound:$LD_LIBRARY_PATH
