#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace SplashWorker
{
    void Start();
    void Stop();
    void Invalidate();

    // One owned snapshot in flight, no unbounded queue. Game-thread operations
    // never wait on the worker; only explicit DLL shutdown joins it.
    template<class Job, class Result> class Mailbox
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::thread thread;
        std::unique_ptr<Job> pending;
        std::unique_ptr<Result> ready;
        std::atomic<bool> stopping{true};
        bool busy = false;
    public:
        bool Start()
        {
            if (thread.joinable()) return true;
            stopping.store(false);
            try
            {
                thread = std::thread([this]
                {
                    for (;;)
                    {
                        std::unique_ptr<Job> job;
                        {
                            std::unique_lock lock(mutex);
                            wake.wait(lock, [&] { return stopping.load() || pending; });
                            if (stopping.load()) return;
                            job = std::move(pending);
                        }
                        std::unique_ptr<Result> result;
                        try { result = std::make_unique<Result>(job->Run(stopping)); }
                        catch (...) { /* Optional acceleration must not terminate the game. */ }
                        {
                            std::lock_guard lock(mutex);
                            if (!stopping.load()) ready = std::move(result);
                            busy = false;
                        }
                    }
                });
                return true;
            }
            catch (...) { stopping.store(true); return false; }
        }
        bool Submit(std::unique_ptr<Job> job)
        {
            std::unique_lock lock(mutex, std::try_to_lock);
            if (!lock || stopping.load() || busy) return false;
            pending = std::move(job); busy = true;
            wake.notify_one();
            return true;
        }
        bool Available()
        {
            std::unique_lock lock(mutex, std::try_to_lock);
            return lock && !stopping.load() && !busy;
        }
        template<class Accept> std::unique_ptr<Result> Take(Accept accept)
        {
            std::unique_lock lock(mutex, std::try_to_lock);
            if (!lock || !ready || !accept(*ready)) return {};
            return std::move(ready);
        }
        void Stop()
        {
            { std::lock_guard lock(mutex); stopping.store(true); }
            wake.notify_one();
            if (thread.joinable()) thread.join();
            std::lock_guard lock(mutex);
            pending.reset(); ready.reset(); busy = false;
        }
        // Stop is explicitly called before FreeLibrary, not from DllMain.
        ~Mailbox() { if (thread.joinable()) std::terminate(); }
    };
}
