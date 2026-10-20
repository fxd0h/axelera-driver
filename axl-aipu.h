/*
 * Copyright (c) 2025 Axelera AI. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef __AXL_AIPU_H__
#define __AXL_AIPU_H__

#ifdef __KERNEL__

#include <linux/version.h>

/* Datastream source identifiers matching stream_source_t in sysctl_mem.h.
 * Defined here (before axl-aipu-fwtrace.h) because that header uses the type.
 * Values are the direct firmware indices used in device_datastream_t.
 * STREAM_SOURCE_RESERVED (0xFF) is the sentinel for "no source assigned". */
typedef enum {
	STREAM_SOURCE_CTRL_CORE_LOG = 0,
	STREAM_SOURCE_CTRL_CORE_TRACE = 1,
	STREAM_SOURCE_AICORE0_LOG = 5,
	STREAM_SOURCE_AICORE0_TRACE = 6,
	STREAM_SOURCE_AICORE1_LOG = 7,
	STREAM_SOURCE_AICORE1_TRACE = 8,
	STREAM_SOURCE_AICORE2_LOG = 9,
	STREAM_SOURCE_AICORE2_TRACE = 10,
	STREAM_SOURCE_AICORE3_LOG = 11,
	STREAM_SOURCE_AICORE3_TRACE = 12,
	STREAM_SOURCE_AICORE4_LOG = 13,
	STREAM_SOURCE_AICORE4_TRACE = 14,
	STREAM_SOURCE_AICORE5_LOG = 15,
	STREAM_SOURCE_AICORE5_TRACE = 16,
	STREAM_SOURCE_AICORE6_LOG = 17,
	STREAM_SOURCE_AICORE6_TRACE = 18,
	STREAM_SOURCE_AICORE7_LOG = 19,
	STREAM_SOURCE_AICORE7_TRACE = 20,
	STREAM_SOURCE_PVE_CORE0_LOG = 51,
	STREAM_SOURCE_PVE_CORE0_TRACE = 52,
	STREAM_SOURCE_PVE_CORE1_LOG = 53,
	STREAM_SOURCE_PVE_CORE1_TRACE = 54,
	STREAM_SOURCE_PVE_CORE2_LOG = 55,
	STREAM_SOURCE_PVE_CORE2_TRACE = 56,
	STREAM_SOURCE_PVE_CORE3_LOG = 57,
	STREAM_SOURCE_PVE_CORE3_TRACE = 58,
	STREAM_SOURCE_PVE_CORE4_LOG = 59,
	STREAM_SOURCE_PVE_CORE4_TRACE = 60,
	STREAM_SOURCE_PVE_CORE5_LOG = 61,
	STREAM_SOURCE_PVE_CORE5_TRACE = 62,
	STREAM_SOURCE_PVE_CORE6_LOG = 63,
	STREAM_SOURCE_PVE_CORE6_TRACE = 64,
	STREAM_SOURCE_PVE_CORE7_LOG = 65,
	STREAM_SOURCE_PVE_CORE7_TRACE = 66,
	STREAM_SOURCE_PVE_CORE8_LOG = 67,
	STREAM_SOURCE_PVE_CORE8_TRACE = 68,
	STREAM_SOURCE_PVE_CORE9_LOG = 69,
	STREAM_SOURCE_PVE_CORE9_TRACE = 70,
	STREAM_SOURCE_PVE_CORE10_LOG = 71,
	STREAM_SOURCE_PVE_CORE10_TRACE = 72,
	STREAM_SOURCE_PVE_CORE11_LOG = 73,
	STREAM_SOURCE_PVE_CORE11_TRACE = 74,
	STREAM_SOURCE_PVE_CORE12_LOG = 75,
	STREAM_SOURCE_PVE_CORE12_TRACE = 76,
	STREAM_SOURCE_PVE_CORE13_LOG = 77,
	STREAM_SOURCE_PVE_CORE13_TRACE = 78,
	STREAM_SOURCE_PVE_CORE14_LOG = 79,
	STREAM_SOURCE_PVE_CORE14_TRACE = 80,
	STREAM_SOURCE_PVE_CORE15_LOG = 81,
	STREAM_SOURCE_PVE_CORE15_TRACE = 82,
	STREAM_SOURCE_MONITOR = 83,
	STREAM_SOURCE_RESERVED = 255,
	STREAM_SOURCE_MAX = 256,
} stream_source_t;

#include "axl-aipu-fwtrace.h"

/* constants */
#define MAX_MSG		  256
#define MAX_MEMORY_AREA	  2
#define AICORE_COUNT	  4
#define NAME_SIZE	  32
#define MAX_DMA_CHANNEL	  4
#define DMA_SIZE	  (2 * 1024 * 1024)
#define DMA_DESC_BUF_SIZE (32 * 1024)

#define EDMA_V0_MAX_NR_CH    4
#define HDMA_V0_MAX_NR_CH    4
#define RESERVED_DESC_DMA_CH 3
/* ported from sysctl_mem.h */

/*
 * sysctl substructures magic numbers
 * WARNING: not supposed to be changed - used for sanity check
 */
#define SYSCTL_HDIF_HEADER_MAGIC      (0xFD1F)
#define SYSCTL_DATASTREAM_AREA_MAGIC  (0xF0F0)
#define SYSCTL_HOST_DRV_AREA_MAGIC    (0xBAC1)
#define SYSCTL_DMA_SG_DESC_AREA_MAGIC (0xD4D4)
#define SYSCTL_VMSI_AREA_MAGIC	      (0xB5B5)

#define DMA_VM_SUPPORTED (0xC1A0C1A0)

#define MAX_VIRT_MSI	1024
#define VMSI_IRQ_EN_BIT 0
#define VMSI_IRQ_EN	(1 << VMSI_IRQ_EN_BIT)

// hwgen==metis
enum physical_msi_metis {
	PMSI_METIS_KRN_0 = 0,
	PMSI_METIS_KRN_1 = 1,
	PMSI_METIS_KRN_2 = 2,
	PMSI_METIS_KRN_3 = 3,
	PMSI_METIS_RD_CH0 = 4,
	PMSI_METIS_RD_CH1 = 5,
	PMSI_METIS_RD_CH2 = 6,
	PMSI_METIS_RD_CH3 = 7,
	PMSI_METIS_WR_CH0 = 8,
	PMSI_METIS_WR_CH1 = 9,
	PMSI_METIS_WR_CH2 = 10,
	PMSI_METIS_WR_CH3 = 11,
	PMSI_METIS_MSG = 12,
	PMSI_METIS_DEV_AXE_MSG = 24,
	PMSI_METIS_MAX = 32,
};

// hwgen>=europa
enum physical_msi {
	PMSI_KERNEL = 0,
	PMSI_LOG = 1,
	PMSI_TRACE = 2,
	PMSI_MONITOR = 3,
	PMSI_DMA_RD_CH0 = 4,
	PMSI_DMA_RD_CH1 = 5,
	PMSI_DMA_RD_CH2 = 6,
	PMSI_DMA_RD_CH3 = 7,
	PMSI_DMA_WR_CH0 = 8,
	PMSI_DMA_WR_CH1 = 9,
	PMSI_DMA_WR_CH2 = 10,
	PMSI_DMA_WR_CH3 = 11,
	PMSI_MSG = 12,
	PMSI_MAX = 32,
};

/* Command-mailbox notification slot; 12 on every generation. */
#define AXL_SYSMSG_MSI PMSI_MSG

enum vmsi_source {
	VMSI_SOURCE_FW = 0, /* FW sets VMSI_IRQ_EN before triggering */
	VMSI_SOURCE_HW = 1, /* HW triggers directly (1:1 PMSI-to-VMSI) */
};

struct vmsi_info_t {
	uint8_t enabled;
	uint8_t source; /* enum vmsi_source */
	uint8_t reserved[2];
	union {
		struct {
			uint16_t start;
			uint16_t end;
		} vmsi_range; /* VMSI_SOURCE_FW: scan this range for VMSI_IRQ_EN */
		uint32_t vmsi_id; /* VMSI_SOURCE_HW: VMSI slot to signal/wait on */
	};
};

struct device_vmsi_config_t {
	uint16_t num_vmsi;
	uint16_t num_pmsi;
	struct vmsi_info_t pmsi_to_vmsi_info[PMSI_MAX];
	uint8_t vmsi_to_pmsi[];
};

struct device_virt_msi_t {
	volatile u32 msi[MAX_VIRT_MSI];
};

/* Memory types are kept generic as they could represent different things
 * on different devices */
enum {
	MEMORY_AREA_0 = 0,
	MEMORY_AREA_1 = 1,
};

struct buffer_reference_t {
	uint64_t addr;
	uint64_t size;
};

struct device_ctx_t {
	/* --- Context Resources --- */
	uint64_t aicore; // bitmask of aicores allocate to context
	uint64_t l2_base; // L2 context base address
	uint64_t l2_size; // L2 context size
	uint64_t ddr_base; // DDR context base address
	uint64_t ddr_size; // DDR context size
	uint64_t msi; // msi notify allocate to context
	/* --- Kernel Offload Resources --- */
	uint64_t cmd; // context command operation
	uint64_t sts; // context return operation state
	uint64_t arg; // context command argument
	uint64_t elf_base; // elf base address
};

struct device_host_drv_t {
	uint32_t ctrl;
	uint32_t base;
	uint64_t target;
	uint32_t size;
};

struct device_dma_sg_desc_t {
	struct buffer_reference_t dma_sg_desc_buf_ref;
};

struct cmd_t {
	uint32_t opcode;
	uint32_t size;
	char msg[MAX_MSG];
};

/* Values firmware leaves in cmd_t.opcode. From device_commands.h. */
enum cmd_status {
	CMD_IDLE = 0,
	CMD_DONE = 1,
	CMD_FAIL = 2,
	CMD_UNSUPPORTED = 3,
	CMD_UNKNOWN = 4,
};

struct version_t {
	uint8_t major;
	uint8_t minor;
};

struct hdif_header_t {
	uint16_t magic; // magic number for sanity check
	uint8_t reserved[6];
	uint64_t base; // device-physical base address of the host-device interface region
	uint64_t size; // total reserved size of the host-device interface region (static + dynamic)
};

struct memory_reference_t {
	uint16_t magic; // magic number for sanity check
	struct version_t version; // version of the area
	uint32_t size; // size of the area
	uint64_t offset; // offset of the area (w.r.t. BAR)
	uint16_t memory_area; // memory where the area is located
	uint16_t reserved[3];
};

struct device_sys_ctl_t {
	/* -------- Static Runtime Area (1KB) --------- */
	struct hdif_header_t header; // 0x00 (24 bytes)
	uint8_t pad0[152]; // 0x18
	struct cmd_t cmd; // 0xb0
	uint8_t pad1[72];
	struct version_t master_version; // 0x200
	uint8_t pad2[6];
	uint64_t memory_map[MAX_MEMORY_AREA]; // memory types to device phys. addr.
	uint32_t dma_vm; // flag to verify the FW support DMA VM service
	uint8_t pad3[28];
	uint64_t fw_load_addr;
	struct memory_reference_t datastream_mem_ref;
	struct memory_reference_t boardinfo_mem_ref;
	struct memory_reference_t
		axemsg_mem_ref; // [DEPRECATED] can be overwritten
	struct memory_reference_t ctx_mem_ref;
	struct memory_reference_t virt_mem_ref;
	struct memory_reference_t hdrv_mem_ref;
	struct memory_reference_t dmasgdesc_mem_ref;
	struct memory_reference_t vmsi_mem_ref;
	uint8_t __pad4[256];
	/* ---- Static MCUBoot Reserved Area (1KB) ---- */
	uint8_t mcuboot_shared[1024]; // 0x400
};

/* end of ported from sysctl_mem.h */

enum AXL_DMA_TYPE {
	EDMA_DMA = 0,
	HYPER_DMA = 1,
};

enum AXL_DEVICE_MODE {
	AXL_QEMU_MODE = 2,
	AXL_SILICON_MODE = 3,
};

enum AXL_HW_GEN {
	AXL_HW_GEN_METIS = 0,
	AXL_HW_GEN_EUROPA = 1,
};

enum AXL_DEVICE_PROPERTY {
	AXL_PROPERTY_HW_GEN = 0,
	AXL_PROPERTY_AICORE_COUNT = 1,
	AXL_PROPERTY_PVE_CORE_COUNT = 2,
	AXL_PROPERTY_PES_GROUP = 3,
};

struct axe_device_info {
	const char *name;
	const char *devname;
	int hw_gen;
	int mode;
	int dma_rd_ch;
	int dma_wr_ch;
	int dma_type;
	int dma_size;
	int aicore_count;
	int pve_core_count;
};

struct msi_info {
	int num;
	int timeout;
};
/*
 * Single-MSI DMA handshake. The shared vector carries no source, so the handler
 * visits every DMA slot on every interrupt and asks its check callback whether
 * that channel terminated. A channel at rest can answer yes (HDMA ch_stat rests
 * at STOPPED), so the check may only wake a slot that a waiter has ARMED, and
 * the ARMED->DONE transition has to be atomic against the arming.
 */
enum dma_wait_state {
	DMA_WAIT_IDLE = 0, /* no transfer in flight on this slot */
	DMA_WAIT_ARMED, /* about to ring the doorbell, waiter blocking */
	DMA_WAIT_DONE, /* check saw this channel transfer everything */
	DMA_WAIT_ABORTED, /* check saw this channel abort */
};
struct irq_wrk {
	struct axl_pcie_aipu_dev *axldev;
	int id;
	int timeout;
	spinlock_t irq_lock;
	struct completion irq_done;
	int (*check)(struct axl_pcie_aipu_dev *axldev, int id);
	ktime_t stime;
	struct list_head sctx_list;
	atomic_t dma_done; /* enum dma_wait_state, single-MSI mode only */
};
struct dma_wrk {
	struct kref refcount;
	struct work_struct work;
	struct axl_pcie_aipu_dev *axldev;
	struct sysctrl_ctx *sctx;
	int id;
	int timeout;
	int status;
	int channel;
	struct completion done;
	size_t size;
	u32 offset;
	__u64 axi;
	__u64 p2pphy;
	int num_sgt;
	int flags;
	struct sg_table *table;
	bool usr_dma;
	struct page **page_list;
	unsigned long nr_pages;
	enum dma_data_direction dir; /* Logical transfer direction */
	struct dma_queue_ctrl *qctrl;
	ktime_t ktime; /* Request start time */
	ktime_t ktime_wq; /* Workqueue start time */
	ktime_t ktime_dma_start; /* DMA engine start time */
	ktime_t ktime_dma_end; /* DMA completion time */
	ktime_t duration;
	struct dma_buf *dmabuf; /* Reference held during transfer */
};
struct dma_queue_ctrl {
	struct axl_pcie_aipu_dev *axldev;
	int id;
	int timeout;
	struct workqueue_struct *wq;
	atomic_t count;
	// statistics
	int num_xfer;
	int num_err;
	__u64 bytes_xfer;
	int max_sgt;
	size_t size;
	ktime_t duration;
	ktime_t max_duration;
	int mode;
};
struct axl_dev_fops {
	int (*dma_irq_ck)(struct axl_pcie_aipu_dev *axldev, int id);
	void (*dma_enable_ctrl)(struct axl_pcie_aipu_dev *axldev);
	void (*dma_job_submit)(struct dma_wrk *dma_wrk);
	void (*dma_init_imwr)(struct axl_pcie_aipu_dev *axldev);
	void (*dma_p2p_job_submit)(struct dma_wrk *dma_wrk);

	void (*dev_debugfs_init)(struct axl_pcie_aipu_dev *axldev);
	void (*dev_debugfs_exit)(struct axl_pcie_aipu_dev *axldev);
	void (*dev_dynmem_init)(struct axl_pcie_aipu_dev *axldev);
};

struct axl_msi_fops {
	int (*init)(struct axl_pcie_aipu_dev *axldev);
	void (*remap)(struct axl_pcie_aipu_dev *axldev);
};
#define MAX_MEMORY_AREA 2
struct dev_res_info {
	__u64 sysspm_base;
	__u64 sysspm_size;
	__u64 l2_base;
	__u64 l2_size;
	__u64 ddr_base[MAX_MEMORY_AREA];
	__u64 ddr_size[MAX_MEMORY_AREA];
};

struct dev_mem_window {
	__u64 np_base;
	__u64 np_size;
	__u64 p_base;
	__u64 p_size;
	__u64 base_res[6];
	__u64 size_res[6];
};

struct axl_pcie_aipu_dev {
	char name[NAME_SIZE];
	char compat_name[NAME_SIZE];
	struct pci_dev *pdev;
	int dma_enabled : 1;
	int dma_vm : 1;
	int dma_poll_only : 1;
	struct device_host_drv_t *hdrv_base;
	dma_addr_t dma_addr;
	dma_addr_t dma_addr_unaligned;
	unsigned long *dma_va;
	unsigned long *dma_va_unaligned;
	int dma_size;
	int dma_alloc_size;
	int nmsi;
	int irq_vec;
	struct msi_msg irq_msi;
	struct axl_dev_fops *fops;
	struct axl_msi_fops *msi_fops;
	struct axe_device_info *dev_info;
	struct dev_res_info *res_info;
	struct dev_mem_window *mem_win;
	int pes_group;
	// vmsi stat
	/* Host copy of the BAR-resident table; NULL until firmware publishes it */
	struct device_vmsi_config_t *msi_cfg;
	struct device_vmsi_config_t *msi_cfg_buf;
	atomic_t *vmsi_count;
	int max_msi;
	// base DMA MSI id (DMA_RD_CH0)
	int dma_msi_base;
	// context
	struct mutex mutex;
	struct mutex msg_mutex;
	uint64_t glob_ctx_mask; // global context mask
	uint64_t *ctx_mask; // per device context mask
	// char
	struct cdev cdev;
	struct device *class_dev;
	struct dentry *dentry;
	int minor;
	// recovery thread
	int dlllarc; // Data Link Layer Link Active Reporting Capable
	struct task_struct *recovery;
	struct pci_saved_state *pcie_state;
	int dev_state;
	int restore_fails;
	atomic_t dev_unreachable;
	bool auto_recovery; // thread may start a restore on its own
	// pcie dma
	void *dma;
	phys_addr_t pdma;
	void __iomem *vbase;
	phys_addr_t pbase;
	struct irq_wrk *irq_wrk;
	struct dma_queue_ctrl *dma_wrqc;
	struct dma_queue_ctrl *dma_rdqc;
	spinlock_t msi_lock;
	struct mutex desc_mutex; /* Protects descriptor channel access */
	uint64_t desc_base;
	uint64_t desc_offset;
	/* Host-side descriptor buffer (in DMA region after VMSI) */
	struct dw_edma_ll_buf *desc_host_va; /* Virtual address in host memory */
	dma_addr_t desc_host_pa; /* Physical/DMA address in host memory */
	size_t desc_host_size; /* Size of descriptor buffer */
	/* DMA trace buffer */
	struct dma_trace_buffer *trace_buf; /* NULL if allocation failed */
	/* Firmware trace consumer */
	struct fwtrace_consumer fwtrace;
};

/**
 * struct fwtrace_session - Per-file-descriptor trace session state
 *
 * Each open file descriptor in trace mode gets its own kfifo so that
 * multiple concurrent readers all receive a full copy of the data.
 */
struct fwtrace_session {
	struct kfifo fifo; /* Per-session kernel ring buffer */
	void *fifo_buf; /* Backing store for @fifo (kvmalloc'd) */
	spinlock_t lock; /* Protects fifo */
	wait_queue_head_t wait_queue; /* For blocking reads */
	atomic64_t overruns; /* Per-session overrun count */
	struct list_head list; /* Link into fwtrace_buffer.sessions */
};

struct sysctrl_ctx {
	struct kref refcount;
	struct axl_pcie_aipu_dev *axldev;
	uint64_t ctx_mask; // context mask
	atomic_t msg_flag;
	struct dmabuf_imp di;
	atomic_t dma_xfer_state;
	struct dma_wrk *dma_wrk;
	wait_queue_head_t poll_wait_queue;
	atomic_t poll_event_cnt;
	struct list_head node;
	/* Firmware trace session fields */
	bool fwtrace_mode; /* True if fd is in trace mode */
	stream_source_t fwtrace_src; /* Which log source to read */
	struct fwtrace_session fwtrace_session; /* Per-fd trace state */
};

/*
 * Record a DMA transfer state (DMA_XFER_*) on the owning sysctrl context.
 */
static inline void sctx_set_dma_xfer_state(struct sysctrl_ctx *sctx, int status)
{
	if (sctx)
		atomic_set(&sctx->dma_xfer_state, status);
}

struct dma_channel_stats {
	int count; /* Current active transfers */
	int num_xfer; /* Total number of transfers */
	int num_err; /* Number of errors */
	__u64 bytes_xfer; /* Total bytes transferred */
	int max_sgt; /* Maximum scatter-gather table entries */
	__u64 max_duration_us; /* Maximum duration in microseconds */
	__u64 duration_us; /* Last transfer duration in microseconds */
	size_t size; /* Last transfer size in bytes */
	__u32 speed_mbps; /* Last transfer speed in MB/s */
};

struct dma_stats {
	struct dma_channel_stats rd_channels[MAX_DMA_CHANNEL];
	struct dma_channel_stats wr_channels[MAX_DMA_CHANNEL];
};

/**
 * struct dma_trace_entry - Single DMA trace entry
 * @timestamp: Monotonic timestamp when trace was captured (ns)
 * @duration_ns: Total duration from request to completion (ns)
 * @ktime_ns: Request start time (ns)
 * @ktime_wq_ns: Workqueue start time (ns)
 * @ktime_dma_start_ns: DMA engine start time (ns)
 * @ktime_dma_end_ns: DMA completion time (ns)
 * @transfer_size: Total bytes transferred
 * @channel: DMA channel number (0-3)
 * @sgt_entries: Number of scatter-gather table entries
 * @flags: Transfer flags (direction, sync/async, etc.)
 * @mode: Transfer mode ("RD" or "WR")
 */
struct dma_trace_entry {
	u64 timestamp;
	u64 duration_ns;
	u64 ktime_ns;
	u64 ktime_wq_ns;
	u64 ktime_dma_start_ns;
	u64 ktime_dma_end_ns;
	size_t transfer_size;
	u16 channel;
	u16 sgt_entries;
	u16 flags;
	char mode[4];
	/* Total: 64 bytes (cache-line aligned) */
};

/**
 * struct dma_trace_buffer - Circular buffer for DMA traces
 * @entries: Pointer to array of trace entries
 * @head: Write position (producer index)
 * @tail: Read position (consumer index)
 * @size: Total number of entries (power of 2)
 * @size_mask: Mask for circular indexing (size - 1)
 * @lock: Spinlock for synchronized access
 * @enabled: Tracing enabled flag (atomic)
 * @overruns: Counter for buffer overruns
 * @total_traces: Total traces captured (including overruns)
 */
struct dma_trace_buffer {
	struct dma_trace_entry *entries;
	unsigned long head;
	unsigned long tail;
	unsigned int size;
	unsigned int size_mask;
	spinlock_t lock;
	atomic_t enabled;
	atomic64_t overruns;
	atomic64_t total_traces;
};

static inline void axl_aipu_dev_debugfs_exit(struct axl_pcie_aipu_dev *axldev)
{
	if (axldev->fops->dev_debugfs_exit)
		axldev->fops->dev_debugfs_exit(axldev);
}

static inline void axl_aipu_dma_enable_ctrl(struct axl_pcie_aipu_dev *axldev)
{
	if (axldev->fops->dma_enable_ctrl)
		axldev->fops->dma_enable_ctrl(axldev);
}

static inline void axl_aipu_dma_init_imwr(struct axl_pcie_aipu_dev *axldev)
{
	if (axldev->fops->dma_init_imwr)
		axldev->fops->dma_init_imwr(axldev);
}

static inline void axl_aipu_dma_job_submit(struct axl_pcie_aipu_dev *axldev,
					   struct dma_wrk *dma_wrk)
{
	if (axldev->fops->dma_job_submit)
		axldev->fops->dma_job_submit(dma_wrk);
}

static inline void axl_aipu_dma_p2p_job_submit(struct axl_pcie_aipu_dev *axldev,
					       struct dma_wrk *dma_wrk)
{
	if (axldev->fops->dma_p2p_job_submit)
		axldev->fops->dma_p2p_job_submit(dma_wrk);
}
static inline int axl_aipu_dma_irq_ck(struct axl_pcie_aipu_dev *axldev, int id)
{
	if (!axldev->fops->dma_irq_ck)
		return -EINVAL;

	return axldev->fops->dma_irq_ck(axldev, id);
}

static inline bool axl_aipu_has_vmsi_cfg(struct axl_pcie_aipu_dev *axldev)
{
	return axldev->msi_cfg != NULL;
}

/**
 * dma_wait_arm() - Declare a transfer in flight on @id (see enum dma_wait_state)
 * @axldev: Device context
 * @id: MSI slot of the channel
 *
 * Arm last, after reinit_completion() and after the channel status has been
 * cleared: a check callback already running against the previous transfer then
 * has no armed slot to wake, and cannot hand its completion to this one.
 */
static inline void dma_wait_arm(struct axl_pcie_aipu_dev *axldev, int id)
{
	atomic_set(&axldev->irq_wrk[id].dma_done, DMA_WAIT_ARMED);
}

/**
 * dma_wait_is_armed() - Is a transfer in flight on @id?
 * @axldev: Device context
 * @id: MSI slot of the channel
 *
 * Test this before reading a channel status register: dma_wait_arm() runs after
 * the status has been cleared, so a status read that follows an armed reading
 * cannot be describing the previous transfer.
 */
static inline bool dma_wait_is_armed(struct axl_pcie_aipu_dev *axldev, int id)
{
	return atomic_read(&axldev->irq_wrk[id].dma_done) == DMA_WAIT_ARMED;
}

/**
 * dma_wait_claim() - Take ownership of the wake for @id, from a check callback
 * @axldev: Device context
 * @id: MSI slot of the channel
 * @outcome: DMA_WAIT_DONE or DMA_WAIT_ABORTED, how the transfer ended
 *
 * The outcome is recorded here because the channel status register is not stable
 * afterwards: a linked-list channel returns to RUNNING once it resumes polling
 * the next descriptor, so the waiter cannot re-read what happened.
 *
 * Return: true if @id was armed and now holds @outcome - the caller may complete
 * the waiter; false if no transfer is in flight, in which case the terminated
 * status the caller read belongs to a channel at rest and means nothing.
 */
static inline bool dma_wait_claim(struct axl_pcie_aipu_dev *axldev, int id,
				  int outcome)
{
	return atomic_cmpxchg(&axldev->irq_wrk[id].dma_done, DMA_WAIT_ARMED,
			      outcome) == DMA_WAIT_ARMED;
}

/**
 * dma_wait_consume() - Disarm @id and report how the wait ended
 * @axldev: Device context
 * @id: MSI slot of the channel
 *
 * Return: DMA_WAIT_DONE if a check callback claimed this transfer, otherwise
 * the slot was woken by something else (or timed out).
 */
static inline int dma_wait_consume(struct axl_pcie_aipu_dev *axldev, int id)
{
	return atomic_xchg(&axldev->irq_wrk[id].dma_done, DMA_WAIT_IDLE);
}

static inline void axl_aipu_dev_dynmem_init(struct axl_pcie_aipu_dev *axldev)
{
	if (axldev->fops->dev_dynmem_init)
		axldev->fops->dev_dynmem_init(axldev);
}

static inline unsigned int first_set_bit(unsigned int n)
{
	unsigned int pos = 0;
	while (!(n & (1 << pos))) {
		pos++;
	}
	return pos;
}

static inline void get_max_duration(struct dma_wrk *dma_wrk)
{
	struct dma_queue_ctrl *dma_ctrl = dma_wrk->qctrl;
	dma_wrk->duration = ktime_sub(ktime_get(), dma_wrk->ktime);
	dma_ctrl->duration = dma_wrk->duration;
	dma_ctrl->max_duration =
		max_t(ktime_t, dma_ctrl->duration, dma_wrk->duration);
}
static inline int get_timeout_ms(int timeout)
{
	return timeout == 0 ? msecs_to_jiffies(1000) :
			      msecs_to_jiffies(timeout * 1000);
}

extern unsigned int dma_timeout;
extern unsigned int dma_poll;

/*
 * dma_poll module param is an override: it forces every transfer to poll.
 * dma_poll_only is the same thing decided per device, for a generation and MSI
 * mode whose DMA completion interrupts are not usable (see axl_pci_msi_init()).
 */
static inline bool dma_poll_mode(struct dma_wrk *dma_wrk)
{
	return dma_poll || dma_wrk->axldev->dma_poll_only ||
	       (dma_wrk->flags & DMABUF_XFER_FLAG_POLL);
}

static inline int
axl_aipu_find_free_dma_channel(struct axl_pcie_aipu_dev *axldev, int flags)
{
	int i, max_dma_ch = axldev->dev_info->dma_rd_ch;
	struct dma_queue_ctrl *dma_ctrl;

	if (flags & DMABUF_XFER_FLAG_READ)
		dma_ctrl = axldev->dma_rdqc;
	else
		dma_ctrl = axldev->dma_wrqc;

	for (i = 0; i < max_dma_ch; i++) {
		if (atomic_read(&dma_ctrl[i].count) == 0)
			return i;
	}
	return 0;
}

static inline int validate_dma_xfer(struct dmabuf_xfer *dxfer,
				    struct pci_dev *pdev)
{
	int read_write = DMABUF_XFER_FLAG_READ | DMABUF_XFER_FLAG_WRITE;
	int async_sync = DMABUF_XFER_FLAG_ASYNC | DMABUF_XFER_FLAG_SYNC;

	dev_dbg(&pdev->dev,
		"dma xfer channel:%s%d | phy 0x%llx | off 0x%x | size 0x%x | flags 0x%x | ch %d\n",
		dxfer->flags & DMABUF_XFER_FLAG_READ ? "RD" : "WR",
		dxfer->channel, dxfer->phy, dxfer->offset, (int)dxfer->size,
		dxfer->flags, dxfer->channel);

	if (((dxfer->flags & read_write) == 0) ||
	    ((dxfer->flags & read_write) == read_write))
		return -EINVAL;
	if (((dxfer->flags & async_sync) == 0) ||
	    ((dxfer->flags & async_sync) == async_sync))
		return -EINVAL;
	if (dxfer->size == 0)
		return -EINVAL;
	return 0;
}

static inline int validate_usr_dma_xfer(struct dma_xfer *xfer,
					struct pci_dev *pdev)
{
	int read_write = DMABUF_XFER_FLAG_READ | DMABUF_XFER_FLAG_WRITE;
	int async_sync = DMABUF_XFER_FLAG_ASYNC | DMABUF_XFER_FLAG_SYNC;

	dev_dbg(&pdev->dev,
		"usr dma xfer %s | phy 0x%llx | virt 0x%llx | size 0x%zx | flags 0x%x\n",
		xfer->flags & DMABUF_XFER_FLAG_READ ? "RD" : "WR", xfer->phy,
		xfer->virt, xfer->size, xfer->flags);

	if (xfer->size == 0)
		return -EINVAL;
	if (((xfer->flags & read_write) == 0) ||
	    ((xfer->flags & read_write) == read_write))
		return -EINVAL;
	if ((xfer->flags & async_sync) == async_sync)
		return -EINVAL;
	return 0;
}

static inline struct device_dma_sg_desc_t *
axl_aipu_get_dma_sg_desc_area(struct axl_pcie_aipu_dev *axldev)
{
	struct device_sys_ctl_t *dsctl = axldev->vbase;
	if (dsctl->dmasgdesc_mem_ref.magic != SYSCTL_DMA_SG_DESC_AREA_MAGIC) {
		return NULL;
	}
	return (struct device_dma_sg_desc_t *)((uintptr_t)dsctl +
					       dsctl->dmasgdesc_mem_ref.offset);
}

static inline uint64_t axl_aipu_get_hdif_base(struct axl_pcie_aipu_dev *axldev)
{
	struct device_sys_ctl_t *dsctl = axldev->vbase;
	if (dsctl->header.magic == SYSCTL_HDIF_HEADER_MAGIC) {
		return dsctl->header.base;
	}

	return dsctl->memory_map[0];
}

static inline struct device_vmsi_config_t *
axl_aipu_get_msi_config_area(struct axl_pcie_aipu_dev *axldev)
{
	struct device_sys_ctl_t *dsctl = axldev->vbase;
	if (dsctl->vmsi_mem_ref.magic != SYSCTL_VMSI_AREA_MAGIC) {
		return NULL;
	}
	return (struct device_vmsi_config_t *)((uintptr_t)dsctl +
					       dsctl->vmsi_mem_ref.offset);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
static inline void vm_flags_clear(struct vm_area_struct *vma, vm_flags_t flags)
{
	vma->vm_flags &= ~flags;
}
#endif

long sysctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int axl_aipu_send_msg(struct axl_pcie_aipu_dev *axldev, struct cmd_t *cmd,
		      unsigned int timeout, u32 *status);
void axl_aipu_sys_ctx_release(struct kref *kref);
void axl_aipu_dma_wrk_release(struct kref *kref);
void axl_aipu_sys_ctx_cleanup_dmabuf(struct sysctrl_ctx *sys_ctx);

void axl_aipu_config_dev_dma(struct axl_pcie_aipu_dev *axldev);
void axl_aipu_config_dev_msi(struct axl_pcie_aipu_dev *axldev);
void axl_aipu_dma_imwr_restore(struct axl_pcie_aipu_dev *axldev);

void axl_aipu_dev_problem(struct axl_pcie_aipu_dev *axldev, const char *reason);

static inline int axl_aipu_dma_dev_gone(struct axl_pcie_aipu_dev *axldev,
					struct dma_wrk *dma_wrk)
{
	struct pci_dev *pdev = axldev->pdev;
	const char *mode = dma_wrk->flags & DMABUF_XFER_FLAG_READ ? "WR" : "RD";

	dev_err(&pdev->dev, "DMA %s CH%d device not responding (irq %d)\n",
		mode, dma_wrk->channel, dma_wrk->id);
	axl_aipu_dev_problem(axldev, "DMA register read");
	dma_wrk->status = -EIO;
	sctx_set_dma_xfer_state(dma_wrk->sctx, DMA_XFER_FAIL);
	dma_wrk->qctrl->num_err++;
	return -1;
}

static inline bool axl_aipu_dev_is_unreachable(struct axl_pcie_aipu_dev *axldev)
{
	return atomic_read(&axldev->dev_unreachable) != 0;
}

#endif // __KERNEL__

#endif // __AXL_AIPU_H__
