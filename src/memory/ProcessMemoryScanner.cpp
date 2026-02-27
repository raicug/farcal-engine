#include "farcal/memory/ProcessMemoryScanner.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
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

namespace farcal::memory {
namespace {

template <typename T>
bool parseIntegral(std::string_view text, bool hexInput, T& outValue) {
  if (text.empty()) {
    return false;
  }

  const int base = hexInput ? 16 : 10;
  std::string temp(text);

  try {
    if constexpr (std::is_signed_v<T>) {
      const long long value = std::stoll(temp, nullptr, base);
      if (value < static_cast<long long>(std::numeric_limits<T>::min())
          || value > static_cast<long long>(std::numeric_limits<T>::max())) {
        return false;
      }
      outValue = static_cast<T>(value);
    } else {
      const unsigned long long value = std::stoull(temp, nullptr, base);
      if (value > static_cast<unsigned long long>(std::numeric_limits<T>::max())) {
        return false;
      }
      outValue = static_cast<T>(value);
    }
  } catch (...) {
    return false;
  }

  return true;
}

template <typename T>
void appendValueBytes(T value, std::vector<std::uint8_t>& outBytes) {
  outBytes.resize(sizeof(T));
  std::memcpy(outBytes.data(), &value, sizeof(T));
}

bool equalCaseInsensitiveAscii(const std::vector<std::uint8_t>& left,
                               const std::vector<std::uint8_t>& right) {
  if (left.size() != right.size()) {
    return false;
  }

  for (std::size_t i = 0; i < left.size(); ++i) {
    const unsigned char l = static_cast<unsigned char>(left[i]);
    const unsigned char r = static_cast<unsigned char>(right[i]);
    if (std::tolower(l) != std::tolower(r)) {
      return false;
    }
  }
  return true;
}

template <typename T>
bool readValue(const std::vector<std::uint8_t>& bytes, T& outValue) {
  if (bytes.size() != sizeof(T)) {
    return false;
  }
  std::memcpy(&outValue, bytes.data(), sizeof(T));
  return true;
}

} // namespace

ProcessMemoryScanner::ProcessMemoryScanner(const MemoryReader* reader) : m_reader(reader) {}

void ProcessMemoryScanner::setReader(const MemoryReader* reader) noexcept {
  m_reader = reader;
}

void ProcessMemoryScanner::reset() {
  m_results.clear();
  m_history.clear();
  m_lastError.clear();
}

bool ProcessMemoryScanner::firstScan(const ScanSettings& settings,
                                     const std::string&  query,
                                     ProgressCallback    progress) {
  m_lastError.clear();
  if (m_reader == nullptr || !m_reader->attached()) {
    m_lastError = "No process attached.";
    return false;
  }

  if (settings.scanType != ScanType::ExactValue) {
    m_lastError = "First Scan currently supports Exact Value only.";
    return false;
  }

  std::vector<std::uint8_t> queryBytes;
  if (!buildQueryBytes(settings, query, queryBytes)) {
    if (m_lastError.empty()) {
      m_lastError = "Invalid query value.";
    }
    return false;
  }

  std::vector<Region> regions;
  if (!collectReadableRegions(settings.includeReadOnly, regions)) {
    if (m_lastError.empty()) {
      m_lastError = "Failed to enumerate readable memory regions.";
    }
    return false;
  }

  std::vector<ScanEntry> newResults;
  if (!scanAllRegionsExact(settings, regions, queryBytes, newResults, progress)) {
    if (m_lastError.empty()) {
      m_lastError = "Failed to scan process memory.";
    }
    return false;
  }

  m_history.clear();
  m_results = std::move(newResults);
  m_lastSettings = settings;
  return true;
}

bool ProcessMemoryScanner::nextScan(const ScanSettings& settings,
                                    const std::string&  query,
                                    ProgressCallback    progress) {
  m_lastError.clear();
  if (m_reader == nullptr || !m_reader->attached()) {
    m_lastError = "No process attached.";
    return false;
  }

  if (m_results.empty()) {
    m_lastError = "No previous scan results. Run First Scan first.";
    return false;
  }

  if (settings.valueType != m_lastSettings.valueType) {
    m_lastError = "Value type must stay the same between First Scan and Next Scan.";
    return false;
  }

  std::vector<std::uint8_t> queryBytes;
  if (settings.scanType == ScanType::ExactValue) {
    if (!buildQueryBytes(settings, query, queryBytes)) {
      if (m_lastError.empty()) {
        m_lastError = "Invalid query value.";
      }
      return false;
    }
  }

  m_history.push_back(m_results);
  if (!rescanExisting(settings, queryBytes, progress)) {
    if (m_history.empty()) {
      m_results.clear();
    } else {
      m_results = m_history.back();
      m_history.pop_back();
    }
    if (m_lastError.empty()) {
      m_lastError = "Failed to perform Next Scan.";
    }
    return false;
  }

  m_lastSettings = settings;
  return true;
}

bool ProcessMemoryScanner::undo() {
  if (m_history.empty()) {
    m_lastError = "Nothing to undo.";
    return false;
  }

  m_results = std::move(m_history.back());
  m_history.pop_back();
  m_lastError.clear();
  return true;
}

const std::vector<ScanEntry>& ProcessMemoryScanner::results() const noexcept {
  return m_results;
}

std::size_t ProcessMemoryScanner::resultCount() const noexcept {
  return m_results.size();
}

const ScanSettings& ProcessMemoryScanner::lastSettings() const noexcept {
  return m_lastSettings;
}

const std::string& ProcessMemoryScanner::lastError() const noexcept {
  return m_lastError;
}

bool ProcessMemoryScanner::collectReadableRegions(bool includeReadOnly, std::vector<Region>& outRegions) {
#ifndef _WIN32
  (void)includeReadOnly;
  (void)outRegions;
  m_lastError = "Only implemented on Windows.";
  return false;
#else
  outRegions.clear();
  if (m_reader == nullptr || !m_reader->attached()) {
    m_lastError = "No process attached.";
    return false;
  }

  const HANDLE process = m_reader->process().nativeHandle();
  if (process == nullptr) {
    m_lastError = "Invalid process handle.";
    return false;
  }

  SYSTEM_INFO systemInfo{};
  ::GetSystemInfo(&systemInfo);

  std::uintptr_t address = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
  const std::uintptr_t maxAddress =
      reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);

  while (address < maxAddress) {
    MEMORY_BASIC_INFORMATION mbi{};
    const SIZE_T queried = ::VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi));
    if (queried == 0) {
      break;
    }

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    const std::uintptr_t next = base + static_cast<std::uintptr_t>(mbi.RegionSize);
    const DWORD protect = mbi.Protect & 0xFF;

    const bool committed = (mbi.State == MEM_COMMIT);
    const bool guarded = (mbi.Protect & PAGE_GUARD) != 0;
    const bool noAccess = (mbi.Protect & PAGE_NOACCESS) != 0;
    const bool readable =
        protect == PAGE_READONLY || protect == PAGE_READWRITE || protect == PAGE_WRITECOPY
        || protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE
        || protect == PAGE_EXECUTE_WRITECOPY;
    const bool isReadOnlyPage = (protect == PAGE_READONLY || protect == PAGE_EXECUTE_READ);
    const bool allowedByReadOnlyToggle = includeReadOnly || !isReadOnlyPage;

    if (committed && !guarded && !noAccess && readable && allowedByReadOnlyToggle
        && mbi.RegionSize >= 1) {
      outRegions.push_back({base, static_cast<std::size_t>(mbi.RegionSize)});
    }

    if (next <= address) {
      break;
    }
    address = next;
  }

  return true;
#endif
}

bool ProcessMemoryScanner::buildQueryBytes(const ScanSettings& settings,
                                           const std::string&  query,
                                           std::vector<std::uint8_t>& outQueryBytes) const {
  outQueryBytes.clear();

  if (query.empty()) {
    return false;
  }

  switch (settings.valueType) {
    case ScanValueType::Int8: {
      std::int8_t value = 0;
      if (!parseIntegral(query, settings.hexInput, value)) {
        return false;
      }
      appendValueBytes(value, outQueryBytes);
      return true;
    }
    case ScanValueType::Int16: {
      std::int16_t value = 0;
      if (!parseIntegral(query, settings.hexInput, value)) {
        return false;
      }
      appendValueBytes(value, outQueryBytes);
      return true;
    }
    case ScanValueType::Int32: {
      std::int32_t value = 0;
      if (!parseIntegral(query, settings.hexInput, value)) {
        return false;
      }
      appendValueBytes(value, outQueryBytes);
      return true;
    }
    case ScanValueType::Int64: {
      std::int64_t value = 0;
      if (!parseIntegral(query, settings.hexInput, value)) {
        return false;
      }
      appendValueBytes(value, outQueryBytes);
      return true;
    }
    case ScanValueType::Float: {
      try {
        const float value = std::stof(query);
        appendValueBytes(value, outQueryBytes);
        return true;
      } catch (...) {
        return false;
      }
    }
    case ScanValueType::Double: {
      try {
        const double value = std::stod(query);
        appendValueBytes(value, outQueryBytes);
        return true;
      } catch (...) {
        return false;
      }
    }
    case ScanValueType::String: {
      if (!settings.unicode) {
        outQueryBytes.assign(query.begin(), query.end());
        return !outQueryBytes.empty();
      }

      outQueryBytes.clear();
      outQueryBytes.reserve(query.size() * 2);
      for (const char ch : query) {
        const std::uint16_t wch = static_cast<std::uint8_t>(ch);
        outQueryBytes.push_back(static_cast<std::uint8_t>(wch & 0xFFu));
        outQueryBytes.push_back(static_cast<std::uint8_t>((wch >> 8u) & 0xFFu));
      }
      return !outQueryBytes.empty();
    }
  }

  return false;
}

bool ProcessMemoryScanner::scanAllRegionsExact(const ScanSettings&             settings,
                                               const std::vector<Region>&      regions,
                                               const std::vector<std::uint8_t>& queryBytes,
                                               std::vector<ScanEntry>&         outEntries,
                                               const ProgressCallback&          progress) {
  if (queryBytes.empty()) {
    m_lastError = "Query bytes are empty.";
    return false;
  }
  if (m_reader == nullptr || !m_reader->attached()) {
    m_lastError = "No process attached.";
    return false;
  }

  outEntries.clear();

  constexpr std::size_t kChunkSize = 1u << 20u;
  const std::size_t valueSize = queryBytes.size();
  const std::size_t alignment = std::max<std::size_t>(1, settings.alignment);
  std::vector<std::uint8_t> buffer;
  buffer.resize(kChunkSize + valueSize);

  for (std::size_t regionIndex = 0; regionIndex < regions.size(); ++regionIndex) {
    const Region& region = regions[regionIndex];

    std::size_t regionOffset = 0;
    while (regionOffset < region.size) {
      const std::size_t remaining = region.size - regionOffset;
      const std::size_t bytesToRead = std::min(buffer.size(), remaining);
      const std::uintptr_t chunkAddress = region.base + regionOffset;

      if (!m_reader->readBytes(chunkAddress, buffer.data(), bytesToRead)) {
        break;
      }

      if (bytesToRead < valueSize) {
        break;
      }

      std::size_t scanLimit = bytesToRead - valueSize + 1;
      if (regionOffset + kChunkSize < region.size) {
        scanLimit = std::min(scanLimit, kChunkSize);
      }

      for (std::size_t offset = 0; offset < scanLimit; ++offset) {
        const std::uintptr_t address = chunkAddress + offset;
        if ((address % alignment) != 0) {
          continue;
        }

        if (settings.valueType == ScanValueType::String && !settings.caseSensitive) {
          std::vector<std::uint8_t> candidate(valueSize);
          std::memcpy(candidate.data(), buffer.data() + offset, valueSize);
          if (!equalCaseInsensitiveAscii(candidate, queryBytes)) {
            continue;
          }
        } else if (std::memcmp(buffer.data() + offset, queryBytes.data(), valueSize) != 0) {
          continue;
        }

        ScanEntry entry;
        entry.address = address;
        entry.previousValue.assign(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                                   buffer.begin() + static_cast<std::ptrdiff_t>(offset + valueSize));
        entry.currentValue = entry.previousValue;
        outEntries.push_back(std::move(entry));
      }

      regionOffset += kChunkSize;
    }

    if (progress) {
      progress(regionIndex + 1, regions.size());
    }
  }

  return true;
}

bool ProcessMemoryScanner::rescanExisting(const ScanSettings&             settings,
                                          const std::vector<std::uint8_t>& queryBytes,
                                          const ProgressCallback&          progress) {
  if (m_reader == nullptr || !m_reader->attached()) {
    m_lastError = "No process attached.";
    return false;
  }

  std::vector<ScanEntry> filtered;
  filtered.reserve(m_results.size());

  for (std::size_t i = 0; i < m_results.size(); ++i) {
    const ScanEntry& oldEntry = m_results[i];
    const std::size_t valueSize = valueSizeFromSettings(
        settings,
        (settings.scanType == ScanType::ExactValue && settings.valueType == ScanValueType::String)
            ? queryBytes.size()
            : oldEntry.currentValue.size());

    if (valueSize == 0) {
      continue;
    }

    std::vector<std::uint8_t> current(valueSize);
    if (!m_reader->readBytes(oldEntry.address, current.data(), valueSize)) {
      if (progress) {
        progress(i + 1, m_results.size());
      }
      continue;
    }

    if (!matchesCondition(settings, queryBytes, oldEntry.currentValue, current)) {
      if (progress) {
        progress(i + 1, m_results.size());
      }
      continue;
    }

    ScanEntry newEntry;
    newEntry.address = oldEntry.address;
    newEntry.previousValue = oldEntry.currentValue;
    newEntry.currentValue = std::move(current);
    filtered.push_back(std::move(newEntry));

    if (progress) {
      progress(i + 1, m_results.size());
    }
  }

  m_results = std::move(filtered);
  return true;
}

std::size_t ProcessMemoryScanner::valueSizeFromSettings(const ScanSettings& settings,
                                                        std::size_t         queryByteLength) {
  switch (settings.valueType) {
    case ScanValueType::Int8:
      return 1;
    case ScanValueType::Int16:
      return 2;
    case ScanValueType::Int32:
    case ScanValueType::Float:
      return 4;
    case ScanValueType::Int64:
    case ScanValueType::Double:
      return 8;
    case ScanValueType::String:
      return queryByteLength;
  }
  return 0;
}

bool ProcessMemoryScanner::isNumericType(ScanValueType valueType) {
  return valueType != ScanValueType::String;
}

bool ProcessMemoryScanner::matchesCondition(const ScanSettings&             settings,
                                            const std::vector<std::uint8_t>& queryBytes,
                                            const std::vector<std::uint8_t>& previous,
                                            const std::vector<std::uint8_t>& current) const {
  switch (settings.scanType) {
    case ScanType::ExactValue:
      if (settings.valueType == ScanValueType::String && !settings.caseSensitive) {
        return equalCaseInsensitiveAscii(current, queryBytes);
      }
      return current == queryBytes;

    case ScanType::ChangedValue:
      return current != previous;

    case ScanType::UnchangedValue:
      return current == previous;

    case ScanType::IncreasedValue:
      if (!isNumericType(settings.valueType)) {
        return false;
      }
      switch (settings.valueType) {
        case ScanValueType::Int8:
          return compareNumeric<std::int8_t>(settings.scanType, previous, current);
        case ScanValueType::Int16:
          return compareNumeric<std::int16_t>(settings.scanType, previous, current);
        case ScanValueType::Int32:
          return compareNumeric<std::int32_t>(settings.scanType, previous, current);
        case ScanValueType::Int64:
          return compareNumeric<std::int64_t>(settings.scanType, previous, current);
        case ScanValueType::Float:
          return compareNumeric<float>(settings.scanType, previous, current);
        case ScanValueType::Double:
          return compareNumeric<double>(settings.scanType, previous, current);
        case ScanValueType::String:
          return false;
      }
      return false;

    case ScanType::DecreasedValue:
      if (!isNumericType(settings.valueType)) {
        return false;
      }
      switch (settings.valueType) {
        case ScanValueType::Int8:
          return compareNumeric<std::int8_t>(settings.scanType, previous, current);
        case ScanValueType::Int16:
          return compareNumeric<std::int16_t>(settings.scanType, previous, current);
        case ScanValueType::Int32:
          return compareNumeric<std::int32_t>(settings.scanType, previous, current);
        case ScanValueType::Int64:
          return compareNumeric<std::int64_t>(settings.scanType, previous, current);
        case ScanValueType::Float:
          return compareNumeric<float>(settings.scanType, previous, current);
        case ScanValueType::Double:
          return compareNumeric<double>(settings.scanType, previous, current);
        case ScanValueType::String:
          return false;
      }
      return false;
  }

  return false;
}

bool ProcessMemoryScanner::compareAsString(const ScanSettings&             settings,
                                           const std::vector<std::uint8_t>& left,
                                           const std::vector<std::uint8_t>& right) const {
  if (settings.caseSensitive) {
    return left == right;
  }
  return equalCaseInsensitiveAscii(left, right);
}

template <typename T>
bool ProcessMemoryScanner::compareNumeric(ScanType scanType,
                                          const std::vector<std::uint8_t>& previous,
                                          const std::vector<std::uint8_t>& current) {
  T previousValue{};
  T currentValue{};
  if (!readValue(previous, previousValue) || !readValue(current, currentValue)) {
    return false;
  }

  if constexpr (std::is_floating_point_v<T>) {
    if (std::isnan(previousValue) || std::isnan(currentValue)) {
      return false;
    }
  }

  if (scanType == ScanType::IncreasedValue) {
    return currentValue > previousValue;
  }
  if (scanType == ScanType::DecreasedValue) {
    return currentValue < previousValue;
  }
  return false;
}

} // namespace farcal::memory
