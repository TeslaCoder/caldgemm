#include "qmalloc.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winbase.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000 /* arch specific */
#endif
#endif

#ifndef STD_OUT
#define STD_OUT stdout
#endif

int qmalloc::qMallocCount = 0;
int qmalloc::qMallocUsed = 0;
qmalloc::qMallocData* qmalloc::qMallocs = NULL;

#ifdef _WIN32
static void Privilege(TCHAR* pszPrivilege, BOOL bEnable)
{
	HANDLE           hToken;
	TOKEN_PRIVILEGES tp;
	BOOL             status;
	DWORD            error;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
	{
		fprintf(STD_OUT, "Error obtaining process token\n");
	}

	if (!LookupPrivilegeValue(NULL, pszPrivilege, &tp.Privileges[0].Luid))
	{
		fprintf(STD_OUT, "Error looking up priviledge value\n");
	}

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	status = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)NULL, 0);

	error = GetLastError();
	if (!status || (error != ERROR_SUCCESS))
	{
		fprintf(STD_OUT, "Error obtaining Priviledge %d\n", GetLastError());
	}

	CloseHandle(hToken);
}
#endif

void* qmalloc::qMalloc(size_t size, bool huge, bool executable, bool locked)
{
	int pagesize;
	void* addr;
	if (huge)
	{
#ifdef _WIN32
		static int tokenObtained = 0;
		pagesize = GetLargePageMinimum();
		if (tokenObtained == 0)
		{
			fprintf(STD_OUT, "Obtaining security token\n");
			Privilege(TEXT("SeLockMemoryPrivilege"), TRUE);
			tokenObtained = 1;
		}
#else
		pagesize = 1024 * 2048;
#endif
	}
	else
	{
#ifdef _WIN32
		SYSTEM_INFO si;
        GetSystemInfo(&si);
		pagesize = si.dwPageSize;
#else
		pagesize = sysconf(_SC_PAGESIZE);
#endif
	}
	if (size % pagesize) size += pagesize - size % pagesize;
#ifdef _WIN32
	DWORD flags = MEM_COMMIT;
	if (huge)
	{
		flags |= MEM_LARGE_PAGES;
	}
	DWORD protect = PAGE_READWRITE;
	if (executable)
	{
		protect = PAGE_EXECUTE_READWRITE;
	}
	addr = VirtualAlloc(NULL, size, flags, protect);
#else
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	int prot = PROT_READ | PROT_WRITE;
	if (huge) flags |= MAP_HUGETLB;
	if (executable) prot |= PROT_EXEC;
	if (locked) flags |= MAP_LOCKED;
	addr = mmap(NULL, size, prot, flags, 0, 0);
	if (addr == MAP_FAILED) addr = NULL;
#endif

	if (addr)
	{
		if (qMallocCount == qMallocUsed)
		{
			if (qMallocCount == 0) qMallocCount = 8;
			else if (qMallocCount < 1024) qMallocCount *= 2;
			else qMallocCount += 1024;
			if (qMallocUsed == 0)
			{
				qMallocs = (qMallocData*) malloc(qMallocCount * sizeof(qMallocData));
			}
			else
			{
				qMallocs = (qMallocData*) realloc(qMallocs, qMallocCount * sizeof(qMallocData));
			}
		}
		qMallocs[qMallocUsed].addr = addr;
		qMallocs[qMallocUsed].size = size;
		qMallocUsed++;
	}
	else
	{
#ifdef _WIN32
		DWORD error = GetLastError();
#endif
	}

#ifdef _WIN32
	if (addr && locked)
	{
		size_t minp, maxp;
		HANDLE pid = GetCurrentProcess();
		if (GetProcessWorkingSetSize(pid, &minp, &maxp) == 0) fprintf(STD_OUT, "Error getting minimum working set size\n");
		if (SetProcessWorkingSetSize(pid, minp + size, maxp + size) == 0) fprintf(STD_OUT, "Error settings maximum working set size\n");
		if (VirtualLock(addr, size) == 0)
		{
			fprintf(STD_OUT, "Error locking memory\n");
			DWORD error = GetLastError();
			VirtualFree(addr, 0, MEM_RELEASE);
			if (SetProcessWorkingSetSize(pid, minp, maxp) == 0) fprintf(STD_OUT, "Error settings maximum working set size\n");
			addr = NULL;
		}
	}
#endif

	return(addr);
}

int qmalloc::qFree(void* ptr)
{
	for (int i = 0;i < qMallocUsed;i++)
	{
		if (qMallocs[i].addr == ptr)
		{
#ifdef _WIN32
			if (VirtualFree(ptr, 0, MEM_RELEASE) == 0) return(1);
#else
			if (munmap(ptr, qMallocs[i].size)) return(1);
#endif
			qMallocUsed--;
			if (i < qMallocUsed) memcpy(&qMallocs[i], &qMallocs[qMallocUsed], sizeof(qMallocData));
			return(0);
		}
	}
	return(1);
}