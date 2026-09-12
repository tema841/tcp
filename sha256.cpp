#include "sha256.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>
namespace crypto {
static constexpr std::uint32_t K[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static inline std::uint32_t rotr(std::uint32_t x,int n){return (x>>n)|(x<<(32-n));}
SHA256::SHA256(){state_[0]=0x6a09e667;state_[1]=0xbb67ae85;state_[2]=0x3c6ef372;state_[3]=0xa54ff53a;state_[4]=0x510e527f;state_[5]=0x9b05688c;state_[6]=0x1f83d9ab;state_[7]=0x5be0cd19;}
void SHA256::transform(const std::uint8_t* d){std::uint32_t w[64]{}; for(int i=0;i<16;i++) w[i]=(std::uint32_t(d[i*4])<<24)|(std::uint32_t(d[i*4+1])<<16)|(std::uint32_t(d[i*4+2])<<8)|d[i*4+3]; for(int i=16;i<64;i++){auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;} std::uint32_t a=state_[0],b=state_[1],c=state_[2],d0=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7]; for(int i=0;i<64;i++){auto S1=rotr(e,6)^rotr(e,11)^rotr(e,25);auto ch=(e&f)^((~e)&g);auto t1=h+S1+ch+K[i]+w[i];auto S0=rotr(a,2)^rotr(a,13)^rotr(a,22);auto maj=(a&b)^(a&c)^(b&c);auto t2=S0+maj;h=g;g=f;f=e;e=d0+t1;d0=c;c=b;b=a;a=t1+t2;} state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d0;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;}
void SHA256::update(const void* data,std::size_t len){if(finalized_)return;const auto* p=(const std::uint8_t*)data;while(len--){data_[datalen_++]=*p++;if(datalen_==64){transform(data_);bitlen_+=512;datalen_=0;}}}
void SHA256::update(const std::vector<char>& v){update(v.data(),v.size());}
std::string SHA256::final_hex(){if(!finalized_){bitlen_+=datalen_*8;data_[datalen_++]=0x80;if(datalen_>56){while(datalen_<64)data_[datalen_++]=0;transform(data_);datalen_=0;}while(datalen_<56)data_[datalen_++]=0;for(int i=7;i>=0;i--)data_[datalen_++]=(bitlen_>>(i*8))&0xff;transform(data_);finalized_=true;}std::ostringstream o;for(auto x:state_)o<<std::hex<<std::setw(8)<<std::setfill('0')<<x;return o.str();}
std::string file_sha256(const std::string& path){std::ifstream f(path,std::ios::binary);if(!f)return{};SHA256 h;std::array<char,1<<16>b{};while(f){f.read(b.data(),b.size());auto n=f.gcount();if(n>0)h.update(b.data(),(std::size_t)n);}return h.final_hex();}
}
