#pragma once
class IGameEvent;
namespace Statistics
{
    void Tick();
    void Event(IGameEvent* event);
    void Draw();
    void Shutdown();
    void DeathEventsAvailable(bool available);
}
