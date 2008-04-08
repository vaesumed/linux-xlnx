#ifndef __UWB_BUGS_H__
#define __UWB_BUGS_H__

#include <linux/uwb/bugs-macros.h>

#define UWB_BUG_010612024004	1
#define UWB_BUG_445		1
#define UWB_BUG_514		1
#define UWB_BUG_543		1
#define UWB_BUG_548		1
#define UWB_BUG_573		0


#define UWB_BUGS_ENABLED "445 514 543 548 010612024004"

enum { UWB_BUG_COUNT = 6 };

#endif /* #ifndef __UWB_BUGS_H__ */
