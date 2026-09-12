#pragma once
#include "socket.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
namespace transfer { using ProgressCallback=std::function<void(std::uint64_t,std::uint64_t,std::uint64_t,double,double)>; struct Result{bool ok=false;std::uint64_t bytes=0;double seconds=0;double rtt_ms=0;std::uint64_t chunk=0;std::string checksum;}; Result send_file(net::socket_t,const std::filesystem::path&,std::uint64_t,std::uint64_t,const ProgressCallback&,std::uint64_t offset=0); Result receive_file(net::socket_t,const std::filesystem::path&,std::uint64_t,std::uint64_t,const ProgressCallback&,std::uint64_t offset=0,const std::string& expected_hash="");}
