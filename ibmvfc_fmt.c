#include <stdio.h>
#include <stdlib.h>
#include <pci/types.h>
#include <errno.h>
#include <asm/byteorder.h>

typedef unsigned long long u64;
static int le;

static u16 trc16_to_cpu(u16 n)
{
	if (le)
		return n;
	else
		return ntohs(n);
}

static u32 trc32_to_cpu(u32 n)
{
	if (le)
		return n;
	else
		return ntohl(n);
}

static u64 trc64_to_cpu(u64 n)
{
	if (le)
		return n;
	else
		return __be64_to_cpu(n);
}

enum ibmvfc_crq_formats {
	IBMVFC_CMD_FORMAT		= 0x01,
	IBMVFC_ASYNC_EVENT	= 0x02,
	IBMVFC_MAD_FORMAT		= 0x04,
};

const char *fmt [255] = {
	[IBMVFC_CMD_FORMAT] = "CMD  ",
	[IBMVFC_ASYNC_EVENT] = "ASYNC",
	[IBMVFC_MAD_FORMAT] = "MAD  ",
};

struct ibmvfc_trace_start_entry {
	u32 xfer_len;
}__attribute__((packed));

struct ibmvfc_trace_end_entry {
	u16 status;
	u16 error;
	u8 fcp_rsp_flags;
	u8 rsp_code;
	u8 scsi_status;
	u8 reserved;
}__attribute__((packed));

struct ibmvfc_trace_entry {
	u64 evt;
	u32 time;
	u32 scsi_id;
	u32 lun;
	u8 fmt;
	u8 op_code;
	u8 tmf_flags;
	u8 type;
#define IBMVFC_TRC_START	0x00
#define IBMVFC_TRC_END		0xff
	union {
		struct ibmvfc_trace_start_entry start;
		struct ibmvfc_trace_end_entry end;
	};
}__attribute__((packed, aligned (8)));

int main (int argc, char *argv[])
{
	char line[1000];
	int rc, len, entries = 0, i = 0, j, sz = 0, skips = 0;
	struct ibmvfc_trace_entry *trace;
	u64 *data = NULL;
	u64 ignore64, data1, data2;
	char ignores[1000];
	int end_only = 0, start_only = 0, dup_chk = 0;
	char dup = ' ';
	char wrap = ' ';

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-e") == 0)
			end_only = 1;
		else if (strcmp(argv[i], "-s") == 0)
			start_only = 1;
		else if (strcmp(argv[i], "-c") == 0)
			dup_chk = 1;
		else if (strcmp(argv[i], "-l") == 0)
			le = 1;
	}

	for (i = 0, entries = 0; skips < 10;) {
		rc = scanf("%llx: %llx %llx  %16c\n", &ignore64, &data1, &data2, ignores);

		if (rc == EOF)
			break;
		if (rc != 4) {
			skips++;
			continue;
		}

		skips = 0;
		sz += (sizeof(u64) * 2);
		data = realloc(data, sz);
		if (!data) {
			fprintf(stderr, "Cannot allocate memory\n");
			return -ENOMEM;
		}

		data[i++] = __cpu_to_be64(data1);
		data[i++] = __cpu_to_be64(data2);
		entries++;
	}

	entries /= 2;
	trace = (struct ibmvfc_trace_entry *)data;

	printf("Type  EVT              Time     SCSI ID  LUN      FMT   OP TMF Additional\n");
	printf("-------------------------------------------------------------------------\n");

	for (i = 0; i < entries; i++) {
		if (trace[i].type == 0 && end_only)
			continue;
		if (trace[i].type == 0xff && start_only)
			continue;

		dup = ' ';
		if (dup_chk) {
			for (j = i + 1; j < entries; j++) {
				if (trace[i].evt != trace[j].evt)
					continue;
				if (trace[i].type != trace[j].type)
					continue;
				dup = '*';
			}
		}

		if (i > 0 && trc32_to_cpu(trace[i].time) < trc32_to_cpu(trace[i-1].time))
			wrap = '+';

		if (trace[i].type == 0)
			fprintf(stdout, "%cS%c   ", wrap, dup);
		else
			fprintf(stdout, "%cE%c   ", wrap, dup);

		if (le)
			fprintf(stdout, "%016llX ", trc64_to_cpu(trace[i].evt));
		else
			fprintf(stdout, "%016llX ", trc64_to_cpu(trace[i].evt));

		fprintf(stdout, "%08X %08X %08X %s %X %X ",
			trc32_to_cpu(trace[i].time), trc32_to_cpu(trace[i].scsi_id), trc32_to_cpu(trace[i].lun),
			fmt[trace[i].fmt], trace[i].op_code, trace[i].tmf_flags);

		if (trace[i].type == 0) {
			fprintf(stdout, "xfer_len = 0x%08X\n", trc32_to_cpu(trace[i].start.xfer_len));
		} else {
			fprintf(stdout, "%02X %02X %X %X %X\n", trc16_to_cpu(trace[i].end.status),
				trc16_to_cpu(trace[i].end.error), trace[i].end.fcp_rsp_flags,
				trace[i].end.rsp_code, trace[i].end.scsi_status);
		}
	}

	fflush(stdout);
	return 0;
}
