#pragma once

#include "farcal/memory/MemoryReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace farcal::memory
{

    class StringScanner
    {
    public:
        enum class Encoding
        {
            Ascii,
            Utf16
        };

        struct StringEntry
        {
            std::uintptr_t address = 0;
            std::string text;
            Encoding encoding = Encoding::Ascii;
        };

        struct ScanOptions
        {
            std::uintptr_t start_address = 0;
            std::uintptr_t end_address = 0;
            std::size_t min_length = 4;
            std::size_t max_length = 512;
            std::size_t max_results = 0;
            std::size_t chunk_size = 1024 * 1024;
            bool scan_ascii = true;
            bool scan_utf16 = true;
            bool include_writable_regions = true;
            bool case_sensitive_filter = false;
            std::string contains;
            std::size_t worker_threads = 0;
        };

        explicit StringScanner(const MemoryReader *reader = nullptr)
            : m_reader(reader)
        {
        }

        void setReader(const MemoryReader *reader) noexcept
        {
            m_reader = reader;
        }

        [[nodiscard]] const MemoryReader *reader() const noexcept
        {
            return m_reader;
        }

        [[nodiscard]] std::vector<StringEntry> find_all() const
        {
            return find_all(ScanOptions{});
        }

        [[nodiscard]] std::vector<StringEntry> find_all(const ScanOptions &options) const
        {
            std::vector<StringEntry> result;
            find_all_batched(options, 4096, [&result](std::vector<StringEntry> &&batch)
                             {
                                 result.insert(
                                     result.end(),
                                     std::make_move_iterator(batch.begin()),
                                     std::make_move_iterator(batch.end()));
                             });

            return result;
        }

        template <typename BatchCallback>
        void find_all_batched(const ScanOptions &options, std::size_t batch_size, BatchCallback &&on_batch) const
        {
            if (m_reader == nullptr || !m_reader->attached())
            {
                return;
            }
            if (!options.scan_ascii && !options.scan_utf16)
            {
                return;
            }

            const auto regions = queryRegions();
            if (regions.empty())
            {
                return;
            }

            const std::uintptr_t scan_start = options.start_address;
            const std::uintptr_t scan_end = options.end_address == 0
                                                ? (std::numeric_limits<std::uintptr_t>::max)()
                                                : options.end_address;
            const std::size_t min_len = (std::max)(std::size_t{1}, options.min_length);
            const std::size_t max_len = (std::max)(min_len, options.max_length == 0 ? std::size_t{512} : options.max_length);
            const std::size_t chunk_size = (std::max)(std::size_t{4096}, options.chunk_size);
            const std::size_t overlap = (std::max)(max_len * 2, max_len);
            const std::size_t effective_batch_size = (std::max)(std::size_t{256}, batch_size);
            std::vector<StringEntry> outgoing;
            outgoing.reserve(effective_batch_size);

            std::size_t total_results = 0;
            auto push_entries = [&](std::vector<StringEntry> &&entries) -> bool {
                for (auto &entry : entries)
                {
                    if (options.max_results > 0 && total_results >= options.max_results)
                    {
                        return false;
                    }

                    outgoing.push_back(std::move(entry));
                    ++total_results;

                    if (outgoing.size() >= effective_batch_size)
                    {
                        on_batch(std::move(outgoing));
                        outgoing.clear();
                        outgoing.reserve(effective_batch_size);
                    }
                }
                return true;
            };

            const std::size_t requested_workers = options.worker_threads == 0
                                                      ? static_cast<std::size_t>((std::max)(1u, std::thread::hardware_concurrency()))
                                                      : options.worker_threads;
            const std::size_t worker_count = (std::max)(std::size_t{1}, (std::min)(requested_workers, regions.size()));

            if (worker_count == 1)
            {
                std::vector<StringEntry> entries = scanRegionSubset(
                    regions,
                    0,
                    1,
                    scan_start,
                    scan_end,
                    min_len,
                    max_len,
                    chunk_size,
                    overlap,
                    options
                );
                (void)push_entries(std::move(entries));
            }
            else
            {
                std::vector<std::future<std::vector<StringEntry>>> futures;
                futures.reserve(worker_count);

                for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index)
                {
                    futures.emplace_back(std::async(std::launch::async, [this,
                                                                         &regions,
                                                                         worker_index,
                                                                         worker_count,
                                                                         scan_start,
                                                                         scan_end,
                                                                         min_len,
                                                                         max_len,
                                                                         chunk_size,
                                                                         overlap,
                                                                         options]() {
                        return scanRegionSubset(
                            regions,
                            worker_index,
                            worker_count,
                            scan_start,
                            scan_end,
                            min_len,
                            max_len,
                            chunk_size,
                            overlap,
                            options
                        );
                    }));
                }

                for (auto &future : futures)
                {
                    std::vector<StringEntry> entries = future.get();
                    if (!push_entries(std::move(entries)))
                    {
                        break;
                    }
                }
            }

            if (!outgoing.empty())
            {
                on_batch(std::move(outgoing));
            }
        }

        [[nodiscard]] std::optional<StringEntry> find_first(const std::string &text, bool case_sensitive = false) const
        {
            ScanOptions options{};
            options.contains = text;
            options.case_sensitive_filter = case_sensitive;
            options.max_results = 1;
            auto found = find_all(options);
            if (found.empty())
            {
                return std::nullopt;
            }
            return found.front();
        }

    private:
        struct MemoryRegion
        {
            std::uintptr_t base = 0;
            std::size_t size = 0;
            std::uint32_t protection = 0;
            std::uint32_t state = 0;
        };

        [[nodiscard]] static bool isAsciiChar(std::uint8_t value)
        {
            return value == 0x09 || (value >= 0x20 && value <= 0x7E);
        }

        [[nodiscard]] static bool isUtf16Unit(std::uint16_t value)
        {
            if (value == 0x09)
            {
                return true;
            }
            if (value >= 0x20 && value <= 0x7E)
            {
                return true;
            }
            if (value >= 0xA0 && value <= 0xD7FF)
            {
                return true;
            }
            if (value >= 0xE000 && value <= 0xFFFD)
            {
                return true;
            }
            return false;
        }

        [[nodiscard]] static std::string toLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        [[nodiscard]] static bool matchesFilter(const std::string &text, const ScanOptions &options)
        {
            if (options.contains.empty())
            {
                return true;
            }

            if (options.case_sensitive_filter)
            {
                return text.find(options.contains) != std::string::npos;
            }

            const std::string lower_text = toLower(text);
            const std::string lower_filter = toLower(options.contains);
            return lower_text.find(lower_filter) != std::string::npos;
        }

        static std::size_t scanAsciiBlock(
            std::uintptr_t block_base,
            const std::uint8_t *data,
            std::size_t size,
            std::size_t min_length,
            std::size_t max_length,
            const ScanOptions &options,
            std::unordered_set<std::uintptr_t> &seen_addresses,
            std::size_t max_to_add,
            std::vector<StringEntry> &out)
        {
            std::size_t added = 0;
            std::size_t i = 0;
            while (i < size)
            {
                if (!isAsciiChar(data[i]))
                {
                    ++i;
                    continue;
                }

                const std::size_t start = i;
                std::string text;
                text.reserve((std::min)(max_length, std::size_t{256}));

                while (i < size && isAsciiChar(data[i]))
                {
                    if (text.size() < max_length)
                    {
                        text.push_back(static_cast<char>(data[i]));
                    }
                    ++i;
                }

                const std::size_t sequence_len = i - start;
                if (sequence_len < min_length || text.empty())
                {
                    continue;
                }

                const std::uintptr_t address = block_base + start;
                if (!seen_addresses.insert(address).second)
                {
                    continue;
                }
                if (!matchesFilter(text, options))
                {
                    continue;
                }

                out.push_back(StringEntry{address, std::move(text), Encoding::Ascii});
                ++added;
                if (max_to_add > 0 && added >= max_to_add)
                {
                    return added;
                }
            }

            return added;
        }

        static std::size_t scanUtf16Block(
            std::uintptr_t block_base,
            const std::uint8_t *data,
            std::size_t size,
            std::size_t min_length,
            std::size_t max_length,
            const ScanOptions &options,
            std::unordered_set<std::uintptr_t> &seen_addresses,
            std::size_t max_to_add,
            std::vector<StringEntry> &out)
        {
            std::size_t added = 0;
            std::size_t i = 0;
            while (i + 1 < size)
            {
                const std::uintptr_t absolute = block_base + i;
                if ((absolute & 1) != 0)
                {
                    ++i;
                    continue;
                }

                const std::uint16_t first =
                    static_cast<std::uint16_t>(data[i]) | (static_cast<std::uint16_t>(data[i + 1]) << 8);
                if (!isUtf16Unit(first))
                {
                    i += 2;
                    continue;
                }

                const std::size_t start = i;
                std::u16string text_utf16;
                text_utf16.reserve((std::min)(max_length, std::size_t{256}));

                while (i + 1 < size)
                {
                    const std::uint16_t value =
                        static_cast<std::uint16_t>(data[i]) | (static_cast<std::uint16_t>(data[i + 1]) << 8);
                    if (!isUtf16Unit(value))
                    {
                        break;
                    }
                    if (text_utf16.size() < max_length)
                    {
                        text_utf16.push_back(static_cast<char16_t>(value));
                    }
                    i += 2;
                }

                const std::size_t sequence_len = (i - start) / 2;
                if (sequence_len < min_length || text_utf16.empty())
                {
                    continue;
                }

                std::string text = utf16ToUtf8(text_utf16);
                if (text.empty())
                {
                    continue;
                }

                const std::uintptr_t address = block_base + start;
                if (!seen_addresses.insert(address).second)
                {
                    continue;
                }
                if (!matchesFilter(text, options))
                {
                    continue;
                }

                out.push_back(StringEntry{address, std::move(text), Encoding::Utf16});
                ++added;
                if (max_to_add > 0 && added >= max_to_add)
                {
                    return added;
                }
            }

            return added;
        }

        [[nodiscard]] static std::string utf16ToUtf8(const std::u16string &text)
        {
            if (text.empty())
            {
                return {};
            }

#ifdef _WIN32
            std::wstring wide;
            wide.reserve(text.size());
            for (const auto c : text)
            {
                wide.push_back(static_cast<wchar_t>(c));
            }

            const int required = ::WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.data(),
                static_cast<int>(wide.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (required <= 0)
            {
                return {};
            }

            std::string out(static_cast<std::size_t>(required), '\0');
            const int written = ::WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.data(),
                static_cast<int>(wide.size()),
                out.data(),
                required,
                nullptr,
                nullptr);
            if (written <= 0)
            {
                return {};
            }
            return out;
#else
            std::string out;
            out.reserve(text.size());
            for (const auto c : text)
            {
                out.push_back(static_cast<char>(c & 0xFF));
            }
            return out;
#endif
        }

        [[nodiscard]] static std::uintptr_t regionEnd(const MemoryRegion &region)
        {
            const auto limit = (std::numeric_limits<std::uintptr_t>::max)();
            if (region.base > limit - region.size)
            {
                return limit;
            }
            return region.base + static_cast<std::uintptr_t>(region.size);
        }

        [[nodiscard]] static bool isReadableProtection(std::uint32_t protection)
        {
#ifdef _WIN32
            if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0)
            {
                return false;
            }
            const std::uint32_t base = protection & 0xFF;
            return base == PAGE_READONLY || base == PAGE_READWRITE || base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
#else
            (void)protection;
            return false;
#endif
        }

        [[nodiscard]] static bool isWritableProtection(std::uint32_t protection)
        {
#ifdef _WIN32
            const std::uint32_t base = protection & 0xFF;
            return base == PAGE_READWRITE || base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
#else
            (void)protection;
            return false;
#endif
        }

        [[nodiscard]] std::vector<MemoryRegion> queryRegions() const
        {
            std::vector<MemoryRegion> regions;

#ifdef _WIN32
            if (m_reader == nullptr || !m_reader->attached())
            {
                return regions;
            }

            const HANDLE process = m_reader->process().nativeHandle();
            if (process == nullptr)
            {
                return regions;
            }

            SYSTEM_INFO system_info{};
            ::GetSystemInfo(&system_info);
            std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(system_info.lpMinimumApplicationAddress);
            const std::uintptr_t max_address = reinterpret_cast<std::uintptr_t>(system_info.lpMaximumApplicationAddress);

            while (cursor < max_address)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                const SIZE_T queried = ::VirtualQueryEx(
                    process,
                    reinterpret_cast<LPCVOID>(cursor),
                    &mbi,
                    sizeof(mbi));
                if (queried == 0)
                {
                    cursor += 0x1000;
                    continue;
                }

                MemoryRegion region{};
                region.base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
                region.size = static_cast<std::size_t>(mbi.RegionSize);
                region.protection = static_cast<std::uint32_t>(mbi.Protect);
                region.state = static_cast<std::uint32_t>(mbi.State);

                const std::uintptr_t next = regionEnd(region);
                if (next <= cursor)
                {
                    break;
                }
                cursor = next;

                if (region.state == MEM_COMMIT && region.size > 0)
                {
                    regions.push_back(region);
                }
            }

            std::sort(regions.begin(), regions.end(), [](const MemoryRegion &a, const MemoryRegion &b)
                      { return a.base < b.base; });
#endif

            return regions;
        }

        [[nodiscard]] std::vector<StringEntry> scanRegionSubset(
            const std::vector<MemoryRegion> &regions,
            std::size_t start_index,
            std::size_t stride,
            std::uintptr_t scan_start,
            std::uintptr_t scan_end,
            std::size_t min_len,
            std::size_t max_len,
            std::size_t chunk_size,
            std::size_t overlap,
            const ScanOptions &options) const
        {
            std::vector<StringEntry> result;
            const std::size_t reserve_hint = options.max_results == 0
                                                 ? std::size_t{32768}
                                                 : (std::min)(options.max_results, std::size_t{32768});
            result.reserve((std::min)(reserve_hint, std::size_t{131072}));

            std::vector<std::uint8_t> buffer;
            buffer.resize(chunk_size);

            std::unordered_set<std::uintptr_t> seen_addresses;
            seen_addresses.reserve(reserve_hint * 2);

            for (std::size_t region_index = start_index; region_index < regions.size(); region_index += stride)
            {
                const auto &region = regions[region_index];
                if (!isReadableProtection(region.protection))
                {
                    continue;
                }
                if (!options.include_writable_regions && isWritableProtection(region.protection))
                {
                    continue;
                }

                const std::uintptr_t region_end = regionEnd(region);
                std::uintptr_t local_start = (std::max)(scan_start, region.base);
                const std::uintptr_t local_end = (std::min)(scan_end, region_end);
                if (local_start >= local_end)
                {
                    continue;
                }

                std::uintptr_t cursor = local_start;
                while (cursor < local_end)
                {
                    const std::size_t to_read = static_cast<std::size_t>((std::min)(
                        std::uint64_t(chunk_size),
                        std::uint64_t(local_end - cursor)
                    ));
                    if (to_read == 0)
                    {
                        break;
                    }

                    if (!m_reader->readBytes(cursor, buffer.data(), to_read))
                    {
                        const std::uintptr_t step = (std::min)(std::uintptr_t{4096}, local_end - cursor);
                        if (step == 0)
                        {
                            break;
                        }
                        cursor += step;
                        continue;
                    }

                    if (options.scan_ascii)
                    {
                        scanAsciiBlock(
                            cursor,
                            buffer.data(),
                            to_read,
                            min_len,
                            max_len,
                            options,
                            seen_addresses,
                            0,
                            result
                        );
                    }

                    if (options.scan_utf16)
                    {
                        scanUtf16Block(
                            cursor,
                            buffer.data(),
                            to_read,
                            min_len,
                            max_len,
                            options,
                            seen_addresses,
                            0,
                            result
                        );
                    }

                    const std::size_t step_size = to_read > overlap ? (to_read - overlap) : to_read;
                    if (step_size == 0)
                    {
                        break;
                    }
                    cursor += static_cast<std::uintptr_t>(step_size);
                }
            }

            return result;
        }

        const MemoryReader *m_reader = nullptr;
    };

} // namespace farcal::memory
