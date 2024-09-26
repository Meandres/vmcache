CXX = clang++
CXX_CAP = clang++-morello
CXXFLAGS = -DNDEBUG -g -O3 -std=c++20 -fnon-call-exceptions -fasynchronous-unwind-tables -Wcheri
MORELLO_FLAGS = -march=morello -mabi=purecap
LDFLAGS = -lpthread

load_injector_cap: load_injector.cpp tpcc/*pp *.hpp
	$(CXX_CAP) $(CXXFLAGS) $(MORELLO_FLAGS) load_injector.cpp -o load_injector_cap $(LDFLAGS)

load_injector_base: load_injector.cpp tpcc/*pp *.hpp
	$(CXX) $(CXXFLAGS) load_injector.cpp -o load_injector_base $(LDFLAGS)

all: load_injector_cap load_injector_base

clean:
	rm -f load_injector_cap load_injector_base
