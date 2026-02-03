# Passthru user-mode IPv4 block tool

`ptblock.c` is a minimal user-mode sample that talks to the Passthru NDIS
intermediate driver via `\\.\Passthru`. It:

1. Waits for NDIS bind completion.
2. Enumerates Passthru bindings (`list`).
3. Opens a binding by index.
4. Pushes an IPv4 block list into the filter (`IOCTL_PTUSERIO_SET_IPv4_BLOCK_FILTER`).

The driver filters IPv4 by destination address for sends and source address
for receives. The list must be sorted because the kernel uses binary search.

## Build (Visual Studio Developer Prompt)

```
cl /W4 /DWIN32 /D_WIN32_WINNT=0x0600 ptblock.c /link ws2_32.lib
```

## Usage

```
ptblock.exe list
ptblock.exe <binding-index> <ipv4> [ipv4 ...]
```

Example:

```
ptblock.exe 0 192.0.2.10 198.51.100.7
```
