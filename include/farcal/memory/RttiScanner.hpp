#pragma once

#include "farcal/memory/MemoryReader.hpp"
#include "q_lit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace farcal::memory {

class RttiScanner {
 public:
  struct TypeInfo {
    std::uintptr_t              type_descriptor = 0;
    std::string                 demangled_name;
    std::vector<std::uintptr_t> vftables;
  };

  struct ScanOptions {
    std::size_t max_results                   = 0;
    std::size_t max_candidates                = 0;
    std::size_t pointer_stride                = sizeof(std::uintptr_t);
    std::size_t max_name_length               = 0;
    std::size_t max_vftables_per_type         = 0;
    bool        require_executable_first_slot = true;
    bool        include_writable_regions      = false;
    bool        demangle_names                = true;
  };

  explicit RttiScanner(const MemoryReader* reader = nullptr) : m_reader(reader) {}

  void setReader(const MemoryReader* reader) { m_reader = reader; }

  std::vector<TypeInfo> find_all() const { return find_all(ScanOptions{}); }

  std::vector<TypeInfo> find_all(const ScanOptions& options) const {
    std::vector<TypeInfo> results;
    if (m_reader == nullptr || !m_reader->attached()) {
      return results;
    }

    auto regions = queryRegions();
    if (regions.empty()) {
      return results;
    }

    const std::size_t max_results =
        options.max_results == 0 ? std::size_t{60000} : options.max_results;
    const std::size_t max_name_len =
        options.max_name_length == 0 ? std::size_t{256} : options.max_name_length;
    const std::size_t max_vftables =
        options.max_vftables_per_type == 0 ? std::size_t{16} : options.max_vftables_per_type;
    const std::size_t stride =
        options.pointer_stride == 0 ? sizeof(std::uintptr_t) : options.pointer_stride;
    const std::size_t max_candidates =
        options.max_candidates == 0 ? std::size_t{4000000} : options.max_candidates;

    std::unordered_map<std::uintptr_t, std::size_t> type_to_index;
    type_to_index.reserve((std::min)(max_results, std::size_t{65536}));
    results.reserve((std::min)(max_results, std::size_t{65536}));

    discoverTypeDescriptors(regions, options, max_name_len, max_results, type_to_index, results);

    if (results.empty()) {
      return results;
    }

    discoverVftables(
        regions, options, stride, max_candidates, max_vftables, type_to_index, results);

    return results;
  }

  std::optional<std::string> get_rtti_of_address(std::uintptr_t address,
                                                 bool           demangle = true) const {
    if (m_reader == nullptr || !m_reader->attached() || address == 0) {
      return std::nullopt;
    }

    // Prefer treating input as a vftable pointer first.
    // In structure dissectors, qword fields often already store vftable addresses.
    auto direct = get_rtti_of_vftable(address, demangle);
    if (direct.has_value() && !isGenericTypeInfoName(*direct)) {
      return direct;
    }

    auto       by_object      = std::optional<std::string>{};
    const auto object_vftable = m_reader->read<std::uintptr_t>(address);
    if (object_vftable.has_value() && *object_vftable != 0 && *object_vftable != address) {
      by_object = get_rtti_of_vftable(*object_vftable, demangle);
      if (by_object.has_value() && !isGenericTypeInfoName(*by_object)) {
        return by_object;
      }
    }

    if (direct.has_value() && !direct->empty()) {
      return direct;
    }
    if (by_object.has_value() && !by_object->empty()) {
      return by_object;
    }
    return std::nullopt;
  }

  std::optional<std::string> get_rtti_of_vftable(std::uintptr_t vftable_address,
                                                 bool           demangle = true) const {
    if (m_reader == nullptr || !m_reader->attached() || vftable_address < sizeof(std::uintptr_t)) {
      return std::nullopt;
    }

    const auto col_ptr = m_reader->read<std::uintptr_t>(vftable_address - sizeof(std::uintptr_t));
    if (!col_ptr.has_value() || *col_ptr == 0) {
      return std::nullopt;
    }

    const auto td = resolveTypeDescriptorFromCol(*m_reader, *col_ptr);
    if (!td.has_value() || *td < sizeof(std::uintptr_t) * 2) {
      return std::nullopt;
    }

    const auto decorated = readDecoratedNameFromProcess(*td + (sizeof(std::uintptr_t) * 2), 256);
    if (!decorated.has_value() || !looksLikeRttiDecoratedName(*decorated)) {
      return std::nullopt;
    }

    if (!demangle) {
      return decorated;
    }

    return demangleFast(*decorated);
  }

 private:
  struct MemoryRegion {
    std::uintptr_t base       = 0;
    std::size_t    size       = 0;
    std::uint32_t  protection = 0;
    std::uint32_t  state      = 0;
  };

  static bool isReadableProtection(std::uint32_t protection) {
#ifdef _WIN32
    if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) {
      return false;
    }
    const std::uint32_t base = protection & 0xFF;
    return base == PAGE_READONLY || base == PAGE_READWRITE || base == PAGE_WRITECOPY
           || base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE
           || base == PAGE_EXECUTE_WRITECOPY;
#else
    (void)protection;
    return false;
#endif
  }

  static bool isExecutableProtection(std::uint32_t protection) {
#ifdef _WIN32
    const std::uint32_t base = protection & 0xFF;
    return base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE
           || base == PAGE_EXECUTE_WRITECOPY;
#else
    (void)protection;
    return false;
#endif
  }

  static bool isWritableProtection(std::uint32_t protection) {
#ifdef _WIN32
    const std::uint32_t base = protection & 0xFF;
    return base == PAGE_READWRITE || base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READWRITE
           || base == PAGE_EXECUTE_WRITECOPY;
#else
    (void)protection;
    return false;
#endif
  }

  static std::uintptr_t regionEnd(const MemoryRegion& region) {
    const auto limit = (std::numeric_limits<std::uintptr_t>::max)();
    if (region.base > limit - region.size) {
      return limit;
    }
    return region.base + static_cast<std::uintptr_t>(region.size);
  }

  static const MemoryRegion* findRegionForAddress(const std::vector<MemoryRegion>& regions,
                                                  std::uintptr_t                   address) {
    const auto it = std::upper_bound(
        regions.begin(),
        regions.end(),
        address,
        [](std::uintptr_t value, const MemoryRegion& region) { return value < region.base; });

    if (it == regions.begin()) {
      return nullptr;
    }

    const MemoryRegion& candidate = *(it - 1);
    if (address < candidate.base) {
      return nullptr;
    }
    const std::uintptr_t end = regionEnd(candidate);
    if (address >= end) {
      return nullptr;
    }
    return &candidate;
  }

  std::vector<MemoryRegion> queryRegions() const {
    std::vector<MemoryRegion> regions;

#ifdef _WIN32
    if (m_reader == nullptr || !m_reader->attached()) {
      return regions;
    }

    const HANDLE process = m_reader->process().nativeHandle();
    if (process == nullptr) {
      return regions;
    }

    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo(&systemInfo);

    std::uintptr_t cursor =
        reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const std::uintptr_t maxAddress =
        reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);

    while (cursor < maxAddress) {
      MEMORY_BASIC_INFORMATION mbi{};
      const SIZE_T             queried =
          ::VirtualQueryEx(process, reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi));
      if (queried == 0) {
        cursor += 0x1000;
        continue;
      }

      MemoryRegion region{};
      region.base       = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
      region.size       = static_cast<std::size_t>(mbi.RegionSize);
      region.protection = static_cast<std::uint32_t>(mbi.Protect);
      region.state      = static_cast<std::uint32_t>(mbi.State);

      const std::uintptr_t next = regionEnd(region);
      if (next <= cursor) {
        break;
      }
      cursor = next;

      if (region.state == MEM_COMMIT && region.size > 0) {
        regions.push_back(region);
      }
    }

    std::sort(regions.begin(), regions.end(), [](const MemoryRegion& a, const MemoryRegion& b) {
      return a.base < b.base;
    });
#endif

    return regions;
  }

  static bool looksLikeRttiDecoratedName(const std::string& name) {
    if (name.size() < 5) {
      return false;
    }
    if (name.rfind((".?A"), 0) != 0) {
      return false;
    }
    if (name.find(("@@")) == std::string::npos) {
      return false;
    }
    return true;
  }

  static bool isGenericTypeInfoName(std::string_view name) {
    return name == ("type_info") || name == ("std::type_info") || name == ("class type_info")
           || name == (".?AVtype_info@@") || name == ("?AVtype_info@@");
  }

  static bool isRttiNameByte(std::uint8_t ch) {
    return ch == '.' || ch == '?' || ch == '@' || ch == '$' || ch == '_' || (ch >= '0' && ch <= '9')
           || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
  }

  std::optional<std::string> readDecoratedNameFromProcess(std::uintptr_t address,
                                                          std::size_t    max_len) const {
    std::string out;
    out.reserve((std::min)(max_len, std::size_t{256}));

    for (std::size_t i = 0; i < max_len; ++i) {
      const auto c = m_reader->read<std::uint8_t>(address + i);
      if (!c.has_value()) {
        return std::nullopt;
      }
      if (*c == 0) {
        if (out.empty()) {
          return std::nullopt;
        }
        return out;
      }
      if (!isRttiNameByte(*c)) {
        return std::nullopt;
      }
      out.push_back(static_cast<char>(*c));
    }

    return std::nullopt;
  }

  static std::optional<std::string> parseDecoratedNameInChunk(const std::uint8_t* data,
                                                              std::size_t         size,
                                                              std::size_t         offset,
                                                              std::size_t         max_len) {
    if (offset >= size) {
      return std::nullopt;
    }

    std::string out;
    out.reserve((std::min)(max_len, std::size_t{256}));
    for (std::size_t i = 0; i < max_len && (offset + i) < size; ++i) {
      const std::uint8_t ch = data[offset + i];
      if (ch == 0) {
        if (out.empty()) {
          return std::nullopt;
        }
        return out;
      }
      if (!isRttiNameByte(ch)) {
        return std::nullopt;
      }
      out.push_back(static_cast<char>(ch));
    }

    return std::nullopt;
  }

  static std::string demangleFast(std::string_view decorated) {
    if (decorated.empty()) {
      return {};
    }

    std::string value(decorated);
    if (!value.empty() && value.front() == '.') {
      value.erase(value.begin());
    }

    if (value.size() < 5 || value[0] != '?' || value[1] != 'A') {
      return std::string(decorated);
    }

    const char tag = value[2];
    if (tag != 'V' && tag != 'U' && tag != 'T' && tag != 'W') {
      return std::string(decorated);
    }

    const auto end = value.find(("@@"));
    if (end == std::string::npos || end <= 3) {
      return std::string(decorated);
    }

    const std::string body = value.substr(3, end - 3);
    if (body.empty()) {
      return std::string(decorated);
    }

    std::vector<std::string> parts;
    std::size_t              start = 0;
    while (start < body.size()) {
      const auto        at   = body.find('@', start);
      const std::size_t stop = at == std::string::npos ? body.size() : at;
      if (stop > start) {
        parts.push_back(body.substr(start, stop - start));
      }
      if (at == std::string::npos) {
        break;
      }
      start = at + 1;
    }

    if (parts.empty()) {
      return std::string(decorated);
    }

    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
      if (!out.empty()) {
        out += ("::");
      }
      out += *it;
    }

    return out.empty() ? std::string(decorated) : out;
  }

  static std::optional<std::uintptr_t> resolveTypeDescriptorFromCol(const MemoryReader& reader,
                                                                    std::uintptr_t col_address) {
    if (col_address == 0) {
      return std::nullopt;
    }

    if constexpr (sizeof(std::uintptr_t) == 8) {
      const auto signature = reader.read<std::uint32_t>(col_address + 0);
      const auto td_rva    = reader.read<std::int32_t>(col_address + 12);
      const auto self_rva  = reader.read<std::int32_t>(col_address + 20);
      if (!signature.has_value() || !td_rva.has_value() || !self_rva.has_value()
          || *self_rva == 0) {
        return std::nullopt;
      }
      if (*signature != 0 && *signature != 1) {
        return std::nullopt;
      }

      const auto image_base =
          static_cast<std::int64_t>(col_address) - static_cast<std::int64_t>(*self_rva);
      const auto td_address = image_base + static_cast<std::int64_t>(*td_rva);
      if (image_base <= 0 || td_address <= 0) {
        return std::nullopt;
      }
      return static_cast<std::uintptr_t>(td_address);
    } else {
      const auto td_abs = reader.read<std::uint32_t>(col_address + 12);
      if (!td_abs.has_value() || *td_abs == 0) {
        return std::nullopt;
      }
      return static_cast<std::uintptr_t>(*td_abs);
    }
  }

  void discoverTypeDescriptors(const std::vector<MemoryRegion>&                 regions,
                               const ScanOptions&                               options,
                               std::size_t                                      max_name_len,
                               std::size_t                                      max_results,
                               std::unordered_map<std::uintptr_t, std::size_t>& type_to_index,
                               std::vector<TypeInfo>&                           results) const {
    constexpr std::size_t kChunkSize = 1024 * 1024;
    constexpr std::size_t kOverlap   = 512;

    std::vector<std::uint8_t> buffer(kChunkSize);

    for (const auto& region : regions) {
      if (!isReadableProtection(region.protection)) {
        continue;
      }
      if (!options.include_writable_regions && isWritableProtection(region.protection)) {
        continue;
      }

      const std::uintptr_t start = region.base;
      const std::uintptr_t end   = regionEnd(region);
      if (start >= end) {
        continue;
      }

      std::uintptr_t cursor = start;
      while (cursor < end) {
        const std::size_t to_read = static_cast<std::size_t>(
            (std::min)(std::uint64_t(kChunkSize), std::uint64_t(end - cursor)));
        if (to_read == 0) {
          break;
        }

        if (!m_reader->readBytes(cursor, buffer.data(), to_read)) {
          const std::uintptr_t step = (std::min)(std::uintptr_t{4096}, end - cursor);
          if (step == 0) {
            break;
          }
          cursor += step;
          continue;
        }

        for (std::size_t i = 0; i + 3 < to_read; ++i) {
          if (buffer[i] != '.' || buffer[i + 1] != '?' || buffer[i + 2] != 'A') {
            continue;
          }

          const std::uintptr_t name_addr = cursor + static_cast<std::uintptr_t>(i);
          if (name_addr < sizeof(std::uintptr_t) * 2) {
            continue;
          }

          std::optional<std::string> name =
              parseDecoratedNameInChunk(buffer.data(), to_read, i, max_name_len);
          if (!name.has_value()) {
            name = readDecoratedNameFromProcess(name_addr, max_name_len);
          }
          if (!name.has_value() || !looksLikeRttiDecoratedName(*name)) {
            continue;
          }

          const std::uintptr_t type_descriptor = name_addr - (sizeof(std::uintptr_t) * 2);
          if (type_to_index.find(type_descriptor) != type_to_index.end()) {
            continue;
          }

          TypeInfo info{};
          info.type_descriptor = type_descriptor;
          info.demangled_name  = options.demangle_names ? demangleFast(*name) : *name;

          type_to_index.emplace(type_descriptor, results.size());
          results.push_back(std::move(info));

          if (results.size() >= max_results) {
            return;
          }
        }

        const std::size_t step = to_read > kOverlap ? (to_read - kOverlap) : to_read;
        if (step == 0) {
          break;
        }
        cursor += static_cast<std::uintptr_t>(step);
      }
    }
  }

  static std::uintptr_t readPointerFromBytes(const std::uint8_t* data) {
    std::uintptr_t value = 0;
    std::memcpy(&value, data, sizeof(std::uintptr_t));
    return value;
  }

  void discoverVftables(const std::vector<MemoryRegion>&                       regions,
                        const ScanOptions&                                     options,
                        std::size_t                                            stride,
                        std::size_t                                            max_candidates,
                        std::size_t                                            max_vftables,
                        const std::unordered_map<std::uintptr_t, std::size_t>& type_to_index,
                        std::vector<TypeInfo>&                                 results) const {
    constexpr std::size_t     kChunkSize = 1024 * 1024;
    std::vector<std::uint8_t> buffer(kChunkSize);

    std::size_t candidate_count = 0;
    bool        stop            = false;

    for (const auto& region : regions) {
      if (stop) {
        break;
      }
      if (!isReadableProtection(region.protection)) {
        continue;
      }
      if (!options.include_writable_regions && isWritableProtection(region.protection)) {
        continue;
      }

      const std::uintptr_t start = region.base;
      const std::uintptr_t end   = regionEnd(region);
      if (start >= end) {
        continue;
      }

      std::uintptr_t cursor = start;
      while (cursor < end) {
        if (stop) {
          break;
        }

        const std::size_t to_read = static_cast<std::size_t>(
            (std::min)(std::uint64_t(kChunkSize), std::uint64_t(end - cursor)));
        if (to_read < sizeof(std::uintptr_t)) {
          break;
        }

        if (!m_reader->readBytes(cursor, buffer.data(), to_read)) {
          const std::uintptr_t step = (std::min)(std::uintptr_t{4096}, end - cursor);
          if (step == 0) {
            break;
          }
          cursor += step;
          continue;
        }

        for (std::size_t i = 0; i + sizeof(std::uintptr_t) <= to_read; i += stride) {
          ++candidate_count;
          if (candidate_count >= max_candidates) {
            stop = true;
            break;
          }

          const std::uintptr_t slot_address = cursor + static_cast<std::uintptr_t>(i);
          const std::uintptr_t col_address  = readPointerFromBytes(buffer.data() + i);
          if (col_address == 0) {
            continue;
          }

          const auto td = resolveTypeDescriptorFromCol(*m_reader, col_address);
          if (!td.has_value()) {
            continue;
          }

          const auto type_it = type_to_index.find(*td);
          if (type_it == type_to_index.end()) {
            continue;
          }

          TypeInfo& type_info = results[type_it->second];
          if (type_info.vftables.size() >= max_vftables) {
            continue;
          }

          const std::uintptr_t vftable_address = slot_address + sizeof(std::uintptr_t);
          if (options.require_executable_first_slot) {
            const auto first_slot = m_reader->read<std::uintptr_t>(vftable_address);
            if (!first_slot.has_value() || *first_slot == 0) {
              continue;
            }

            const MemoryRegion* slot_region = findRegionForAddress(regions, *first_slot);
            if (slot_region == nullptr || !isExecutableProtection(slot_region->protection)) {
              continue;
            }
          }

          const bool exists =
              std::find(type_info.vftables.begin(), type_info.vftables.end(), vftable_address)
              != type_info.vftables.end();
          if (!exists) {
            type_info.vftables.push_back(vftable_address);
          }
        }

        cursor += static_cast<std::uintptr_t>(to_read);
      }
    }
  }

  const MemoryReader* m_reader = nullptr;
};

}  // namespace farcal::memory
