#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
namespace monitor {
struct Transfer {std::uint64_t id=0; std::string direction,file,peer,state; std::uint64_t total=0,done=0; std::uint64_t chunk=0; double speed=0; double rtt_ms=0; double cpu_hint=0; std::string checksum;};
class Registry {public: std::uint64_t begin(const std::string&,const std::string&,const std::string&,std::uint64_t,std::uint64_t); void update(std::uint64_t,std::uint64_t,std::uint64_t,double,double,const std::string&); void finish(std::uint64_t,const std::string&,const std::string&); std::vector<Transfer> snapshot() const; std::uint64_t total_bytes() const; std::uint64_t completed() const; private: mutable std::mutex m_; std::vector<Transfer> items_; std::uint64_t next_=1;};
std::string json_escape(const std::string&);
std::string status_json(const Registry&);
}
