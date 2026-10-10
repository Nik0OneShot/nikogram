#pragma once
#include <array>
#include <cstdint>
#include <format>
#include <string>

namespace PhotoAssetPolicy
{
    struct Asset { const char* key; int resource; };
    inline constexpr std::array<Asset,3> assets{{{"portrait",301},{"money",302},{"giga",303}}};
    constexpr int Index(int selection){return selection>=1&&selection<=3?selection-1:-1;}
    inline std::string TextureName(int index,std::uint64_t generation)
    {
        if(index<0||index>=int(assets.size()))return {};
        return std::format("nikogram/photo065_{:016x}_{}",generation,assets[index].key);
    }
    inline std::string MaterialName(const char* texture,bool model,unsigned serial)
    {return std::format("{}_world_{}_{}",texture,model?"prop":"brush",serial);}
}
