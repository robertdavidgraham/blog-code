/*
    This grabs benchmark/performance numbers using CPU hardware counters.
    It grabs:
        - elapsed time
        - instructions executed
        - CPU clock cycles
        - branches
        - branch misses
        - L1D cache misses
    This is for trying to compare various algorithms.
 
    It's totally vibe coded. I understand very little how it works.
    The macOS counters seem unreliable.
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 199309L

#include "bench.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(__linux__)
  #include <sys/syscall.h>
  #include <unistd.h>
  #include <sys/ioctl.h>
  #include <linux/perf_event.h>
  #include <time.h>
#elif defined(__APPLE__)
  #include <dlfcn.h>
  #include <mach/mach_time.h>
#elif defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <stdio.h>
#endif

struct bench_ctx {
  /* time */
#if defined(__linux__)
  struct timespec ts0;
#elif defined(__APPLE__)
  uint64_t t0;
  mach_timebase_info_data_t tbi;
#elif defined(_WIN32)
  LARGE_INTEGER qpc0, qpc_freq;
#endif

  /* counters */
#if defined(__linux__)
  int fd_leader, fd_insn, fd_brmiss, fd_l1d, fd_branches;
#elif defined(__APPLE__)
  void *h_kperf, *h_kperfdata;

  int  (*kpc_force_all_ctrs_get)(int*);
  int  (*kpc_force_all_ctrs_set)(int);
  int  (*kpc_set_counting)(uint32_t);
  int  (*kpc_set_thread_counting)(uint32_t);
  int  (*kpc_get_thread_counters)(uint32_t, uint32_t, uint64_t*);
  int  (*kpc_set_config)(uint32_t, uint64_t*);
  uint32_t (*kpc_get_counter_count)(uint32_t);

  int  (*kpep_db_create)(const char*, void**);
  void (*kpep_db_free)(void*);
  int  (*kpep_db_event)(void*, const char*, void**);

  int  (*kpep_config_create)(void*, void**);
  void (*kpep_config_free)(void*);
  int  (*kpep_config_force_counters)(void*);
  int  (*kpep_config_add_event)(void*, void**, uint32_t, void*);
  int  (*kpep_config_kpc_classes)(void*, uint32_t*);
  int  (*kpep_config_kpc_count)(void*, size_t*);
  int  (*kpep_config_kpc)(void*, uint64_t*, size_t);
  int  (*kpep_config_kpc_map)(void*, size_t*, size_t);

  uint32_t classes;
  uint32_t ctr_count;
  size_t   reg_count;

  int map_cycles, map_insn, map_brmiss, map_l1d, map_branches;

  uint64_t c0[32];

#elif defined(_WIN32)
  DWORD tid;
  char tmp_dir[MAX_PATH];
  char etl0[MAX_PATH];
  char etl_merged[MAX_PATH];
  char txt_out[MAX_PATH];
  const char *session;
#endif
};

static void zero_result(bench_result_t *r){ memset(r,0,sizeof(*r)); }

/* ---------------- Linux perf_event_open ---------------- */
#if defined(__linux__)

static long perf_open(struct perf_event_attr *a, int group_fd) {
  return syscall(__NR_perf_event_open, a, 0, -1, group_fd, 0);
}

static int linux_open_group(struct bench_ctx *c) {
  c->fd_leader = c->fd_insn = c->fd_brmiss = c->fd_l1d = c->fd_branches = -1;

  struct perf_event_attr pe;
  memset(&pe,0,sizeof(pe));
  pe.size = sizeof(pe);
  pe.disabled = 1;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;
  pe.inherit = 0;
  pe.read_format = PERF_FORMAT_GROUP;

  /* leader: cycles */
  pe.type = PERF_TYPE_HARDWARE;
  pe.config = PERF_COUNT_HW_CPU_CYCLES;
  c->fd_leader = (int)perf_open(&pe, -1);
  if (c->fd_leader < 0) {
      perror("cycles");
      return -1;
  }
  
  /* instructions */
  memset(&pe,0,sizeof(pe));
  pe.size = sizeof(pe);
  pe.disabled = 0; pe.exclude_kernel = 1; pe.exclude_hv = 1;
  pe.read_format = PERF_FORMAT_GROUP;
  pe.type = PERF_TYPE_HARDWARE;
  pe.config = PERF_COUNT_HW_INSTRUCTIONS;
  c->fd_insn = (int)perf_open(&pe, c->fd_leader);
  if (c->fd_insn < 0) return -1;

  /* branch misses */
  memset(&pe,0,sizeof(pe));
  pe.size = sizeof(pe);
  pe.disabled = 0; pe.exclude_kernel = 1; pe.exclude_hv = 1;
  pe.read_format = PERF_FORMAT_GROUP;
  pe.type = PERF_TYPE_HARDWARE;
  pe.config = PERF_COUNT_HW_BRANCH_MISSES;
  c->fd_brmiss = (int)perf_open(&pe, c->fd_leader);
  if (c->fd_brmiss < 0) return -1;

  /* branch instructions (NEW) */
  memset(&pe,0,sizeof(pe));
  pe.size = sizeof(pe);
  pe.disabled = 0; pe.exclude_kernel = 1; pe.exclude_hv = 1;
  pe.read_format = PERF_FORMAT_GROUP;
  pe.type = PERF_TYPE_HARDWARE;
  pe.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
  c->fd_branches = (int)perf_open(&pe, c->fd_leader);
    if (c->fd_branches < 0) {
        fprintf(stderr, "perf_open(branches): failed\n");
        return -1;
    }

  /* L1D read misses */
  memset(&pe,0,sizeof(pe));
  pe.size = sizeof(pe);
  pe.disabled = 0; pe.exclude_kernel = 1; pe.exclude_hv = 1;
  pe.read_format = PERF_FORMAT_GROUP;
  pe.type = PERF_TYPE_HW_CACHE;
  pe.config =
    (PERF_COUNT_HW_CACHE_L1D) |
    (PERF_COUNT_HW_CACHE_OP_READ << 8) |
    (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
  c->fd_l1d = (int)perf_open(&pe, c->fd_leader);
  if (c->fd_l1d < 0) return -1;

  ioctl(c->fd_leader, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
  ioctl(c->fd_leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
  return 0;
}

static int linux_read_stop(struct bench_ctx *c, bench_result_t *r) {
  if (c->fd_leader < 0) return -1;
  ioctl(c->fd_leader, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);

  /* Open order: cycles(leader), insn, brmiss, branches, l1d */
  struct { uint64_t nr; uint64_t v[16]; } buf;
  ssize_t n = read(c->fd_leader, &buf, sizeof(buf));
  if (n < (ssize_t)sizeof(uint64_t) || buf.nr < 5) return -1;

  r->cycles        = buf.v[0];
  r->instructions  = buf.v[1];
  r->branch_misses = buf.v[2];
  r->branches      = buf.v[3];
  r->l1d_misses    = buf.v[4];

  r->valid_mask |= BENCH_VALID_CYCLES|BENCH_VALID_INSTRUCTIONS|
                   BENCH_VALID_BRANCH_MISSES|BENCH_VALID_BRANCHES|BENCH_VALID_L1D_MISSES;
  return 0;
}

static void linux_close(struct bench_ctx *c) {
  if (c->fd_l1d >= 0) close(c->fd_l1d);
  if (c->fd_branches >= 0) close(c->fd_branches);
  if (c->fd_brmiss >= 0) close(c->fd_brmiss);
  if (c->fd_insn>= 0) close(c->fd_insn);
  if (c->fd_leader>=0) close(c->fd_leader);
  c->fd_leader = c->fd_insn = c->fd_brmiss = c->fd_branches = c->fd_l1d = -1;
}

#endif

/* ---------------- macOS kpc/kpep via dlopen ---------------- */
#if defined(__APPLE__)

static int dlsym_req(void *h, const char *n, void **out){ *out=dlsym(h,n); return *out?0:-1; }

static void mac_unload(struct bench_ctx *c){
  if (c->kpc_set_thread_counting) c->kpc_set_thread_counting(0);
  if (c->kpc_set_counting)        c->kpc_set_counting(0);
  if (c->h_kperfdata) { dlclose(c->h_kperfdata); c->h_kperfdata=NULL; }
  if (c->h_kperf)     { dlclose(c->h_kperf);     c->h_kperf=NULL; }
}

static void* mac_find_event(struct bench_ctx *c, void *db, const char *const *names){
  for (; *names; ++names){
    void *ev=NULL;
    if (c->kpep_db_event(db, *names, &ev)==0 && ev) return ev;
  }
  return NULL;
}

static int mac_init(struct bench_ctx *c){
    c->h_kperf     = dlopen("/System/Library/PrivateFrameworks/kperf.framework/kperf", RTLD_LAZY);
    if (c->h_kperf == NULL) {
        perror("/System/Library/PrivateFrameworks/kperf.framework/kperf");
        return -1;
    }
    c->h_kperfdata = dlopen("/System/Library/PrivateFrameworks/kperfdata.framework/kperfdata", RTLD_LAZY);
    if (c->h_kperfdata == NULL) {
        perror("/System/Library/PrivateFrameworks/kperfdata.framework/kperfdata");
        return -1;
    }


  if (dlsym_req(c->h_kperf,"kpc_force_all_ctrs_get",(void**)&c->kpc_force_all_ctrs_get)) return -1;
  if (dlsym_req(c->h_kperf,"kpc_force_all_ctrs_set",(void**)&c->kpc_force_all_ctrs_set)) return -1;
  if (dlsym_req(c->h_kperf,"kpc_set_counting",(void**)&c->kpc_set_counting)) return -1;
  if (dlsym_req(c->h_kperf,"kpc_set_thread_counting",(void**)&c->kpc_set_thread_counting)) return -1;
  if (dlsym_req(c->h_kperf,"kpc_get_thread_counters",(void**)&c->kpc_get_thread_counters)) return -1;
  if (dlsym_req(c->h_kperf,"kpc_set_config",(void**)&c->kpc_set_config)) return -1;
  if (dlsym_req(c->h_kperf,"kpc_get_counter_count",(void**)&c->kpc_get_counter_count)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_db_create",(void**)&c->kpep_db_create)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_db_free",(void**)&c->kpep_db_free)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_db_event",(void**)&c->kpep_db_event)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_config_create",(void**)&c->kpep_config_create)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_config_free",(void**)&c->kpep_config_free)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_config_force_counters",(void**)&c->kpep_config_force_counters)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_config_add_event",(void**)&c->kpep_config_add_event)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_config_kpc_classes",(void**)&c->kpep_config_kpc_classes)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_config_kpc_count",(void**)&c->kpep_config_kpc_count)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_config_kpc",(void**)&c->kpep_config_kpc)) return -1;
  if (dlsym_req(c->h_kperfdata,"kpep_config_kpc_map",(void**)&c->kpep_config_kpc_map)) return -1;

  int forced=0;
  if (c->kpc_force_all_ctrs_get(&forced)!=0) return -1;
  (void)c->kpc_force_all_ctrs_set(1);

  void *db=NULL;
  if (c->kpep_db_create(NULL,&db)!=0 || !db) return -1;

  void *cfg=NULL;
  if (c->kpep_config_create(db,&cfg)!=0 || !cfg){ c->kpep_db_free(db); return -1; }
  if (c->kpep_config_force_counters(cfg)!=0){ c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }

  /* Event names come from /usr/share/kpep/ *.plist; Apple commonly has INST_BRANCH, Intel has BR_INST_RETIRED.ALL_BRANCHES. */
  static const char *cycles_names[] = {"FIXED_CYCLES","CPU_CLK_UNHALTED.THREAD","CPU_CLK_UNHALTED.CORE",NULL};
  static const char *insn_names[]   = {"FIXED_INSTRUCTIONS","INST_RETIRED.ANY",NULL};
  static const char *brmiss_names[] = {"BRANCH_MISPRED_NONSPEC","BRANCH_MISPREDICT","BR_MISP_RETIRED.ALL_BRANCHES","BR_INST_RETIRED.MISPRED",NULL};
  static const char *branches_names[] = {"INST_BRANCH","BR_INST_RETIRED.ALL_BRANCHES","BR_INST_RETIRED.ALL_BRANCHES_PS",NULL};
  static const char *l1d_names[]    = {"L1D_CACHE_MISS_LD",
      "DCACHE_LOAD_MISS","L1D_CACHE_MISS_LD_NONSPEC","L1D_CACHE_MISS_LD","L1D_CACHE_MISS","MEM_LOAD_RETIRED.L1_MISS","L1D.REPLACEMENT",
      "CYCLE_ACTIVITY.STALLS_L1D_MISS", NULL};

  void *ev_cycles   = mac_find_event(c, db, cycles_names);
  void *ev_insn     = mac_find_event(c, db, insn_names);
  void *ev_brmiss   = mac_find_event(c, db, brmiss_names);
  void *ev_branches = mac_find_event(c, db, branches_names);
  void *ev_l1d      = mac_find_event(c, db, l1d_names);

  if (!ev_cycles||!ev_insn||!ev_brmiss||!ev_branches||!ev_l1d){
    c->kpep_config_free(cfg); c->kpep_db_free(db); return -1;
  }

    if (c->kpep_config_add_event(cfg,&ev_cycles,0,NULL)!=0) {
        fprintf(stderr, "[-] cycle count disabled\n");
    }
    if (c->kpep_config_add_event(cfg,&ev_insn,0,NULL)!=0) {
        fprintf(stderr, "[-] instruction count disabled\n");
    }
    if (c->kpep_config_add_event(cfg,&ev_brmiss,0,NULL)!=0) {
        fprintf(stderr, "[-] branch miss disabled\n");
    }
    if (c->kpep_config_add_event(cfg,&ev_branches,0,NULL)!=0) {
        fprintf(stderr, "[-] branch count disabled\n");
    }
    if (c->kpep_config_add_event(cfg,&ev_l1d,0,NULL)!=0) {
        //c->kpep_config_free(cfg); c->kpep_db_free(db); return -1;
        //fprintf(stderr, "[-] ltd miss disabled\n");
    }

  if (c->kpep_config_kpc_classes(cfg,&c->classes)!=0) { c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }
  if (c->kpep_config_kpc_count(cfg,&c->reg_count)!=0) { c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }
  if (c->reg_count > 32) { c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }

  uint64_t regs[32]; size_t map[32];
  memset(regs,0,sizeof(regs)); memset(map,0,sizeof(map));

  if (c->kpep_config_kpc(cfg, regs, c->reg_count * sizeof(uint64_t))!=0) { c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }
  if (c->kpep_config_kpc_map(cfg, map, 32 * sizeof(size_t))!=0)          { c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }

  if (c->kpc_set_config(c->classes, regs)!=0) { c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }
  if (c->kpc_set_counting(c->classes)!=0)     { c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }
  if (c->kpc_set_thread_counting(c->classes)!=0){ c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }

  c->ctr_count = c->kpc_get_counter_count(c->classes);
  if (c->ctr_count==0 || c->ctr_count>32){ c->kpep_config_free(cfg); c->kpep_db_free(db); return -1; }

  /* Added in this order: cycles=0, insn=1, brmiss=2, branches=3, l1d=4 */
  c->map_cycles   = (int)map[0];
  c->map_insn     = (int)map[1];
  c->map_brmiss   = (int)map[2];
  c->map_branches = (int)map[3];
  c->map_l1d      = (int)map[4];

  c->kpep_config_free(cfg);
  c->kpep_db_free(db);

  memset(c->c0,0,sizeof(c->c0));
  if (c->kpc_get_thread_counters(0, c->ctr_count, c->c0)!=0) return -1;

  return 0;
}

#endif

/* ---------------- Windows: tracelog/xperf session + parse text dump ---------------- */
#if defined(_WIN32)

static int run_cmd_hidden(const char *cmdline) {
  STARTUPINFOA si; PROCESS_INFORMATION pi;
  memset(&si,0,sizeof(si)); memset(&pi,0,sizeof(pi));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;

  char *buf = _strdup(cmdline);
  if (!buf) return -1;

  BOOL ok = CreateProcessA(NULL, buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  if (!ok) { free(buf); return -1; }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD ec = 0;
  GetExitCodeProcess(pi.hProcess, &ec);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  free(buf);
  return (ec == 0) ? 0 : -1;
}

/* We set PMC order explicitly in tracelog command:
   BranchInstructions,BranchMispredictions,InstructionRetired,TotalCycles,CacheMisses
*/
static int parse_pmc_text_5(const char *path, DWORD tid,
                            uint64_t *out_branches,
                            uint64_t *out_brmiss,
                            uint64_t *out_insn,
                            uint64_t *out_cycles,
                            uint64_t *out_cache)
{
  FILE *f = fopen(path, "rb");
  if (!f) return -1;

  uint64_t fb=0, fbm=0, fi=0, fc=0, fca=0;
  uint64_t lb=0, lbm=0, li=0, lc=0, lca=0;
  int have_first = 0;

  char line[4096];
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "                        Pmc,", 28) != 0 &&
        strncmp(line, "Pmc,", 4) != 0) continue;

    unsigned long long ts=0, t=0;
    unsigned long long v1=0, v2=0, v3=0, v4=0, v5=0;

    int n = sscanf(line, " %*[^,], %llu, %llu, %llu, %llu, %llu, %llu, %llu",
                   &ts, &t, &v1, &v2, &v3, &v4, &v5);
    if (n < 7) continue;
    if ((DWORD)t != tid) continue;

    uint64_t branches = (uint64_t)v1;
    uint64_t brmiss   = (uint64_t)v2;
    uint64_t insn     = (uint64_t)v3;
    uint64_t cycles   = (uint64_t)v4;
    uint64_t cache    = (uint64_t)v5;

    if (!have_first) {
      fb=branches; fbm=brmiss; fi=insn; fc=cycles; fca=cache;
      have_first = 1;
    }
    lb=branches; lbm=brmiss; li=insn; lc=cycles; lca=cache;
  }

  fclose(f);
  if (!have_first) return -1;

  *out_branches = lb  - fb;
  *out_brmiss   = lbm - fbm;
  *out_insn     = li  - fi;
  *out_cycles   = lc  - fc;
  *out_cache    = lca - fca;
  return 0;
}

#endif

/* ---------------- Public API ---------------- */

bench_ctx* bench_start(void) {
  struct bench_ctx *c = (struct bench_ctx*)calloc(1, sizeof(*c));
  if (!c) return NULL;

#if defined(__linux__)
    clock_gettime(CLOCK_MONOTONIC, &c->ts0);
    if (linux_open_group(c) != 0) {
        //fprintf(stderr, "[-] failed CPU counters, time only\n");
        linux_close(c); /* time-only */
    }

#elif defined(__APPLE__)
    mach_timebase_info(&c->tbi);
    if (mac_init(c) != 0) {
        fprintf(stderr, "mac_init(): failed\n");
        mac_unload(c); /* time-only */
        }
    c->t0 = mach_absolute_time();

#elif defined(_WIN32)
  QueryPerformanceFrequency(&c->qpc_freq);
  QueryPerformanceCounter(&c->qpc0);
  c->tid = GetCurrentThreadId();
  c->session = "benchpmc";

  DWORD n = GetTempPathA(MAX_PATH, c->tmp_dir);
  if (n==0 || n>=MAX_PATH) { free(c); return NULL; }

  snprintf(c->etl0, sizeof(c->etl0), "%sbenchpmc_raw.etl", c->tmp_dir);
  snprintf(c->etl_merged, sizeof(c->etl_merged), "%sbenchpmc_merged.etl", c->tmp_dir);
  snprintf(c->txt_out, sizeof(c->txt_out), "%sbenchpmc_dump.txt", c->tmp_dir);

  char cmd[2048];
  snprintf(cmd, sizeof(cmd), "xperf -stop %s >NUL 2>NUL", c->session);
  (void)run_cmd_hidden(cmd);

  /* Start kernel trace with PMC counters on CSWITCH. Counter names are “profile sources”. :contentReference[oaicite:2]{index=2} */
  snprintf(cmd, sizeof(cmd),
           "tracelog.exe -start %s -f \"%s\" -eflag CSWITCH+PROC_THREAD+LOADER "
           "-PMC BranchInstructions,BranchMispredictions,InstructionRetired,TotalCycles,CacheMisses:CSWITCH",
           c->session, c->etl0);
  (void)run_cmd_hidden(cmd); /* if it fails, you'll get time-only */

#endif

  return c;
}

bench_result_t bench_stop(bench_ctx* c) {
    bench_result_t r;
    zero_result(&r);
    //if (!c) { r.backend_error = -1; return r; }

#if defined(__linux__)
  struct timespec ts1;
  clock_gettime(CLOCK_MONOTONIC, &ts1);
  time_t ds = ts1.tv_sec - c->ts0.tv_sec;
  long dn = ts1.tv_nsec - c->ts0.tv_nsec;
  if (dn < 0) { dn += 1000000000L; ds -= 1; }
  r.elapsed_seconds = (double)ds + (double)dn * 1e-9;
  r.valid_mask |= BENCH_VALID_TIME;

  if (c->fd_leader >= 0) {
    if (linux_read_stop(c, &r) != 0) r.backend_error = -2;
  }
  linux_close(c);

#elif defined(__APPLE__)
  uint64_t t1 = mach_absolute_time();
  long double ns = (long double)(t1 - c->t0) * (long double)c->tbi.numer / (long double)c->tbi.denom;
  r.elapsed_seconds = (double)(ns * 1e-9L);
  r.valid_mask |= BENCH_VALID_TIME;

  if (c->kpc_get_thread_counters && c->ctr_count) {
    uint64_t c1[32]; memset(c1,0,sizeof(c1));
    if (c->kpc_get_thread_counters(0, c->ctr_count, c1)==0) {
      if (c->map_cycles>=0   && c->map_cycles<(int)c->ctr_count)   { r.cycles = c1[c->map_cycles]-c->c0[c->map_cycles]; r.valid_mask |= BENCH_VALID_CYCLES; }
      if (c->map_insn>=0     && c->map_insn<(int)c->ctr_count)     { r.instructions = c1[c->map_insn]-c->c0[c->map_insn]; r.valid_mask |= BENCH_VALID_INSTRUCTIONS; }
      if (c->map_brmiss>=0   && c->map_brmiss<(int)c->ctr_count)   { r.branch_misses = c1[c->map_brmiss]-c->c0[c->map_brmiss]; r.valid_mask |= BENCH_VALID_BRANCH_MISSES; }
      if (c->map_branches>=0 && c->map_branches<(int)c->ctr_count) { r.branches = c1[c->map_branches]-c->c0[c->map_branches]; r.valid_mask |= BENCH_VALID_BRANCHES; }
      if (c->map_l1d>=0      && c->map_l1d<(int)c->ctr_count)      { r.l1d_misses = c1[c->map_l1d]-c->c0[c->map_l1d]; r.valid_mask |= BENCH_VALID_L1D_MISSES; }
    } else {
      r.backend_error = -3;
    }
  }
  mac_unload(c);

#elif defined(_WIN32)
  LARGE_INTEGER qpc1;
  QueryPerformanceCounter(&qpc1);
  r.elapsed_seconds = (double)(qpc1.QuadPart - c->qpc0.QuadPart) / (double)c->qpc_freq.QuadPart;
  r.valid_mask |= BENCH_VALID_TIME;

  char cmd[2048];

  snprintf(cmd, sizeof(cmd), "xperf -stop %s", c->session);
  if (run_cmd_hidden(cmd) != 0) { r.backend_error = -10; free(c); return r; }

  snprintf(cmd, sizeof(cmd), "xperf -merge \"%s\" \"%s\"", c->etl0, c->etl_merged);
  if (run_cmd_hidden(cmd) != 0) { r.backend_error = -11; free(c); return r; }

  snprintf(cmd, sizeof(cmd), "xperf -i \"%s\" -o \"%s\"", c->etl_merged, c->txt_out);
  if (run_cmd_hidden(cmd) != 0) { r.backend_error = -12; free(c); return r; }

  uint64_t branches=0, brmiss=0, insn=0, cyc=0, cache=0;
  if (parse_pmc_text_5(c->txt_out, c->tid, &branches, &brmiss, &insn, &cyc, &cache) == 0) {
    r.branches      = branches;
    r.branch_misses = brmiss;
    r.instructions  = insn;
    r.cycles        = cyc;
    r.l1d_misses    = cache; /* Windows “CacheMisses” profile source (not strictly L1D). :contentReference[oaicite:3]{index=3} */
    r.valid_mask |= BENCH_VALID_BRANCHES | BENCH_VALID_BRANCH_MISSES |
                    BENCH_VALID_INSTRUCTIONS | BENCH_VALID_CYCLES | BENCH_VALID_L1D_MISSES;
  } else {
    r.backend_error = -13;
  }

#else
  r.backend_error = -99;
#endif

  free(c);
  return r;
}
