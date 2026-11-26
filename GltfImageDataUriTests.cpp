#include "GltfImageDataUri.hpp"
#include <iostream>
#include <random>
#include <string>
using namespace kairo::assets::gltf_image_detail;
void Check(bool value){if(!value)throw std::runtime_error("image data URI regression");}
template<class F>void Reject(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}Check(bad);}
std::string Encode(const std::vector<std::byte>& bytes)
{
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result="data:image/png;base64,";
    for(std::size_t i=0;i<bytes.size();i+=3)
    {
        const auto n=bytes.size()-i;
        const auto a=std::to_integer<unsigned>(bytes[i]);
        const auto b=n>1?std::to_integer<unsigned>(bytes[i+1]):0u;
        const auto c=n>2?std::to_integer<unsigned>(bytes[i+2]):0u;
        const auto bits=(a<<16)|(b<<8)|c;
        result+=alphabet[(bits>>18)&63];result+=alphabet[(bits>>12)&63];
        result+=n>1?alphabet[(bits>>6)&63]:'=';result+=n>2?alphabet[bits&63]:'=';
    }
    return result;
}
int main()
{
    const auto one=DecodeDataImage("data:image/jpeg;base64,QQ==");
    Check(one.Mime=="image/jpeg"&&one.Bytes==std::vector<std::byte>{std::byte{65}});
    Reject([]{(void)DecodeDataImage("data:image/png;base64,QR==");});
    Reject([]{(void)DecodeDataImage("data:image/png;base64,QUJ=");});
    Reject([]{(void)DecodeDataImage("data:image/png;base64,Q===");});
    Reject([]{(void)DecodeDataImage("data:image/png;base64,QQ==AAAA");});
    Reject([]{(void)DecodeDataImage("data:image/png;base64,QQ==",0);});
    Reject([]{(void)DecodeDataImage("data:image/webp;base64,QQ==");});
    std::mt19937 rng(2026);
    for(unsigned i=0;i<10000;++i)
    {
        std::vector<std::byte> bytes(1+rng()%512);
        for(auto& b:bytes)b=static_cast<std::byte>(rng()&255u);
        const auto decoded=DecodeDataImage(Encode(bytes));
        Check(decoded.Mime=="image/png"&&decoded.Bytes==bytes);
    }
    std::cout<<"10000 randomized embedded image base64 round trips and rejection fixtures passed\n";
}
