#include "master.h"
#include "msr_core.h"

#define MSR_TURBO_RATIO_LIMIT 0x1AD

void read_turbo_limit();
void set_turbo_limit(unsigned int limit);
void set_all_turbo_limit(uint64_t limit);
unsigned get_turbo_limit();
void enable_turbo(const unsigned tid);
void disable_turbo(const unsigned tid);

void get_rapl_units(double *power_unit, double *seconds_unit);
void set_rapl(unsigned sec, double watts, double pu, double su, unsigned affinity);
void set_perf(const unsigned freq, const unsigned tid);
void dump_rapl();
//inline void disable_rapl();
