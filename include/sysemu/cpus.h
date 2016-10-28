#ifndef QEMU_CPUS_H
#define QEMU_CPUS_H

/* cpus.c */
bool qemu_in_vcpu_thread(void);
void qemu_init_cpu_loop(void);
void resume_all_vcpus(void);
void pause_all_vcpus(void);
void cpu_stop_current(void);
bool all_cpu_threads_idle(void);

void cpu_synchronize_all_states(void);
void cpu_synchronize_all_post_reset(void);
void cpu_synchronize_all_post_init(void);

void tcg_clock_warp(int64_t dest);
bool tcg_idle_clock_warp(int64_t dest);
void qtest_clock_warp(int64_t dest);

void qemu_icount_enable_idle_timewarps(bool enable);

#ifndef CONFIG_USER_ONLY
/* vl.c */
extern int smp_cores;
extern int smp_threads;
#else
/* *-user doesn't have configurable SMP topology */
#define smp_cores   1
#define smp_threads 1
#endif

void list_cpus(FILE *f, fprintf_function cpu_fprintf, const char *optarg);

#endif
