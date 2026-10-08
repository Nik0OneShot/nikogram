#pragma once
#include "Binds.h"
#include <vector>

// One snapshot per bind evaluation. Matching conditions share a result, while
// different class/range/tolerance settings retain their own result.
class CConditionSensor
{
    CTFPlayer* local;
    struct Result{int type;ConditionPolicy::Options options;bool value;};
    std::vector<Result> results;
    bool Behind(const ConditionPolicy::Options& options);
    bool Threat(const ConditionPolicy::Options& options);
public:
    explicit CConditionSensor(CTFPlayer* player):local(player){}
    bool Evaluate(int type,const ConditionPolicy::Options& options);
};
