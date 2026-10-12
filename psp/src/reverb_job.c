/* Runs on ME. No OS, allocation, global mixer state, logging or VFPU. */
#include <stdint.h>
#include "reverb_job.h"
int reverb_job_worker(int param)
{return reverb_job_execute((const ReverbJob *)(uintptr_t)(unsigned)param);}
