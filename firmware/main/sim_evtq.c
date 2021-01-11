#include "sim_defs.h"
#include "sim_evtq.h"

UNIT *sim_clock_queue = QUEUE_LIST_END;
int stop_cpu=0;
#define UPDATE_SIM_TIME
#define SIM_DBG_ACTIVATE 0
#define SIM_DBG_EVENT 0
int32_t sim_interval=1000;
int32_t noqueue_time=0;
int sim_processing_event;
double sim_time=0, sim_rtime=0;
int sim_is_running=1;

static const char *sim_evq_description (DEVICE *dptr)
{
return "SCP Event Processing";
}

static UNIT evq_unit;

DEVICE sim_evq_dev = {
    "EVQ-PROCESS", &evq_unit, NULL, NULL, 
    1, 0, 0, 0, 0, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL, 
    NULL, DEV_NOSAVE|DEV_DEBUG, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL,
    sim_evq_description};

/* Event queue package

        sim_activate            add entry to event queue
        sim_activate_abs        add entry to event queue even if event already scheduled
        sim_activate_notbefore  add entry to event queue even if event already scheduled
                                but not before the specified time
        sim_activate_after      add entry to event queue after a specified amount of wall time
        sim_cancel              remove entry from event queue
        sim_process_event       process entries on event queue
        sim_is_active           see if entry is on event queue
        sim_activate_time       return time until activation
        sim_atime               return absolute time for an entry
        sim_gtime               return global time
        sim_qcount              return event queue entry count

   Asynchronous events are set up by queueing a unit data structure
   to the event queue with a timeout (in simulator units, relative
   to the current time).  Each simulator 'times' these events by
   counting down interval counter sim_interval.  When this reaches
   zero the simulator calls sim_process_event to process the event
   and to see if further events need to be processed, or sim_interval
   reset to count the next one.

   The event queue is maintained in clock order; entry timeouts are
   RELATIVE to the time in the previous entry.

   sim_process_event - process event

   Inputs:
        none
   Outputs:
        reason  =       reason code returned by any event processor,
                        or 0 (SCPE_OK) if no exceptions
*/

t_stat sim_process_event (void)
{
UNIT *uptr;
t_stat reason, bare_reason;

if (stop_cpu) {                                         /* stop CPU? */
    stop_cpu = 0;
    return SCPE_STOP;
    }
AIO_UPDATE_QUEUE;
UPDATE_SIM_TIME;                                        /* update sim time */

if (sim_clock_queue == QUEUE_LIST_END) {                /* queue empty? */
    sim_interval = noqueue_time = NOQUEUE_WAIT;         /* flag queue empty */
    sim_debug (SIM_DBG_EVENT, &sim_evq_dev, "Queue Empty New Interval = %d\n", sim_interval);
    return SCPE_OK;
    }
sim_processing_event = TRUE;
do {
    uptr = sim_clock_queue;                             /* get first */
    sim_clock_queue = uptr->next;                       /* remove first */
    uptr->next = NULL;                                  /* hygiene */
    sim_interval -= uptr->time;
    uptr->time = 0;
    if (sim_clock_queue != QUEUE_LIST_END)
        sim_interval += sim_clock_queue->time;
    else
        sim_interval = noqueue_time = NOQUEUE_WAIT;
    AIO_EVENT_BEGIN(uptr);
    if (uptr->usecs_remaining) {
        sim_debug (SIM_DBG_EVENT, &sim_evq_dev, "Requeueing %s after %.0f usecs\n", sim_uname (uptr), uptr->usecs_remaining);
        reason = sim_timer_activate_after (uptr, uptr->usecs_remaining);
        }
    else {
        sim_debug (SIM_DBG_EVENT, &sim_evq_dev, "Processing Event for %s\n", sim_uname (uptr));
        if (uptr->action != NULL)
            reason = uptr->action (uptr);
        else
            reason = SCPE_OK;
        }
    AIO_EVENT_COMPLETE(uptr, reason);
    bare_reason = SCPE_BARE_STATUS (reason);
    if ((bare_reason != SCPE_OK)      && /* Provide context for unexpected errors */
        (bare_reason >= SCPE_BASE)    &&
        (bare_reason != SCPE_EXPECT)  &&
        (bare_reason != SCPE_REMOTE)  &&
        (bare_reason != SCPE_MTRLNT)  && 
        (bare_reason != SCPE_STOP)    && 
        (bare_reason != SCPE_STEP)    && 
        (bare_reason != SCPE_RUNTIME) && 
        (bare_reason != SCPE_EXIT))
        sim_messagef (reason, "\nUnexpected internal error while processing event for %s which returned %d - %s\n", sim_uname (uptr), reason, sim_error_text (reason));
    } while ((reason == SCPE_OK) && 
             (sim_interval <= 0) && 
             (sim_clock_queue != QUEUE_LIST_END) &&
             (!stop_cpu));

if (sim_clock_queue == QUEUE_LIST_END) {                /* queue empty? */
    sim_interval = noqueue_time = NOQUEUE_WAIT;         /* flag queue empty */
    sim_debug (SIM_DBG_EVENT, &sim_evq_dev, "Processing Queue Complete New Interval = %d\n", sim_interval);
    }
else
    sim_debug (SIM_DBG_EVENT, &sim_evq_dev, "Processing Queue Complete New Interval = %d(%s)\n", sim_interval, sim_uname(sim_clock_queue));

if ((reason == SCPE_OK) && stop_cpu) {
    stop_cpu = FALSE;
    reason = SCPE_STOP;
    }
sim_processing_event = FALSE;
return reason;
}

/* sim_activate - activate (queue) event

   Inputs:
        uptr    =       pointer to unit
        event_time =    relative timeout
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate (UNIT *uptr, int32 event_time)
{
if (uptr->dynflags & UNIT_TMR_UNIT)
    return sim_timer_activate (uptr, event_time);
return _sim_activate (uptr, event_time);
}

t_stat _sim_activate (UNIT *uptr, int32 event_time)
{
UNIT *cptr, *prvptr;
int32 accum;

AIO_ACTIVATE (_sim_activate, uptr, event_time);
if (sim_is_active (uptr))                               /* already active? */
    return SCPE_OK;
UPDATE_SIM_TIME;                                        /* update sim time */

sim_debug (SIM_DBG_ACTIVATE, &sim_evq_dev, "Activating %s delay=%d\n", sim_uname (uptr), event_time);

prvptr = NULL;
accum = 0;
for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
    if (event_time < (accum + cptr->time))
        break;
    accum = accum + cptr->time;
    prvptr = cptr;
    }
if (prvptr == NULL) {                                   /* insert at head */
    cptr = uptr->next = sim_clock_queue;
    sim_clock_queue = uptr;
    }
else {
    cptr = uptr->next = prvptr->next;                   /* insert at prvptr */
    prvptr->next = uptr;
    }
uptr->time = event_time - accum;
if (cptr != QUEUE_LIST_END)
    cptr->time = cptr->time - uptr->time;
sim_interval = sim_clock_queue->time;
return SCPE_OK;
}

/* sim_activate_abs - activate (queue) event even if event already scheduled

   Inputs:
        uptr    =       pointer to unit
        event_time =    relative timeout
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate_abs (UNIT *uptr, int32 event_time)
{
AIO_ACTIVATE (sim_activate_abs, uptr, event_time);
sim_cancel (uptr);
return _sim_activate (uptr, event_time);
}

/* sim_activate_notbefore - activate (queue) event even if event already scheduled
                            but not before the specified time

   Inputs:
        uptr    =       pointer to unit
        rtime   =       relative timeout
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate_notbefore (UNIT *uptr, int32 rtime)
{
uint32 rtimenow, urtime = (uint32)rtime;

AIO_ACTIVATE (sim_activate_notbefore, uptr, rtime);
sim_cancel (uptr);
rtimenow = sim_grtime();
sim_cancel (uptr);
if (0x80000000 <= urtime-rtimenow)
    return _sim_activate (uptr, 0);
else
    return sim_activate (uptr, urtime-rtimenow);
}

/* sim_activate_after - activate (queue) event

   Inputs:
        uptr    =       pointer to unit
        usec_delay =    relative timeout (in microseconds)
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate_after_abs (UNIT *uptr, uint32 usec_delay)
{
return _sim_activate_after_abs (uptr, (double)usec_delay);
}

t_stat sim_activate_after_abs_d (UNIT *uptr, double usec_delay)
{
return _sim_activate_after_abs (uptr, usec_delay);
}

t_stat _sim_activate_after_abs (UNIT *uptr, double usec_delay)
{
AIO_VALIDATE(uptr);             /* Can't call asynchronously */
sim_cancel (uptr);
return _sim_activate_after (uptr, usec_delay);
}

t_stat sim_activate_after (UNIT *uptr, uint32 usec_delay)
{
return _sim_activate_after (uptr, (double)usec_delay);
}

t_stat sim_activate_after_d (UNIT *uptr, double usec_delay)
{
return _sim_activate_after (uptr, usec_delay);
}

t_stat _sim_activate_after (UNIT *uptr, double usec_delay)
{
AIO_VALIDATE(uptr);             /* Can't call asynchronously */
if (sim_is_active (uptr))       /* already active? */
    return SCPE_OK;
return sim_timer_activate_after (uptr, usec_delay);
}

/* sim_cancel - cancel (dequeue) event

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        reason  =       result (SCPE_OK if ok)

*/

t_stat sim_cancel (UNIT *uptr)
{
UNIT *cptr, *nptr;

AIO_VALIDATE(uptr);
if ((uptr->cancel) && uptr->cancel (uptr))
    return SCPE_OK;
if (uptr->dynflags & UNIT_TMR_UNIT)
    sim_timer_cancel (uptr);
AIO_CANCEL(uptr);
AIO_UPDATE_QUEUE;
if (sim_clock_queue == QUEUE_LIST_END)
    return SCPE_OK;
if (!sim_is_active (uptr))
    return SCPE_OK;
UPDATE_SIM_TIME;                                        /* update sim time */
sim_debug (SIM_DBG_EVENT, &sim_evq_dev, "Canceling Event for %s\n", sim_uname(uptr));
nptr = QUEUE_LIST_END;

if (sim_clock_queue == uptr) {
    nptr = sim_clock_queue = uptr->next;
    uptr->next = NULL;                                  /* hygiene */
    }
else {
    for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
        if (cptr->next == uptr) {
            nptr = cptr->next = uptr->next;
            uptr->next = NULL;                          /* hygiene */
            break;                                      /* end queue scan */
            }
        }
    }
if (nptr != QUEUE_LIST_END)
    nptr->time += (uptr->next) ? 0 : uptr->time;
if (!uptr->next)
    uptr->time = 0;
uptr->usecs_remaining = 0;
if (sim_clock_queue != QUEUE_LIST_END)
    sim_interval = sim_clock_queue->time;
else
    sim_interval = noqueue_time = NOQUEUE_WAIT;
if (uptr->next) {
    sim_printf ("Cancel failed for %s\n", sim_uname(uptr));
    if (sim_deb)
        fclose(sim_deb);
    abort ();
    }
return SCPE_OK;
}

/* sim_is_active - test for entry in queue

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result =        TRUE if unit is busy, FALSE inactive
*/

t_bool sim_is_active (UNIT *uptr)
{
AIO_VALIDATE(uptr);
AIO_UPDATE_QUEUE;
return (((uptr->next) || AIO_IS_ACTIVE(uptr) || ((uptr->dynflags & UNIT_TMR_UNIT) ? sim_timer_is_active (uptr) : FALSE)) ? TRUE : FALSE);
}

/* sim_activate_time - return activation time

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result =        absolute activation time + 1, 0 if inactive
*/

int32 _sim_activate_queue_time (UNIT *uptr)
{
UNIT *cptr;
int32 accum;

accum = 0;
for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
    if (cptr == sim_clock_queue) {
        if (sim_interval > 0)
            accum = accum + sim_interval;
        }
    else
        accum = accum + cptr->time;
    if (cptr == uptr)
        return accum + 1;
    }
return 0;
}

int32 _sim_activate_time (UNIT *uptr)
{
int32 accum = _sim_activate_queue_time (uptr);

if (accum)
    return accum + (int32)((uptr->usecs_remaining * sim_timer_inst_per_sec ()) / 1000000.0);
return 0;
}

int32 sim_activate_time (UNIT *uptr)
{
int32 accum;

AIO_VALIDATE(uptr);
accum = _sim_timer_activate_time (uptr);
if (accum >= 0)
    return accum;
return _sim_activate_time (uptr);
}

double sim_activate_time_usecs (UNIT *uptr)
{
UNIT *cptr;
int32 accum;
double result;

AIO_VALIDATE(uptr);
result = sim_timer_activate_time_usecs (uptr);
if (result >= 0)
    return result;
accum = 0;
for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
    if (cptr == sim_clock_queue) {
        if (sim_interval > 0)
            accum = accum + sim_interval;
        }
    else
        accum = accum + cptr->time;
    if (cptr == uptr)
        return 1.0 + uptr->usecs_remaining + ((1000000.0 * accum) / sim_timer_inst_per_sec ());
    }
return 0.0;
}

/* sim_gtime - return global time
   sim_grtime - return global time with rollover

   Inputs: none
   Outputs:
        time    =       global time
*/

double sim_gtime (void)
{
if (AIO_MAIN_THREAD) {
    UPDATE_SIM_TIME;
    }
return sim_time;
}

uint32 sim_grtime (void)
{
UPDATE_SIM_TIME;
return sim_rtime;
}

/* sim_qcount - return queue entry count

   Inputs: none
   Outputs:
        count   =       number of entries on the queue
*/

int32 sim_qcount (void)
{
int32 cnt;
UNIT *uptr;

cnt = 0;
for (uptr = sim_clock_queue; uptr != QUEUE_LIST_END; uptr = uptr->next)
    cnt++;
return cnt;
}
