#include <ctype.h>
#include "scp.h"

extern int32 sim_interval;
extern UNIT *sim_clock_queue;


t_stat sim_process_event (void);
t_stat sim_activate (UNIT *uptr, int32 interval);
t_stat _sim_activate (UNIT *uptr, int32 interval);
t_stat sim_activate_abs (UNIT *uptr, int32 interval);
t_stat sim_activate_notbefore (UNIT *uptr, int32 rtime);
t_stat sim_activate_after (UNIT *uptr, uint32 usecs_walltime);
t_stat sim_activate_after_d (UNIT *uptr, double usecs_walltime);
t_stat _sim_activate_after (UNIT *uptr, double usecs_walltime);
t_stat sim_activate_after_abs (UNIT *uptr, uint32 usecs_walltime);
t_stat sim_activate_after_abs_d (UNIT *uptr, double usecs_walltime);
t_stat _sim_activate_after_abs (UNIT *uptr, double usecs_walltime);
t_stat sim_cancel (UNIT *uptr);
t_bool sim_is_active (UNIT *uptr);
int32 sim_activate_time (UNIT *uptr);
int32 _sim_activate_queue_time (UNIT *uptr);
int32 _sim_activate_time (UNIT *uptr);
double sim_activate_time_usecs (UNIT *uptr);
t_stat sim_run_boot_prep (int32 flag);
double sim_gtime (void);
uint32 sim_grtime (void);
int32 sim_qcount (void);

extern int sim_is_running;
