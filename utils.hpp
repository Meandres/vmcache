#ifndef UTILS_HPP
#define UTILS_HPP

#include <atomic>
#include <algorithm>
#include <cassert>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>
#include <vector>
#include <span>

#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
//#include <immintrin.h>
#include <arm_acle.h>
#include <pthread_np.h>

namespace std {
    typedef uint8_t u8;
    typedef uint16_t u16;
    typedef uint32_t u32;
    typedef uint64_t u64;
    typedef u64 PID; // page id type

    static const u64 pageSize = 4096;

    struct alignas(4096) Page {
        bool dirty;
    };

    typedef struct parameters {
        u64 virtSize;
        u64 physSize;
    } params_t;

    static const int16_t maxWorkerThreads = 128;
    
    struct OLCRestartException {};
    
    typedef u64 KeyType;
 
#define die(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)

    typedef chrono::time_point<std::chrono::high_resolution_clock> timepoint;

    timepoint now(){
	    return chrono::high_resolution_clock::now();
    }
    
    uint64_t diff_ns(timepoint s, timepoint e){
	    return chrono::nanoseconds(e - s).count();
    }

    uint64_t rdtsc() {
        int64_t tsc;
	asm volatile("mrs %0, cntvct_el0" : "=r"(tsc));
	//asm volatile("mrs %0, pmccntr_el0" : "=r" (tsc));
        return tsc;
    }

    // allocate memory using huge pages
    void* allocHuge(size_t size) {
        void* p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        //madvise(p, size, MADV_HUGEPAGE);
        return p;
    }

    // use when lock is not free
    void yield(u64 counter) {
	__yield();
    }

    u64 envOr(const char* env, u64 value) {
        if (getenv(env))
            return atof(getenv(env));
        return value;
    }

    int pin_thread_to_core(int core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);

        pthread_t current_thread = pthread_self();
        return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    }

    template<class Fn>
        void parallel_for(uint64_t begin, uint64_t end, uint64_t nthreads, Fn fn) {
            std::vector<std::thread> threads;
            uint64_t n = end-begin;
            if (n<nthreads)
                nthreads = n;
            uint64_t perThread = n/nthreads;
            for (unsigned i=0; i<nthreads; i++) {
                threads.emplace_back([&,i]() {
                        pin_thread_to_core(i);
                        uint64_t b = (perThread*i) + begin;
                        uint64_t e = (i==(nthreads-1)) ? end : ((b+perThread) + begin);
                        fn(i, b, e);
                        });
            }
            for (auto& t : threads)
                t.join();
        }
}
#endif 
