#include "pebs_sampler.h"

#include <cstring>
#include <iostream>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>

namespace membrain {

#pragma pack(push, 1)
struct SampleRecord {
    uint64_t ip;
    uint32_t pid;
    uint32_t tid;
    uint64_t addr;
};
#pragma pack(pop)

PebsSampler::PebsSampler() {
    m_pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

PebsSampler::~PebsSampler() {
    cleanup();
}

bool PebsSampler::init(uint64_t samplePeriod, size_t numDataPages) {
    cleanup();

    m_samplePeriod = samplePeriod;
    m_numDataPages = numDataPages;
    m_totalSamples = 0;

    int numCpus = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    m_targetPid = static_cast<uint32_t>(getpid());

    struct perf_event_attr pe;
    std::memset(&pe, 0, sizeof(pe));
    pe.type = 4; // CPU raw PMU
    pe.size = sizeof(pe);
    pe.config = 0x20d1; // mem_load_retired.l3_miss on Sapphire Rapids
    pe.sample_period = m_samplePeriod;
    pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.precise_ip = 2; // PEBS precise sampling

    size_t mmapLen = (1 + m_numDataPages) * m_pageSize;

#ifndef PERF_FLAG_FD_CLOEXEC
#define PERF_FLAG_FD_CLOEXEC 8
#endif

    for (int cpu = 0; cpu < numCpus; ++cpu) {
        int fd = static_cast<int>(syscall(__NR_perf_event_open, &pe, -1, cpu, -1, PERF_FLAG_FD_CLOEXEC));
        if (fd < 0) {
            continue;
        }

        void *base = mmap(nullptr, mmapLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base == MAP_FAILED) {
            close(fd);
            continue;
        }

        ioctl(fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
        m_buffers.push_back({fd, base, mmapLen, cpu});
    }

    m_enabled = !m_buffers.empty();
    m_running = m_enabled;
    if (m_enabled) {
        std::cout << "[MemBrainRT] Initialized in-process PEBS buffers across " 
                  << m_buffers.size() << " CPU cores (sample period: " << m_samplePeriod << ").\n";
    } else {
        std::cerr << "[MemBrainRT] Warning: Failed to open in-process PEBS buffers.\n";
    }

    return m_enabled;
}

void PebsSampler::start() {
    if (!m_enabled || m_running) return;

    for (auto &b : m_buffers) {
        ioctl(b.fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(b.fd, PERF_EVENT_IOC_ENABLE, 0);
    }
    m_running = true;
}

void PebsSampler::stop() {
    if (!m_enabled || !m_running) return;

    for (auto &b : m_buffers) {
        ioctl(b.fd, PERF_EVENT_IOC_DISABLE, 0);
    }
    m_running = false;
}

void PebsSampler::drainSamples(const std::function<void(uint64_t addr)>& onSample) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_drainMutex);
    size_t dataSize = m_numDataPages * m_pageSize;

    for (auto &b : m_buffers) {
        auto *header = static_cast<struct perf_event_mmap_page *>(b.base);
        if (!header) continue;

        uint64_t head = header->data_head;
        std::atomic_thread_fence(std::memory_order_acquire);
        uint64_t tail = header->data_tail;

        auto *dataBase = reinterpret_cast<char *>(b.base) + m_pageSize;

        while (tail < head) {
            uint64_t offset = tail % dataSize;
            auto *ev = reinterpret_cast<struct perf_event_header *>(dataBase + offset);

            if (ev->size == 0) break; // Avoid infinite loop on malformed record

            if (ev->type == PERF_RECORD_SAMPLE) {
                const auto *sample = reinterpret_cast<const SampleRecord *>(
                    reinterpret_cast<const char *>(ev) + sizeof(struct perf_event_header)
                );
                if (sample->pid == m_targetPid && sample->addr != 0) {
                    onSample(sample->addr);
                    m_totalSamples++;
                }
            }

            tail += ev->size;
        }

        header->data_tail = tail;
        std::atomic_thread_fence(std::memory_order_release);
    }
}

void PebsSampler::cleanup() {
    stop();
    for (auto &b : m_buffers) {
        if (b.base && b.base != MAP_FAILED) {
            munmap(b.base, b.mmapLen);
        }
        if (b.fd >= 0) {
            close(b.fd);
        }
    }
    m_buffers.clear();
    m_enabled = false;
    m_running = false;
}

} // namespace membrain
