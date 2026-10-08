#pragma once
namespace Dapper
{
    // Saved IDs: zero is off, then portrait, money photo, and Giga Mann.
    inline constexpr const char* Names[]{"Dapper Mann", "Dapper Mann - Money", "Giga Mann"};
    void Load();
    void Unload();
    const char* TextureName(int index);
    void Draw(int selection, float x, float y, float width, float height);
}
