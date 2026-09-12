#include "transfer.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
namespace transfer {
static void put64(std::uint8_t*p,std::uint64_t x){for(int i=7;i>=0;--i)p[7-i]=(x>>(i*8))&255;}
static std::uint64_t get64(const std::uint8_t*p){std::uint64_t x=0;for(int i=0;i<8;i++)x=(x<<8)|p[i];return x;}
static std::uint64_t adapt(std::uint64_t c,double speed,double prev){if(speed<=0)return c;if(prev<=0)return c;if(speed>prev*1.08)return std::min<std::uint64_t>(c*2,4ull*1024*1024);if(speed<prev*0.78)return std::max<std::uint64_t>(c/2,4ull*1024);return c;}
Result send_file(net::socket_t fd,const std::filesystem::path&source,std::uint64_t chunk,std::uint64_t total_hint,const ProgressCallback&cb,std::uint64_t offset){
 Result r;r.chunk=chunk;std::ifstream f(source,std::ios::binary);if(!f)return r;f.seekg(0,std::ios::end);auto total=(std::uint64_t)f.tellg();if(total_hint&&total_hint!=total)return r; if(offset>total)return r;f.seekg((std::streamoff)offset);std::uint64_t done=offset;auto start=std::chrono::steady_clock::now(),window=start;std::uint64_t winbytes=0;double prev=0;std::vector<char>b((size_t)chunk);
 while(done<total){std::uint64_t want=std::min<std::uint64_t>(b.size(),total-done);f.read(b.data(),(std::streamsize)want);auto n=f.gcount();if(n<=0)break;std::uint8_t hdr[8];put64(hdr,(std::uint64_t)n);if(!net::send_all(fd,hdr,8)||!net::send_all(fd,b.data(),(size_t)n))break;done+=(std::uint64_t)n;winbytes+=(std::uint64_t)n;auto now=std::chrono::steady_clock::now();double secs=std::chrono::duration<double>(now-window).count();if(secs>=0.25||done==total){double speed=winbytes/secs/1024.0/1024.0;if(prev>0)r.chunk=adapt(r.chunk,speed,prev);prev=speed;window=now;winbytes=0;std::uint64_t next=r.chunk;if(next!=b.size())b.resize((size_t)next);r.bytes=done;r.seconds=std::chrono::duration<double>(now-start).count();if(cb)cb(done,total,r.chunk,speed,r.rtt_ms);}}
 r.bytes=done;r.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();r.ok=done==total; r.checksum=r.ok?crypto::file_sha256(source.string()):"";return r;
}
Result receive_file(net::socket_t fd,const std::filesystem::path&dest,std::uint64_t total,std::uint64_t chunk,const ProgressCallback&cb,std::uint64_t offset,const std::string&expected_hash){
 Result r;r.chunk=chunk;if(offset>total)return r;auto tmp=std::filesystem::path(dest.string()+".part");std::fstream f(tmp,std::ios::in|std::ios::out|std::ios::binary);if(!f){std::ofstream create(tmp,std::ios::binary);create.close();f.open(tmp,std::ios::in|std::ios::out|std::ios::binary);}if(!f)return r;f.seekp((std::streamoff)offset);std::uint64_t done=offset;auto start=std::chrono::steady_clock::now(),window=start;std::uint64_t winbytes=0;double prev=0;std::vector<char>b;
 while(done<total){std::uint8_t hdr[8];if(!net::recv_all(fd,hdr,8))break;auto n=get64(hdr);if(n==0||n>4ull*1024*1024)break;b.resize((size_t)n);if(!net::recv_all(fd,b.data(),b.size()))break;f.write(b.data(),(std::streamsize)b.size());if(!f)break;done+=n;winbytes+=n;auto now=std::chrono::steady_clock::now();double secs=std::chrono::duration<double>(now-window).count();if(secs>=0.25||done==total){double speed=winbytes/secs/1024.0/1024.0;if(prev>0)r.chunk=adapt(r.chunk,speed,prev);prev=speed;window=now;winbytes=0;r.bytes=done;r.seconds=std::chrono::duration<double>(now-start).count();if(cb)cb(done,total,r.chunk,speed,r.rtt_ms);}}
 f.flush();f.close();r.bytes=done;r.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();if(done!=total)return r;auto hash=crypto::file_sha256(tmp.string());if(!expected_hash.empty()&&hash!=expected_hash)return r;std::error_code ec;std::filesystem::remove(dest,ec);std::filesystem::rename(tmp,dest,ec);if(ec)return r;r.checksum=hash;r.ok=true;return r;
}
}
