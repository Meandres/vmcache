vmcache: vmcache.cpp tpcc/*pp
	g++ -std=c++20 -g -fnon-call-exceptions -fasynchronous-unwind-tables vmcache.cpp -o vmcache -laio

clean:
	rm vmcache
