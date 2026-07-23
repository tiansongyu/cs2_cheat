#include "memory.hpp"

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
			pID = proc.th32ProcessID;
			if (gHandle) {
				CloseHandle(gHandle);
				gHandle = nullptr;
			}

			gHandle = OpenProcess(
				PROCESS_QUERY_INFORMATION |
				PROCESS_VM_READ |
				PROCESS_VM_WRITE |
				PROCESS_VM_OPERATION,
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
	pID = 0;
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
