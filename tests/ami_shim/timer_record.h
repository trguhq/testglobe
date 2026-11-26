/* Recorded timer.device and amiga.lib port/request calls for host tests that
 * link src/ami_time.c. Include in one translation unit, after defining
 * test_timer_systime(), which answers TR_GETSYSTIME. Not an NDK or ABI shim. */
#ifndef TEST_TIMER_RECORD_H
#define TEST_TIMER_RECORD_H
#include <assert.h>
#include <string.h>
#include <Amiga.h>

static struct Device timer_device;
static struct MsgPort timer_port;
static struct timerequest timer_request;
/* Version 36 introduced ReadEClock; 1.3 answers only TR_GETSYSTIME. */
static unsigned int timer_version = 39;
static unsigned int timer_fail; /* 1: port, 2: request, 3: OpenDevice */
static unsigned int timer_ports, timer_requests, timer_opens;
static unsigned int timer_systime_calls, timer_eclock_calls;
static ULONG timer_eclock_hi, timer_eclock_lo;
static ULONG timer_eclock_step = 11823UL, timer_eclock_rate = 709379UL;

static int timer_idle(void)
{
    return !timer_ports && !timer_requests && !timer_opens;
}

/* The seconds an E-clock sample with these words represents. */
static double timer_eclock_seconds(void)
{
    return ((double)timer_eclock_hi * 4294967296.0 + (double)timer_eclock_lo) /
           (double)timer_eclock_rate;
}

struct MsgPort *CreatePort(CONST_STRPTR name, LONG priority)
{
    assert(name == NULL && priority == 0 && timer_idle());
    if (timer_fail == 1U) return NULL;
    ++timer_ports;
    return &timer_port;
}

void DeletePort(struct MsgPort *port)
{
    assert(port == &timer_port && timer_ports == 1U && !timer_requests);
    --timer_ports;
}

struct IORequest *CreateExtIO(struct MsgPort *port, LONG size)
{
    assert(port == &timer_port && timer_ports == 1U && !timer_requests);
    assert(size == (LONG)sizeof(struct timerequest));
    if (timer_fail == 2U) return NULL;
    memset(&timer_request, 0, sizeof(timer_request));
    timer_request.tr_node.io_Message.mn_ReplyPort = port;
    ++timer_requests;
    return &timer_request.tr_node;
}

void DeleteExtIO(struct IORequest *request)
{
    assert(request == &timer_request.tr_node && timer_requests == 1U && !timer_opens);
    --timer_requests;
}

BYTE OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *request, ULONG flags)
{
    assert(strcmp(name, "timer.device") == 0 && unit == UNIT_VBLANK && flags == 0UL);
    assert(request == &timer_request.tr_node && timer_requests == 1U && !timer_opens);
    assert(request->io_Message.mn_ReplyPort == &timer_port);
    if (timer_fail == 3U)
    {
        request->io_Error = -1;
        return -1;
    }
    timer_device.dd_Library.lib_Version = (UWORD)timer_version;
    request->io_Device = &timer_device;
    ++timer_opens;
    return 0;
}

void CloseDevice(struct IORequest *request)
{
    assert(request == &timer_request.tr_node && timer_opens == 1U);
    --timer_opens;
}

static void test_timer_systime(ULONG *seconds, ULONG *micros);

BYTE DoIO(struct IORequest *request)
{
    assert(request == &timer_request.tr_node && timer_opens == 1U);
    /* V36 and later read the monotonic E-clock instead of system time. */
    assert(request->io_Command == TR_GETSYSTIME && timer_version < 36U);
    ++timer_systime_calls;
    test_timer_systime(&timer_request.tr_time.tv_secs, &timer_request.tr_time.tv_micro);
    return 0;
}

ULONG ami_clock_eclock(struct Device *base, struct EClockVal *value)
{
    assert(base == &timer_device && timer_opens == 1U && timer_version >= 36U);
    ++timer_eclock_calls;
    timer_eclock_lo = (timer_eclock_lo + timer_eclock_step) & 0xffffffffUL;
    if (timer_eclock_lo < timer_eclock_step) timer_eclock_hi = (timer_eclock_hi + 1UL) & 0xffffffffUL;
    value->ev_hi = timer_eclock_hi;
    value->ev_lo = timer_eclock_lo;
    return timer_eclock_rate;
}

#endif /* TEST_TIMER_RECORD_H */
