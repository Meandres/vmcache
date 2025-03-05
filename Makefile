src= ../..
CXXFLAGS = -g -DNDEBUG -DCONF_debug_memory=0 -std=c++20 -mavx -mavx2 -O3
CXXFLAGS += -fnon-call-exceptions -fasynchronous-unwind-tables -shared -fPIC -I.
INCLUDES = -I$(src)/include -I$(src) -I$(src)/arch/common -I$(src)/arch/x64 -I$(src)/build/release/gen/include

linux-vmcache: vmcache.cpp tpcc/*pp rte_string.hh
	g++ $(CXXFLAGS) -DLINUX vmcache.cpp -o vmcache -laio

osv-vmcache: vmcache.cpp tpcc/* rte_string.hh
	g++ $(CXXFLAGS) -DOSV $(INCLUDES) vmcache.cpp -o vmcache_eval

all: linux-vmcache

module: osv-vmcache

preprocessed:
	cpp $(CXXFLAGS) -DLINUX vmcache.cpp > clean_code_linux.cpp
	cpp $(CXXFLAGS) -DOSV $(INCLUDES) vmcache.cpp > clean_code_osv.cpp

clean:
	rm vmcache ucache_eval
