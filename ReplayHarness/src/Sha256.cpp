#include <SekiroVisionAI/Replay.h>
#include <array>
#include <bit>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace sekiro::replay {
namespace {
class Sha256 {
    std::array<std::uint32_t,8> state_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<unsigned char,64> block_{};
    std::size_t used_{};std::uint64_t bytes_{};
    void compress() {
        constexpr std::uint32_t constants[64]{
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::array<std::uint32_t,64> words{};
        for(std::size_t i=0;i<16;++i)words[i]=(std::uint32_t(block_[i*4])<<24)|
            (std::uint32_t(block_[i*4+1])<<16)|(std::uint32_t(block_[i*4+2])<<8)|block_[i*4+3];
        for(std::size_t i=16;i<64;++i) {
            const auto x=words[i-15],y=words[i-2];
            words[i]=words[i-16]+(std::rotr(x,7)^std::rotr(x,18)^(x>>3))+words[i-7]+(std::rotr(y,17)^std::rotr(y,19)^(y>>10));
        }
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for(std::size_t i=0;i<64;++i) {
            const auto t1=h+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+constants[i]+words[i];
            const auto t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
public:
    void update(const unsigned char* data,std::size_t size) {
        if(size>std::numeric_limits<std::uint64_t>::max()/8-bytes_)throw std::runtime_error("SHA-256 input exceeds bit length contract");
        bytes_+=size;
        for(std::size_t i=0;i<size;++i){block_[used_++]=data[i];if(used_==64){compress();used_=0;}}
    }
    std::string finish() {
        const auto bits=bytes_*8;block_[used_++]=0x80;
        if(used_>56){while(used_<64)block_[used_++]=0;compress();used_=0;}
        while(used_<56)block_[used_++]=0;
        for(int i=7;i>=0;--i)block_[used_++]=static_cast<unsigned char>(bits>>(i*8));
        compress();std::ostringstream out;out<<std::hex<<std::setfill('0');
        for(auto value:state_)out<<std::setw(8)<<value;
        return out.str();
    }
};
}
std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot hash replay/model file");
    Sha256 hash;std::array<unsigned char,65536> buffer{};
    while(stream.read(reinterpret_cast<char*>(buffer.data()),buffer.size())||stream.gcount()>0)
        hash.update(buffer.data(),static_cast<std::size_t>(stream.gcount()));
    if(!stream.eof())throw std::runtime_error("Reading file for SHA-256 failed");
    return hash.finish();
}
}
