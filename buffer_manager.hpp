#ifndef BUFFER_MANAGER_HPP
#define BUFFER_MANAGER_HPP

#include "utils.hpp"
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

namespace std {

    struct PageState {
        atomic<u64> stateAndVersion;

        static const u64 Unlocked = 0;
        static const u64 MaxShared = 252;
        static const u64 Locked = 253;
        static const u64 Marked = 254;
        static const u64 Evicted = 255;

        PageState() {}

        void init() { stateAndVersion.store(sameVersion(0, Evicted), std::memory_order_release); }

        static inline u64 sameVersion(u64 oldStateAndVersion, u64 newState) { return ((oldStateAndVersion<<8)>>8) | newState<<56; }
        static inline u64 nextVersion(u64 oldStateAndVersion, u64 newState) { return (((oldStateAndVersion<<8)>>8)+1) | newState<<56; }

        bool tryLockX(u64 oldStateAndVersion) {
            return stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, Locked));
        }

        void unlockX() {
            assert(getState() == Locked);
            stateAndVersion.store(nextVersion(stateAndVersion.load(), Unlocked), std::memory_order_release);
        }

        void unlockXEvicted() {
            assert(getState() == Locked);
            stateAndVersion.store(nextVersion(stateAndVersion.load(), Evicted), std::memory_order_release);
        }

        void downgradeLock() {
            assert(getState() == Locked);
            stateAndVersion.store(nextVersion(stateAndVersion.load(), 1), std::memory_order_release);
        }

        bool tryLockS(u64 oldStateAndVersion) {
            u64 s = getState(oldStateAndVersion);
            if (s<MaxShared)
                return stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, s+1));
            if (s==Marked)
                return stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, 1));
            return false;
        }

        void unlockS() {
            while (true) {
                u64 oldStateAndVersion = stateAndVersion.load();
                u64 state = getState(oldStateAndVersion);
                assert(state>0 && state<=MaxShared);
                if (stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, state-1)))
                    return;
            }
        }

        bool tryMark(u64 oldStateAndVersion) {
            assert(getState(oldStateAndVersion)==Unlocked);
            return stateAndVersion.compare_exchange_strong(oldStateAndVersion, sameVersion(oldStateAndVersion, Marked));
        }

        static u64 getState(u64 v) { return v >> 56; };
        u64 getState() { return getState(stateAndVersion.load()); }

        void operator=(PageState&) = delete;
    };

    // open addressing hash table used for second chance replacement to keep track of currently-cached pages
    struct ResidentPageSet {
        static const u64 empty = ~0ull;
        static const u64 tombstone = (~0ull)-1;

        struct Entry {
            atomic<u64> pid;
        };

        Entry* ht;
        u64 count;
        u64 mask;
        atomic<u64> clockPos;

        ResidentPageSet(u64 maxCount) : count(next_pow2(maxCount * 1.5)), mask(count - 1), clockPos(0) {
            ht = (Entry*)allocHuge(count * sizeof(Entry));
            memset((void*)ht, 0xFF, count * sizeof(Entry));
        }

        ~ResidentPageSet() {
            munmap(ht, count * sizeof(u64));
        }

        u64 next_pow2(u64 x) {
            return 1<<(64-__builtin_clzl(x-1));
        }

        u64 hash(u64 k) {
            const u64 m = 0xc6a4a7935bd1e995;
            const int r = 47;
            u64 h = 0x8445d61a4e774912 ^ (8*m);
            k *= m;
            k ^= k >> r;
            k *= m;
            h ^= k;
            h *= m;
            h ^= h >> r;
            h *= m;
            h ^= h >> r;
            return h;
        }

        void insert(u64 pid) {
            u64 pos = hash(pid) & mask;
            while (true) {
                u64 curr = ht[pos].pid.load();
                assert(curr != pid);
                if ((curr == empty) || (curr == tombstone))
                    if (ht[pos].pid.compare_exchange_strong(curr, pid))
                        return;

                pos = (pos + 1) & mask;
            }
        }

        bool remove(u64 pid) {
            u64 pos = hash(pid) & mask;
            while (true) {
                u64 curr = ht[pos].pid.load();
                if (curr == empty)
                    return false;

                if (curr == pid)
                    if (ht[pos].pid.compare_exchange_strong(curr, tombstone))
                        return true;

                pos = (pos + 1) & mask;
            }
        }

        template<class Fn>
            void iterateClockBatch(u64 batch, Fn fn) {
                u64 pos, newPos;
                do {
                    pos = clockPos.load();
                    newPos = (pos+batch) % count;
                } while (!clockPos.compare_exchange_strong(pos, newPos));

                for (u64 i=0; i<batch; i++) {
                    u64 curr = ht[pos].pid.load();
                    if ((curr != tombstone) && (curr != empty))
                        fn(curr);
                    pos = (pos + 1) & mask;
                }
            }
    };

    struct BufferManager {
        static const u64 mb = 1024ull * 1024;
        static const u64 gb = 1024ull * 1024 * 1024;
        u64 virtSize;
        u64 physSize;
        u64 virtCount;
        u64 physCount;

        atomic<u64> physUsedCount;
        ResidentPageSet residentSet;
        atomic<u64> allocCount;

        Page* virtMem;
        PageState* pageState;

        PageState& getPageState(PID pid) {
            return pageState[pid];
        }

        BufferManager(params_t* params) : virtSize(params->virtSize*gb), physSize(params->physSize*gb), virtCount(virtSize / pageSize), physCount(physSize / pageSize), residentSet(physCount) {
            assert(virtSize>=physSize);
            u64 virtAllocSize = virtSize + (1<<16); // we allocate 64KB extra to prevent segfaults during optimistic reads

            virtMem = (Page*)mmap(NULL, virtAllocSize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
            //madvise(virtMem, virtAllocSize, MADV_NOHUGEPAGE);

            pageState = (PageState*)allocHuge(virtCount * sizeof(PageState));
            for (u64 i=0; i<virtCount; i++)
                pageState[i].init();
            if (virtMem == MAP_FAILED)
                die("mmap failed");

            physUsedCount = 0;
            allocCount = 1; // pid 0 reserved for meta data

        }
        ~BufferManager() {}

        Page* fixX(PID pid) {
            PageState& ps = getPageState(pid);
            for (u64 repeatCounter=0; ; repeatCounter++) {
                u64 stateAndVersion = ps.stateAndVersion.load();
                switch (PageState::getState(stateAndVersion)) {
                    case PageState::Evicted: {
                                                 if (ps.tryLockX(stateAndVersion)) {
                                                     handleFault(pid);
                                                     return virtMem + pid;
                                                 }
                                                 break;
                                             }
                    case PageState::Marked: case PageState::Unlocked: {
                                                                          if (ps.tryLockX(stateAndVersion))
                                                                              return virtMem + pid;
                                                                          break;
                                                                      }
                }
                yield(repeatCounter);
            }
        }

        Page* fixS(PID pid) {
            PageState& ps = getPageState(pid);
            for (u64 repeatCounter=0; ; repeatCounter++) {
                u64 stateAndVersion = ps.stateAndVersion;
                switch (PageState::getState(stateAndVersion)) {
                    case PageState::Locked: {
                                                break;
                                            } case PageState::Evicted: {
                                                if (ps.tryLockX(stateAndVersion)) {
                                                    handleFault(pid);
                                                    ps.unlockX();
                                                }
                                                break;
                                            }
                    default: {
                                 if (ps.tryLockS(stateAndVersion))
                                     return virtMem + pid;
                             }
                }
                yield(repeatCounter);
            }
        }

        void unfixS(PID pid) {
            getPageState(pid).unlockS();
        }

        void unfixX(PID pid) {
            getPageState(pid).unlockX();
        }

        bool isValidPtr(void* page) { return (page >= virtMem) && (page < (virtMem + virtSize + 16)); }
        PID toPID(void* page) { return reinterpret_cast<Page*>(page) - virtMem; }
        Page* toPtr(PID pid) { return virtMem + pid; }

        void ensureFreePages() {
            if (physUsedCount >= physCount){
		cerr << "No more available pages. Failing." << endl;
                assert(false);
	    }
        }

        // allocated new page and fix it
        Page* allocPage() {
            physUsedCount++;
            ensureFreePages();
            u64 pid = allocCount++;
            if (pid >= virtCount) {
                cerr << "VIRTGB is too low" << endl;
                exit(EXIT_FAILURE);
            }
            u64 stateAndVersion = getPageState(pid).stateAndVersion;
            bool succ = getPageState(pid).tryLockX(stateAndVersion);
            assert(succ);
            residentSet.insert(pid);
            virtMem[pid].dirty = true;

            return virtMem + pid;
        }

        void handleFault(PID pid){
		PageState& ps = getPageState(pid);
		u64 stateAndVersion = ps.stateAndVersion.load();
		if(PageState::getState(stateAndVersion == PageState::Evicted)){
		    ps.stateAndVersion.compare_exchange_strong(stateAndVersion, PageState::sameVersion(stateAndVersion, PageState::Unlocked));
		}
		residentSet.insert(pid);
		virtMem[pid].dirty = true;
        }

    };
}

#endif

