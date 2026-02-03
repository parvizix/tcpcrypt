# PTUser IPv4 Blocker (user-mode sample)

This simple user-mode program opens the `NdisProt` control device, lists the
available bindings, opens a selected binding, and installs an IPv4 block list
using the driver's `IOCTL_PTUSERIO_SET_IPv4_BLOCK_FILTER` interface.

## Build (MSVC)

```cmd
cl /W4 /EHsc /DWIN32 /I. ptuser_blocker.c /link ws2_32.lib
```

## Usage

```cmd
ptuser_blocker.exe [--index N] <ipv4> [ipv4 ...]
```

Examples:

```cmd
ptuser_blocker.exe --index 0 192.168.1.10 203.0.113.5
ptuser_blocker.exe 10.0.0.2
```

Notes:
- The block list is an IPv4 address array sorted before being sent to the driver.
- The default binding index is `0` if `--index` is not supplied.
