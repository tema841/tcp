#include "sha256.hpp"
#include "socket.hpp"
#include "transfer.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
namespace fs=std::filesystem;
static bool u64(const std::string&s,std::uint64_t&v){try{size_t p=0;auto x=std::stoull(s,&p);if(p!=s.size())return false;v=x;return true;}catch(...){return false;}}
struct Remote{std::string host,path;};
static bool remote(const std::string&s,Remote&r){auto p=s.find(':');if(p==std::string::npos||p==0||p+1>=s.size())return false;r.host=s.substr(0,p);r.path=s.substr(p+1);return true;}
static net::socket_t connect_to(const std::string&host,std::uint16_t port){auto s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==net::invalid_socket)return s;sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);if(inet_pton(AF_INET,host.c_str(),&a.sin_addr)<=0){net::close_socket(s);return net::invalid_socket;}if(::connect(s,(sockaddr*)&a,sizeof(a))<0){net::close_socket(s);return net::invalid_socket;}net::set_io_timeout(s,2000);return s;}
static std::uint64_t partial_size(const fs::path&p){std::error_code e;if(fs::exists(p.string()+".part",e))return fs::file_size(p.string()+".part",e);return 0;}
static void usage(){std::cout<<"GET: client -p 4040 -src IP:file -dst local [-chunk 65536]\nPUT: client -p 4040 -src local -dst IP:file [-chunk 65536]\n  add -no-resume to start from zero\n";}
int main(int argc,char**argv){if(!net::init())return 1;std::string src,dst;std::uint16_t port=4040;std::uint64_t chunk=65536;bool resume=true;for(int i=1;i<argc;i++){std::string a=argv[i];if(a=="-src"&&i+1<argc)src=argv[++i];else if(a=="-dst"&&i+1<argc)dst=argv[++i];else if(a=="-p"&&i+1<argc){std::uint64_t x;if(!u64(argv[++i],x)||x<1||x>65535)return 1;port=(std::uint16_t)x;}else if(a=="-chunk"&&i+1<argc){if(!u64(argv[++i],chunk)||chunk<4096||chunk>4ull*1024*1024)return 1;}else if(a=="-no-resume")resume=false;else{usage();return 1;}}if(src.empty()||dst.empty()){usage();return 1;}Remote rs,rd;bool sr=remote(src,rs),dr=remote(dst,rd);if(sr==dr){std::cerr<<"Exactly one of src/dst must be remote\n";return 1;}
 if(sr){auto s=connect_to(rs.host,port);if(s==net::invalid_socket){std::cerr<<"connect failed\n";return 1;}std::uint64_t off=resume?partial_size(dst):0;std::string q="GET "+rs.path+" 0 "+std::to_string(off)+" "+std::to_string(chunk)+"\n";if(!net::send_all(s,q.data(),q.size()))return 1;std::string line;if(!net::recv_line(s,line)){return 1;}std::istringstream in(line);std::string ok,total_s,server_off,hash;in>>ok>>total_s>>server_off>>hash;std::uint64_t total=0,so=0;if(ok!="OK"||!u64(total_s,total)||!u64(server_off,so)){std::cerr<<line<<"\n";return 1;}if(so!=off)off=so;std::cout<<"Downloading "<<rs.path<<" (resume "<<off<<" / "<<total<<")\n";auto res=transfer::receive_file(s,dst,total,chunk,[&](auto d,auto t,auto c,double speed,double){double pct=t?100.0*d/t:100;std::cout<<"\r"<<(int)pct<<"%  "<<d<<"/"<<t<<" bytes  "<<speed<<" MB/s  chunk "<<c/1024<<" KiB"<<std::flush;},off,hash);std::cout<<"\n"<<(res.ok?"Completed":"Failed")<<"  SHA-256: "<<res.checksum<<"\n";net::close_socket(s);net::cleanup();return res.ok?0:1;}
 if(!fs::is_regular_file(src)){std::cerr<<"source not found\n";return 1;}auto total=fs::file_size(src);auto s=connect_to(rd.host,port);if(s==net::invalid_socket){std::cerr<<"connect failed\n";return 1;}std::uint64_t off=0;if(resume&&fs::exists(dst))off=fs::file_size(dst);if(off>total)off=0;auto hash=crypto::file_sha256(src);std::string q="PUT "+rd.path+" "+std::to_string(total)+" "+std::to_string(off)+" "+std::to_string(chunk)+" "+hash+"\n";if(!net::send_all(s,q.data(),q.size()))return 1;std::string line;if(!net::recv_line(s,line)){return 1;}std::istringstream in(line);std::string ok,off_s;in>>ok>>off_s;std::uint64_t server_off=0;if(ok!="OK"||!u64(off_s,server_off)){std::cerr<<line<<"\n";return 1;}if(server_off!=off){off=server_off;}std::cout<<"Uploading "<<src<<" (resume "<<off<<" / "<<total<<")\n";auto res=transfer::send_file(s,src,chunk,total,[&](auto d,auto t,auto c,double speed,double){double pct=t?100.0*d/t:100;std::cout<<"\r"<<(int)pct<<"%  "<<d<<"/"<<t<<" bytes  "<<speed<<" MB/s  chunk "<<c/1024<<" KiB"<<std::flush;},off);std::cout<<"\n";std::string done;if(net::recv_line(s,done))std::cout<<done;std::cout<<"SHA-256: "<<res.checksum<<"\n";net::close_socket(s);net::cleanup();return res.ok?0:1;}
