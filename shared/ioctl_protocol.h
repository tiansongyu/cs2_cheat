#pragma once

// Shared between user-mode (cheat) and kernel driver (cs2cheat-driver).
// Keep this header free of OS-specific includes; both sides include the
// minimal surface they need before including this file.

#ifdef _KERNEL_MODE
#include <ntifs.h>
#else
#include <Windows.h>
#endif

#include <stdint.h>

#define CS2CHEAT_DEVICE_NAME    L"\\Device\\cs2cheat"
#define CS2CHEAT_SYMLINK_NAME   L"\\DosDevices\\cs2cheat"
#define CS2CHEAT_USER_PATH      L"\\\\.\\cs2cheat"

#define CS2CHEAT_DEVICE_TYPE 0x8000

#define IOCTL_CS2CHEAT_GET_PID_BY_NAME \
    CTL_CODE(CS2CHEAT_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CS2CHEAT_GET_MODULE_BASE \
    CTL_CODE(CS2CHEAT_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CS2CHEAT_READ_MEMORY \
    CTL_CODE(CS2CHEAT_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CS2CHEAT_WRITE_MEMORY \
    CTL_CODE(CS2CHEAT_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CS2CHEAT_BATCH_READ \
    CTL_CODE(CS2CHEAT_DEVICE_TYPE, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define CS2CHEAT_NAME_MAX 260
#define CS2CHEAT_READ_MAX (16 * 1024 * 1024)
#define CS2CHEAT_BATCH_MAX_ENTRIES 256

#pragma pack(push, 1)

typedef struct _CS2CHEAT_GET_PID_REQUEST {
    wchar_t name[CS2CHEAT_NAME_MAX];
} CS2CHEAT_GET_PID_REQUEST;

typedef struct _CS2CHEAT_GET_PID_RESPONSE {
    uint32_t pid;
} CS2CHEAT_GET_PID_RESPONSE;

typedef struct _CS2CHEAT_GET_MODULE_REQUEST {
    uint32_t pid;
    wchar_t  module[CS2CHEAT_NAME_MAX];
} CS2CHEAT_GET_MODULE_REQUEST;

typedef struct _CS2CHEAT_GET_MODULE_RESPONSE {
    uint64_t base;
} CS2CHEAT_GET_MODULE_RESPONSE;

typedef struct _CS2CHEAT_READ_REQUEST {
    uint32_t pid;
    uint64_t address;
    uint32_t size;
} CS2CHEAT_READ_REQUEST;

typedef struct _CS2CHEAT_WRITE_REQUEST {
    uint32_t pid;
    uint64_t address;
    uint32_t size;
    uint8_t  data[1];
} CS2CHEAT_WRITE_REQUEST;

// Batch read: one IOCTL fans out into many small reads, all in the same target
// process. Driver lays results out in the output buffer at the per-entry
// output_offset specified by the caller. Failed entries are zero-filled (so
// callers see well-defined memory) and reported in the per-entry status field.
typedef struct _CS2CHEAT_BATCH_ENTRY {
    uint64_t address;
    uint32_t size;
    uint32_t output_offset;
    int32_t  status;       // 0 = success, otherwise NTSTATUS-ish error
} CS2CHEAT_BATCH_ENTRY;

typedef struct _CS2CHEAT_BATCH_READ_REQUEST {
    uint32_t pid;
    uint32_t entry_count;
    uint32_t output_size;  // total size of result buffer caller will receive
    uint32_t reserved;
    CS2CHEAT_BATCH_ENTRY entries[1];  // entry_count entries follow
} CS2CHEAT_BATCH_READ_REQUEST;

#pragma pack(pop)
