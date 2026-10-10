#pragma once
#include <optional>
namespace CapturePolicy
{
    inline bool NativePostProcessing(bool cleanEnabled,bool remove,bool unload)
    {return cleanEnabled||!remove||unload;}
    struct Latch
    {
        int last = -1, through = -1;
        bool Update(bool enabled, bool capturing, int frame)
        {
            if (!enabled || frame < last) through = -1;
            last = frame;
            if (!enabled) return false;
            // A late Steam capture also suppresses the next complete scene.
            if (capturing) through = frame + 1;
            return frame <= through;
        }
    };
    inline thread_local std::optional<bool> scene;
    struct SceneScope
    {
        std::optional<bool> saved = scene;
        explicit SceneScope(bool capture) { if (!scene) scene = capture; }
        ~SceneScope() { scene = saved; }
    };
}
