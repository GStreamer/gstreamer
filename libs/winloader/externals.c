#include "externals.h"
#include <stdio.h>
#include <pthread.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/timeb.h>

#include <wine/winbase.h>
#include <wine/winreg.h>
#include <wine/winnt.h>
#include <wine/winerror.h>
#include <wine/debugtools.h>

#include <registry.h>

void dbgprintf(char* fmt, ...)
{
    va_list va;
    FILE* f;
    va_start(va, fmt);
    f=fopen("./log", "a");
    vfprintf(f, fmt, va);
    fsync(f);
    fclose(f);
}    
char export_names[500][30]={
"name1",
//"name2",
//"name3"
};
//#define min(x,y) ((x)<(y)?(x):(y))

static unsigned char* heap=NULL; 
static int heap_counter=0;
void test_heap()
{
    int offset=0;	
    if(heap==0)
	return;
    while(offset<heap_counter)
    {
	if(*(int*)(heap+offset)!=0x433476)
	{
	    printf("Heap corruption at address %d\n", offset);
	    return;
	}
	offset+=8+*(int*)(heap+offset+4);
    }
    for(;offset<min(offset+1000, 20000000); offset++)
	if(heap[offset]!=0xCC)
	    {
		printf("Free heap corruption at address %d\n", offset);
	}	    
}
#undef MEMORY_DEBUG

#ifdef MEMORY_DEBUG

void* my_mreq(int size, int to_zero)
{
    test_heap();
    if(heap==NULL)
    {
	heap=malloc(20000000);
	memset(heap, 0xCC,20000000);
    }
    if(heap_counter+size>20000000)
    {
	printf("No enough memory\n");
	return 0;
    }	
    *(int*)(heap+heap_counter)=0x433476;
    heap_counter+=4;
    *(int*)(heap+heap_counter)=size;
    heap_counter+=4;
    printf("Allocated %d bytes of memory: sys %d, user %d-%d\n", size, heap_counter-8, heap_counter, heap_counter+size);
    if(to_zero)
    	memset(heap+heap_counter, 0, size);	    
    heap_counter+=size;
    return heap+heap_counter-size;	
}
int my_release(char* memory)
{
    test_heap();
    if(*(int*)(memory-8)!=0x433476)
    {
	printf("MEMORY CORRUPTION !!!!!!!!!!!!!!!!!!!\n");
	return 0;
    }
    printf("Freed %d bytes of memory\n", *(int*)(memory-4));
//    memset(memory-8, *(int*)(memory-4), 0xCC);
    return 0;
}    	     

#else
void* my_mreq(int size, int to_zero)
{
    if(to_zero)
	return calloc(size, 1);
	else
	return malloc(size);
}	
int my_release(char* memory)
{
    free(memory);
    return 0;
}
#endif

extern int unk_exp1;
char extcode[20000];// place for 200 unresolved exports
int pos=0;

int WINAPI ext_unknown()
{
    printf("Unknown func called\n");
    return 0;
}    
int WINAPI expIsBadWritePtr(void* ptr, unsigned int count)
{
    if(count==0)
	return 0;
    if(ptr==0)
        return 1;
    return 0;
}
int WINAPI expIsBadReadPtr(void* ptr, unsigned int count)
{
    if(count==0)
	return 0;
    if(ptr==0)
        return 1;
    return 0;
}
void* CDECL expmalloc(int size)
{
//printf("malloc");
//    return malloc(size);
    return my_mreq(size,0);
}
void CDECL expfree(void* mem)
{
//    return free(mem);
    my_release(mem);
}
void* CDECL expnew(int size)
{
//    printf("NEW:: Call from address %08x\n STACK DUMP:\n", *(-1+(int*)&size));
//    printf("%08x %08x %08x %08x\n",
//    size, *(1+(int*)&size),
//    *(2+(int*)&size),*(3+(int*)&size));
    return malloc(size);
}    
int CDECL expdelete(void* memory)
{
    free(memory);
    return 0;
}
int WINAPI expDisableThreadLibraryCalls(int module)
{
    return 0;
}    
int CDECL exp_initterm(int v1, int v2)
{
    return 0;
}    

typedef struct {
    unsigned int     	uDriverSignature;
    void*        	hDriverModule;
    void*    		DriverProc;
    unsigned int        dwDriverID;
} DRVR;

void* WINAPI expGetDriverModuleHandle(DRVR* pdrv)
{
    return pdrv->hDriverModule;
}
struct th_list_t;
typedef struct th_list_t{
int id;
void* thread;
struct th_list_t* next;
struct th_list_t* prev;
}th_list;

static th_list* list=NULL;



void* WINAPI expCreateThread(void* pSecAttr, long dwStackSize, void* lpStartAddress,
	void* lpParameter, long dwFlags, long* dwThreadId)
{
    pthread_t *pth;
//    printf("CreateThread:");
    pth=my_mreq(sizeof(pthread_t), 0);
    dbgprintf("pthread_create\n");
    pthread_create(pth, NULL, lpStartAddress, lpParameter);
    if(dwFlags)
	dbgprintf( "WARNING: CreateThread flags not supported\n");
    if(dwThreadId)
	*dwThreadId=(long)pth;
    dbgprintf( "Created thread %08X\n", pth);
    if(list==NULL)
    {
	list=my_mreq(sizeof(th_list), 1);
	list->next=list->prev=NULL;
    }
    else
    {
	list->next=my_mreq(sizeof(th_list), 0);
	list->next->prev=list;
	list->next->next=NULL;
	list=list->next;
    }		
    list->thread=pth;
    return pth;
}

struct mutex_list_t;

struct mutex_list_t
{
    pthread_mutex_t *pm;
    char name[64];
    struct mutex_list_t* next;
    struct mutex_list_t* prev;
};
typedef struct mutex_list_t mutex_list;
static mutex_list* mlist=NULL; 
void* WINAPI expCreateEventA(void* pSecAttr, char bManualReset, 
    char bInitialState, const char* name)
{
#warning ManualReset
    pthread_mutex_t *pm;
//    printf("CreateEvent:");
    dbgprintf("CreateEvent\n");
    if(mlist!=NULL)
    {
	mutex_list* pp=mlist;
	if(name!=NULL)
	do
	{
	    if(strcmp(pp->name, name)==0)
		return pp->pm;
	}while(pp=pp->next);
    }	
    pm=my_mreq(sizeof(pthread_mutex_t), 0);
    pthread_mutex_init(pm, NULL);
    if(mlist==NULL)
    {
	mlist=my_mreq(sizeof(mutex_list), 00);
	mlist->next=mlist->prev=NULL;
    }
    else
    {
	mlist->next=my_mreq(sizeof(mutex_list), 00);
	mlist->next->prev=mlist->next;
	mlist->next->next=NULL;
	mlist=mlist->next;
    }
    mlist->pm=pm;
    if(name!=NULL)
        strncpy(mlist->name, name, 64);
	else
	mlist->name[0]=0;
    if(pm==NULL)
	dbgprintf("ERROR::: CreateEventA failure\n");
    if(bInitialState)
        pthread_mutex_lock(pm);
    return pm;
}    

void* WINAPI expSetEvent(void* event)
{
    dbgprintf("Trying to lock %X\n", event);
    pthread_mutex_lock(event);
}
void* WINAPI expResetEvent(void* event)
{
    dbgprintf("Unlocking %X\n", event);
    pthread_mutex_unlock(event);    
}

void* WINAPI expWaitForSingleObject(void* object, int duration)
{
#warning not sure
    dbgprintf("WaitForSingleObject: duration %d\n", duration);
    pthread_mutex_lock(object);
    pthread_mutex_unlock(object);
}    

static BYTE PF[64] = {0,};

void WINAPI expGetSystemInfo(SYSTEM_INFO* si)
{
    	/* FIXME: better values for the two entries below... */
	static int cache = 0;
	static SYSTEM_INFO cachedsi;
	HKEY	xhkey=0,hkey;

	if (cache) {
		memcpy(si,&cachedsi,sizeof(*si));
		return;
	}
	memset(PF,0,sizeof(PF));

	cachedsi.u.s.wProcessorArchitecture     = PROCESSOR_ARCHITECTURE_INTEL;
	cachedsi.dwPageSize 			= getpagesize();

	/* FIXME: better values for the two entries below... */
	cachedsi.lpMinimumApplicationAddress	= (void *)0x40000000;
	cachedsi.lpMaximumApplicationAddress	= (void *)0x7FFFFFFF;
	cachedsi.dwActiveProcessorMask		= 1;
	cachedsi.dwNumberOfProcessors		= 1;
	cachedsi.dwProcessorType		= PROCESSOR_INTEL_386;
	cachedsi.dwAllocationGranularity	= 0x10000;
	cachedsi.wProcessorLevel		= 3; /* 386 */
	cachedsi.wProcessorRevision		= 0;
	
	{
	char buf[20];
	char line[200];
	FILE *f = fopen ("/proc/cpuinfo", "r");

	if (!f)
		return;
        xhkey = 0;
	while (fgets(line,200,f)!=NULL) {
		char	*s,*value;

		/* NOTE: the ':' is the only character we can rely on */
		if (!(value = strchr(line,':')))
			continue;
		/* terminate the valuename */
		*value++ = '\0';
		/* skip any leading spaces */
		while (*value==' ') value++;
		if ((s=strchr(value,'\n')))
			*s='\0';

		/* 2.1 method */
		if (!lstrncmpiA(line, "cpu family",strlen("cpu family"))) {
			if (isdigit (value[0])) {
				switch (value[0] - '0') {
				case 3: cachedsi.dwProcessorType = PROCESSOR_INTEL_386;
					cachedsi.wProcessorLevel= 3;
					break;
				case 4: cachedsi.dwProcessorType = PROCESSOR_INTEL_486;
					cachedsi.wProcessorLevel= 4;
					break;
				case 5: cachedsi.dwProcessorType = PROCESSOR_INTEL_PENTIUM;
					cachedsi.wProcessorLevel= 5;
					break;
				case 6: cachedsi.dwProcessorType = PROCESSOR_INTEL_PENTIUM;
					cachedsi.wProcessorLevel= 5;
					break;
				default:
					break;
				}
			}
			/* set the CPU type of the current processor */
			sprintf(buf,"CPU %ld",cachedsi.dwProcessorType);
			continue;
		}
		/* old 2.0 method */
		if (!lstrncmpiA(line, "cpu",strlen("cpu"))) {
			if (	isdigit (value[0]) && value[1] == '8' && 
				value[2] == '6' && value[3] == 0
			) {
				switch (value[0] - '0') {
				case 3: cachedsi.dwProcessorType = PROCESSOR_INTEL_386;
					cachedsi.wProcessorLevel= 3;
					break;
				case 4: cachedsi.dwProcessorType = PROCESSOR_INTEL_486;
					cachedsi.wProcessorLevel= 4;
					break;
				case 5: cachedsi.dwProcessorType = PROCESSOR_INTEL_PENTIUM;
					cachedsi.wProcessorLevel= 5;
					break;
				case 6: cachedsi.dwProcessorType = PROCESSOR_INTEL_PENTIUM;
					cachedsi.wProcessorLevel= 5;
					break;
				default:
					break;
				}
			}
			/* set the CPU type of the current processor */
			sprintf(buf,"CPU %ld",cachedsi.dwProcessorType);
			continue;
		}
		if (!lstrncmpiA(line,"fdiv_bug",strlen("fdiv_bug"))) {
			if (!lstrncmpiA(value,"yes",3))
				PF[PF_FLOATING_POINT_PRECISION_ERRATA] = TRUE;

			continue;
		}
		if (!lstrncmpiA(line,"fpu",strlen("fpu"))) {
			if (!lstrncmpiA(value,"no",2))
				PF[PF_FLOATING_POINT_EMULATED] = TRUE;

			continue;
		}
		if (!lstrncmpiA(line,"processor",strlen("processor"))) {
			/* processor number counts up...*/
			int	x;

			if (sscanf(value,"%d",&x))
				if (x+1>cachedsi.dwNumberOfProcessors)
					cachedsi.dwNumberOfProcessors=x+1;

			/* Create a new processor subkey on a multiprocessor
			 * system
			 */
			sprintf(buf,"%d",x);
		}
		if (!lstrncmpiA(line,"stepping",strlen("stepping"))) {
			int	x;

			if (sscanf(value,"%d",&x))
				cachedsi.wProcessorRevision = x;
		}
		if (!lstrncmpiA(line,"flags",strlen("flags"))) {
			if (strstr(value,"cx8"))
				PF[PF_COMPARE_EXCHANGE_DOUBLE] = TRUE;
			if (strstr(value,"mmx"))
				PF[PF_MMX_INSTRUCTIONS_AVAILABLE] = TRUE;

		}
	}
	fclose (f);
	}
	memcpy(si,&cachedsi,sizeof(*si));
}

long WINAPI expGetVersion()
{
    return 0xC0000A04;//Windows 98
}    

HANDLE WINAPI expHeapCreate(long flags, long init_size, long max_size)
{
//    printf("HeapCreate:");
    dbgprintf("HeapCreate(%X, %X, %X)\n", flags, init_size, max_size); 
    if(init_size==0)
    	return (HANDLE)my_mreq(0x110000, 0);
    else
	return (HANDLE)my_mreq(init_size, 0);
}		
void* WINAPI expHeapAlloc(HANDLE heap, int flags, int size)
{
    void* z;
    dbgprintf("HeapAlloc(%X, %X, %X)\n", heap, flags, size); 
//    printf("HeapAlloc:");
    z=my_mreq(size, flags&8);    
//    z=HeapAlloc(heap,flags,size);
    if(z==0)
	printf("HeapAlloc failure\n");
    return z;
}
long WINAPI expHeapDestroy(void* heap)
{
    dbgprintf("HeapDestroy(%X)\n", heap); 
    my_release(heap);
    return 1;
}    
void* WINAPI expVirtualAlloc(void* v1, long v2, long v3, long v4)
{
    void* z;
    dbgprintf("VirtualAlloc(%d %d %d %d) \n",v1,v2,v3,v4);
    z=VirtualAlloc(v1, v2, v3, v4);
    if(z==0)
	printf("VirtualAlloc failure\n");
    return z;
}
int WINAPI expVirtualFree(void* v1, int v2, int v3)
{
    dbgprintf("VirtualFree(%X %X %X) \n",v1,v2,v3);
    return VirtualFree(v1,v2,v3);
}    
void WINAPI expInitializeCriticalSection(CRITICAL_SECTION* c)
{
    dbgprintf("InitCriticalSection(%X) \n", c);
    if(sizeof(pthread_mutex_t)>sizeof(CRITICAL_SECTION))
    {
	printf(" ERROR:::: sizeof(pthread_mutex_t) is %d, expected <=%d!\n",
	     sizeof(pthread_mutex_t), sizeof(CRITICAL_SECTION));
	return;
    }
    pthread_mutex_init((pthread_mutex_t*)c, NULL);   
    return;
}          
void WINAPI expEnterCriticalSection(CRITICAL_SECTION* c)
{
    dbgprintf("EnterCriticalSection(%X) \n",c);
    pthread_mutex_lock((pthread_mutex_t*)c);
    return;
}          
void WINAPI expLeaveCriticalSection(CRITICAL_SECTION* c)
{
    dbgprintf("LeaveCriticalSection(%X) \n",c);
    pthread_mutex_unlock((pthread_mutex_t*)c);
    return;
}
void WINAPI expDeleteCriticalSection(CRITICAL_SECTION *c)
{
    dbgprintf("DeleteCriticalSection(%X) \n",c);
    pthread_mutex_destroy((pthread_mutex_t*)c);
    return;
}
int WINAPI expGetCurrentThreadId()
{
    dbgprintf("GetCurrentThreadId() \n");
    return getpid();
}                  
struct tls_s;
typedef struct tls_s
{
    void* value;
    int used;
    struct tls_s* prev;
    struct tls_s* next;
}tls_t;

tls_t* g_tls=NULL;    
    
void* WINAPI expTlsAlloc()
{
    dbgprintf("TlsAlloc \n");
    if(g_tls==NULL)
    {
	g_tls=my_mreq(sizeof(tls_t), 0);
	g_tls->next=g_tls->prev=NULL;
    }
    else
    {
	g_tls->next=my_mreq(sizeof(tls_t), 0);
	g_tls->next->prev=g_tls;
	g_tls->next->next=NULL;
	g_tls=g_tls->next;
    }
    return g_tls;
}

int WINAPI expTlsSetValue(tls_t* index, void* value)
{
    dbgprintf("TlsSetVal(%X %X) \n", index, value );
    if(index==0)
	return 0;
    index->value=value;
    return 1;
}
void* WINAPI expTlsGetValue(tls_t* index)
{
    dbgprintf("TlsGetVal(%X) \n", index );
    if(index==0)
	return 0;
    return index->value;	
}
int WINAPI expTlsFree(tls_t* index)
{
    dbgprintf("TlsFree(%X) \n", index);
    if(index==0)
	return 0;
    if(index->next)
	index->next->prev=index->prev;
    if(index->prev)
        index->prev->next=index->next;
    my_release((void*)index);
    return 1;
}     

void* WINAPI expLocalAlloc(int flags, int size)
{
    void* z;
    dbgprintf("LocalAlloc(%d, flags %X)\n", size, flags);
    if(flags&GMEM_ZEROINIT)
	z=my_mreq(size, 1);
    else
	z=my_mreq(size, 0);
    if(z==0)
	printf("LocalAlloc() failed\n");
    return z;
}	
void* WINAPI expLocalLock(void* z)
{
   dbgprintf("LocalLock\n");
    return z;
}    
void* WINAPI expGlobalAlloc(int flags, int size)
{
    void* z;
     dbgprintf("GlobalAlloc(%d, flags 0x%X)\n", size, flags);
    if(flags&GMEM_ZEROINIT)
	z=my_mreq(size, 1);
	else
	z=my_mreq(size, 0);
    if(z==0)
	printf("LocalAlloc() failed\n");
    return z;
}	
void* WINAPI expGlobalLock(void* z)
{
     dbgprintf("GlobalLock\n");
    return z;
}    

int WINAPI expLoadStringA(long instance, long  id, void* buf, long size)
{
    dbgprintf("LoadStringA\n");
    return LoadStringA(instance, id, buf, size);
}    	    	

long WINAPI expMultiByteToWideChar(long v1, long v2, char* s1, long siz1, char* s2, int siz2)
{
#warning fixme
    dbgprintf("MB2WCh\n");
//    printf("WARNING: Unsupported call: MBToWCh %s\n", s1);       
    if(s2==0)
	return 1;
    s2[0]=s2[1]=0;
    return 1;
}
long WINAPI expWideCharToMultiByte(long v1, long v2, short* s1, long siz1, char* s2, int siz2, char* c3, int* siz3)
{
    dbgprintf("WCh2MB\n");
    return WideCharToMultiByte(v1, v2, s1, siz1, s2, siz2, c3, siz3);
}
long WINAPI expGetVersionExA(OSVERSIONINFOA* c)
{
    dbgprintf("GetVersionExA\n");
    c->dwMajorVersion=4;
    c->dwMinorVersion=10;
    c->dwBuildNumber=0x40a07ce;
    c->dwPlatformId=VER_PLATFORM_WIN32_WINDOWS;
    strcpy(c->szCSDVersion, "Win98");
    return 1;
}        
HANDLE WINAPI expCreateSemaphoreA(char* v1, long init_count, long max_count, char* name)
{
#warning fixme
    void* z;
    dbgprintf("CreateSemaphoreA\n");
//    printf("CreateSemaphore:");
    z=my_mreq(24, 0);
    pthread_mutex_init(z, NULL);
    return (HANDLE)z;
}
        
long WINAPI expReleaseSemaphore(long hsem, long increment, long* prev_count)
{
// The state of a semaphore object is signaled when its count 
// is greater than zero and nonsignaled when its count is equal to zero
// Each time a waiting thread is released because of the semaphore's signaled
// state, the count of the semaphore is decreased by one. 
    dbgprintf("ReleaseSemaphore\n");
    printf("WARNING: Unsupported call: ReleaseSemaphoreA\n");       
    return 1;//zero on error
}


long WINAPI expRegOpenKeyExA(long key, char* subkey, long reserved, long access, long* newkey)
{
    return RegOpenKeyExA(key, subkey, reserved, access, newkey);
}    
long WINAPI expRegCloseKey(long key)
{
    return RegCloseKey(key);
}         
long WINAPI expRegQueryValueExA(long key, char* value, int* reserved, int* type, int* data, int* count)
{
    return RegQueryValueExA(key, value, reserved, type, data, count);
}  
long WINAPI expRegCreateKeyExA(long key, char* name, long reserved,
							   void* classs, long options, long security,
							   void* sec_attr, int* newkey, int* status) 
{
    return RegCreateKeyExA(key, name, reserved, classs, options, security, sec_attr, newkey, status);
}
long WINAPI expRegSetValueExA(long key, char* name, long v1, long v2, void* data, long size)
{
    return RegSetValueExA(key, name, v1, v2, data, size);
}        

long WINAPI expRegOpenKeyA (long hKey, LPCSTR lpSubKey, long* phkResult)
{
    return  RegOpenKeyExA(hKey, lpSubKey, 0, 0, phkResult);
}

long WINAPI expQueryPerformanceCounter(long long* z)
{
    __asm__(
    "rdtsc\n\t"
    "movl %%eax, 0(%0)\n\t"
    "movl %%edx, 4(%0)\n\t"
    ::"c"(z));
    return 1; 
}

long WINAPI expQueryPerformanceFrequency(long long* z)
{
#warning fixme
    *z=(long long)550000000;
    return 1; 
}
long WINAPI exptimeGetTime()
{
    struct timeb t;
    ftime(&t);
    return 1000*t.time+t.millitm;
}
void* WINAPI expLocalHandle(void* v)
{
    dbgprintf("LocalHandle\n");
    return v;
}        
void* WINAPI expGlobalHandle(void* v)
{
    dbgprintf("GlobalHandle\n");
    return v;
}        
int WINAPI expGlobalUnlock(void* v)
{
    dbgprintf("GlobalUnlock\n");
    return 1;
}
//
void* WINAPI expGlobalFree(void* v)
{
    dbgprintf("GlobalFree(%X)\n", v);
    my_release(v);
    return 0;
}        

int WINAPI expLocalUnlock(void* v)
{
    dbgprintf("LocalUnlock\n");
    return 1;
}
//
void* WINAPI expLocalFree(void* v)
{
    dbgprintf("LocalFree(%X)\n", v);
    my_release(v);
    return 0;
}        

// HRSRC fun(HMODULE module, char* name, char* type)
HRSRC WINAPI expFindResourceA(HMODULE module, char* name, char* type)
{
    dbgprintf("FindResourceA\n");
    return FindResourceA(module, name, type);
}
//HGLOBAL fun(HMODULE module, HRSRC res)
HGLOBAL WINAPI expLoadResource(HMODULE module, HRSRC res)
{
    dbgprintf("LoadResource\n");
    return LoadResource(module, res);;    
}
void* WINAPI expLockResource(long res)
{
    dbgprintf("LockResource\n");
    return LockResource(res);
}    
int /*bool*/ WINAPI expFreeResource(long res)
{
    dbgprintf("FreeResource\n");
    return FreeResource(res);
}    
//bool fun(HANDLE)
//!0 on success
int WINAPI expCloseHandle(long v1)
{
    dbgprintf("CloseHandle\n");
    return 1;
}    

const char* WINAPI expGetCommandLineA()
{
    dbgprintf("GetCommandLine\n");
    return "aviplay";
}
LPWSTR WINAPI expGetEnvironmentStringsW()
{
    static short envs[]={0};
    dbgprintf("GetEnvStringsW\n");
    return envs;
}

int WINAPI expFreeEnvironmentStringsW(short* strings)
{
    dbgprintf("FreeEnvStringsW\n");
    return 1;
}
LPCSTR WINAPI expGetEnvironmentStrings()
{
    dbgprintf("GetEnvStrings\n");
    return "\0\0";
}

int WINAPI expGetStartupInfoA(STARTUPINFOA *s)
{
    dbgprintf("GetStartupInfoA\n");
    return  1;
}    

int WINAPI expGetStdHandle(int z)
{
    dbgprintf("GetStdHandle\n");
    printf("WARNING: Unsupported call: GetStdHandle\n");       
    return 1234;
}
int WINAPI expGetFileType(int handle)
{
    dbgprintf("GetFileType\n");
    printf("WARNING: Unsupported call: GetFileType\n");       
    return 5678;
}
int WINAPI expSetHandleCount(int count)
{
    dbgprintf("SetHandleCount\n");
    return 1;        
}
int WINAPI expGetACP()
{
    dbgprintf("GetACP\n");
    printf("WARNING: Unsupported call: GetACP\n");       
    return 0; 
}
int WINAPI expGetModuleFileNameA(int module, char* s, int len)
{
    dbgprintf("GetModuleFileNameA\n");
    printf("File name of module %X requested\n", module);
    if(s==0)
    return 0;
    if(len<10)
    return 0;
    strcpy(s, "aviplay");
    return 1;
}    
    
int WINAPI expSetUnhandledExceptionFilter(void* filter)
{
    dbgprintf("SetUnhandledExcFilter\n");
    return 1;//unsupported and probably won't ever be supported
}    
int WINAPI expLoadLibraryA(char* name)
{
    char qq[20]="./";
    dbgprintf("LoadLibraryA\n");
    printf("They want library %s\n", name);
    strcat(qq, name);
    return LoadLibraryA(qq);
}      
int WINAPI expFreeLibrary(int module)
{
    dbgprintf("FreeLibrary\n");
    return FreeLibrary(module);
//    return 0;
}   
void* WINAPI expGetProcAddress(HMODULE mod, char* name)
{
    dbgprintf("GetProcAddress\n");
    return GetProcAddress(mod, name);
}    

long WINAPI expCreateFileMappingA(int hFile, void* lpAttr,
    long flProtect, long dwMaxHigh, long dwMaxLow, const char* name)
{
    dbgprintf("CreateFileMappingA\n");
    return CreateFileMappingA(hFile, lpAttr, flProtect, dwMaxHigh, dwMaxLow, name);
}    

long WINAPI expOpenFileMappingA(long hFile, long hz, const char* name)
{
#warning fixme
// dbgprintf("OpenFileMappingA\n");
    return OpenFileMappingA(hFile, hz, name);
//  return 0;
}

void* WINAPI expMapViewOfFile(HANDLE file, DWORD mode, DWORD offHigh, DWORD offLow, DWORD size)
{
    dbgprintf("MapViewOfFile(%d, %x, %x, %x, %x)\n",
	file,mode,offHigh,offLow,size);
    return (char*)file+offLow;
}

void* WINAPI expSleep(int time)
{
    dbgprintf("Sleep(%d)\n", time);
    usleep(time);
    return 0;
}        

























struct exports
{
    char name[64];
    int id;
    void* func;
};
struct libs 
{
    char name[64];
    int length;
    struct exports* exps;
};

#define FF(X,Y) \
{#X, Y, exp##X},

struct exports exp_kernel32[]={
FF(IsBadWritePtr, 357)
FF(IsBadReadPtr, 354)
FF(DisableThreadLibraryCalls, -1)
FF(CreateThread, -1)
FF(CreateEventA, -1)
FF(SetEvent, -1)
FF(ResetEvent, -1)
FF(WaitForSingleObject, -1)
FF(GetSystemInfo, -1)
FF(GetVersion, 332)
FF(HeapCreate, 461)
FF(HeapAlloc, -1)
FF(HeapDestroy, -1)
FF(VirtualAlloc, -1)
FF(VirtualFree, -1)
FF(InitializeCriticalSection, -1) 
FF(EnterCriticalSection, -1) 
FF(LeaveCriticalSection, -1) 
FF(DeleteCriticalSection, -1)
FF(TlsAlloc, -1)
FF(TlsFree, -1)
FF(TlsGetValue, -1)
FF(TlsSetValue, -1)
FF(GetCurrentThreadId, -1)
FF(LocalAlloc, -1) 
FF(LocalLock, -1)
FF(GlobalAlloc, -1) 
FF(GlobalLock, -1)
FF(MultiByteToWideChar, 427)
FF(WideCharToMultiByte, -1)
FF(GetVersionExA, -1)
FF(CreateSemaphoreA, -1)
FF(QueryPerformanceCounter, -1)
FF(QueryPerformanceFrequency, -1)
FF(LocalHandle, -1)
FF(LocalUnlock, -1)
FF(LocalFree, -1)
FF(GlobalHandle, -1)
FF(GlobalUnlock, -1)
FF(GlobalFree, -1)
FF(LoadResource, -1)
FF(ReleaseSemaphore, -1) 
FF(FindResourceA, -1)
FF(LockResource, -1)
FF(FreeResource, -1)
FF(CloseHandle, -1)
FF(GetCommandLineA, -1)
FF(GetEnvironmentStringsW, -1)  
FF(FreeEnvironmentStringsW, -1)  
FF(GetEnvironmentStrings, -1)  
FF(GetStartupInfoA, -1)
FF(GetStdHandle, -1)
FF(GetFileType, -1)
FF(SetHandleCount, -1)
FF(GetACP, -1)
FF(GetModuleFileNameA, -1)
FF(SetUnhandledExceptionFilter, -1)
FF(LoadLibraryA, -1)
FF(GetProcAddress, -1)
FF(FreeLibrary, -1)
FF(CreateFileMappingA, -1)
FF(OpenFileMappingA, -1)
FF(MapViewOfFile, -1)
FF(Sleep, -1)
};

struct exports exp_msvcrt[]={
FF(malloc, -1)
FF(_initterm, -1)
FF(free, -1)
{"??3@YAXPAX@Z", -1, expdelete},
{"??2@YAPAXI@Z", -1, expnew},
};
struct exports exp_winmm[]={
FF(GetDriverModuleHandle, -1)
FF(timeGetTime, -1) 
};
struct exports exp_user32[]={
FF(LoadStringA, -1)
};
struct exports exp_advapi32[]={
FF(RegOpenKeyA, -1)
FF(RegOpenKeyExA, -1)
FF(RegCreateKeyExA, -1)
FF(RegQueryValueExA, -1)
FF(RegSetValueExA, -1)
FF(RegCloseKey, -1)
};
#define LL(X) \
{#X".dll", sizeof(exp_##X)/sizeof(struct exports), exp_##X},

struct libs libraries[]={
LL(kernel32)
LL(msvcrt)
LL(winmm)
LL(user32)
LL(advapi32)
};

void* LookupExternal(const char* library, int ordinal)
{
    char* answ;
    int i,j;
    if(library==0)
    {
	printf("ERROR: library=0\n");
	return (void*)ext_unknown;
    }
    printf("External func %s:%d\n", library, ordinal);
//    printf("%x %x\n", &unk_exp1, &unk_exp2);

    for(i=0; i<sizeof(libraries)/sizeof(struct libs); i++)
    {
	if(strcasecmp(library, libraries[i].name))
	    continue;
	for(j=0; j<libraries[i].length; j++)
	{
	    if(ordinal!=libraries[i].exps[j].id)
		continue;
	    printf("Hit: 0x%08X\n", libraries[i].exps[j].func);
	    return libraries[i].exps[j].func;
	}
    }
    answ=(char*)extcode+pos*0x64;
    memcpy(answ, &unk_exp1, 0x64);
    *(int*)(answ+9)=pos;
    *(int*)(answ+47)-=((int)answ-(int)&unk_exp1);
    sprintf(export_names[pos], "%s:%d", library, ordinal);
    pos++;    
    return (void*)answ;
}    

void* LookupExternalByName(const char* library, const char* name)
{
    char* answ;
    int i,j;
    if(library==0)
    {
	printf("ERROR: library=0\n");
	return (void*)ext_unknown;
    }
    if(name==0)
    {
	printf("ERROR: name=0\n");
	return (void*)ext_unknown;
    }
//    printf("External func %s:%s\n", library, name);
    for(i=0; i<sizeof(libraries)/sizeof(struct libs); i++)
    {
	if(strcasecmp(library, libraries[i].name))
	    continue;
	for(j=0; j<libraries[i].length; j++)
	{
	    if(strcmp(name, libraries[i].exps[j].name))
		continue;
//	    printf("Hit: 0x%08X\n", libraries[i].exps[j].func);
	    return libraries[i].exps[j].func;
	}
    }//    printf("%x %x\n", &unk_exp1, &unk_exp2);
    strcpy(export_names[pos], name);
    answ=(char*)extcode+pos*0x64;
    memcpy(answ, &unk_exp1, 0x64);
    *(int*)(answ+9)=pos;
    *(int*)(answ+47)-=((int)answ-(int)&unk_exp1);
    pos++;
    return (void*)answ;
//    memcpy(extcode, &unk_exp1, 0x64);
//    *(int*)(extcode+52)-=((int)extcode-(int)&unk_exp1);
//    return (void*)extcode;
//    printf("Unknown func %s:%s\n", library, name);
//    return (void*)ext_unknown;
}

