#include "msr.h"
#include <math.h>
#include <stdio.h>

void read_turbo_limit()
{
	uint64_t turbo_limit;
	read_msr_by_coord(0, 0, 0, MSR_TURBO_RATIO_LIMIT, &turbo_limit);

	printf("1 core: %x\n", (unsigned) (turbo_limit & 0xFF));
	printf("2 core: %x\n", (unsigned) ((turbo_limit >> 8) & 0xFF));
	printf("3 core: %x\n", (unsigned) ((turbo_limit >> 8) & 0xFF));
	printf("4 core: %x\n", (unsigned) ((turbo_limit >> 8) & 0xFF));
}

unsigned get_turbo_limit()
{
	uint64_t turbo_limit;
	read_msr_by_coord(0, 0, 0, MSR_TURBO_RATIO_LIMIT, &turbo_limit);
	return turbo_limit;// & 0xFF;
}

void set_turbo_limit(unsigned int limit)
{
	uint64_t turbo_limit;
	limit &= 0xFF;
	turbo_limit = 0x0 | (limit) | (limit << 8) | (limit << 16) | (limit << 24);
	//printf("set turbo limit %lx\n", (unsigned long) turbo_limit);
	write_msr_by_coord(0, 0, 0, MSR_TURBO_RATIO_LIMIT, turbo_limit);
}

void set_all_turbo_limit(uint64_t limit)
{
	write_msr_by_coord(0, 0, 0, MSR_TURBO_RATIO_LIMIT, limit);
}

void set_rapl(unsigned sec, double watts, double pu, double su, unsigned affinity)
{
	uint64_t power = (unsigned long) (watts / pu);
	uint64_t seconds;
	uint64_t timeval_y = 0, timeval_x = 0;
	double logremainder = 0;

	timeval_y = (uint64_t) log2(sec / su);
	// store the mantissa of the log2
	logremainder = (double) log2(sec / su) - (double) timeval_y;
	timeval_x = 0;
	// based on the mantissa, we can choose the appropriate multiplier
	if (logremainder > 0.15 && logremainder <= 0.45)
	{
		timeval_x = 1;
	}
	else if (logremainder > 0.45 && logremainder <= 0.7)
	{
		timeval_x = 2;
	}
	else if (logremainder > 0.7)
	{
		timeval_x = 3;
	}
	// store the bits in the Intel specified format
	seconds = (uint64_t) (timeval_y | (timeval_x << 5));
	uint64_t rapl = 0x0 | power | (seconds << 17);

	rapl |= (1LL << 15) | (1LL << 16);
	write_msr_by_coord(0, 0, 0, MSR_PKG_POWER_LIMIT, rapl);
}

void get_rapl_units(double *power_unit, double *seconds_unit)
{
	uint64_t unit;
	read_msr_by_coord(0, 0, 0, MSR_RAPL_POWER_UNIT, &unit);
	uint64_t pu = unit & 0xF;
	*power_unit = 1.0 / (0x1 << pu);
	fprintf(stderr, "power unit: %lx\n", pu);
	uint64_t su = (unit >> 16) & 0x1F;
	*seconds_unit = 1.0 / (0x1 << su);
	fprintf(stderr, "seconds unit: %lx\n", su);
}

void set_perf(const unsigned freq, const unsigned tid)
{
	static uint64_t perf_ctl = 0x0ul;
	uint64_t freq_mask = 0x0ul;
	if (perf_ctl == 0x0ul)
	{
		read_msr_by_coord(0, tid, 0, IA32_PERF_CTL, &perf_ctl);
	}
	perf_ctl &= 0xFFFFFFFFFFFF0000ul;
	freq_mask = freq;
	freq_mask <<= 8;
	perf_ctl |= freq_mask;
	write_msr_by_coord(0, tid, 0, IA32_PERF_CTL, perf_ctl);
}

void enable_turbo(const unsigned tid)
{
	uint64_t perf_ctl;
	read_msr_by_coord(0, tid, 0, IA32_PERF_CTL, &perf_ctl);
	perf_ctl &= 0xFFFFFFFEFFFFFFFFul;
	write_msr_by_coord(0, tid, 0, IA32_PERF_CTL, perf_ctl);
}

void disable_turbo(const unsigned tid)
{
	uint64_t perf_ctl;
	read_msr_by_coord(0, tid, 0, IA32_PERF_CTL, &perf_ctl);
	perf_ctl |= 0x0000000100000000ul;
	write_msr_by_coord(0, tid, 0, IA32_PERF_CTL, perf_ctl);
}

inline void disable_rapl()
{
	uint64_t rapl_disable = 0x0ul;
	read_msr_by_coord(0, 0, 0, MSR_PKG_POWER_LIMIT, &rapl_disable);
	rapl_disable &= 0xFFFFFFFFFFFE7FFFul;
	rapl_disable |= 0x1FFFul;
	write_msr_by_coord(0, 0, 0, MSR_PKG_POWER_LIMIT, rapl_disable);
}

void dump_rapl()
{
	uint64_t rapl;
	read_msr_by_coord(0, 0, 0, MSR_PKG_POWER_LIMIT, &rapl);
	fprintf(stderr, "rapl is %lx\n", (unsigned long) rapl);
}
