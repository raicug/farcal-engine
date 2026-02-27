#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace farcal::memory
{

    class Process
    {
    public:
        using Id = std::uint32_t;
#ifdef _WIN32
        using NativeHandle = HANDLE;
#else
        using NativeHandle = void *;
#endif

        Process() noexcept = default;

        Process(const Process &) = delete;
        Process &operator=(const Process &) = delete;

        Process(Process &&other) noexcept
            : m_id(other.m_id), m_nativeHandle(other.m_nativeHandle)
        {
            other.m_id = 0;
            other.m_nativeHandle = nullptr;
        }

        Process &operator=(Process &&other) noexcept
        {
            if (this != &other)
            {
                reset();
                m_id = other.m_id;
                m_nativeHandle = other.m_nativeHandle;
                other.m_id = 0;
                other.m_nativeHandle = nullptr;
            }
            return *this;
        }

        ~Process()
        {
            reset();
        }

        Id id() const noexcept
        {
            return m_id;
        }

        NativeHandle nativeHandle() const noexcept
        {
            return m_nativeHandle;
        }

        bool valid() const noexcept
        {
            return m_nativeHandle != nullptr;
        }

        void reset() noexcept
        {
#ifdef _WIN32
            if (m_nativeHandle != nullptr)
            {
                ::CloseHandle(m_nativeHandle);
            }
#endif
            m_nativeHandle = nullptr;
            m_id = 0;
        }

        void set(Id id, NativeHandle nativeHandle) noexcept
        {
            reset();
            m_id = id;
            m_nativeHandle = nativeHandle;
        }

    private:
        Id m_id = 0;
        NativeHandle m_nativeHandle = nullptr;
    };

    class MemoryReader
    {
    public:
        MemoryReader() = default;

        MemoryReader(const MemoryReader &) = delete;
        MemoryReader &operator=(const MemoryReader &) = delete;

        MemoryReader(MemoryReader &&) = default;
        MemoryReader &operator=(MemoryReader &&) = default;

        ~MemoryReader() = default;

        bool attach(Process::Id processId)
        {
            detach();

#ifdef _WIN32
            constexpr DWORD kReadWriteMask = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION;
            constexpr DWORD kReadOnlyMask = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;

            HANDLE processHandle = ::OpenProcess(kReadWriteMask, FALSE, static_cast<DWORD>(processId));
            if (processHandle == nullptr)
            {
                processHandle = ::OpenProcess(kReadOnlyMask, FALSE, static_cast<DWORD>(processId));
                if (processHandle == nullptr)
                {
                    return false;
                }
                m_canWrite = false;
            }
            else
            {
                m_canWrite = true;
            }

            m_process.set(processId, processHandle);
            return true;
#else
            (void)processId;
            return false;
#endif
        }

        void detach()
        {
            m_process.reset();
            m_canWrite = false;
        }

        bool attached() const noexcept
        {
            return m_process.valid();
        }

        const Process &process() const noexcept
        {
            return m_process;
        }

        bool readBytes(std::uintptr_t address, void *outBuffer, std::size_t size) const
        {
            if (!attached() || outBuffer == nullptr || size == 0)
            {
                return false;
            }

#ifdef _WIN32
            SIZE_T bytesRead = 0;
            const BOOL ok = ::ReadProcessMemory(
                m_process.nativeHandle(),
                reinterpret_cast<LPCVOID>(address),
                outBuffer,
                static_cast<SIZE_T>(size),
                &bytesRead);
            return ok != FALSE && bytesRead == static_cast<SIZE_T>(size);
#else
            (void)address;
            (void)outBuffer;
            (void)size;
            return false;
#endif
        }

        bool writeBytes(std::uintptr_t address, const void *inBuffer, std::size_t size) const
        {
            if (!attached() || !m_canWrite || inBuffer == nullptr || size == 0)
            {
                return false;
            }

#ifdef _WIN32
            SIZE_T bytesWritten = 0;
            const BOOL ok = ::WriteProcessMemory(
                m_process.nativeHandle(),
                reinterpret_cast<LPVOID>(address),
                inBuffer,
                static_cast<SIZE_T>(size),
                &bytesWritten);
            return ok != FALSE && bytesWritten == static_cast<SIZE_T>(size);
#else
            (void)address;
            (void)inBuffer;
            (void)size;
            return false;
#endif
        }

        template <typename T>
        std::optional<T> read(std::uintptr_t address) const
        {
            static_assert(std::is_trivially_copyable_v<T>, "read<T> requires trivially copyable types.");

            T value{};
            if (!readBytes(address, &value, sizeof(T)))
            {
                return std::nullopt;
            }
            return value;
        }

        template <typename T>
        bool write(std::uintptr_t address, const T &value) const
        {
            static_assert(std::is_trivially_copyable_v<T>, "write<T> requires trivially copyable types.");
            return writeBytes(address, &value, sizeof(T));
        }

        bool canWrite() const noexcept
        {
            return attached() && m_canWrite;
        }

    private:
        Process m_process;
        bool m_canWrite = false;
    };

} // namespace farcal::memory
