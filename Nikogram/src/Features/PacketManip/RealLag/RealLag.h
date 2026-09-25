#pragma once
#include <string>
namespace RealLag
{
    void Update();
    void Shutdown();
    bool Controlling();
    std::string Status();
    std::string DiagnosticPath();
}
