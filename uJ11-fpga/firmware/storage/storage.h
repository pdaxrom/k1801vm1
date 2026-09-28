#ifndef UJ11_STORAGE_H
#define UJ11_STORAGE_H
#include "label.h"
typedef uint32_t u32;
#define MMIO(a) (*(volatile u32 *)(a))
#define BUS_STATUS MMIO(0x40000000u)
#define BUS_REQUEST MMIO(0x40000004u)
#define BUS_RESPONSE MMIO(0x40000008u)
#define BUS_IRQ MMIO(0x4000000cu)
#define BUS_CONTROL MMIO(0x40000010u)
#define SPI MMIO(0x40000100u)
#define CONTROL MMIO(0x40000104u)
#define DMA_ADDR MMIO(0x40000200u)
#define DMA_COUNT MMIO(0x40000204u)
#define ENGINE MMIO(0x40000208u)
#define WINDOW MMIO(0x4000020cu)
#define DMA_MODE MMIO(0x40000210u)
#define DIRECT MMIO(0x40000214u)
#define TIME MMIO(0x40000300u)
enum { RH_BANK,RL_BANK,XP_BANK,RK_BANK,RQ_BANK,NCONTROLLERS };
struct controller {
	uint16_t r[22],epoch;
	uint8_t busy,irq,present,ro,pad[14];
};

extern struct controller *current;
extern uint16_t *regs,boot_status;
extern unsigned service_bank;
#define R(n) regs[n]
void poll_io(void);
int active(void);
void controller_reset(unsigned bank);
void controller_finish(u32 error);
void rq_interrupt(void);
void sd_ownership(unsigned take);
enum { CS1, WC, BA, DA, CS2, DS, ER, MR, DC, DB };
enum { RX=1, TX, LOAD, STORE, COMPARE };
enum { ILF=0000001, IAE=0002000, DTE=0020000, WLE=0004000, DCK=0100000, NXM=0200000 };
/* The label bytes are dead after decoding. Reuse their RAM for controller
 * registers, drive positions and MSCP packets instead of reserving both. */
union storage_scratch {
	uint8_t sector[512];
	struct {
		struct controller controllers[NCONTROLLERS];
		uint16_t xp[8][6],packet[2][32];
	} live;
};
extern union storage_scratch scratch;
#define controllers scratch.live.controllers
void controllers_init(void);
void close_card(void);
u32 transfer(u32 op);
u32 fetch_sector(u32 lba);
u32 sector_io(u32 lba,u32 op,u32 count,u32 offset,u32 span);
struct sd_partition *find_drive(u32 kind,u32 unit);
u32 rk05_command(struct sd_partition *partition);
u32 rq_service(void);
void service_begin(unsigned bank);
#endif
