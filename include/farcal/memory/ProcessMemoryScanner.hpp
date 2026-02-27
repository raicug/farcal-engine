#pragma once

#include "farcal/memory/MemoryReader.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace farcal::memory {

enum class ScanType {
  ExactValue = 0,
  IncreasedValue,
  DecreasedValue,
  ChangedValue,
  UnchangedValue
};

enum class ScanValueType {
  Int8 = 0,
  Int16,
  Int32,
  Int64,
  Float,
  Double,
  String
};

struct ScanSettings {
  ScanType      scanType = ScanType::ExactValue;
  ScanValueType valueType = ScanValueType::Int32;
  bool          hexInput = false;
  bool          includeReadOnly = false;
  bool          caseSensitive = false;
  bool          unicode = false;
  std::size_t   alignment = 1;
};

struct ScanEntry {
  std::uintptr_t           address = 0;
  std::vector<std::uint8_t> previousValue;
  std::vector<std::uint8_t> currentValue;
};

class ProcessMemoryScanner final {
 public:
  using ProgressCallback = std::function<void(std::size_t, std::size_t)>;

  explicit ProcessMemoryScanner(const MemoryReader* reader = nullptr);

  void setReader(const MemoryReader* reader) noexcept;
  void reset();

  [[nodiscard]] bool firstScan(const ScanSettings& settings,
                               const std::string&  query,
                               ProgressCallback    progress = {});
  [[nodiscard]] bool nextScan(const ScanSettings& settings,
                              const std::string&  query,
                              ProgressCallback    progress = {});
  [[nodiscard]] bool undo();

  [[nodiscard]] const std::vector<ScanEntry>& results() const noexcept;
  [[nodiscard]] std::size_t                   resultCount() const noexcept;
  [[nodiscard]] const ScanSettings&           lastSettings() const noexcept;
  [[nodiscard]] const std::string&            lastError() const noexcept;

 private:
  struct Region {
    std::uintptr_t base = 0;
    std::size_t    size = 0;
  };

  [[nodiscard]] bool collectReadableRegions(bool includeReadOnly, std::vector<Region>& outRegions);
  [[nodiscard]] bool buildQueryBytes(const ScanSettings& settings,
                                     const std::string&  query,
                                     std::vector<std::uint8_t>& outQueryBytes) const;
  [[nodiscard]] bool scanAllRegionsExact(const ScanSettings&          settings,
                                         const std::vector<Region>&   regions,
                                         const std::vector<std::uint8_t>& queryBytes,
                                         std::vector<ScanEntry>&      outEntries,
                                         const ProgressCallback&       progress);
  [[nodiscard]] bool rescanExisting(const ScanSettings&    settings,
                                    const std::vector<std::uint8_t>& queryBytes,
                                    const ProgressCallback& progress);

  [[nodiscard]] static std::size_t valueSizeFromSettings(const ScanSettings& settings,
                                                         std::size_t         queryByteLength = 0);
  [[nodiscard]] static bool        isNumericType(ScanValueType valueType);
  [[nodiscard]] bool               matchesCondition(const ScanSettings&            settings,
                                                    const std::vector<std::uint8_t>& queryBytes,
                                                    const std::vector<std::uint8_t>& previous,
                                                    const std::vector<std::uint8_t>& current) const;
  [[nodiscard]] bool               compareAsString(const ScanSettings&            settings,
                                                   const std::vector<std::uint8_t>& left,
                                                   const std::vector<std::uint8_t>& right) const;

  template <typename T>
  [[nodiscard]] static bool compareNumeric(ScanType scanType, const std::vector<std::uint8_t>& previous,
                                           const std::vector<std::uint8_t>& current);

  const MemoryReader* m_reader = nullptr;
  std::vector<ScanEntry> m_results;
  std::vector<std::vector<ScanEntry>> m_history;
  ScanSettings m_lastSettings{};
  std::string  m_lastError;
};

} // namespace farcal::memory
