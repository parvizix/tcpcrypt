#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ntddndis.h>

#include "../nuiouser.h"
#include "../iocommon.h"

#define PASSTHRU_SYMBOLIC_NAME "\\\\.\\Passthru"

static void print_win_error(const char *context) {
    DWORD err = GetLastError();
    fprintf(stderr, "%s failed (error %lu)\n", context, err);
}

static int is_no_more_items_error(DWORD err) {
    return err == ERROR_NO_MORE_ITEMS || err == ERROR_INVALID_PARAMETER;
}

static int query_binding(HANDLE device, ULONG binding_index, NDISPROT_QUERY_BINDING **binding) {
    ULONG buffer_size = 4096;
    NDISPROT_QUERY_BINDING *query = (NDISPROT_QUERY_BINDING *)malloc(buffer_size);
    DWORD bytes_returned = 0;

    if (!query) {
        fprintf(stderr, "Out of memory.\n");
        return 0;
    }

    memset(query, 0, buffer_size);
    query->BindingIndex = binding_index;

    if (!DeviceIoControl(device,
                         IOCTL_NDISPROT_QUERY_BINDING,
                         query,
                         buffer_size,
                         query,
                         buffer_size,
                         &bytes_returned,
                         NULL)) {
        DWORD err = GetLastError();
        free(query);
        if (is_no_more_items_error(err)) {
            return 0;
        }
        print_win_error("IOCTL_NDISPROT_QUERY_BINDING");
        return 0;
    }

    *binding = query;
    return 1;
}

static void list_bindings(HANDLE device) {
    ULONG index = 0;

    printf("Available Passthru bindings:\n");

    while (1) {
        NDISPROT_QUERY_BINDING *binding = NULL;
        if (!query_binding(device, index, &binding)) {
            break;
        }

        if (binding->DeviceNameOffset && binding->DeviceNameLength) {
            const WCHAR *device_name = (const WCHAR *)((const unsigned char *)binding + binding->DeviceNameOffset);
            const WCHAR *device_desc = NULL;

            if (binding->DeviceDescrOffset && binding->DeviceDescrLength) {
                device_desc = (const WCHAR *)((const unsigned char *)binding + binding->DeviceDescrOffset);
            }

            wprintf(L"  [%lu] %ls", index, device_name);
            if (device_desc) {
                wprintf(L" - %ls", device_desc);
            }
            wprintf(L"\n");
        }

        free(binding);
        ++index;
    }

    if (index == 0) {
        printf("  (no bindings reported)\n");
    }
}

static int open_binding(HANDLE device, ULONG binding_index) {
    NDISPROT_QUERY_BINDING *binding = NULL;
    DWORD bytes_returned = 0;
    int ok = 0;

    if (!query_binding(device, binding_index, &binding)) {
        fprintf(stderr, "Failed to find binding index %lu.\n", binding_index);
        return 0;
    }

    if (!DeviceIoControl(device,
                         IOCTL_NDISPROT_OPEN_DEVICE,
                         (void *)((unsigned char *)binding + binding->DeviceNameOffset),
                         binding->DeviceNameLength,
                         NULL,
                         0,
                         &bytes_returned,
                         NULL)) {
        print_win_error("IOCTL_NDISPROT_OPEN_DEVICE");
        goto cleanup;
    }

    ok = 1;

cleanup:
    free(binding);
    return ok;
}

static ULONG parse_ipv4(const char *text) {
    ULONG addr = inet_addr(text);

    if (addr == INADDR_NONE && strcmp(text, "255.255.255.255") != 0) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", text);
        return 0;
    }

    return addr;
}

static int compare_ulong(const void *a, const void *b) {
    ULONG ua = *(const ULONG *)a;
    ULONG ub = *(const ULONG *)b;

    if (ua < ub) {
        return -1;
    }
    if (ua > ub) {
        return 1;
    }
    return 0;
}

static int set_ipv4_block_list(HANDLE device, int argc, char **argv) {
    size_t count = (size_t)argc;
    size_t buffer_size = sizeof(ULONG) + sizeof(ULONG) * count;
    IPv4BlockAddrArray *block_list = (IPv4BlockAddrArray *)malloc(buffer_size);
    DWORD bytes_returned = 0;
    size_t i;

    if (!block_list) {
        fprintf(stderr, "Out of memory.\n");
        return 0;
    }

    block_list->NumberElements = (ULONG)count;

    for (i = 0; i < count; ++i) {
        ULONG addr = parse_ipv4(argv[i]);
        if (addr == 0 && strcmp(argv[i], "0.0.0.0") != 0) {
            free(block_list);
            return 0;
        }
        block_list->IPAddrArray[i] = addr;
    }

    qsort(block_list->IPAddrArray, count, sizeof(ULONG), compare_ulong);

    if (!DeviceIoControl(device,
                         IOCTL_PTUSERIO_SET_IPv4_BLOCK_FILTER,
                         block_list,
                         (DWORD)buffer_size,
                         NULL,
                         0,
                         &bytes_returned,
                         NULL)) {
        print_win_error("IOCTL_PTUSERIO_SET_IPv4_BLOCK_FILTER");
        free(block_list);
        return 0;
    }

    free(block_list);
    return 1;
}

static void print_usage(const char *exe) {
    printf("Usage:\n");
    printf("  %s list\n", exe);
    printf("  %s <binding-index> <ipv4> [ipv4 ...]\n", exe);
    printf("\nExample:\n");
    printf("  %s 0 192.0.2.10 198.51.100.7\n", exe);
}

int main(int argc, char **argv) {
    WSADATA wsa_data;
    HANDLE device = INVALID_HANDLE_VALUE;
    DWORD bytes_returned = 0;

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }

    device = CreateFileA(PASSTHRU_SYMBOLIC_NAME,
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);

    if (device == INVALID_HANDLE_VALUE) {
        print_win_error("CreateFile(\\\\.\\Passthru)");
        WSACleanup();
        return 1;
    }

    if (!DeviceIoControl(device,
                         IOCTL_NDISPROT_BIND_WAIT,
                         NULL,
                         0,
                         NULL,
                         0,
                         &bytes_returned,
                         NULL)) {
        print_win_error("IOCTL_NDISPROT_BIND_WAIT");
        CloseHandle(device);
        WSACleanup();
        return 1;
    }

    if (argc < 2 || strcmp(argv[1], "list") == 0) {
        list_bindings(device);
        CloseHandle(device);
        WSACleanup();
        return 0;
    }

    if (argc < 3) {
        print_usage(argv[0]);
        CloseHandle(device);
        WSACleanup();
        return 1;
    }

    char *endptr = NULL;
    ULONG binding_index = (ULONG)strtoul(argv[1], &endptr, 10);
    if (endptr == argv[1] || *endptr != '\0') {
        fprintf(stderr, "Binding index must be a number.\n");
        print_usage(argv[0]);
        CloseHandle(device);
        WSACleanup();
        return 1;
    }

    if (!open_binding(device, binding_index)) {
        CloseHandle(device);
        WSACleanup();
        return 1;
    }

    if (!set_ipv4_block_list(device, argc - 2, argv + 2)) {
        CloseHandle(device);
        WSACleanup();
        return 1;
    }

    printf("Updated IPv4 block list on binding %lu.\n", binding_index);

    CloseHandle(device);
    WSACleanup();
    return 0;
}
