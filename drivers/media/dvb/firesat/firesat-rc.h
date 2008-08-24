#ifndef _FIREDTV_RC_H
#define _FIREDTV_RC_H

#include <linux/types.h>

int firesat_register_rc(void);
int firesat_unregister_rc(void);
int firesat_got_remotecontrolcode(u16 code);

#endif /* _FIREDTV_RC_H */
