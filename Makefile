src= ../..
CXXFLAGS = -g -DNDEBUG -DCONF_debug_memory=0 -std=c++20 -mavx -mavx2 -O3
CXXFLAGS += -fnon-call-exceptions -fasynchronous-unwind-tables -shared -fPIC
INCLUDES = -I$(src)/include -I$(src) -I$(src)/arch/common -I$(src)/arch/x64 -I$(src)/build/release/gen/include

vmcache: vmcache.cpp tpcc/* rte_string.hh
	g++ $(CXXFLAGS) -DOSV $(INCLUDES) vmcache.cpp -o vmcache_eval

module: vmcache

preprocessed:
	cpp $(CXXFLAGS) -DLINUX vmcache.cpp > clean_code_linux.cpp
	cpp $(CXXFLAGS) -DOSV $(INCLUDES) vmcache.cpp > clean_code_osv.cpp

clean:
	rm -f vmcache_eval
