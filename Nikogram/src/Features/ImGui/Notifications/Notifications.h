#pragma once
#include "../../../SDK/SDK.h"
#include <mutex>

struct Notification_t
{
	std::string m_sText = "";
	const char* m_sIcon = nullptr;
	float m_flCreateTime = 0.f;
	float m_flLifeTime = 0.f;
	float m_flPanTime = 0.f;

	Color_t m_tColor = {};
	bool m_bWorkspaceAccent = false;
};

class CNotifications
{
private:
	std::deque<Notification_t> m_vNotifications;
	std::recursive_mutex m_tMutex; // Add can be called from the loader thread and the game thread

public:
	void Add(const std::string& sText, const char* sIcon, Color_t tColor = { 0, 0, 0, 0 }, float flLifeTime = Vars::Logging::NotificationTime.Value, float flPanTime = 0.2f);
	void Add(const std::string& sText, Color_t tColor = { 0, 0, 0, 0 }, float flLifeTime = Vars::Logging::NotificationTime.Value, float flPanTime = 0.2f);
	void Draw();
};

ADD_FEATURE(CNotifications, Notifications);
