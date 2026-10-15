// Pre-4.14 kernels reject MADV_WIPEONFORK (advice 18) with EINVAL, and the
// bionic arc4random bootstrap aborts on that ("arc4random data
// MADV_WIPEONFORK failed"). Emulate the wipe: remember every range that asked
// to be wiped, and make the fork child see it zeroed.
//
// The child is wiped with MADV_DONTNEED, not memset. For a MAP_PRIVATE |
// MAP_ANONYMOUS mapping MADV_DONTNEED drops the private pages and the next
// access faults in a fresh zero page -- the WIPEONFORK result -- while leaving
// the parent untouched, needing no page-aligned length, and faulting nothing
// if the range was unmapped in the meantime (it just returns ENOMEM). One
// syscall, safe in an atfork child.
//
// Registered unconditionally at init: bionic allocates its arc4random range
// lazily (on the first arc4random() call), i.e. after .init_array has run, so
// a "register only if a range exists" guard would never register and the child
// would inherit the parent's RNG state (PARENT == CHILD).
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>

#ifndef MADV_WIPEONFORK
#define MADV_WIPEONFORK 18
#endif

int __real_madvise(void*, size_t, int);

// Static list: no allocation from the wrapper (called from libc init paths) and
// none from the atfork child. 64 ranges is far beyond bionic's needs; if it is
// ever exceeded we still suppress the abort and simply stop recording.
#define MAX_REGIONS 64
static struct { void* p; size_t n; } regions[MAX_REGIONS];
static unsigned nregions;

static void wipe_child(void) {
  for (unsigned i = 0; i < nregions; i++)
    __real_madvise(regions[i].p, regions[i].n, MADV_DONTNEED);
}

static void wipeonfork_compat_init(void) {
  pthread_atfork(NULL, NULL, wipe_child);
}
__attribute__((section(".init_array"), used))
static void (*wipeonfork_compat_init_entry)(void) = wipeonfork_compat_init;

int __wrap_madvise(void* addr, size_t len, int advice) {
  int saved = errno;
  int r = __real_madvise(addr, len, advice);
  if (advice == MADV_WIPEONFORK && r == -1 && errno == EINVAL) {
    if (nregions < MAX_REGIONS) {
      regions[nregions].p = addr;
      regions[nregions].n = len;
      nregions++;
    }
    // Report success even when we could not record: bionic turns a failure
    // here into __libc_fatal, and running with an unwiped child beats aborting.
    errno = saved;
    return 0;
  }
  return r;
}
