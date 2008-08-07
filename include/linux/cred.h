/* Credentials management
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_CRED_H
#define _LINUX_CRED_H

#define current_fsuid()		(current->fsuid)
#define current_fsgid()		(current->fsgid)
#define current_cap()		(current->cap_effective)

#define current_fsuid_fsgid(_uid, _gid)		\
do {						\
	*(_uid) = current->fsuid;		\
	*(_gid) = current->fsgid;		\
} while(0)

#endif /* _LINUX_CRED_H */
