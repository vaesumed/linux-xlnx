#ifndef _HWA742_H
#define _HWA742_H

struct hwa742_platform_data {
	struct clk 	*sys_ck;
	unsigned	te_connected:1;
};

#endif
