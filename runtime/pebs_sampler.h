#ifndef MEMBRAIN_PEBS_SAMPLER_H
#define MEMBRAIN_PEBS_SAMPLER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>
#include <mutex>

namespace membrain {

struct PerfRingBuffer {
    int fd{-1};
    void *base{nullptr};
    size_t mmapLen{0};
    int cpu{-1};
};

class PebsSampler {
public:
    PebsSampler();
    ~PebsSampler();

    bool init(uint64_t samplePeriod = 512, size_t numDataPages = 1);
    void start();
    void stop();
    void drainSamples(const std::function<void(uint64_t addr)>& onSample);
    void cleanup();

    bool isEnabled() const { return m_enabled; }
    size_t getTotalSamples() const { return m_totalSamples.load(); }

private:
    bool m_enabled{false};
    bool m_running{false};
    uint64_t m_samplePeriod{512};
    size_t m_numDataPages{1};
    size_t m_pageSize{4096};
    uint32_t m_targetPid{0};
    std::vector<PerfRingBuffer> m_buffers;
    std::mutex m_drainMutex;
    std::atomic<size_t> m_totalSamples{0};
};

} // namespace membrain

#endif // MEMBRAIN_PEBS_SAMPLER_H
