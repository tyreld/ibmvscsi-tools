#include <stdio.h>
#include <stdlib.h>
#include <pci/types.h>
#include <errno.h>
#include <asm/byteorder.h>

typedef unsigned long long u64;

enum ibmvscsi_crq_formats {
	VIOSRP_SRP_FORMAT	= 0x01,
	VIOSRP_MAD_FORMAT	= 0x02
};

const char *fmt [255] = {
	[VIOSRP_SRP_FORMAT] = "SRP  ",
	[VIOSRP_MAD_FORMAT] = "MAD  ",
};

enum ibmvscsi_srp_formats {
	SRP_LOGIN_REQ	= 0x00,
	SRP_TSK_MGMT	= 0x01,
	SRP_CMD		= 0x02,
	SRP_I_LOGOUT	= 0x03,
	SRP_LOGIN_RSP	= 0xc0,
	SRP_RSP		= 0xc1,
	SRP_LOGIN_REJ	= 0xc2,
	SRP_T_LOGOUT	= 0x80,
	SRP_CRED_REQ	= 0x81,
	SRP_AER_REQ	= 0x82,
	SRP_CRED_RSP	= 0x41,
	SRP_AER_RSP	= 0x42
};

const char *srp_op [255] = {
	[SRP_LOGIN_REQ]		= "Login Request      ",
	[SRP_TSK_MGMT]		= "Task Management    ",
	[SRP_CMD]		= "SRP Command        ",
	[SRP_I_LOGOUT]		= "Initiator Logout   ",
	[SRP_LOGIN_RSP]		= "Login Response     ",
	[SRP_RSP]		= "SRP Response       ",
	[SRP_LOGIN_REJ]		= "Login Reject       ",
	[SRP_T_LOGOUT]		= "Target Logout      ",
};

enum ibmvscsi_mad_types {
	MAD_EMPTY_IU_TYPE	= 0x01,
	MAD_ERROR_LOG_TYPE	= 0x02,
	MAD_ADAPTER_INFO_TYPE	= 0x03,
	MAD_CAPABILITIES_TYPE	= 0x05,
	MAD_ENABLE_FAST_FAIL	= 0x08,
};

const char *mad_op [255] = {
	[MAD_EMPTY_IU_TYPE]	= "Empty IU MAD	    ",
	[MAD_ERROR_LOG_TYPE]	= "Error Log MAD    ",
	[MAD_ADAPTER_INFO_TYPE] = "Adapter Info     ",
	[MAD_CAPABILITIES_TYPE] = "Capabilaities    ",
	[MAD_ENABLE_FAST_FAIL] 	= "Enable Fast Fail ",
};

enum task_funcs {
	SRP_TSK_ABORT_TASK	= 0x01,
	SRP_TSK_ABORT_TASK_SET	= 0x02,
	SRP_TSK_CLEAR_TASK_SET	= 0x04,
	SRP_TSK_LUN_RESET	= 0x08,
	SRP_TSK_CLEAR_ACA	= 0x40
};

const char *task_func [255] = {
	[SRP_TSK_ABORT_TASK]		= "Abort Task",
	[SRP_TSK_ABORT_TASK_SET]	= "Abort Task Set",
	[SRP_TSK_CLEAR_TASK_SET]	= "Clear Task Set",
	[SRP_TSK_LUN_RESET]		= "LUN Reset",
	[SRP_TSK_CLEAR_ACA]		= "Clear ACA"
};

struct ibmvscsi_trace_start_entry {
	u8 scsi_opcode;
	u32 scsi_lun;
	u32 xfer_len;
	u8 task_func;
	u64 task_tag;
}__attribute__((packed));

struct ibmvscsi_trace_end_entry {
	u32 cmnd_result;
	u8 status;
	u8 flags;
	u32 reason;
}__attribute__((packed));

struct ibmvscsi_trace_entry {
	u64 evt;
	u64 mftb;
	u64 time;
	u32 fmt;
	u8 op_code;
	u64 tag;
	u8 type;
#define IBMVSCSI_TRC_START	0x00
#define IBMVSCSI_TRC_DUP	0x08
#define IBMVSCSI_TRC_END	0xff
	union {
		struct ibmvscsi_trace_start_entry start;
		struct ibmvscsi_trace_end_entry end;
	};
	u8 reserved[4];
	char srp_data[64];
}__attribute__((packed, aligned (8)));

int main (int argc, char *argv[])
{
	char line[1000];
	int rc, len, entries = 0, i = 0, j, sz = 0, skips = 0;
	struct ibmvscsi_trace_entry *trace;
	u64 *data = NULL;
	u64 ignore64, data1, data2;
	char ignores[1000];
	int end_only = 0, start_only = 0, dup_chk = 0, add_info = 0;
	int num = 4096, xmon = 0;
	char dup = ' ';
	char wrap = ' ';

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-e") == 0)
			end_only = 1;
		else if (strcmp(argv[i], "-s") == 0)
			start_only = 1;
		else if (strcmp(argv[i], "-c") == 0)
			dup_chk = 1;
		else if (strcmp(argv[i], "-a") == 0)
			add_info = 1;
		else if (strcmp(argv[i], "-x") == 0)
			xmon = 1;
		else if (strcmp(argv[i], "-n") == 0) {
			i++;
			num = atoi(argv[i]);
		}
	}

	for (i = 0, entries = 0; skips < 10;) {
		if (xmon)
			rc = scanf("%llx %llx %llx  %18c\n", &ignore64, &data1, &data2, ignores);
		else
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

		data[i++] = (data1);
		data[i++] = (data2);
		entries++;
	}

	entries /= (sizeof(struct ibmvscsi_trace_entry) / 16);
	trace = (struct ibmvscsi_trace_entry *)data;

	printf("Index  Type  EVT              mftb            Time   FMT   Tag              OP     \n");
	printf("------------------------------------------------------------------------------------------\n");

	for (i = 0; i < entries && i < num; i++) {
		if (trace[i].type == 0 && end_only)
			continue;
		if (trace[i].type == 0xff && start_only)
			continue;

		dup = ' ';
		if (dup_chk) {
			for (j = i + 1; j < entries && j < num; j++) {
				if (trace[i].evt != trace[j].evt)
					continue;
				if (trace[i].type != trace[j].type)
					continue;
				dup = '*';
			}
		}

		if (i > 0 && (trace[i].time) < (trace[i-1].time))
			wrap = '+';

		fprintf(stdout, "%-4d   ", i);

		switch (trace[i].type) {
		case IBMVSCSI_TRC_START:
			fprintf(stdout, "%cS%c   ", wrap, dup);
			break;
		case IBMVSCSI_TRC_DUP:
			fprintf(stdout, "%cD%c   ", wrap, dup);
			break;
		case IBMVSCSI_TRC_END:
			fprintf(stdout, "%cE%c   ", wrap, dup);
			break;
		default:
			fprintf(stdout, "%cU%c   ", wrap, dup);
		}

		fprintf(stdout, "%016llX ", (trace[i].evt));

		fprintf(stdout, "%016llX %lu %s %016llX ",
			trace[i].mftb, trace[i].time, fmt[trace[i].fmt], trace[i].tag);

		if (trace[i].fmt == VIOSRP_MAD_FORMAT)
			fprintf(stdout, "%s\n", mad_op[trace[i].op_code]);
		else
			fprintf(stdout, "%s\n", srp_op[trace[i].op_code]);

		if (add_info) {
		switch (trace[i].type) {
		case IBMVSCSI_TRC_START:
			fprintf(stdout, "\t\tscsi_opcode = 0x%02X\n\t\tscsi_lun = %08X\n\t\txfer_len = 0x%08X\n",
				trace[i].start.scsi_opcode, trace[i].start.scsi_lun, trace[i].start.xfer_len);
			fprintf(stdout, "\t\ttask_tag = %016llX\n\t\ttask_func = %s\n",
				trace[i].start.task_tag, task_func[trace[i].start.task_func]);
			if (trace[i].op_code == SRP_CMD) {
				fprintf(stdout, "\t\tCDB = ");
				for (j = 0; j < 16; j++)
					fprintf(stdout, "%02X ", trace[i].srp_data[j+32]);
				fprintf(stdout, "\n");
			}
			break;
		case IBMVSCSI_TRC_END:
			fprintf(stdout, "\t\tresult = %08X\n\t\tstatus = %02X\n\t\tflags = %02X\n\t\treason = %08X\n",
				trace[i].end.cmnd_result, trace[i].end.status, trace[i].end.flags,
				trace[i].end.reason);
			break;
		case IBMVSCSI_TRC_DUP:
			fprintf(stdout, "\t\tSRP = ");
			for (j = 0; j < 64; j++)
				fprintf(stdout, "%02X ", trace[i].srp_data[j]);
			fprintf(stdout, "\n");
			break;
		default:
			fprintf(stdout, "\t\t**** UNKNOWN TRACE TYPE ****\n");
		}
		}
	}

	fflush(stdout);
	return 0;
}
