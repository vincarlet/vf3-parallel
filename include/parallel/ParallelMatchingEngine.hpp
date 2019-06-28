/*
 * ThreadPool.hpp
 *
 *  Created on: 21 nov 2017
 *      Author: vcarletti
 */

/*
Parallel Matching Engine with global state stack only (no look-free stack)
*/

#ifndef PARALLELMATCHINGTHREADPOOL_HPP
#define PARALLELMATCHINGTHREADPOOL_HPP

#include <atomic>
#include <thread>
#include <mutex>
#include <array>
#include <vector>
#include <stack>
#include <cstdint>

#include "ARGraph.hpp"
#include "MatchingEngine.hpp"

namespace vflib {

template<typename VFState>
class ParallelMatchingEngine
		: public MatchingEngine<VFState>
{
private:
	typedef unsigned short ThreadId;
	using MatchingEngine<VFState>::solutions;
	using MatchingEngine<VFState>::visit;
	//using MatchingEngine<VFState>::solCount;
	using MatchingEngine<VFState>::storeSolutions;

	std::mutex statesMutex;
	std::mutex solutionsMutex;

	std::atomic<uint32_t> solCount;
	int16_t cpu;
	int16_t numThreads;
	std::vector<std::thread> pool;
	std::atomic<int16_t> activeWorkerCount;
	bool teminate;
	std::stack<VFState*> globalStateStack;
	
public:
	ParallelMatchingEngine(unsigned short int numThreads, 
		bool storeSolutions=false, 
		short int cpu = -1,
		MatchingVisitor<VFState> *visit = NULL):
		MatchingEngine<VFState>(visit, storeSolutions),
		solCount(0),
		cpu(cpu),
		numThreads(numThreads),
		pool(numThreads),
		activeWorkerCount(0){}

	~ParallelMatchingEngine(){}

	inline size_t GetSolutionsCount() { return (size_t) solCount; }

	bool FindAllMatchings(VFState& s)
	{
		ProcessState(&s);
		StartPool();

		//Waiting for process thread
		for (auto &th : pool) {
			if (th.joinable()) {
				th.join();
			}
		}

		//Exiting
		return true;
	}

	inline size_t GetThreadCount() const {
		return pool.size();
	}

private: 

	inline unsigned GetRemainingStates() {
		std::lock_guard<std::mutex> guard(statesMutex);
		return globalStateStack.size();
	}

	void Run(int thread_id) 
	{
		VFState* s = NULL;
		do
		{
			if(s)
			{
				ProcessState(s);
				delete s;
				activeWorkerCount--;	
			}
			s = GetState();
		}while(s || activeWorkerCount>0);
	}

	bool ProcessState(VFState *s)
	{
		if (s->IsGoal())
		{
			//std::cout << "Solution" << std::endl;
			solCount++;
			if(storeSolutions)
			{
				std::lock_guard<std::mutex> guard(solutionsMutex);
				MatchingSolution sol;
				s->GetCoreSet(sol);
				solutions.push_back(sol);
			}
			if (visit)
			{
				return (*visit)(*s);
			}
			return true;
		}

		if (s->IsDead())
			return false;

		nodeID_t n1 = NULL_NODE, n2 = NULL_NODE;
		while (s->NextPair(&n1, &n2, n1, n2))
		{
			if (s->IsFeasiblePair(n1, n2))
			{
				VFState* s1 = new VFState(*s);
				s1->AddPair(n1, n2);
				PutState(s1);
			}
		}		
		return false;
		
	}

	void PutState(VFState* s) {
		std::lock_guard<std::mutex> guard(statesMutex);
		globalStateStack.push(s);
	}

	//In questo modo, quando sono finiti gli stati i thread rimangono appesi.
	//Come facciamo a definire una condizione di chiusura dei thread?
	VFState* GetState()
	{
		VFState* res = NULL;
		std::unique_lock<std::mutex> stateLock(statesMutex);
		if(globalStateStack.size())
		{
			res = globalStateStack.top();
			globalStateStack.pop();
			activeWorkerCount++;
		}
		return res;
	}

#ifndef WIN32
	void SetAffinity(int cpu, pthread_t handle)
	{
		cpu_set_t cpuset;
    	CPU_ZERO(&cpuset);
    	CPU_SET(cpu, &cpuset);
		int rc = pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset);
		if (rc != 0) 
		{
			std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
		}
	}
#endif

	void StartPool()
	{
		int current_cpu = cpu; 

		for (size_t i = 0; i < numThreads; ++i)
		{
			pool[i] = std::thread( [this,i]{ this->Run(i); } );
#ifndef WIN32
			//If cpu is not -1 set the thread affinity starting from the cpu
			if(current_cpu > -1)
			{
				SetAffinity(current_cpu, pool[i].native_handle());
				current_cpu++;
			}
#endif
		}
	}

};

}

#endif /* INCLUDE_PARALLEL_PARALLELMATCHINGTHREADPOOL_HPP_ */
