#pragma once

namespace AimPreview
{
	inline bool Fresh(int recordedTick, int currentTick)
	{
		return recordedTick > 0 && currentTick >= recordedTick && currentTick - recordedTick <= 2;
	}
	inline bool UseCooldown(bool enabled, bool aimEnabled, int recordedTick, int currentTick)
	{
		return enabled && aimEnabled && Fresh(recordedTick, currentTick);
	}
}
