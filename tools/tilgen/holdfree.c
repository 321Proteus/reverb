// cc -O2 -shared -fPIC -o holdfree.so holdfree.c -lpthread

#define _GNU_SOURCE

#include <malloc.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

extern void __libc_free(void*);
extern void* __libc_malloc(size_t);

#define SLOTS ((size_t)1 << 27)

#define DEFAULT_MB 1024

struct slot {
	void* p;
	size_t n;
};

static struct slot* ring;
static size_t head, tail, count, held, budget;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static void evict_one(void) {
	__libc_free(ring[tail].p);

	held -= ring[tail].n;
	tail = (tail + 1) % SLOTS;
	count--;
}

void free(void* p) {
	if (!p) return;

	size_t n = malloc_usable_size(p);
	pthread_mutex_lock(&mu);

	if (!ring) {
		const char* e = getenv("TILGEN_QUARANTINE_MB");
		budget = (size_t)(e && atol(e) > 0 ? atol(e) : DEFAULT_MB) << 20;

		void* m = mmap(NULL, SLOTS * sizeof *ring, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

		if (m == MAP_FAILED) {
			pthread_mutex_unlock(&mu);
			__libc_free(p);
			return;
		}

		ring = m;
	}

	if (count == SLOTS) evict_one();
	ring[head].p = p;
	ring[head].n = n;

	head = (head + 1) % SLOTS;
	count++;
	held += n;

	while (held > budget && count > 1)
		evict_one();

	pthread_mutex_unlock(&mu);
}

void cfree(void* p) { free(p); }

void* realloc(void* p, size_t n) {
	if (!p) return __libc_malloc(n);

	if (!n) {
		free(p);
		return NULL;
	}

	void* q = __libc_malloc(n);
	if (!q) return NULL;

	size_t old = malloc_usable_size(p);
	memcpy(q, p, old < n ? old : n);
	free(p);

	return q;
}
