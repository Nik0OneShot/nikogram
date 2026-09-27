#pragma once
#include "../../SDK/SDK.h"
#include "Model.h"
#include <mutex>

class CBlockbot
{
    mutable std::mutex m_mutex;
    BlockbotModel::Override m_override;
    bool m_controlling = false;
    Vec3 m_velocity = {};
    Vec3 m_waypoint = {}, m_plannedGoal = {};
    Vec3 m_navWaypoint = {}, m_navGoal = {};
    double m_navUntil = 0;
    int m_navUser = 0;
    bool m_navValid = false;
    double m_nextPlan = 0;
    int m_plannedUser = 0;
    bool m_routeValid = false;
    int m_manualUser = 0;
    int m_followUser = 0, m_followMode = -1;
    uint32_t m_followAccount = 0;
    uint32_t m_manualAccount = 0;
    std::string m_session, m_manualSession, m_manualName;
    std::string m_status = "Off";
public:
    bool Run(CTFPlayer* local, CUserCmd* cmd);
    void Apply(CUserCmd* cmd);
    bool Controlling() const { return m_controlling; }
    void Reset();
    void SetManual(int user, uint32_t account, const std::string& name);
    std::string Status() const;
    std::string ManualName() const;
};
ADD_FEATURE(CBlockbot, Blockbot);
