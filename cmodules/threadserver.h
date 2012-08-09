#ifndef THREADSERVER_H
#define THREADSERVER_H

#ifdef _WIN32
#include "pthread_mutex_win32_wrapper.h"
#include "sched_affinity_win32_wrapper.h"
#else
#include <pthread.h>
#include <sched.h>
#endif

class qThreadServerException
{

};

template <class S, class T> class qThreadCls;

class qThreadParam
{
	template <class S, class T> friend class qThreadCls;

public:
	qThreadParam()
	{
		for (int i = 0;i < 2;i++) if (pthread_mutex_init(&threadMutex[i], NULL)) {fprintf(STD_OUT, "Error creating mutex\n");throw(qThreadServerException());}
		for (int i = 0;i < 2;i++) if (pthread_mutex_lock(&threadMutex[i])) {fprintf(STD_OUT, "Error locking mutex\n");throw(qThreadServerException());}
		terminate = false;
		pinCPU = -1;
	}

	~qThreadParam()
	{
		for (int i = 0;i < 2;i++) if (pthread_mutex_unlock(&threadMutex[i])) {fprintf(STD_OUT, "Error unlocking mutex\n");throw(qThreadServerException());}
		for (int i = 0;i < 2;i++) if (pthread_mutex_destroy(&threadMutex[i])) {fprintf(STD_OUT, "Error destroying mutex\n");throw(qThreadServerException());}
	}

	bool WaitForTask()
	{
		if (pthread_mutex_unlock(&threadMutex[1])) {fprintf(STD_OUT, "Error unlocking mutex\n");throw(qThreadServerException());}
		if (pthread_mutex_lock(&threadMutex[0])) {fprintf(STD_OUT, "Error locking mutex\n");throw(qThreadServerException());}
		return(!terminate);
	}

	int threadNum;

protected:
	int pinCPU;
	pthread_mutex_t threadMutex[2];
	volatile bool terminate;
};

template <class S> class qThreadParamCls : public qThreadParam
{
	template <class SS, class TT> friend class qThreadCls;
	
private:
	S* pCls;
	void (S::*pFunc)(void*);
};

template <class S, class T> static void* qThreadWrapperCls(void* arg);

template <class S, class T> class qThreadCls
{
public:
	qThreadCls() {started = false;};
	qThreadCls(S* pCls, void (S::*pFunc)(T*), int threadNum = 0, int pinCPU = -1) : threadParam() {started = false;SpawnThread(pCls, pFunc, threadNum, pinCPU);}

	void SpawnThread(S* pCls, void (S::*pFunc)(T*), int threadNum = 0, int pinCPU = -1, bool wait = true)
	{
		qThreadParamCls<S>& threadParam = *((qThreadParamCls<S>*) &this->threadParam);

		threadParam.pCls = pCls;
		threadParam.pFunc = (void (S::*)(void*)) pFunc;
		threadParam.threadNum = threadNum;
		threadParam.pinCPU = pinCPU;
		pthread_t thr;
		pthread_create(&thr, NULL, (void* (*) (void*)) &qThreadWrapperCls, &threadParam);
		if (wait) WaitForSpawn();
		started = true;
	}
	
	void WaitForSpawn()
	{
		if (pthread_mutex_lock(&threadParam.threadMutex[1])) {fprintf(STD_OUT, "Error locking mutex\n");throw(qThreadServerException());}
	}

	~qThreadCls()
	{
		if (started)
		{
			End();
		}
	}

	void End()
	{
		qThreadParamCls<S>& threadParam = *((qThreadParamCls<S>*) &this->threadParam);
	
		threadParam.terminate = true;
		if (pthread_mutex_unlock(&threadParam.threadMutex[0])) {fprintf(STD_OUT, "Error unlocking mutex\n");throw(qThreadServerException());}
		if (pthread_mutex_lock(&threadParam.threadMutex[1])) {fprintf(STD_OUT, "Error locking mutex\n");throw(qThreadServerException());}
		started = false;
	}

	void Start()
	{
		if (pthread_mutex_unlock(&threadParam.threadMutex[0])) {fprintf(STD_OUT, "Error unlocking mutex\n");throw(qThreadServerException());}
	}

	void Sync()
	{
		if (pthread_mutex_lock(&threadParam.threadMutex[1])) {fprintf(STD_OUT, "Error locking mutex\n");throw(qThreadServerException());}
	}
		
private:
	bool started;
	T threadParam;
	
	static void* qThreadWrapperCls(T* arg);
};

template <class S, class T> void* qThreadCls<S, T>::qThreadWrapperCls(T* arg)
{
	qThreadParamCls<S>* const arg_A = (qThreadParamCls<S>*) arg;
	if (arg_A->pinCPU != -1)
	{
		cpu_set_t tmp_mask;
		CPU_ZERO(&tmp_mask);
		CPU_SET(arg_A->pinCPU, &tmp_mask);
		sched_setaffinity(0, sizeof(tmp_mask), &tmp_mask);
	}

	void (S::*pFunc)(T*) = (void (S::*)(T*)) arg_A->pFunc;
	(arg_A->pCls->*pFunc)(arg);

	if (pthread_mutex_unlock(&arg_A->threadMutex[1])) {fprintf(STD_OUT, "Error unlocking mutex\n");throw(qThreadServerException());}
	pthread_exit(NULL);
	return(NULL);
}

template <class S, class T> class qThreadClsArray
{
public:
	qThreadClsArray() {pArray = NULL;nThreadsRunning = 0;}
	qThreadClsArray(int n, S* pCls, void (S::*pFunc)(T*), int threadNumOffset = 0, int* pinCPU = NULL) {pArray = NULL;nThreadsRunning = 0;SetNumberOfThreads(n, pCls, pFunc, threadNumOffset, pinCPU);}

	void SetNumberOfThreads(int n, S* pCls, void (S::*pFunc)(T*), int threadNumOffset = 0, int* pinCPU = NULL)
	{
		if (nThreadsRunning)
		{
			fprintf(STD_OUT, "Threads already started\n");throw(qThreadServerException());
		}
		pArray = new qThreadCls<S, T>[n];
		nThreadsRunning = n;
		for (int i = 0;i < n;i++)
		{
			pArray[i].SpawnThread(pCls, pFunc, threadNumOffset + i, pinCPU == NULL ? -1 : pinCPU[i], false);
		}
		for (int i = 0;i < n;i++)
		{
			pArray[i].WaitForSpawn();
		}
	}

	~qThreadClsArray()
	{
		if (nThreadsRunning)
		{
			EndThreads();
		}
	}

	void EndThreads()
	{
		delete[] pArray;
		nThreadsRunning = 0;
	}

	void Start()
	{
		for (int i = 0;i < nThreadsRunning;i++) pArray[i].Start();
	}

	void Sync()
	{
		for (int i = 0;i < nThreadsRunning;i++) pArray[i].Sync();
	}

private:
	qThreadCls<S, T>* pArray;
	int nThreadsRunning;
};

#endif
