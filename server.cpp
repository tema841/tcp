#include "metrics.hpp"
#include "sha256.hpp"
#include "socket.hpp"
#include "transfer.hpp"
#include <algorithm>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
namespace fs=std::filesystem;
static std::atomic_bool stop_flag=false; static monitor::Registry registry; static std::mutex clients_m; static std::vector<net::socket_t> clients;
static void sig(int){stop_flag=true;}
static bool u64(const std::string&s,std::uint64_t&v){try{size_t p=0;auto x=std::stoull(s,&p);if(p!=s.size())return false;v=x;return true;}catch(...){return false;}}
static bool safe(const fs::path&root,const fs::path&p){std::error_code e;auto r=fs::weakly_canonical(root,e);if(e)return false;auto c=fs::weakly_canonical(p.is_absolute()?p:r/p,e);if(e)return false;auto a=r.begin(),b=r.end(),x=c.begin(),y=c.end();for(;a!=b;++a,++x)if(x==y||*a!=*x)return false;return true;}
static void add(net::socket_t s){std::lock_guard l(clients_m);clients.push_back(s);} static void rem(net::socket_t s){std::lock_guard l(clients_m);clients.erase(std::remove(clients.begin(),clients.end(),s),clients.end());}
static void handle_http(net::socket_t fd,const fs::path&web){std::string line;if(!net::recv_line(fd,line,8192)){net::close_socket(fd);return;}std::string path="/";std::istringstream ss(line);std::string method;ss>>method>>path;if(path=="/api/status"){auto body=monitor::status_json(registry);std::string h="HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n";net::send_all(fd,h.data(),h.size());net::send_all(fd,body.data(),body.size());net::close_socket(fd);return;}if(path!="/") {if(path.rfind("/",0)==0)path=path.substr(1);}else path="index.html";auto file=web/path;std::ifstream f(file,std::ios::binary);if(!f){std::string b="Not found";std::string h="HTTP/1.1 404 Not Found\r\nContent-Length: "+std::to_string(b.size())+"\r\n\r\n";net::send_all(fd,h.data(),h.size());net::send_all(fd,b.data(),b.size());net::close_socket(fd);return;}std::ostringstream b;b<<f.rdbuf();auto body=b.str();std::string type="text/plain";if(file.extension()==".html")type="text/html; charset=utf-8";else if(file.extension()==".css")type="text/css";else if(file.extension()==".js")type="application/javascript";std::string h="HTTP/1.1 200 OK\r\nContent-Type: "+type+"\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n";net::send_all(fd,h.data(),h.size());net::send_all(fd,body.data(),body.size());net::close_socket(fd);}
static void handle_transfer(net::socket_t fd,const fs::path&root){add(fd);auto guard=[&]{rem(fd);net::close_socket(fd);};try{std::string req;if(!net::recv_line(fd,req)){guard();return;}std::istringstream in(req);std::string cmd,path,sz,off,chunk,hash;in>>cmd>>path>>sz>>off>>chunk>>hash;std::uint64_t total=0,offset=0,ch=65536;if(!sz.empty())u64(sz,total);if(!off.empty())u64(off,offset);if(!chunk.empty())u64(chunk,ch);if(ch<4096||ch>4ull*1024*1024||path.empty()||!safe(root,root/path)){net::send_all(fd,"ERR invalid request\n",20);guard();return;}fs::path target=root/path;std::string peer=net::peer_address(fd);
 if(cmd=="GET"){if(!fs::is_regular_file(target)){net::send_all(fd,"ERR not found\n",15);guard();return;}auto actual=fs::file_size(target);if(offset>actual)offset=0;auto id=registry.begin("download",path,peer,actual,ch);auto fullhash=crypto::file_sha256(target.string());std::string meta="OK "+std::to_string(actual)+" "+std::to_string(offset)+" "+fullhash+"\n";if(!net::send_all(fd,meta.data(),meta.size())){guard();return;}auto res=transfer::send_file(fd,target,ch,actual,[&](auto d,auto t,auto c,double s,double r){registry.update(id,d,c,s,r,"");},offset);registry.finish(id,res.ok?"completed":"interrupted",res.checksum);guard();return;}
 if(cmd=="PUT"){if(!u64(sz,total)){net::send_all(fd,"ERR size\n",9);guard();return;}auto part=fs::path(target.string()+".part");std::uint64_t existing=0;if(fs::exists(part)){existing=fs::file_size(part);if(existing>total)existing=0;}auto id=registry.begin("upload",path,peer,total,ch);std::string reply="OK "+std::to_string(existing)+"\n";if(!net::send_all(fd,reply.data(),reply.size())){guard();return;}auto res=transfer::receive_file(fd,target,total,ch,[&](auto d,auto t,auto c,double s,double r){registry.update(id,d,c,s,r,"");},existing,hash);registry.finish(id,res.ok?"completed":"interrupted",res.checksum);std::string done=(res.ok?"DONE ":"ERR ")+res.checksum+"\n";net::send_all(fd,done.data(),done.size());guard();return;}
 net::send_all(fd,"ERR command\n",12);guard();}catch(...){guard();}}
static void accept_loop(net::socket_t listen,const fs::path&root,const fs::path&web,bool http){while(!stop_flag){sockaddr_in a{};
#ifdef _WIN32
 int n=sizeof(a);
#else
 socklen_t n=sizeof(a);
#endif
 auto fd=::accept(listen,(sockaddr*)&a,&n);if(fd==net::invalid_socket){if(stop_flag)break;continue;}std::thread(http?handle_http:handle_transfer,fd,http?web:root).detach();}}
int main(int argc,char**argv){if(!net::init()){std::cerr<<"network init failed\n";return 1;}std::uint16_t port=4040,http_port=8080;fs::path root="storage",web="web";for(int i=1;i<argc;i++){std::string a=argv[i];if(a=="-p"&&i+1<argc){std::uint64_t x;if(!u64(argv[++i],x)||x>65535)return 1;port=(std::uint16_t)x;}else if(a=="-root"&&i+1<argc)root=argv[++i];else if(a=="-web-port"&&i+1<argc){std::uint64_t x;if(!u64(argv[++i],x)||x>65535)return 1;http_port=(std::uint16_t)x;}else if(a=="-web"&&i+1<argc)web=argv[++i];else{std::cerr<<"Unknown argument: "<<a<<"\n";return 1;}}
 fs::create_directories(root);if(!fs::exists(web)){std::cerr<<"Web directory not found: "<<web<<"\n";return 1;}std::signal(SIGINT,sig);
 auto make_listener=[&](std::uint16_t p){auto s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==net::invalid_socket)return s;int yes=1;
#ifdef _WIN32
 setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof(yes));
#else
 setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
#endif
 sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(p);a.sin_addr.s_addr=htonl(INADDR_ANY);if(::bind(s,(sockaddr*)&a,sizeof(a))<0||::listen(s,64)<0){net::close_socket(s);return net::invalid_socket;}return s;};
 auto data=make_listener(port), webfd=make_listener(http_port);if(data==net::invalid_socket||webfd==net::invalid_socket){std::cerr<<"bind/listen failed\n";return 1;}std::thread t1(accept_loop,data,root,web,false),t2(accept_loop,webfd,root,web,true);std::cout<<"Adaptive TCP server: TCP "<<port<<", dashboard http://127.0.0.1:"<<http_port<<"\n";while(!stop_flag)std::this_thread::sleep_for(std::chrono::milliseconds(200));net::close_socket(data);net::close_socket(webfd);{std::lock_guard l(clients_m);for(auto s:clients)net::shutdown_socket(s);}t1.join();t2.join();net::cleanup();return 0;}
