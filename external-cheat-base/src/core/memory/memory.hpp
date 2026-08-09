#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <TlHelp32.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace memory
{
	inline HANDLE gHandle = nullptr;
	inline HANDLE gWriteHandle = nullptr;

	inline uintptr_t pID = 0;
	inline bool gWritesAllowed = false;

	struct ReadMetrics
	{
		std::uint64_t calls{};
		std::uint64_t failures{};
		std::uint64_t bytesRequested{};
	};

	inline std::atomic<std::uint64_t> gReadCalls{0};
	inline std::atomic<std::uint64_t> gReadFailures{0};
	inline std::atomic<std::uint64_t> gReadBytesRequested{0};
	inline std::atomic<std::uint64_t> gReadMetricsEpoch{1};

	inline void RecordRead(size_t size, bool succeeded) noexcept
	{
		struct LocalAccumulator
		{
			std::uint64_t epoch{};
			std::uint64_t calls{};
			std::uint64_t failures{};
			std::uint64_t bytes{};
		};
		thread_local LocalAccumulator local;
		const std::uint64_t epoch = gReadMetricsEpoch.load(
			std::memory_order_relaxed);
		if (local.epoch != epoch) {
			local = LocalAccumulator{epoch};
		}
		++local.calls;
		local.bytes += static_cast<std::uint64_t>(size);
		if (!succeeded) {
			++local.failures;
		}
		if (local.calls >= 256) {
			gReadCalls.fetch_add(local.calls, std::memory_order_relaxed);
			gReadFailures.fetch_add(
				local.failures,
				std::memory_order_relaxed);
			gReadBytesRequested.fetch_add(
				local.bytes,
				std::memory_order_relaxed);
			local = LocalAccumulator{epoch};
		}
	}

	inline ReadMetrics GetReadMetrics() noexcept
	{
		return ReadMetrics{
			gReadCalls.load(std::memory_order_relaxed),
			gReadFailures.load(std::memory_order_relaxed),
			gReadBytesRequested.load(std::memory_order_relaxed)
		};
	}

	inline void ResetReadMetrics() noexcept
	{
		gReadCalls.store(0, std::memory_order_relaxed);
		gReadFailures.store(0, std::memory_order_relaxed);
		gReadBytesRequested.store(0, std::memory_order_relaxed);
		gReadMetricsEpoch.fetch_add(1, std::memory_order_relaxed);
	}

	uintptr_t GetProcID(const wchar_t* process);
	uintptr_t GetModuleBaseAddress(uintptr_t procID, const wchar_t* module);
	void Close();
	void SetWritesAllowed(bool allowed);
	bool WritesAllowed();
	bool TryWriteRaw(uintptr_t address, const void* buffer, size_t size);

	template <typename T> bool TryRead(uintptr_t address, T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"Remote memory reads require a trivially copyable type");

		value = T{};
		if (!gHandle || address == 0) {
			RecordRead(sizeof(T), false);
			return false;
		}

		SIZE_T bytesRead = 0;
		if (!ReadProcessMemory(
				gHandle,
				reinterpret_cast<LPCVOID>(address),
				&value,
				sizeof(T),
				&bytesRead) ||
			bytesRead != sizeof(T)) {
			value = T{};
			RecordRead(sizeof(T), false);
			return false;
		}

		RecordRead(sizeof(T), true);
		return true;
	}

	template <typename T> T Read(uintptr_t address)
	{
		T value{};
		TryRead(address, value);
		return value;
	}

	template <typename T> bool Write(uintptr_t address, T value)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"Remote memory writes require a trivially copyable type");
		return TryWriteRaw(address, &value, sizeof(T));
	}

	inline bool ReadRaw(uintptr_t address, void* buffer, size_t size)
	{
		if (!buffer || size == 0) {
			return false;
		}

		std::memset(buffer, 0, size);
		if (!gHandle || address == 0) {
			RecordRead(size, false);
			return false;
		}

		SIZE_T bytesRead = 0;
		if (!ReadProcessMemory(
				gHandle,
				reinterpret_cast<LPCVOID>(address),
				buffer,
				size,
				&bytesRead) ||
			bytesRead != size) {
			std::memset(buffer, 0, size);
			RecordRead(size, false);
			return false;
		}

		RecordRead(size, true);
		return true;
	}


}
