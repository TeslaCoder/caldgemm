#include "cmodules/timer.h"
#ifdef _WIN32
#include "cmodules/pthread_mutex_win32_wrapper.h"
#else
#include <pthread.h>
#endif
#include <cal.h>

#define NUM_FAKE_EVENTS 1000000
#define NUM_FAKE_MEM 10000
#define NUM_FAKE_MODULE 100
#define NUM_FAKE_NAME 1000
#define NUM_MODULE_NAMES 24

#define CAL_FAKE_PASSTHROUGH
#define CAL_FAKE_CHECKMEM
//#define CAL_FAKE_VERBOSE

class cal_fake_event
{
public:
	HighResTimer timer;
	int initialized;
	int queried;
	int reused;
	double delay;
	int mems[NUM_MODULE_NAMES];
	int nmems;
	CALevent through;

	cal_fake_event() {initialized = queried = reused = 0;}
};

class cal_fake_mem
{
public:
	int released;
	int active;
	
	CALmem through;
};

class cal_fake_module
{
public:
	int released;
	int nnames;
	int names[NUM_MODULE_NAMES];
	
	CALmodule through;
	CALfunc throughFunc;
};

class cal_fake_name
{
public:
	int mem;
	
	CALname through;
};

class cal_fake
{
public:
	cal_fake_event event[NUM_FAKE_EVENTS];
	pthread_mutex_t mutex;
	int curevent;

	cal_fake_mem mem[NUM_FAKE_MEM];
	int curmem;

	cal_fake_module module[NUM_FAKE_MODULE];
	int curmodule;

	cal_fake_name name[NUM_FAKE_NAME];
	int curname;

	cal_fake()
	{
		pthread_mutex_init(&mutex, NULL);
		curevent = 0;
		curmem = 0;
		curmodule = 0;
		curname = 0;
	}

	~cal_fake()
	{
		pthread_mutex_destroy(&mutex);
		for (int i = 0;i < curevent;i++)
		{
			if (event[i].queried == 0) printf("Warning, event %d not queried\n", i);
		}
	}

	CALresult AddEvent(CALevent* pevent, bool lock = true)
	{
#ifdef CAL_FAKE_VERBOSE
		printf("CREATE %d\n", curevent);
#endif
		*pevent = curevent;
		if (lock) pthread_mutex_lock(&mutex);
		if (event[curevent].initialized) event[curevent].reused = 1;
		event[curevent].initialized = 1;
		event[curevent].queried = 0;
		event[curevent].timer.Reset();
		event[curevent].timer.Start();
		event[curevent].delay = (rand() % 1000) / 100000.;
		event[curevent].nmems = 0;
		curevent = (curevent + 1) % NUM_FAKE_EVENTS;
		if (lock) pthread_mutex_unlock(&mutex);
		return(CAL_RESULT_OK);
	}

	CALresult QueryEvent(CALevent num)
	{
		//printf("QUERY %d\n", num);
		CALresult retVal;
		pthread_mutex_lock(&mutex);
		if (num >= NUM_FAKE_EVENTS)
		{
			printf("------------------------- Requested fake event with handle %d >= %d\n", num, NUM_FAKE_EVENTS);
			retVal = CAL_RESULT_BAD_HANDLE;
		}
		else if (event[num].initialized == 0)
		{
			printf("------------------------- Fake event with handle %d not initialized\n", num);
			retVal = CAL_RESULT_BAD_HANDLE;
		}
		else if (event[num].queried)
		{
			printf("------------------------- Fake event with handle %d already queried\n", num);
			retVal = CAL_RESULT_BAD_HANDLE;
		}
		else
		{
			event[num].timer.Stop();
#ifndef CAL_FAKE_PASSTHROUGH
			if (event[num].timer.GetElapsedTime() <= event[num].delay)
			{
				event[num].timer.Start();
				retVal = CAL_RESULT_PENDING;
			}
			else
#endif
			{
				event[num].queried = 1;
				for (int i = 0;i < event[num].nmems;i++) mem[event[num].mems[i]].active = 0;
				retVal = CAL_RESULT_OK;
			}
		}
		pthread_mutex_unlock(&mutex);
		if(retVal == CAL_RESULT_BAD_HANDLE) while(true);
		return(retVal);
	}

	CALresult AddMemHandle(CALmem* m)
	{
		pthread_mutex_lock(&mutex);
		if (curmem == NUM_FAKE_MEM)
		{
			fprintf(stderr, "NUM_FAKE_MEM overflow\n");
			while(true);
		}
		*m = curmem;
		mem[curmem].released = 0;
		mem[curmem].active = 0;
		curmem++;
		pthread_mutex_unlock(&mutex);
		return(CAL_RESULT_OK);
	}

	CALresult AddModule(CALmodule* mod)
	{
		pthread_mutex_lock(&mutex);
		if (curmodule == NUM_FAKE_MODULE)
		{
			fprintf(stderr, "NUM_FAKE_MODULE overflow\n");
			while(true);
		}
		*mod = curmodule;
		module[curmodule].released = 0;
		module[curmodule].nnames = 0;
		curmodule++;
		pthread_mutex_unlock(&mutex);
		return(CAL_RESULT_OK);
	}

	CALresult AddName(CALname* nam, CALmodule mod)
	{
		//printf("Giving name %d (mod %d)\n", curname, mod);
		pthread_mutex_lock(&mutex);
		if (curname == NUM_FAKE_NAME)
		{
			fprintf(stderr, "NUM_FAKE_NAME overflow\n");
			while(true);
		}
		if (mod > (unsigned) curmodule)
		{
			fprintf(stderr, "Invalid Module\n");
			while(true);
		}
		if (module[mod].nnames == NUM_MODULE_NAMES)
		{
			fprintf(stderr, "NUM_MODULE_NAMES overflow\n");
			while(true);
		}
		*nam = curname;
		module[mod].names[module[mod].nnames] = curname;
		module[mod].nnames++;
		name[curname].mem = 0;
		curname++;
		pthread_mutex_unlock(&mutex);
		return(CAL_RESULT_OK);
	}

	CALresult FakeMemcpy(CALmem mem1, CALmem mem2, CALevent* ev)
	{
		pthread_mutex_lock(&mutex);
#ifdef CAL_FAKE_CHECKMEM
		if (mem[mem1].active || mem[mem2].active)
		{
			fprintf(stderr, "Memory active when starting memcpy\n");
			while(true);
		}
#endif
		AddEvent(ev, false);
		event[*ev].nmems = 2;
		event[*ev].mems[0] = mem1;
		event[*ev].mems[1] = mem2;
		mem[mem1].active = 1;
		mem[mem2].active = 1;
		pthread_mutex_unlock(&mutex);
		return(CAL_RESULT_OK);
	}

	CALresult FakeKernel(CALfunc func, CALevent* ev)
	{
		pthread_mutex_lock(&mutex);
		if (func > (unsigned) curmodule)
		{
			fprintf(stderr, "Invalid func/module");
			while(true);
		}
#ifdef CAL_FAKE_CHECKMEM
		for (int i = 0;i < module[func].nnames;i++)
		{
			if (mem[name[module[func].names[i]].mem].active)
			{
				fprintf(stderr, "Memory active when starting kernel\n");
				while(true);
			}
			mem[name[module[func].names[i]].mem].active = 1;
		}
#endif
		AddEvent(ev, false);
		event[*ev].nmems = module[func].nnames;
		for (int i = 0;i < module[func].nnames;i++) event[*ev].mems[i] = name[module[func].names[i]].mem;
		pthread_mutex_unlock(&mutex);
		return(CAL_RESULT_OK);
	}

	CALresult SetMem(CALname nam, CALmem m)
	{
		if (nam > (unsigned) curname || m > (unsigned) curmem)
		{
			fprintf(stderr, "Invalid name/mem\n");
			while(true);
		}
		name[nam].mem = m;
		return(CAL_RESULT_OK);
	}

	CALresult GetFunc(CALfunc* fun, CALmodule mod)
	{
		*fun = mod;
		return(CAL_RESULT_OK);
	}

	CALresult ReleaseMem(int m)
	{
		mem[m].released = 1;
		return(CAL_RESULT_OK);
	}

	CALresult UnloadModule(int mod)
	{
		module[mod].released = 1;
		return(CAL_RESULT_OK);
	}
};

cal_fake fake;

#ifndef CAL_FAKE_PASSTHROUGH
#define calCtxRunProgram(event, ctx, func, rect) fake.FakeKernel(func, event)
#define calMemCopy(event, ctx, src, dest, flags) fake.FakeMemcpy(src, dest, event)
#define calCtxIsEventDone(ctx, event) fake.QueryEvent(event)
#define calCtxGetMem(mem, ctx, res) fake.AddMemHandle(mem)
#define calCtxSetMem(ctx, name, mem) fake.SetMem(name, mem)
#define calCtxReleaseMem(ctx, mem) fake.ReleaseMem(mem)
#define calModuleLoad(module, ctx, image) fake.AddModule(module)
#define calModuleUnload(ctx, module) fake.UnloadModule(module)
#define calModuleGetName(name, ctx, module, string) fake.AddName(name, module)
#define calModuleGetEntry(func, ctx, module, string) fake.GetFunc(func, module)
#else

static inline CALresult calCtxRunProgram_a(CALevent* event, CALcontext ctx, CALfunc func, CALdomain* rect)
{
	fake.FakeKernel(func, event);
	return(calCtxRunProgram(&fake.event[*event].through, ctx, fake.module[func].throughFunc, rect));
}

static inline CALresult calMemCopy_a(CALevent* event, CALcontext ctx, CALmem src, CALmem dest, CALuint flags)
{
	fake.FakeMemcpy(src, dest, event);
	return(calMemCopy(&fake.event[*event].through, ctx, fake.mem[src].through, fake.mem[dest].through, flags));
}

static inline CALresult calCtxIsEventDone_a(CALcontext ctx, CALevent event)
{
	CALresult retVal = calCtxIsEventDone(ctx, fake.event[event].through);
	if (retVal == CAL_RESULT_OK) fake.QueryEvent(event);
	return(retVal);
}

static inline CALresult calCtxGetMem_a(CALmem* mem, CALcontext ctx, CALresource res)
{
	fake.AddMemHandle(mem);
	return(calCtxGetMem(&fake.mem[*mem].through, ctx, res));
}

static inline CALresult calCtxSetMem_a(CALcontext ctx, CALname name, CALmem mem)
{
	fake.SetMem(name, mem);
	return(calCtxSetMem(ctx, fake.name[name].through, fake.mem[mem].through));
}

static inline CALresult calCtxReleaseMem_a(CALcontext ctx, CALmem mem)
{
	fake.ReleaseMem(mem);
	return(calCtxReleaseMem(ctx, fake.mem[mem].through));
}

static inline CALresult calModuleLoad_a(CALmodule* module, CALcontext ctx, CALimage image)
{
	fake.AddModule(module);
	return(calModuleLoad(&fake.module[*module].through, ctx, image));
}

static inline CALresult calModuleUnload_a(CALcontext ctx, CALmodule module)
{
	fake.UnloadModule(module);
	return(calModuleUnload(ctx, fake.module[module].through));
}

static inline CALresult calModuleGetName_a(CALname* name, CALcontext ctx, CALmodule module, const CALchar* symbolname)
{
	fake.AddName(name, module);
	return(calModuleGetName(&fake.name[*name].through, ctx, fake.module[module].through, symbolname));
}

static inline CALresult calModuleGetEntry_a(CALfunc* func, CALcontext ctx, CALmodule module, const CALchar* symbolname)
{
	fake.GetFunc(func, module);
	return(calModuleGetEntry(&fake.module[module].throughFunc, ctx, fake.module[module].through, symbolname));
}

#define calCtxRunProgram calCtxRunProgram_a
#define calMemCopy calMemCopy_a
#define calCtxIsEventDone calCtxIsEventDone_a
#define calCtxGetMem calCtxGetMem_a
#define calCtxSetMem calCtxSetMem_a
#define calCtxReleaseMem calCtxReleaseMem_a
#define calModuleLoad calModuleLoad_a
#define calModuleUnload calModuleUnload_a
#define calModuleGetName calModuleGetName_a
#define calModuleGetEntry calModuleGetEntry_a

#endif
