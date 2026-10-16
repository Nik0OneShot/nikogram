#pragma once
// Standalone C++20. No engine, platform, UI, or third-party dependencies.
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace movement {
namespace storage { struct Snapshot; }
constexpr int Format=1, Inputs=13, Hidden=24, Outputs=3;
constexpr float MaxHorizon=1.f, ResidualScale=128.f;
constexpr size_t MaxModels=129, QueueLimit=2048, MinTraining=512;
// Dataset size has no configured lifetime cap; parsing and training remain bounded.
using Vec=std::array<float,3>;
using Features=std::array<float,Inputs>;
struct Observation {
    uint64_t player=0, track=0, session=0;
    double time=0;
    Vec position{}, velocity{};
    bool ground=false;
    bool operator==(const Observation&) const = default;
};
struct Anchor { std::array<Observation,3> past{}; bool valid=false; };
bool MakeFeatures(const Anchor&,float horizon,Features&) noexcept;
Vec Linear(const Features&) noexcept;
struct Point { double time=0; Vec position{}; };
// Predictions, never labels. Host must freeze an unmodified baseline path.
struct Baseline { Anchor anchor; std::array<Point,96> points{}; size_t count=0; };
struct Example {
    uint64_t player=0, group=0;
    Features x{}; Vec actual{}, baseline{}; bool paired=false;
};
struct Network {
    std::array<float,Hidden*Inputs> w1{}; std::array<float,Hidden> b1{};
    std::array<float,Outputs*Hidden> w2{}; Vec b2{};
    explicit Network(uint32_t seed=731291);
    Vec Predict(const Features&) const noexcept;
    void Learn(const Example&,float rate) noexcept;
};
struct Model {
    uint64_t player=0, revision=0, rows=0, evaluatedThrough=0;
    Network net;
    Features low{},high{};
    // Four horizon bands, independently evaluated for ground and air.
    std::array<bool,8> supported{};
    double validationRMSE=0,testRMSE=0,baselineRMSE=0;
    bool promoted=false;
    // Runtime provenance; the accepted model format remains unchanged.
    bool trained=false;
    std::array<uint64_t,8> fitCounts{};
};
struct Pool { std::vector<Model> models; std::vector<uint64_t> disabled; };
struct Prediction { Vec displacement{}; bool valid=false,personal=false; const char* reason="no validated model"; uint64_t revision=0; };
Prediction Predict(const Pool&,uint64_t player,const Features&,const Vec& baseline) noexcept;
Prediction PredictExperimental(const Pool& candidates,const Pool& accepted,uint64_t player,const Features&) noexcept;
struct Options { bool collect=false,use=false,all=false; size_t samples=8192;  bool autoTrain=true; bool includeBots=false;};
// Bots (account 0) stay out of global training unless includeBots; no player exceeds playerShare.
struct TrainingPolicy { bool includeBots=false; double playerShare=.15; };
struct Status {
    std::string state="Starting",error,evaluation;
    uint64_t observations=0,examples=0,paired=0,dropped=0,diskBytes=0;
    size_t models=0,queued=0,candidates=0;
    bool training=false,storageFull=false;
};
enum class Action { Train, Cancel, ExportAnonymous, ExportIdentified, ImportAnonymous, ImportIdentified, ResetAll, ResetPlayer, DisablePlayer, EnablePlayer, TrainExperimental };
// IO and training have separate workers. Host callbacks enqueue bounded POD messages.
// Publication is immutable. Stop cancels training, drains accepted observations,
// joins the worker, and must precede unloading the module.
class Service {
    struct Impl; std::unique_ptr<Impl> p;
public:
    explicit Service(const std::filesystem::path& root);
    ~Service();
    Service(const Service&)=delete; Service& operator=(const Service&)=delete;
    void Configure(Options);
    void InvalidateHistory() noexcept;
    bool Observe(const Observation&) noexcept;
    bool RecordBaseline(const Baseline&) noexcept;
    bool Request(Action,uint64_t player=0) noexcept;
    std::shared_ptr<const Pool> Published() const noexcept;
    std::shared_ptr<const Pool> Candidates() const noexcept;
    Status GetStatus() const;
    std::shared_ptr<storage::Snapshot> SnapshotData() const;
    void Stop() noexcept;
};
void ValidateDataset(const std::filesystem::path&,const std::function<bool()>&cancel={});
// Public format and training helpers support independent host harnesses.
std::string Encode(const Example&);
bool Decode(const std::string&,Example&) noexcept;
void SavePool(const std::filesystem::path&,const Pool&);
Pool LoadPool(const std::filesystem::path&);
void SaveCandidates(const std::filesystem::path&,const Pool&);
Pool LoadCandidates(const std::filesystem::path&);
struct TrainingResult { Model model; std::string reason,diagnostic; bool cancelled=false,trained=false; };
TrainingResult Train(const std::filesystem::path& dataset,uint64_t player,size_t selection,
    const std::function<bool()>& cancel,const Model* incumbent=nullptr,const Network* initial=nullptr,uint64_t evaluationAfter=0,const TrainingPolicy& policy={});
TrainingResult TrainExperimental(const std::filesystem::path& dataset,uint64_t player,size_t selection,
    const std::function<bool()>& cancel,const Network* initial=nullptr,const TrainingPolicy& policy={});
}
