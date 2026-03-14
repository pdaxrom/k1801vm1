#ifndef OPTIONS_H_
#define OPTIONS_H_

#include "../core/core.h"
#include "dev_rk11.h"
#include "dev_rh11.h"
#include "dev_xp.h"
#include "dev_rl11.h"
#include "dev_tq11.h"

#ifndef RL11_MAX_DRIVES
#define RL11_MAX_DRIVES 4
#endif

enum {
    BOOT_DEV_NONE = 0,
    BOOT_DEV_RK,
    BOOT_DEV_RH,
    BOOT_DEV_RL,
    BOOT_DEV_TQ
};

typedef struct {
    const char *rk_path[RK11_MAX_DRIVES];
    const char *rh_path[RH11_MAX_DRIVES];
    const char *xp_path[XP_MAX_DRIVES];
    const char *tq_path[TQ11_MAX_UNITS];
    struct {
        const char *path;
        int type;
    } rl_path[RL11_MAX_DRIVES];
    int rk_count;
    int rh_count;
    int xp_count;
    int rl_count;
    int tq_count;
    const char *socket_path;
    const char *load_path;
    int do_bootcopy;
    int do_bootrt11;
    int do_boottq;
    int do_boot;
    int boot_kind;
    int boot_unit;
    long load_addr;
    long start_pc;
    long sr_value;
    long ram_kb_arg;
    long sys_clock_mhz;
    int force_dl11_alias;
    int disable_dl;
    int disable_dz;
    int disable_kw;
    int kw11_l_override;
    int kw11_p_override;
    int disable_lp;
    int disable_rk;
    int disable_rh;
    int disable_xp;
    int disable_rl;
    int disable_tq;
    int disable_sr;
    long dz_port;
    int dz_port_set;
    byte cpu_model;
    int force_fis;
    int force_fp11;
    int disable_fis;
    int disable_fp11;
    int trace;
    int trace_regs;
    int trace_irq;
    int trace_nxm;
    long trace_after;
    long max_steps;
    int dl11_8bit;
    int display_enable;
    int do_nl_to_cr;
    int exit_on_abort;
    int check_config_only;
    rh11_mode_t rh_mode;
} lsi11_options_t;

void options_init(lsi11_options_t *opts);
int options_parse(lsi11_options_t *opts, int argc, char **argv);
void options_usage(const char *argv0);
const char *cpu_model_name(byte model);

#endif
