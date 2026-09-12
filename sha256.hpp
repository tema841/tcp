#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace crypto {
class SHA256 {
public: SHA256(); void update(const void*,std::size_t); void update(const std::vector<char>&); std::string final_hex();
private: void transform(const std::uint8_t*); std::uint8_t data_[64]{}; std::uint32_t state_[8]{}; std::uint64_t bitlen_=0; std::size_t datalen_=0; bool finalized_=false;
};
std::string file_sha256(const std::string& path);
}
