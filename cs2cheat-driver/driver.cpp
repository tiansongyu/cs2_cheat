#include <ntifs.h>
#include <ntddk.h>
#include <wdm.h>

#include "../shared/ioctl_protocol.h"

extern "C" {

NTKERNELAPI NTSTATUS
MmCopyVirtualMemory(
    PEPROCESS SourceProcess,
    PVOID SourceAddress,
    PEPROCESS TargetProcess,
    PVOID TargetAddress,
    SIZE_T BufferSize,
    KPROCESSOR_MODE PreviousMode,
    PSIZE_T ReturnSize);

NTSTATUS NTAPI
ZwQuerySystemInformation(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength);

NTKERNELAPI PVOID NTAPI PsGetProcessPeb(PEPROCESS Process);

NTSTATUS NTAPI
IoCreateDriver(
    PUNICODE_STRING DriverName,
    PDRIVER_INITIALIZE InitializationFunction);

}

#define SystemProcessInformation 5

typedef struct _SYSTEM_PROCESS_INFORMATION_MIN {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER Reserved[3];
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    // ... rest omitted; we only access fields above
} SYSTEM_PROCESS_INFORMATION_MIN, *PSYSTEM_PROCESS_INFORMATION_MIN;

// PEB / Ldr structures (subset; matches user-mode views).
typedef struct _PEB_LDR_DATA_LOCAL {
    UCHAR  Reserved1[8];
    PVOID  Reserved2[3];
    LIST_ENTRY InLoadOrderModuleList;
} PEB_LDR_DATA_LOCAL, *PPEB_LDR_DATA_LOCAL;

typedef struct _LDR_DATA_TABLE_ENTRY_LOCAL {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} LDR_DATA_TABLE_ENTRY_LOCAL, *PLDR_DATA_TABLE_ENTRY_LOCAL;

typedef struct _PEB_LOCAL {
    UCHAR  InheritedAddressSpace;
    UCHAR  ReadImageFileExecOptions;
    UCHAR  BeingDebugged;
    UCHAR  BitField;
    PVOID  Mutant;
    PVOID  ImageBaseAddress;
    PPEB_LDR_DATA_LOCAL Ldr;
} PEB_LOCAL, *PPEB_LOCAL;

PDEVICE_OBJECT g_DeviceObject = nullptr;
UNICODE_STRING g_DeviceName;
UNICODE_STRING g_SymlinkName;

static NTSTATUS Cs2CopyMemory(
    HANDLE pid,
    PVOID userAddress,
    PVOID kernelBuffer,
    SIZE_T size,
    BOOLEAN write)
{
    if (!pid || !userAddress || !kernelBuffer || size == 0)
        return STATUS_INVALID_PARAMETER;
    if ((ULONG_PTR)userAddress < 0x10000)
        return STATUS_INVALID_PARAMETER;
    if (size > CS2CHEAT_READ_MAX)
        return STATUS_INVALID_PARAMETER;

    PEPROCESS target = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId(pid, &target);
    if (!NT_SUCCESS(status))
        return status;

    SIZE_T copied = 0;
    if (write) {
        status = MmCopyVirtualMemory(
            PsGetCurrentProcess(),
            kernelBuffer,
            target,
            userAddress,
            size,
            KernelMode,
            &copied);
    } else {
        status = MmCopyVirtualMemory(
            target,
            userAddress,
            PsGetCurrentProcess(),
            kernelBuffer,
            size,
            KernelMode,
            &copied);
    }

    ObDereferenceObject(target);
    if (NT_SUCCESS(status) && copied != size)
        status = STATUS_PARTIAL_COPY;
    return status;
}

static NTSTATUS Cs2FindPidByName(const wchar_t* name, ULONG* outPid)
{
    *outPid = 0;
    UNICODE_STRING wanted;
    RtlInitUnicodeString(&wanted, name);

    ULONG bufLen = 0x10000;
    PVOID buf = nullptr;
    NTSTATUS status;

    for (int attempt = 0; attempt < 8; ++attempt) {
        buf = ExAllocatePoolWithTag(NonPagedPoolNx, bufLen, 'C2C ');
        if (!buf) return STATUS_INSUFFICIENT_RESOURCES;

        ULONG returned = 0;
        status = ZwQuerySystemInformation(SystemProcessInformation, buf, bufLen, &returned);
        if (status == STATUS_INFO_LENGTH_MISMATCH) {
            ExFreePool(buf);
            buf = nullptr;
            bufLen *= 2;
            continue;
        }
        break;
    }

    if (!NT_SUCCESS(status)) {
        if (buf) ExFreePool(buf);
        return status;
    }

    auto* info = reinterpret_cast<PSYSTEM_PROCESS_INFORMATION_MIN>(buf);
    while (true) {
        if (info->ImageName.Buffer && info->ImageName.Length > 0) {
            if (RtlEqualUnicodeString(&wanted, &info->ImageName, TRUE)) {
                *outPid = (ULONG)(ULONG_PTR)info->UniqueProcessId;
                break;
            }
        }
        if (info->NextEntryOffset == 0) break;
        info = (PSYSTEM_PROCESS_INFORMATION_MIN)((PUCHAR)info + info->NextEntryOffset);
    }

    ExFreePool(buf);
    return *outPid ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static NTSTATUS Cs2GetModuleBase(ULONG pid, const wchar_t* moduleName, ULONG64* outBase)
{
    *outBase = 0;
    PEPROCESS target = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(status))
        return status;

    KAPC_STATE apc;
    KeStackAttachProcess(target, &apc);

    PVOID peb = PsGetProcessPeb(target);
    if (!peb) {
        KeUnstackDetachProcess(&apc);
        ObDereferenceObject(target);
        return STATUS_NOT_FOUND;
    }

    auto* localPeb = reinterpret_cast<PPEB_LOCAL>(peb);
    UNICODE_STRING wanted;
    RtlInitUnicodeString(&wanted, moduleName);

    __try {
        if (!localPeb->Ldr) {
            KeUnstackDetachProcess(&apc);
            ObDereferenceObject(target);
            return STATUS_NOT_FOUND;
        }

        PLIST_ENTRY head = &localPeb->Ldr->InLoadOrderModuleList;
        for (PLIST_ENTRY cur = head->Flink; cur != head; cur = cur->Flink) {
            auto* entry = CONTAINING_RECORD(cur, LDR_DATA_TABLE_ENTRY_LOCAL, InLoadOrderLinks);
            if (entry->BaseDllName.Buffer && entry->BaseDllName.Length > 0) {
                if (RtlEqualUnicodeString(&wanted, &entry->BaseDllName, TRUE)) {
                    *outBase = (ULONG64)entry->DllBase;
                    break;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outBase = 0;
    }

    KeUnstackDetachProcess(&apc);
    ObDereferenceObject(target);
    return *outBase ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static NTSTATUS HandleGetPid(PIRP irp, PIO_STACK_LOCATION sp)
{
    if (sp->Parameters.DeviceIoControl.InputBufferLength < sizeof(CS2CHEAT_GET_PID_REQUEST) ||
        sp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(CS2CHEAT_GET_PID_RESPONSE))
        return STATUS_BUFFER_TOO_SMALL;

    auto* req = (CS2CHEAT_GET_PID_REQUEST*)irp->AssociatedIrp.SystemBuffer;
    auto* resp = (CS2CHEAT_GET_PID_RESPONSE*)irp->AssociatedIrp.SystemBuffer;

    req->name[CS2CHEAT_NAME_MAX - 1] = 0;
    ULONG pid = 0;
    NTSTATUS status = Cs2FindPidByName(req->name, &pid);

    resp->pid = pid;
    irp->IoStatus.Information = sizeof(CS2CHEAT_GET_PID_RESPONSE);
    return status;
}

static NTSTATUS HandleGetModule(PIRP irp, PIO_STACK_LOCATION sp)
{
    if (sp->Parameters.DeviceIoControl.InputBufferLength < sizeof(CS2CHEAT_GET_MODULE_REQUEST) ||
        sp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(CS2CHEAT_GET_MODULE_RESPONSE))
        return STATUS_BUFFER_TOO_SMALL;

    auto* req = (CS2CHEAT_GET_MODULE_REQUEST*)irp->AssociatedIrp.SystemBuffer;
    ULONG pid = req->pid;
    wchar_t module[CS2CHEAT_NAME_MAX];
    RtlCopyMemory(module, req->module, sizeof(module));
    module[CS2CHEAT_NAME_MAX - 1] = 0;

    auto* resp = (CS2CHEAT_GET_MODULE_RESPONSE*)irp->AssociatedIrp.SystemBuffer;
    ULONG64 base = 0;
    NTSTATUS status = Cs2GetModuleBase(pid, module, &base);
    resp->base = base;

    irp->IoStatus.Information = sizeof(CS2CHEAT_GET_MODULE_RESPONSE);
    return status;
}

static NTSTATUS HandleRead(PIRP irp, PIO_STACK_LOCATION sp)
{
    if (sp->Parameters.DeviceIoControl.InputBufferLength < sizeof(CS2CHEAT_READ_REQUEST))
        return STATUS_BUFFER_TOO_SMALL;

    auto* req = (CS2CHEAT_READ_REQUEST*)irp->AssociatedIrp.SystemBuffer;
    ULONG pid = req->pid;
    PVOID address = (PVOID)(ULONG_PTR)req->address;
    ULONG size = req->size;

    if (sp->Parameters.DeviceIoControl.OutputBufferLength < size)
        return STATUS_BUFFER_TOO_SMALL;

    NTSTATUS status = Cs2CopyMemory((HANDLE)(ULONG_PTR)pid, address,
                                    irp->AssociatedIrp.SystemBuffer, size, FALSE);
    if (NT_SUCCESS(status))
        irp->IoStatus.Information = size;
    else
        irp->IoStatus.Information = 0;
    return status;
}

static NTSTATUS HandleWrite(PIRP irp, PIO_STACK_LOCATION sp)
{
    const ULONG headerSize = FIELD_OFFSET(CS2CHEAT_WRITE_REQUEST, data);
    if (sp->Parameters.DeviceIoControl.InputBufferLength <= headerSize)
        return STATUS_BUFFER_TOO_SMALL;

    auto* req = (CS2CHEAT_WRITE_REQUEST*)irp->AssociatedIrp.SystemBuffer;
    if (req->size == 0 || req->size > CS2CHEAT_READ_MAX)
        return STATUS_INVALID_PARAMETER;
    if (sp->Parameters.DeviceIoControl.InputBufferLength < headerSize + req->size)
        return STATUS_BUFFER_TOO_SMALL;

    return Cs2CopyMemory(
        (HANDLE)(ULONG_PTR)req->pid,
        (PVOID)(ULONG_PTR)req->address,
        req->data, req->size, TRUE);
}

static NTSTATUS HandleBatchRead(PIRP irp, PIO_STACK_LOCATION sp)
{
    const ULONG inLen  = sp->Parameters.DeviceIoControl.InputBufferLength;
    const ULONG outLen = sp->Parameters.DeviceIoControl.OutputBufferLength;
    const ULONG headerSize = FIELD_OFFSET(CS2CHEAT_BATCH_READ_REQUEST, entries);

    if (inLen < headerSize) return STATUS_BUFFER_TOO_SMALL;

    auto* req = (CS2CHEAT_BATCH_READ_REQUEST*)irp->AssociatedIrp.SystemBuffer;
    const ULONG count = req->entry_count;
    if (count == 0 || count > CS2CHEAT_BATCH_MAX_ENTRIES)
        return STATUS_INVALID_PARAMETER;
    if (inLen < headerSize + count * sizeof(CS2CHEAT_BATCH_ENTRY))
        return STATUS_BUFFER_TOO_SMALL;
    if (outLen < req->output_size || req->output_size > CS2CHEAT_READ_MAX)
        return STATUS_BUFFER_TOO_SMALL;

    // METHOD_BUFFERED: input and output share the same SystemBuffer.
    // Snapshot every header field we'll need *before* zeroing the output
    // window — once we zero it, req->* is gone.
    CS2CHEAT_BATCH_ENTRY stack[CS2CHEAT_BATCH_MAX_ENTRIES];
    RtlCopyMemory(stack, req->entries, count * sizeof(CS2CHEAT_BATCH_ENTRY));
    HANDLE pid       = (HANDLE)(ULONG_PTR)req->pid;
    const ULONG outputSize = req->output_size;

    PUCHAR outBase = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(outBase, outputSize);

    PEPROCESS target = nullptr;
    NTSTATUS lookup = PsLookupProcessByProcessId(pid, &target);
    if (!NT_SUCCESS(lookup)) return lookup;

    for (ULONG i = 0; i < count; ++i) {
        CS2CHEAT_BATCH_ENTRY& e = stack[i];
        if (e.size == 0 ||
            e.size > CS2CHEAT_READ_MAX ||
            e.address < 0x10000 ||
            (uint64_t)e.output_offset + e.size > outputSize) {
            e.status = (int32_t)STATUS_INVALID_PARAMETER;
            continue;
        }

        SIZE_T copied = 0;
        NTSTATUS s = MmCopyVirtualMemory(
            target,
            (PVOID)(ULONG_PTR)e.address,
            PsGetCurrentProcess(),
            outBase + e.output_offset,
            e.size,
            KernelMode,
            &copied);
        if (NT_SUCCESS(s) && copied != e.size) {
            // Partial copy: zero the partial region so user-mode sees a clean
            // failure rather than half-stale data.
            RtlZeroMemory(outBase + e.output_offset, e.size);
            s = STATUS_PARTIAL_COPY;
        }
        e.status = (int32_t)s;
    }

    ObDereferenceObject(target);
    irp->IoStatus.Information = outputSize;
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS Cs2DispatchCreateClose(PDEVICE_OBJECT, PIRP irp)
{
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS Cs2DispatchIoctl(PDEVICE_OBJECT, PIRP irp)
{
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    switch (sp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_CS2CHEAT_GET_PID_BY_NAME:  status = HandleGetPid(irp, sp); break;
    case IOCTL_CS2CHEAT_GET_MODULE_BASE:  status = HandleGetModule(irp, sp); break;
    case IOCTL_CS2CHEAT_READ_MEMORY:      status = HandleRead(irp, sp); break;
    case IOCTL_CS2CHEAT_WRITE_MEMORY:     status = HandleWrite(irp, sp); break;
    case IOCTL_CS2CHEAT_BATCH_READ:       status = HandleBatchRead(irp, sp); break;
    default: break;
    }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

extern "C" VOID Cs2DriverUnload(PDRIVER_OBJECT driver)
{
    UNREFERENCED_PARAMETER(driver);
    if (g_DeviceObject) {
        IoDeleteSymbolicLink(&g_SymlinkName);
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = nullptr;
    }
}

extern "C" NTSTATUS Cs2DriverInitialize(PDRIVER_OBJECT driver, PUNICODE_STRING regPath)
{
    UNREFERENCED_PARAMETER(regPath);

    RtlInitUnicodeString(&g_DeviceName, CS2CHEAT_DEVICE_NAME);
    RtlInitUnicodeString(&g_SymlinkName, CS2CHEAT_SYMLINK_NAME);

    NTSTATUS status = IoCreateDevice(
        driver, 0, &g_DeviceName,
        FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
        &g_DeviceObject);
    if (!NT_SUCCESS(status)) return status;

    status = IoCreateSymbolicLink(&g_SymlinkName, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = nullptr;
        return status;
    }

    driver->MajorFunction[IRP_MJ_CREATE]         = Cs2DispatchCreateClose;
    driver->MajorFunction[IRP_MJ_CLOSE]          = Cs2DispatchCreateClose;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Cs2DispatchIoctl;
    driver->DriverUnload                         = Cs2DriverUnload;

    g_DeviceObject->Flags |= DO_BUFFERED_IO;
    g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING regPath)
{
    // Standard load (sc create / .inf): DriverObject is provided.
    if (driver) {
        return Cs2DriverInitialize(driver, regPath);
    }

    // Manual map (kdmapper / KDU): no DriverObject, no RegistryPath.
    // Bootstrap our own via IoCreateDriver and run the standard init inside it.
    UNICODE_STRING bootName;
    RtlInitUnicodeString(&bootName, L"\\Driver\\cs2cheat");
    return IoCreateDriver(&bootName, &Cs2DriverInitialize);
}
