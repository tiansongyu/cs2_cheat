#include "memory.hpp"

namespace
{
	void closeHandle(HANDLE& handle)
	{
		if (handle) {
			CloseHandle(handle);
			handle = nullptr;
		}
	}
}

void memory::Close()
{
	closeHandle(gWriteHandle);
	closeHandle(gHandle);
	pID = 0;
}

void memory::SetWritesAllowed(bool allowed)
{
	gWritesAllowed = allowed;
	if (!allowed) {
		closeHandle(gWriteHandle);
	}
}

bool memory::WritesAllowed()
{
	return gWritesAllowed;
}

bool memory::TryWriteRaw(
	uintptr_t address,
	const void* buffer,
	size_t size)
{
	if (!gWritesAllowed ||
		pID == 0 ||
		address == 0 ||
		!buffer ||
		size == 0) {
		return false;
	}

	if (!gWriteHandle) {
		gWriteHandle = OpenProcess(
			PROCESS_QUERY_LIMITED_INFORMATION |
			PROCESS_VM_WRITE |
			PROCESS_VM_OPERATION,
			FALSE,
			static_cast<DWORD>(pID));
		if (!gWriteHandle) {
			return false;
		}
	}

	SIZE_T bytesWritten = 0;
	return WriteProcessMemory(
			gWriteHandle,
			reinterpret_cast<LPVOID>(address),
			buffer,
			size,
			&bytesWritten) &&
		bytesWritten == size;
}

uintptr_t memory::GetProcID(const wchar_t* process)
{
	HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (handle == INVALID_HANDLE_VALUE) {
		return 0;
	}
	
	PROCESSENTRY32 proc{};
	proc.dwSize = sizeof(PROCESSENTRY32);
	if (!Process32First(handle, &proc)) {
		CloseHandle(handle);
		return 0;
	}

	do
	{
		if (!_wcsicmp(process, proc.szExeFile))
			{
				CloseHandle(handle);
				Close();
				pID = proc.th32ProcessID;

				gHandle = OpenProcess(
					PROCESS_QUERY_LIMITED_INFORMATION |
					PROCESS_VM_READ,
					FALSE,
					static_cast<DWORD>(pID));
			if (!gHandle) {
				pID = 0;
				return 0;
			}
			return proc.th32ProcessID;
		}
	} while (Process32Next(handle, &proc));

	CloseHandle(handle);
	Close();
	return 0;
}

uintptr_t memory::GetModuleBaseAddress(uintptr_t procID, const wchar_t* module)
{
	HANDLE handle = CreateToolhelp32Snapshot(
		TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
		static_cast<DWORD>(procID));
	if (handle == INVALID_HANDLE_VALUE) {
		return 0;
	}

	MODULEENTRY32 mod{};
	mod.dwSize = sizeof(MODULEENTRY32);
	if (!Module32First(handle, &mod)) {
		CloseHandle(handle);
		return 0;
	}

	do
	{
		if (!_wcsicmp(module, mod.szModule))
		{
			CloseHandle(handle);
			return (uintptr_t)mod.modBaseAddr;
		}
	} while (Module32Next(handle, &mod));

	CloseHandle(handle);
	return 0;
}
