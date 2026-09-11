#pragma once

#include <cstdint>

extern "C" void print_progress(std::uint64_t transferred,
                                std::uint64_t total);
