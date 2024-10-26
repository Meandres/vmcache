#ifndef MMAP_ADAPTER_HPP
#define MMAP_ADAPTER_HPP
#include "utils.hpp"

namespace std {
    struct MmapRegion {
        static const u64 mb = 1024ull * 1024;
        static const u64 gb = 1024ull * 1024 * 1024;
        Page* virtMem;
        u64 allocCount;

        MmapRegion(params_t* params){
            int fd = open(params->path, O_RDWR);
            virtMem = (Page*)mmap(NULL, params->virtSize*gb, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
            allocCount = 1;
        }

        u64 getNextPid(){ return allocCount++; }

        u64 toPID(void* page) { return reinterpret_cast<Page*>(page)-virtMem; }

        void* toPtr(u64 pid) { return virtMem + pid; }
    };
}
#endif
