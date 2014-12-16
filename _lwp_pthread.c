/* 
 * Copyright (c) 2014 Antti Kantee
 *
 * This pthread version is not really expected to work.  It disallows
 * creating threads with ENOSYS.  If you manage to enter the code from
 * other places, well, good luck.
 */

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/lwpctl.h>
#include <sys/lwp.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/tls.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ucontext.h>

#include "netbsd_init.h"
#include "rumprunposix_makelwp.h"

#if 0
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

struct schedulable {
	struct tls_tcb scd_tls;

	pthread_t scd_thread;
	int scd_lwpid;

	int scd_state;
	char *scd_name;

	struct lwpctl scd_lwpctl;

	TAILQ_ENTRY(schedulable) entries;
};
static TAILQ_HEAD(, schedulable) scheds = TAILQ_HEAD_INITIALIZER(scheds);

static struct schedulable mainthread = {
	.scd_lwpid = 1,
	.scd_state = LSRUN,
};
struct tls_tcb *curtcb = &mainthread.scd_tls;

struct tls_tcb *_lwp_rumprun_gettcb(void);
struct tls_tcb *
_lwp_rumprun_gettcb(void)
{

	return curtcb;
}

int
_lwp_ctl(int ctl, struct lwpctl **data)
{
	struct schedulable *scd = (struct schedulable *)curtcb;

	*data = (struct lwpctl *)&scd->scd_lwpctl;
	return 0;
}

int
rumprunposix_makelwp(void (*start)(void *), void *arg, void *private,
	void *stack_base, size_t stack_size, unsigned long flag, lwpid_t *lid)
{

	return ENOSYS;
}

static struct schedulable *
lwpid2scd(lwpid_t lid)
{
	struct schedulable *scd;

	TAILQ_FOREACH(scd, &scheds, entries) {
		if (scd->scd_lwpid == lid)
			return scd;
	}
	return NULL;
}

int
_lwp_unpark(lwpid_t lid, const void *hint)
{
	struct schedulable *scd;

	DPRINTF(("lwp unpark %d\n", lid));
	if ((scd = lwpid2scd(lid)) == NULL) {
		return -1;
	}

	scd->scd_state = LSRUN;
	return 0;
}

ssize_t
_lwp_unpark_all(const lwpid_t *targets, size_t ntargets, const void *hint)
{
	ssize_t rv;

	if (targets == NULL)
		return 1024;

	/*
	 * XXX: this it not 100% correct (unmarking has memory), but good
	 * enuf for now
	 */
	rv = ntargets;
	while (ntargets--) {
		if (_lwp_unpark(*targets, NULL) != 0)
			rv--;
		targets++;
	}
	//assert(rv >= 0);
	return rv;
}

void
_lwp_rumprun_scheduler_init(void)
{
	struct schedulable *scd = &mainthread;

	TAILQ_INSERT_TAIL(&scheds, scd, entries);
	scd->scd_lwpctl.lc_curcpu = 0;
}

static void
_lwp_rumprun_scheduler(void)
{

	/* unreachable */
	abort();
}

int
___lwp_park60(clockid_t clock_id, int flags, const struct timespec *ts,
	lwpid_t unpark, const void *hint, const void *unparkhint)
{
	struct schedulable *current = (struct schedulable *)curtcb;

	if (ts) {
		printf("timed sleeps not supported\n");
		abort();
	}

	if (unpark)
		_lwp_unpark(unpark, unparkhint);

	current->scd_state = LSSLEEP;
	_lwp_rumprun_scheduler();
	return 0;
}

void
_lwp_exit(void)
{
	struct schedulable *scd = (struct schedulable *)curtcb;

	scd->scd_state = LSZOMB;
	TAILQ_REMOVE(&scheds, scd, entries);
	scd->scd_lwpctl.lc_curcpu = LWPCTL_CPU_EXITED;
	_lwp_rumprun_scheduler();
}

void
_lwp_continue(lwpid_t lid)
{
	struct schedulable *scd;

	if ((scd = lwpid2scd(lid)) != NULL)
		scd->scd_state = LSRUN;
}

void
_lwp_suspend(lwpid_t lid)
{
	struct schedulable *scd;

	if ((scd = lwpid2scd(lid)) != NULL)
		scd->scd_state = LSSUSPENDED;
}

int
_lwp_wakeup(lwpid_t lid)
{
	struct schedulable *scd;

	if ((scd = lwpid2scd(lid)) == NULL)
		return ESRCH;

	if (scd->scd_state == LSSLEEP) {
		scd->scd_state = LSRUN;
		return 0;
	}
	return ENODEV;
}

int
_lwp_setname(lwpid_t lid, const char *name)
{
	struct schedulable *scd;
	char *newname, *oldname;
	size_t nlen;

	if ((scd = lwpid2scd(lid)) == NULL)
		return ESRCH;

	nlen = strlen(name)+1;
	if (nlen > MAXCOMLEN)
		nlen = MAXCOMLEN;
	newname = malloc(nlen);
	if (newname == NULL)
		return ENOMEM;
	memcpy(newname, name, nlen-1);
	newname[nlen-1] = '\0';

	oldname = scd->scd_name;
	scd->scd_name = newname;
	if (oldname) {
		free(oldname);
	}

	return 0;
}

lwpid_t
_lwp_self(void)
{
	struct schedulable *current = (struct schedulable *)curtcb;

	return current->scd_lwpid;
}

void
_sched_yield(void)
{

	_lwp_rumprun_scheduler();
}

struct tls_tcb *
_rtld_tls_allocate(void)
{

	return malloc(sizeof(struct schedulable));
}

void
_rtld_tls_free(struct tls_tcb *arg)
{

	free(arg);
}

void _lwpnullop(void);
void _lwpnullop(void) { }

__strong_alias(___sigprocmask14,_lwpnullop);
__strong_alias(___nanosleep50,_lwpnullop);
__strong_alias(_write,_lwpnullop);

__strong_alias(pthread__cancel_stub_binder,_lwpnullop);

__strong_alias(__libc_static_tls_setup,_lwpnullop);

int rasctl(void);
int rasctl(void) { return ENOSYS; }

/*
 * There is ongoing work to support these in the rump kernel,
 * so I will just stub them out for now.
 */
__strong_alias(_sched_getaffinity,_lwpnullop);
__strong_alias(_sched_getparam,_lwpnullop);
__strong_alias(_sched_setaffinity,_lwpnullop);
__strong_alias(_sched_setparam,_lwpnullop);
