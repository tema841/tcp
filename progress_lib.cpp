#include "progress.hpp"

#include <cstdint>
#include <iostream>

extern "C" void print_progress(std::uint64_t transferred,
                               std::uint64_t total) {
  const int pct = total
      ? static_cast<int>((transferred * 100ULL) / total)
      : 100;

  std::cout << '\r' << pct << "% "
            << transferred << "/" << total << " bytes "
            << std::flush;
}
