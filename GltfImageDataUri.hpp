#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace kairo::assets::gltf_image_detail
{
    struct DataImage
    {
        std::string_view Mime;
        std::vector<std::byte> Bytes;
    };
    // glTF core images are PNG or JPEG; reject unsupported extension formats
    // instead of accepting a MIME label that the native decoder cannot load.
    inline DataImage DecodeDataImage(std::string_view uri,std::size_t limit=256u*1024u*1024u)
    {
        constexpr std::string_view png="data:image/png;base64,";
        constexpr std::string_view jpeg="data:image/jpeg;base64,";
        DataImage result;
        if(uri.starts_with(png)){result.Mime="image/png";uri.remove_prefix(png.size());}
        else if(uri.starts_with(jpeg)){result.Mime="image/jpeg";uri.remove_prefix(jpeg.size());}
        else throw std::invalid_argument("Unsupported glTF image data URI.");
        if(uri.empty()||uri.size()%4!=0)throw std::invalid_argument("Invalid base64 image length.");
        const std::size_t pad=uri.back()=='='?(uri[uri.size()-2]=='='?2u:1u):0u;
        const auto size=uri.size()/4*3-pad;
        if(!size||size>limit)throw std::length_error("Embedded image exceeds byte budget.");
        const auto digit=[](char c)->unsigned
        {
            if(c>='A'&&c<='Z')return static_cast<unsigned>(c-'A');
            if(c>='a'&&c<='z')return static_cast<unsigned>(c-'a'+26);
            if(c>='0'&&c<='9')return static_cast<unsigned>(c-'0'+52);
            if(c=='+')return 62;
            if(c=='/')return 63;
            throw std::invalid_argument("Invalid base64 image digit.");
        };
        result.Bytes.reserve(size);
        for(std::size_t i=0;i<uri.size();i+=4)
        {
            const bool last=i+4==uri.size();
            const auto a=digit(uri[i]),b=digit(uri[i+1]);
            const auto c=last&&pad==2?0u:digit(uri[i+2]);
            const auto d=last&&pad>0?0u:digit(uri[i+3]);
            if(last&&((pad==2&&(b&15u))||(pad==1&&(c&3u))))
                throw std::invalid_argument("Noncanonical base64 padding bits.");
            const auto bits=(a<<18)|(b<<12)|(c<<6)|d;
            result.Bytes.push_back(static_cast<std::byte>((bits>>16)&255));
            if(!last||pad!=2)result.Bytes.push_back(static_cast<std::byte>((bits>>8)&255));
            if(!last||pad==0)result.Bytes.push_back(static_cast<std::byte>(bits&255));
        }
        return result;
    }
}
