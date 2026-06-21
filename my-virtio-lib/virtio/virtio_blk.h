#ifndef __VIRTIO_BLK_H__
#define __VIRTIO_BLK_H__

/* Feature bits */
#define VMM_VIRTIO_BLK_F_SIZE_MAX	1	/* Indicates maximum segment size */
#define VMM_VIRTIO_BLK_F_SEG_MAX	2	/* Indicates maximum # of segments */
#define VMM_VIRTIO_BLK_F_GEOMETRY	4	/* Legacy geometry available  */
#define VMM_VIRTIO_BLK_F_RO		5	/* Disk is read-only */
#define VMM_VIRTIO_BLK_F_BLK_SIZE	6	/* Block size of disk is available*/
#define VMM_VIRTIO_BLK_F_TOPOLOGY	10	/* Topology information is available */
#define VMM_VIRTIO_BLK_F_MQ		12	/* support more than one vq */
#define VMM_VIRTIO_BLK_F_DISCARD	13	/* DISCARD is supported */
#define VMM_VIRTIO_BLK_F_WRITE_ZEROES	14	/* WRITE ZEROES is supported */

/* Legacy feature bits */
#ifndef VMM_VIRTIO_BLK_NO_LEGACY
#define VMM_VIRTIO_BLK_F_BARRIER	0	/* Does host support barriers? */
#define VMM_VIRTIO_BLK_F_SCSI		7	/* Supports scsi command passthru */
#define VMM_VIRTIO_BLK_F_FLUSH		9	/* Flush command supported */
#define VMM_VIRTIO_BLK_F_CONFIG_WCE	11	/* Writeback mode available in config */
/* Old (deprecated) name for VIRTIO_BLK_F_FLUSH. */
#define VMM_VIRTIO_BLK_F_WCE		VMM_VIRTIO_BLK_F_FLUSH
#endif /* !VMM_VIRTIO_BLK_NO_LEGACY */

#define VMM_VIRTIO_BLK_ID_BYTES		20	/* ID string length */

struct virtio_blk_config {
	/* The capacity (in 512-byte sectors). */
	uint64_t capacity;
	/* The maximum segment size (if VMM_VIRTIO_BLK_F_SIZE_MAX) */
	uint32_t size_max;
	/* The maximum number of segments (if VMM_VIRTIO_BLK_F_SEG_MAX) */
	uint32_t seg_max;
	/* geometry the device (if VMM_VIRTIO_BLK_F_GEOMETRY) */
	struct virtio_blk_geometry {
		uint16_t cylinders;
		uint8_t heads;
		uint8_t sectors;
	} geometry;

	/* block size of device (if VMM_VIRTIO_BLK_F_BLK_SIZE) */
	uint32_t blk_size;

	/* the next 4 entries are guarded by VMM_VIRTIO_BLK_F_TOPOLOGY  */
	/* exponent for physical block per logical block. */
	uint8_t physical_block_exp;
	/* alignment offset in logical blocks. */
	uint8_t alignment_offset;
	/* minimum I/O size without performance penalty in logical blocks. */
	uint16_t min_io_size;
	/* optimal sustained I/O size in logical blocks. */
	uint32_t opt_io_size;

	/* writeback mode (if VMM_VIRTIO_BLK_F_CONFIG_WCE) */
	uint8_t wce;
	uint8_t unused;

	/* number of vqs, only available when VMM_VIRTIO_BLK_F_MQ is set */
	uint16_t num_queues;

	/* the next 3 entries are guarded by VMM_VIRTIO_BLK_F_DISCARD */
	/*
	 * The maximum discard sectors (in 512-byte sectors) for
	 * one segment.
	 */
	uint32_t max_discard_sectors;
	/*
	 * The maximum number of discard segments in a
	 * discard command.
	 */
	uint32_t max_discard_seg;
	/* Discard commands must be aligned to this number of sectors. */
	uint32_t discard_sector_alignment;

	/* the next 3 entries are guarded by VMM_VIRTIO_BLK_F_WRITE_ZEROES */
	/*
	 * The maximum number of write zeroes sectors (in 512-byte sectors) in
	 * one segment.
	 */
	uint32_t max_write_zeroes_sectors;
	/*
	 * The maximum number of segments in a write zeroes
	 * command.
	 */
	uint32_t max_write_zeroes_seg;
	/*
	 * Set if a VMM_VIRTIO_BLK_T_WRITE_ZEROES request may result in the
	 * deallocation of one or more of the sectors.
	 */
	uint8_t write_zeroes_may_unmap;

	uint8_t unused1[3];
} __attribute__((packed));

/* These two define direction. */
#define VMM_VIRTIO_BLK_T_IN		0
#define VMM_VIRTIO_BLK_T_OUT		1

/* This bit says it's a scsi command, not an actual read or write. */
#define VMM_VIRTIO_BLK_T_SCSI_CMD	2

/* Cache flush command */
#define VMM_VIRTIO_BLK_T_FLUSH		4

/* Get device ID command */
#define VMM_VIRTIO_BLK_T_GET_ID		8

/* Discard command */
#define VMM_VIRTIO_BLK_T_DISCARD	11

/* This is the first element of the read scatter-gather list. */
struct virtio_blk_outhdr {
	/* VMM_VIRTIO_BLK_T */
	uint32_t type;
	/* io priority. */
	uint32_t ioprio;
	/* Sector (ie. 512 byte offset) */
	uint64_t sector;
} __attribute__((packed));

/* And this is the final byte of the write scatter-gather list. */
#define VMM_VIRTIO_BLK_S_OK		0
#define VMM_VIRTIO_BLK_S_IOERR		1
#define VMM_VIRTIO_BLK_S_UNSUPP		2

struct virtio_emulator *virtio_blk_emulator_create(void);

#endif
