#include <string.h>
#include "uint256.h"
#include "RandomX/src/randomx.h"
#if !defined(_MSC_VER)
#include <sched.h>
#include <unistd.h>
#endif

#if defined(__GNUC__)
#include <stdint.h>
#elif defined(_WIN32)
#include <intrin.h>
#include <algorithm>
#include <iostream> 
#include <thread>   
#endif


// djm34 style :D 

#if defined(__cplusplus)
extern "C"
{
#include "miner.h"
}
#endif


#define MAXCPU 128
//#define cpus_number 1

static int cpus_number = num_cpus;
static int is_init[MAXCPU];
static uint256 randomx_seed[MAXCPU]; 

static randomx_vm* rx_vm[MAXCPU];
static randomx_cache* small_cache[MAXCPU];

static randomx_flags flags;
static randomx_cache *randomx_cpu_cache;
static randomx_dataset *randomx_cpu_dataset;

static struct Dataset_threads {
	pthread_t thr;
	int id;
} *dataset_threads;
#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t)(size_t) -1)
#define min(a,b)            (((a) < (b)) ? (a) : (b))
static pthread_barrier_t mybarrier;
static pthread_mutex_t myLock = PTHREAD_MUTEX_INITIALIZER;

void randomx_init_barrier(const int num_threads) {
	pthread_barrier_init(&mybarrier, NULL, num_threads);
}


static void* dataset_init_cpu_thr(void *arg) {

	int i = *(int*)arg;
	uint32_t n = cpus_number;
	randomx_init_dataset(randomx_cpu_dataset, randomx_cpu_cache, (i * randomx_dataset_item_count()) / n, ((i + 1) * randomx_dataset_item_count()) / n - (i * randomx_dataset_item_count()) / n);
	return NULL;
}
static void init_dataset(int thr_id, uint256 theseed)
{
#if defined(_MSC_VER)
	cpus_number = min(32, num_cpus);
#else
	cpus_number = std::min(32, num_cpus);
#endif
	if (thr_id == 0)
	{

	if (randomx_cpu_dataset!=NULL) {
		randomx_release_dataset(randomx_cpu_dataset);
	}
		flags = randomx_get_flags();

	if (rdx_hard_aes)
		flags |= RANDOMX_FLAG_HARD_AES;
	if (rdx_large_page)
		flags |= RANDOMX_FLAG_LARGE_PAGES;
	if (rdx_argon_avx2)
		flags |= RANDOMX_FLAG_ARGON2_AVX2;
	if (rdx_argon_ssse3)
		flags |= RANDOMX_FLAG_ARGON2_SSSE3;
	if (rdx_argon)
		flags |= RANDOMX_FLAG_ARGON2;
	if (rdx_full_mem)
		flags |= RANDOMX_FLAG_FULL_MEM;
	if (rdx_jit)
		flags |= RANDOMX_FLAG_JIT;
	if (rdx_secure)
		flags |= RANDOMX_FLAG_SECURE;


		randomx_cpu_cache = randomx_alloc_cache(flags);
		if (randomx_cpu_cache == nullptr) {
			printf( "Cache allocation failed\n"); 
		}
		randomx_init_cache(randomx_cpu_cache, theseed.GetHex().c_str(), theseed.GetHex().size());
		randomx_cpu_dataset = randomx_alloc_dataset(flags);
		if (dataset_threads == NULL) {
			dataset_threads = (Dataset_threads*)malloc(cpus_number * sizeof(Dataset_threads));
		}
		//// make sure all the threads are ran on different cpu thread
#if !defined(_MSC_VER)
		cpu_set_t cpuset;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
#endif
		for (int i = 0; i < cpus_number; ++i) {
			dataset_threads[i].id = i;
#if defined(_MSC_VER)
			pthread_create(&dataset_threads[i].thr, NULL, dataset_init_cpu_thr, (void*)&dataset_threads[i].id);
#else
			CPU_ZERO(&cpuset);
			CPU_SET(i, &cpuset);
			int s = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
			pthread_create(&dataset_threads[i].thr, &attr, dataset_init_cpu_thr, (void*)&dataset_threads[i].id);
#endif
		}
		for (int i = 0; i < cpus_number; ++i) {
			pthread_join(dataset_threads[i].thr, NULL);
		}

		randomx_release_cache(randomx_cpu_cache);
		pthread_barrier_wait(&mybarrier);
	}
	else {
		pthread_barrier_wait(&mybarrier);
	}
}
int scanhash_rx2(int thr_id, struct work* work, const unsigned char* seedhash, uint32_t max_nonce, uint64_t* hashes_done)
{
  	bool ShouldWait = false;

    uint32_t  hash[8];
    uint32_t  endiandata[36];
    uint32_t* pdata = work->data;
    uint32_t* ptarget = work->target;

    const uint32_t Htarg = ptarget[7];
    const uint32_t first_nonce = pdata[19];
    uint32_t n = first_nonce;
 
    bool has_roots = false;
    for (int i=0; i < 36; i++) {
       be32enc(&endiandata[i], pdata[i]);
       if (i >= 20 && pdata[i]) has_roots = true;
    }
    uint8_t endian[32];
//    uint8_t TheSeed[32];
  for (int i=0; i<32; i++) 
      endian[31-i]=seedhash[i];

  uint256 TheSeed;
  memcpy(&TheSeed, endian, 32);
  
	  if (!is_init[thr_id]) {
		  randomx_seed[thr_id] = TheSeed;
		  if (thr_id == 0)
		  printf("rx2 seed = %s\n", TheSeed.GetHex().c_str());
		if (rdx_full_mem) {
			  init_dataset(thr_id, TheSeed);
			  rx_vm[thr_id] = randomx_create_vm(flags, nullptr, randomx_cpu_dataset);
		}
		else {
			  flags = randomx_get_flags();
			  small_cache[thr_id] = randomx_alloc_cache(flags);
			  randomx_init_cache(small_cache[thr_id], TheSeed.GetHex().c_str(), TheSeed.GetHex().size());
			  rx_vm[thr_id] = randomx_create_vm(flags, small_cache[thr_id], NULL);
		}

		  is_init[thr_id] = true;
	  }

	  if (randomx_seed[thr_id] != TheSeed) {
			randomx_seed[thr_id] = TheSeed;
		if (thr_id==0)
		  printf("changing rx2 seed = %s\n", TheSeed.GetHex().c_str());
		  pthread_mutex_lock(&myLock);

			bool TheTest = true;
			for (int i = 0;i<opt_n_threads;i++)
				TheTest = TheTest && randomx_seed[i] == TheSeed;
			while(!TheTest) {
				bool TheTest2 = true;
				for (int i = 0; i<opt_n_threads; i++)
					TheTest2 = TheTest2 && randomx_seed[i] == TheSeed;
				TheTest = TheTest2;
				usleep(250);
			}

			pthread_mutex_unlock(&myLock);	
			randomx_destroy_vm(rx_vm[thr_id]);

		  if (rdx_full_mem) {
			  init_dataset(thr_id, TheSeed);
			  rx_vm[thr_id] = randomx_create_vm(flags, nullptr, randomx_cpu_dataset);
		  } else {
			  randomx_release_cache(small_cache[thr_id]);
			  small_cache[thr_id] = randomx_alloc_cache(flags);
			  randomx_init_cache(small_cache[thr_id], TheSeed.GetHex().c_str(), TheSeed.GetHex().size());
			  rx_vm[thr_id] = randomx_create_vm(flags, small_cache[thr_id], NULL);
		  }
	  }

    do {

        be32enc(&endiandata[19], n);

    randomx_calculate_hash(rx_vm[thr_id], (const char*)endiandata, 144, (char*) hash);
        if (hash[7] < Htarg) {

            *hashes_done = n - first_nonce + 1;
            pdata[19] = n;
            return 1;
        }
        n++;

    } while (n <  max_nonce && !work_restart[thr_id].restart && randomx_seed[thr_id] == TheSeed);

    *hashes_done = n - first_nonce + 1;
    pdata[19] = n;

    return 0;
}
