#pragma once
#include <algorithm>
#include <cmath>

namespace ServerEstimate
{
	// Linear estimate from two network samples up to the synchronized current tick.
	// This is not the future arrival time of a shot and is not a lag-comp record.
	inline float SampleScale(float now, float latest, float previous, int maxMilliseconds)
	{
		if (!std::isfinite(now) || !std::isfinite(latest) || !std::isfinite(previous))
			return 0.f;
		const float sampleTime = latest - previous;
		const float age = now - latest;
		if (sampleTime < 0.001f || sampleTime > 0.1f || age <= 0.f || age > 0.2f)
			return 0.f;
		// The slider ends at 100 ms, but manually entered limits may be higher.
		// Never project beyond sample age; stale samples are still rejected above.
		return std::min(age, std::max(maxMilliseconds, 0) / 1000.f) / sampleTime;
	}
}
