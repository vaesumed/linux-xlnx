/*
 * Novell NCP Redirector for Linux
 * Author: James Turner
 *
 * This file contains functions for accessing files through the daemon.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "vfs.h"
#include "commands.h"
#include "nwerror.h"

/*
 * StripTrailingDots was added because some apps will
 * try and create a file name with a trailing dot.  NetWare
 * doesn't like this and will return an error.
 */
static int StripTrailingDots = 1;

int Novfs_Get_Connected_Server_List(unsigned char ** ServerList, struct schandle *session_id)
{
	GET_CONNECTED_SERVER_LIST_REQUEST req;
	PGET_CONNECTED_SERVER_LIST_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;

	*ServerList = NULL;

	req.command.command_type = VFS_COMMAND_GET_CONNECTED_SERVER_LIST;
	copy_session_id(&req.command.session_id, session_id);

	retCode =
	    Queue_Daemon_Command(&req, sizeof(req), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		DbgPrint("Novfs_Get_Connected_Server_List: reply\n");
		replylen -= sizeof(struct novfs_command_reply_header);
		if (!reply->Reply.error_code && replylen) {
			memcpy(reply, reply->List, replylen);
			*ServerList = (unsigned char *) reply;
			retCode = 0;
		} else {
			kfree(reply);
			retCode = -ENOENT;
		}
	}
	return (retCode);
}

int Novfs_Get_Server_Volume_List(struct qstr *Server, unsigned char ** VolumeList,
				 struct schandle *session_id)
{
	PGET_SERVER_VOLUME_LIST_REQUEST req;
	PGET_SERVER_VOLUME_LIST_REPLY reply = NULL;
	unsigned long replylen = 0, reqlen;
	int retCode;

	*VolumeList = NULL;
	reqlen = sizeof(GET_SERVER_VOLUME_LIST_REQUEST) + Server->len;
	req = kmalloc(reqlen, GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	req->command.command_type = VFS_COMMAND_GET_SERVER_VOLUME_LIST;
	req->Length = Server->len;
	memcpy(req->Name, Server->name, Server->len);
	copy_session_id(&req->command.session_id, session_id);

	retCode = Queue_Daemon_Command(req, reqlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
	if (reply) {
		DbgPrint("Novfs_Get_Server_Volume_List: reply\n");
		mydump(replylen, reply);
		replylen -= sizeof(struct novfs_command_reply_header);

		if (!reply->Reply.error_code && replylen) {
			memcpy(reply, reply->List, replylen);
			*VolumeList = (unsigned char *) reply;
			retCode = 0;
		} else {
			kfree(reply);
			retCode = -ENOENT;
		}
	}
	kfree(req);
	return retCode;
}

int Novfs_Get_File_Info(unsigned char * Path, struct entry_info *Info, struct schandle *session_id)
{
	PVERIFY_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	PVERIFY_FILE_REQUEST cmd;
	int cmdlen;
	int retCode = -ENOENT;
	int pathlen;

	DbgPrint("%s: Path = %s\n", __func__, Path);

	Info->mode = S_IFDIR | 0700;
	Info->uid = current->uid;
	Info->gid = current->gid;
	Info->size = 0;
	Info->atime = Info->mtime = Info->ctime = CURRENT_TIME;

	if (Path && *Path) {
		pathlen = strlen(Path);
		if (StripTrailingDots) {
			if ('.' == Path[pathlen - 1])
				pathlen--;
		}
		cmdlen = offsetof(VERIFY_FILE_REQUEST, path) + pathlen;
		cmd = (PVERIFY_FILE_REQUEST) Novfs_Malloc(cmdlen, GFP_KERNEL);
		if (cmd) {
			cmd->command.command_type = VFS_COMMAND_VERIFY_FILE;
			cmd->command.sequence_number = 0;
			copy_session_id(&cmd->command.session_id, session_id);
			cmd->pathLen = pathlen;
			memcpy(cmd->path, Path, cmd->pathLen);

			retCode =
			    Queue_Daemon_Command(cmd, cmdlen, NULL, 0,
						 (void *)&reply, &replylen,
						 INTERRUPTIBLE);

			if (reply) {

				if (reply->Reply.error_code) {
					retCode = -ENOENT;
				} else {
					Info->type = 3;
					Info->mode = S_IRWXU;

					if (reply->
					    fileMode & NW_ATTRIBUTE_DIRECTORY) {
						Info->mode |= S_IFDIR;
					} else {
						Info->mode |= S_IFREG;
					}

					if (reply->
					    fileMode & NW_ATTRIBUTE_READ_ONLY) {
						Info->mode &= ~(S_IWUSR);
					}

					Info->uid = current->euid;
					Info->gid = current->egid;
					Info->size = reply->fileSize;
					Info->atime.tv_sec =
					    reply->lastAccessTime;
					Info->atime.tv_nsec = 0;
					Info->mtime.tv_sec = reply->modifyTime;
					Info->mtime.tv_nsec = 0;
					Info->ctime.tv_sec = reply->createTime;
					Info->ctime.tv_nsec = 0;
					DbgPrint("%s: replylen=%d sizeof(VERIFY_FILE_REPLY)=%d\n", __func__, replylen, sizeof(VERIFY_FILE_REPLY));
					if (replylen > sizeof(VERIFY_FILE_REPLY)) {
						unsigned int *lp = &reply->fileMode;
						lp++;
						DbgPrint("%s: extra data 0x%x\n", __func__, *lp);
						Info->mtime.tv_nsec = *lp;
					}
					retCode = 0;
				}

				kfree(reply);
			}
			kfree(cmd);
		}
	}

	DbgPrint("%s: return 0x%x\n", __func__, retCode);
	return (retCode);
}

int Novfs_GetX_File_Info(char *Path, const char *Name, char *buffer,
			 ssize_t buffer_size, ssize_t * dataLen,
			 session_t *session_id)
{
	struct novfs_xa_get_reply *reply = NULL;
	unsigned long replylen = 0;
	struct novfs_xa_get_request *cmd;
	int cmdlen;
	int retCode = -ENOENT;

	int namelen = strlen(Name);
	int pathlen = strlen(Path);

	DbgPrint("%s: xattr: Path = %s, pathlen = %i, Name = %s, namelen = %i\n", __func__, Path, pathlen, Name, namelen);

	if (namelen > MAX_XATTR_NAME_LEN)
		return -ENOATTR;

	cmdlen = offsetof(struct novfs_xa_get_request, data) + pathlen + 1 + namelen + 1;	// two '\0'
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->command.command_type = VFS_COMMAND_GET_EXTENDED_ATTRIBUTE;
		cmd->command.sequence_number = 0;
		copy_session_id(&cmd->command.session_id, session_id);

		cmd->path_len = pathlen;
		memcpy(cmd->data, Path, cmd->path_len + 1);	//+ '\0'

		cmd->name_len = namelen;
		memcpy(cmd->data + cmd->path_len + 1, Name, cmd->name_len + 1);

		DbgPrint("%s xattr: PXA_GET_REQUEST BEGIN\n", __func__);
		DbgPrint("%s xattr: Queue_Daemon_Command %d\n", __func__, cmd->command.command_type);
		DbgPrint("%s xattr: command.session_id = %d\n", __func__, cmd->command.session_id);
		DbgPrint("%s xattr: path_len = %d\n", __func__, cmd->path_len);
		DbgPrint("%s xattr: Path = %s\n", __func__, cmd->data);
		DbgPrint("%s xattr: name_len = %d\n", __func__, cmd->name_len);
		DbgPrint("%s xattr: name = %s\n", __func__, (cmd->data + cmd->path_len + 1));
		DbgPrint("%s xattr: PXA_GET_REQUEST END\n", __func__);

		retCode = Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);

		if (reply) {
			if (reply->reply.error_code) {
				DbgPrint("%s xattr: reply->reply.error_code=%d, %X\n", __func__, reply->reply.error_code, reply->reply.error_code);
				DbgPrint("%s xattr: replylen=%d\n", __func__, replylen);

				//0xC9 = EA not found (C9), 0xD1 = EA access denied
				if ((reply->reply.error_code == 0xC9) || (reply->reply.error_code == 0xD1))
					retCode = -ENOATTR;
				else
					retCode = -ENOENT;
			} else {

				*dataLen =
				    replylen - sizeof(struct novfs_command_reply_header);
				DbgPrint("%s xattr: replylen=%u, dataLen=%u\n", __func__, replylen, *dataLen);

				if (buffer_size >= *dataLen) {
					DbgPrint("%s xattr: copying to buffer from &reply->pData\n", __func__);
					memcpy(buffer, &reply->data, *dataLen);

					retCode = 0;
				} else {
					DbgPrint("%s xattr: (!!!) buffer is smaller then reply\n", __func__);
					retCode = -ERANGE;
				}
				DbgPrint("%s xattr: /dumping buffer\n", __func__);
				mydump(*dataLen, buffer);
				DbgPrint("%s xattr: \\after dumping buffer\n", __func__);
			}

			kfree(reply);
		} else {
			DbgPrint("%s xattr: reply = NULL\n", __func__);
		}
		kfree(cmd);

	}

	return retCode;
}

int Novfs_SetX_File_Info(char *path, const char *name, const void *value,
			 unsigned long value_len, unsigned long *bytesWritten,
			 int flags, struct schandle *session_id)
{
	struct novfs_xa_set_reply *reply = NULL;
	unsigned long replylen = 0;
	struct novfs_xa_set_request *cmd;
	int cmdlen;
	int retCode = -ENOENT;
	int name_len = strlen(name);
	int path_len = strlen(path);

	DbgPrint("%s xattr: path = %s, path_len = %i, name = %s, name_len = %i, value_len = %u\n", __func__,
	     path, path_len, name, name_len, value_len);

	if (name_len > MAX_XATTR_NAME_LEN)
		return -ENOATTR;

	cmdlen = offsetof(struct novfs_xa_set_request, data) + path_len + 1 + name_len + 1 + value_len;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->command.command_type = VFS_COMMAND_SET_EXTENDED_ATTRIBUTE;
		cmd->command.sequence_number = 0;
		copy_session_id(&cmd->command.session_id, session_id);

		cmd->flags = flags;
		cmd->path_len = path_len;
		memcpy(cmd->data, path, cmd->path_len + 1);	//+ '\0'

		cmd->name_len = name_len;
		memcpy(cmd->data + cmd->path_len + 1, name, cmd->name_len + 1);

		cmd->value_len = value_len;
		memcpy(cmd->data + cmd->path_len + 1 + cmd->name_len + 1, value, value_len);

		DbgPrint("%s xattr: struct novfs_xa_set_request BEGIN\n", __func__);
		DbgPrint("%s xattr: Queue_Daemon_Command %d\n", __func__, cmd->command.command_type);
		DbgPrint("%s xattr: command.session_id = %d\n", __func__, cmd->command.session_id);
		DbgPrint("%s xattr: path_len = %d\n", __func__, cmd->path_len);
		DbgPrint("%s xattr: Path = %s\n", __func__, cmd->data);
		DbgPrint("%s xattr: name_len = %d\n", __func__, cmd->name_len);
		DbgPrint("%s xattr: name = %s\n", __func__, (cmd->data + cmd->path_len + 1));
		mydump(value_len < 16 ? value_len : 16, (char *)value);

		DbgPrint("%s xattr: struct novfs_xa_set_request END\n", __func__);

		retCode = Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);

		if (reply) {
			if (reply->reply.error_code) {
				DbgPrint("%s xattr: reply->reply.error_code=%d, %X\n", __func__, reply->reply.error_code, reply->reply.error_code);
				DbgPrint("%s xattr: replylen=%d\n", __func__, replylen);

				retCode = -reply->reply.error_code;	//-ENOENT;
			} else {

				DbgPrint("%s xattr: replylen=%u, real len = %u\n", __func__, replylen, replylen - sizeof(struct novfs_command_reply_header));
				memcpy(bytesWritten, &reply->data,
				       replylen - sizeof(struct novfs_command_reply_header));

				retCode = 0;
			}

			kfree(reply);
		} else {
			DbgPrint("%s xattr: reply = NULL\n", __func__);
		}
		kfree(cmd);

	}

	return retCode;
}

int Novfs_ListX_File_Info(char *path, char *buffer, ssize_t buffer_size,
			  ssize_t * dataLen, struct schandle *session_id)
{
	struct novfs_xa_list_reply *reply = NULL;
	unsigned long replylen = 0;
	PVERIFY_FILE_REQUEST cmd;
	int cmdlen;
	int retCode = -ENOENT;

	int pathlen = strlen(path);
	DbgPrint("%s xattr: path = %s, pathlen = %i\n", __func__, path,
		 pathlen);

	*dataLen = 0;
	cmdlen = offsetof(VERIFY_FILE_REQUEST, path) + pathlen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->command.command_type = VFS_COMMAND_LIST_EXTENDED_ATTRIBUTES;
	cmd->command.sequence_number = 0;
	copy_session_id(&cmd->command.session_id, session_id);
	cmd->pathLen = pathlen;
	memcpy(cmd->path, path, cmd->pathLen + 1);	/* + '\0' */
	DbgPrint("%s xattr: PVERIFY_FILE_REQUEST BEGIN\n", __func__);
	DbgPrint("%s xattr: Queue_Daemon_Command %d\n", __func__,
		 cmd->command.command_type);
	DbgPrint("%s xattr: command.session_id = %d\n", __func__, cmd->command.session_id);
	DbgPrint("%s xattr: pathLen = %d\n", __func__, cmd->pathLen);
	DbgPrint("%s xattr: path = %s\n", __func__, cmd->path);
	DbgPrint("%s xattr: PVERIFY_FILE_REQUEST END\n", __func__);

	retCode = Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);

	if (!reply) {
		DbgPrint("%s xattr: reply = NULL\n", __func__);
		goto exit;
	}

	if (reply->reply.error_code) {
		DbgPrint("%s xattr: reply->reply.error_code=%d, %X\n", __func__,
			 reply->reply.error_code, reply->reply.error_code);
		DbgPrint("%s xattr: replylen=%d\n", __func__, replylen);
		retCode = -ENOENT;
		goto error_reply;
	}
	*dataLen = replylen - sizeof(struct novfs_command_reply_header);
	DbgPrint("%s xattr: replylen=%u, dataLen=%u\n", __func__, replylen,
		 *dataLen);

	if (buffer_size >= *dataLen) {
		DbgPrint("%s xattr: copying to buffer from &reply->data\n",
			 __func__);
		memcpy(buffer, &reply->data, *dataLen);
	} else {
		DbgPrint("%s xattr: (!!!) buffer is smaller then reply\n",
			 __func__);
		retCode = -ERANGE;
	}
	DbgPrint("%s xattr: /dumping buffer\n", __func__);
	mydump(*dataLen, buffer);
	DbgPrint("%s xattr: \\after dumping buffer\n", __func__);

	retCode = 0;

error_reply:
	kfree(reply);
exit:
	kfree(cmd);
	return retCode;
}

static int begin_directory_enumerate(unsigned char *Path, int PathLen, HANDLE *EnumHandle, struct schandle *session_id)
{
	PBEGIN_ENUMERATE_DIRECTORY_REQUEST cmd;
	PBEGIN_ENUMERATE_DIRECTORY_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode, cmdlen;

	*EnumHandle = NULL;

	cmdlen = offsetof(BEGIN_ENUMERATE_DIRECTORY_REQUEST, path) + PathLen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->command.command_type = VFS_COMMAND_START_ENUMERATE;
		cmd->command.sequence_number = 0;
		copy_session_id(&cmd->command.session_id, session_id);

		cmd->pathLen = PathLen;
		memcpy(cmd->path, Path, PathLen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
/*
 *      retCode = Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply, &replylen, 0);
 */
		if (reply) {
			if (reply->Reply.error_code) {
				retCode = -EIO;
			} else {
				*EnumHandle = reply->enumerateHandle;
				retCode = 0;
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

static int end_directory_enumerate(HANDLE EnumHandle, struct schandle *session_id)
{
	END_ENUMERATE_DIRECTORY_REQUEST cmd;
	PEND_ENUMERATE_DIRECTORY_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode;

	cmd.command.command_type = VFS_COMMAND_END_ENUMERATE;
	cmd.command.sequence_number = 0;
	copy_session_id(&cmd.command.session_id, session_id);

	cmd.enumerateHandle = EnumHandle;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, 0);
	if (reply) {
		retCode = 0;
		if (reply->Reply.error_code) {
			retCode = -EIO;
		}
		kfree(reply);
	}

	return (retCode);
}

static int directory_enumerate_ex(HANDLE *EnumHandle, struct schandle *session_id, int *Count, struct entry_info **PInfo, int Interrupt)
{
	ENUMERATE_DIRECTORY_EX_REQUEST cmd;
	PENUMERATE_DIRECTORY_EX_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;
	struct entry_info *info;
	PENUMERATE_DIRECTORY_EX_DATA data;
	int isize;

	if (PInfo) {
		*PInfo = NULL;
	}
	*Count = 0;

	cmd.command.command_type = VFS_COMMAND_ENUMERATE_DIRECTORY_EX;
	cmd.command.sequence_number = 0;
	copy_session_id(&cmd.command.session_id, session_id);

	cmd.enumerateHandle = *EnumHandle;
	cmd.pathLen = 0;
	cmd.path[0] = '\0';

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, Interrupt);

	if (reply) {
		retCode = 0;
		/*
		 * The VFS_COMMAND_ENUMERATE_DIRECTORY call can return an
		 * error but there could still be valid data.
		 */

		if (!reply->Reply.error_code ||
		    ((replylen > sizeof(struct novfs_command_reply_header)) &&
		     (reply->enumCount > 0))) {
			DbgPrint("directory_enumerate_ex: isize=%d\n",
				 replylen);
			data = (PENUMERATE_DIRECTORY_EX_DATA) ((char *)reply + sizeof(ENUMERATE_DIRECTORY_EX_REPLY));
			isize = replylen - sizeof(PENUMERATE_DIRECTORY_EX_REPLY) - reply->enumCount * offsetof(ENUMERATE_DIRECTORY_EX_DATA, name);
			isize += (reply->enumCount * offsetof(struct entry_info, name));

			if (PInfo) {
				*PInfo = info = Novfs_Malloc(isize, GFP_KERNEL);
				if (*PInfo) {
					DbgPrint("directory_enumerate_ex1: data=0x%p info=0x%p\n", data, info);
					*Count = reply->enumCount;
					do {
						DbgPrint("directory_enumerate_ex2: data=0x%p length=%d\n", data);

						info->type = 3;
						info->mode = S_IRWXU;

						if (data->mode & NW_ATTRIBUTE_DIRECTORY) {
							info->mode |= S_IFDIR;
							info->mode |= S_IXUSR;
						} else {
							info->mode |= S_IFREG;
						}

						if (data->mode & NW_ATTRIBUTE_READ_ONLY) {
							info->mode &= ~(S_IWUSR);
						}

						if (data->mode & NW_ATTRIBUTE_EXECUTE) {
							info->mode |= S_IXUSR;
						}

						info->uid = current->euid;
						info->gid = current->egid;
						info->size = data->size;
						info->atime.tv_sec = data->lastAccessTime;
						info->atime.tv_nsec = 0;
						info->mtime.tv_sec = data->modifyTime;
						info->mtime.tv_nsec = 0;
						info->ctime.tv_sec = data->createTime;
						info->ctime.tv_nsec = 0;
						info->namelength = data->nameLen;
						memcpy(info->name, data->name, data->nameLen);
						data = (PENUMERATE_DIRECTORY_EX_DATA)&data->name[data->nameLen];
						replylen = (int)((char *)&info->name[info->namelength] - (char *)info);
						DbgPrint("directory_enumerate_ex3: info=0x%p\n", info);
						mydump(replylen, info);

						info = (struct entry_info *)&info->name[info->namelength];

					} while (--reply->enumCount);
				}
			}

			if (reply->Reply.error_code) {
				retCode = -1;	/* Eof of data */
			}
			*EnumHandle = reply->enumerateHandle;
		} else {
			retCode = -ENODATA;
		}
		kfree(reply);
	}

	return (retCode);
}

int Novfs_Get_Directory_ListEx(unsigned char * Path, HANDLE * EnumHandle, int *Count,
			       struct entry_info **Info, struct schandle *session_id)
{
	int retCode = -ENOENT;

	if (Count)
		*Count = 0;
	if (Info)
		*Info = NULL;

	if ((HANDLE) - 1 == *EnumHandle) {
		return (-ENODATA);
	}

	if (NULL == *EnumHandle)
		retCode = begin_directory_enumerate(Path, strlen(Path), EnumHandle, session_id);

	if (*EnumHandle) {
		retCode = directory_enumerate_ex(EnumHandle, session_id, Count, Info, INTERRUPTIBLE);
		if (retCode) {
			end_directory_enumerate(*EnumHandle, session_id);
			if (-1 == retCode) {
				retCode = 0;
				*EnumHandle = Uint32toHandle(-1);
			}
		}
	}
	return (retCode);
}

int Novfs_Open_File(unsigned char * Path, int Flags, struct entry_info *Info, HANDLE * handle,
		    session_t session_id)
{
	POPEN_FILE_REQUEST cmd;
	POPEN_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	*handle = NULL;

	cmdlen = offsetof(OPEN_FILE_REQUEST, path) + pathlen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->command.command_type = VFS_COMMAND_OPEN_FILE;
		cmd->command.sequence_number = 0;
		cmd->command.session_id = session_id;

		cmd->access = 0;

		if (!(Flags & O_WRONLY) || (Flags & O_RDWR)) {
			cmd->access |= NWD_ACCESS_READ;
		}

		if ((Flags & O_WRONLY) || (Flags & O_RDWR)) {
			cmd->access |= NWD_ACCESS_WRITE;
		}

		switch (Flags & (O_CREAT | O_EXCL | O_TRUNC)) {
		case O_CREAT:
			cmd->disp = NWD_DISP_OPEN_ALWAYS;
			break;

		case O_CREAT | O_EXCL:
			cmd->disp = NWD_DISP_CREATE_NEW;
			break;

		case O_TRUNC:
			cmd->disp = NWD_DISP_CREATE_ALWAYS;
			break;

		case O_CREAT | O_TRUNC:
			cmd->disp = NWD_DISP_CREATE_ALWAYS;
			break;

		case O_CREAT | O_EXCL | O_TRUNC:
			cmd->disp = NWD_DISP_CREATE_NEW;
			break;

		default:
			cmd->disp = NWD_DISP_OPEN_EXISTING;
			break;
		}

		cmd->mode = NWD_SHARE_READ | NWD_SHARE_WRITE | NWD_SHARE_DELETE;

		cmd->pathLen = pathlen;
		memcpy(cmd->path, Path, pathlen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {
			if (reply->Reply.error_code) {
				if (NWE_OBJECT_EXISTS == reply->Reply.error_code) {
					retCode = -EEXIST;
				} else if (NWE_ACCESS_DENIED ==
					   reply->Reply.error_code) {
					retCode = -EACCES;
				} else if (NWE_FILE_IN_USE ==
					   reply->Reply.error_code) {
					retCode = -EBUSY;
				} else {
					retCode = -ENOENT;
				}
			} else {
				*handle = reply->handle;
				retCode = 0;
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

int Novfs_Create(unsigned char * Path, int DirectoryFlag, session_t session_id)
{
	PCREATE_FILE_REQUEST cmd;
	PCREATE_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	cmdlen = offsetof(CREATE_FILE_REQUEST, path) + pathlen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->command.command_type = VFS_COMMAND_CREATE_FILE;
		if (DirectoryFlag) {
			cmd->command.command_type = VFS_COMMAND_CREATE_DIRECOTRY;
		}
		cmd->command.sequence_number = 0;
		cmd->command.session_id = session_id;

		cmd->pathlength = pathlen;
		memcpy(cmd->path, Path, pathlen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {
			retCode = 0;
			if (reply->Reply.error_code) {
				retCode = -EIO;
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

int Novfs_Close_File(HANDLE handle, session_t session_id)
{
	CLOSE_FILE_REQUEST cmd;
	PCLOSE_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode;

	cmd.command.command_type = VFS_COMMAND_CLOSE_FILE;
	cmd.command.sequence_number = 0;
	cmd.command.session_id = session_id;

	cmd.handle = handle;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, 0);
	if (reply) {
		retCode = 0;
		if (reply->Reply.error_code) {
			retCode = -EIO;
		}
		kfree(reply);
	}
	return (retCode);
}

int Novfs_Read_File(HANDLE handle, unsigned char __user *buffer, size_t *bytes,
		    loff_t *offset, session_t session_id)
{
	READ_FILE_REQUEST cmd;
	PREAD_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;
	size_t len;

	len = *bytes;
	*bytes = 0;

	if ((offsetof(READ_FILE_REPLY, data) + len) > MaxIoSize) {
		len = MaxIoSize - offsetof(READ_FILE_REPLY, data);
		len = (len / PAGE_SIZE) * PAGE_SIZE;
	}

	cmd.command.command_type = VFS_COMMAND_READ_FILE;
	cmd.command.sequence_number = 0;
	cmd.command.session_id = session_id;

	cmd.handle = handle;
	cmd.len = len;
	cmd.offset = *offset;

	retCode = Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);

	DbgPrint("%s: Queue_Daemon_Command 0x%x replylen=%d\n", __func__, retCode, replylen);

	if (!retCode) {
		if (reply->Reply.error_code) {
			if (NWE_FILE_IO_LOCKED == reply->Reply.error_code) {
				retCode = -EBUSY;
			} else {
				retCode = -EIO;
			}
		} else {
			replylen -= offsetof(READ_FILE_REPLY, data);
			if (replylen > 0) {
				replylen -= copy_to_user(buffer, reply->data, replylen);
				*bytes = replylen;
			}
		}
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("%s: *bytes=0x%x retCode=0x%x\n", __func__, *bytes, retCode);

	return (retCode);
}

int Novfs_Read_Pages(HANDLE handle, struct data_list *DList, int DList_Cnt,
		     size_t * bytes, loff_t * offset, session_t session_id)
{
	READ_FILE_REQUEST cmd;
	PREAD_FILE_REPLY reply = NULL;
	READ_FILE_REPLY lreply;
	unsigned long replylen = 0;
	int retCode = 0;
	size_t len;

	len = *bytes;
	*bytes = 0;

	DbgPrint
	    ("Novfs_Read_Pages: handle=0x%p Dlst=0x%p Dlcnt=%d bytes=%d offset=%lld session_id=0x%p:%p\n",
	     handle, DList, DList_Cnt, len, *offset, session_id.hTypeId,
	     session_id.hId);

	cmd.command.command_type = VFS_COMMAND_READ_FILE;
	cmd.command.sequence_number = 0;
	cmd.command.session_id = session_id;

	cmd.handle = handle;
	cmd.len = len;
	cmd.offset = *offset;

	/*
	 * Dlst first entry is reserved for reply header.
	 */
	DList[0].page = NULL;
	DList[0].offset = &lreply;
	DList[0].len = offsetof(READ_FILE_REPLY, data);
	DList[0].rwflag = DLWRITE;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), DList, DList_Cnt,
				 (void *)&reply, &replylen, INTERRUPTIBLE);

	DbgPrint("Novfs_Read_Pages: Queue_Daemon_Command 0x%x\n", retCode);

	if (!retCode) {
		if (reply) {
			memcpy(&lreply, reply, sizeof(lreply));
		}

		if (lreply.Reply.error_code) {
			if (NWE_FILE_IO_LOCKED == lreply.Reply.error_code) {
				retCode = -EBUSY;
			} else {
				retCode = -EIO;
			}
		}
		*bytes = replylen - offsetof(READ_FILE_REPLY, data);
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("Novfs_Read_Pages: retCode=0x%x\n", retCode);

	return (retCode);
}

int Novfs_Write_File(HANDLE handle, const char __user *buffer,
		     size_t *bytes, loff_t *offset, session_t session_id)
{
	WRITE_FILE_REQUEST cmd;
	PWRITE_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	size_t len;

	unsigned long boff;
	struct page **pages;
	struct data_list *dlist;
	int res = 0, npage, i;
	WRITE_FILE_REPLY lreply;

	len = *bytes;
	cmdlen = offsetof(WRITE_FILE_REQUEST, data);

	*bytes = 0;

	memset(&lreply, 0, sizeof(lreply));

	DbgPrint("%s: cmdlen=%ld len=%ld\n", __func__, cmdlen, len);

	if ((cmdlen + len) > MaxIoSize) {
		len = MaxIoSize - cmdlen;
		len = (len / PAGE_SIZE) * PAGE_SIZE;
	}
	cmd.command.command_type = VFS_COMMAND_WRITE_FILE;
	cmd.command.sequence_number = 0;
	cmd.command.session_id = session_id;
	cmd.handle = handle;
	cmd.len = len;
	cmd.offset = *offset;

	DbgPrint("%s: cmdlen=%ld len=%ld\n", __func__, cmdlen, len);

	npage =
	    (((unsigned long)buffer & ~PAGE_MASK) + len +
	     (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	dlist = Novfs_Malloc(sizeof(struct data_list) * (npage + 1), GFP_KERNEL);
	if (NULL == dlist) {
		return (-ENOMEM);
	}

	pages = Novfs_Malloc(sizeof(struct page *) * npage, GFP_KERNEL);

	if (NULL == pages) {
		kfree(dlist);
		return (-ENOMEM);
	}

	down_read(&current->mm->mmap_sem);

	res = get_user_pages(current, current->mm, (unsigned long)buffer, npage, 0,	/* read type */
			     0,	/* don't force */
			     pages, NULL);

	up_read(&current->mm->mmap_sem);

	DbgPrint("%s: res=%d\n", __func__, res);

	if (res > 0) {
		boff = (unsigned long)buffer & ~PAGE_MASK;

		flush_dcache_page(pages[0]);
		dlist[0].page = pages[0];
		dlist[0].offset = (char *)boff;
		dlist[0].len = PAGE_SIZE - boff;
		dlist[0].rwflag = DLREAD;

		if (dlist[0].len > len) {
			dlist[0].len = len;
		}

		DbgPrint("%s: page=0x%p offset=0x%p len=%d\n", __func__,
			 dlist[0].page, dlist[0].offset, dlist[0].len);

		boff = dlist[0].len;

		DbgPrint("%s: len=%d boff=%d\n", __func__, len, boff);

		for (i = 1; (i < res) && (boff < len); i++) {
			flush_dcache_page(pages[i]);

			dlist[i].page = pages[i];
			dlist[i].offset = NULL;
			dlist[i].len = len - boff;
			if (dlist[i].len > PAGE_SIZE) {
				dlist[i].len = PAGE_SIZE;
			}
			dlist[i].rwflag = DLREAD;

			boff += dlist[i].len;
			DbgPrint("%s: %d: page=0x%p offset=0x%p len=%d\n", __func__,
			     i, dlist[i].page, dlist[i].offset, dlist[i].len);
		}

		dlist[i].page = NULL;
		dlist[i].offset = &lreply;
		dlist[i].len = sizeof(lreply);
		dlist[i].rwflag = DLWRITE;
		res++;

		DbgPrint("%s: buffer=0x%p boff=0x%x len=%d\n", __func__,
			 buffer, boff, len);

		retCode =
		    Queue_Daemon_Command(&cmd, cmdlen, dlist, res,
					 (void *)&reply, &replylen,
					 INTERRUPTIBLE);

	} else {
		char *kdata;

		res = 0;

		kdata = Novfs_Malloc(len, GFP_KERNEL);
		if (kdata) {
			len -= copy_from_user(kdata, buffer, len);
			dlist[0].page = NULL;
			dlist[0].offset = kdata;
			dlist[0].len = len;
			dlist[0].rwflag = DLREAD;

			dlist[1].page = NULL;
			dlist[1].offset = &lreply;
			dlist[1].len = sizeof(lreply);
			dlist[1].rwflag = DLWRITE;

			retCode =
			    Queue_Daemon_Command(&cmd, cmdlen, dlist, 2,
						 (void *)&reply, &replylen,
						 INTERRUPTIBLE);

			kfree(kdata);
		}
	}

	DbgPrint("%s: retCode=0x%x reply=0x%p\n", __func__, retCode, reply);

	if (!retCode) {
		switch (lreply.Reply.error_code) {
		case 0:
			*bytes = (size_t) lreply.bytesWritten;
			retCode = 0;
			break;

		case NWE_INSUFFICIENT_SPACE:
			retCode = -ENOSPC;
			break;

		case NWE_ACCESS_DENIED:
			retCode = -EACCES;
			break;

		default:
			retCode = -EIO;
			break;
		}
	}

	if (res) {
		for (i = 0; i < res; i++) {
			if (dlist[i].page) {
				page_cache_release(dlist[i].page);
			}
		}
	}

	kfree(pages);
	kfree(dlist);

	DbgPrint("%s: *bytes=0x%x retCode=0x%x\n", __func__, *bytes,
		 retCode);

	return (retCode);
}

/*
 *  Arguments: HANDLE handle - novfsd file handle
 *             struct page *Page - Page to be written out
 *             session_t session_id - novfsd session handle
 *
 *  Returns:   0 - Success
 *             -ENOSPC - Out of space on server
 *             -EACCES - Access denied
 *             -EIO - Any other error
 *
 *  Abstract:  Write page to file.
 */
int Novfs_Write_Page(HANDLE handle, struct page *Page, session_t session_id)
{
	WRITE_FILE_REQUEST cmd;
	WRITE_FILE_REPLY lreply;
	PWRITE_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	struct data_list dlst[2];

	DbgPrint
	    ("Novfs_Write_Page: handle=0x%p Page=0x%p Index=%lu session_id=0x%llx\n",
	     handle, Page, Page->index, session_id);

	dlst[0].page = NULL;
	dlst[0].offset = &lreply;
	dlst[0].len = sizeof(lreply);
	dlst[0].rwflag = DLWRITE;

	dlst[1].page = Page;
	dlst[1].offset = NULL;
	dlst[1].len = PAGE_CACHE_SIZE;
	dlst[1].rwflag = DLREAD;

	cmdlen = offsetof(WRITE_FILE_REQUEST, data);

	cmd.command.command_type = VFS_COMMAND_WRITE_FILE;
	cmd.command.sequence_number = 0;
	cmd.command.session_id = session_id;

	cmd.handle = handle;
	cmd.len = PAGE_CACHE_SIZE;
	cmd.offset = (loff_t) Page->index << PAGE_CACHE_SHIFT;;

	retCode =
	    Queue_Daemon_Command(&cmd, cmdlen, &dlst, 2, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (!retCode) {
		if (reply) {
			memcpy(&lreply, reply, sizeof(lreply));
		}
		switch (lreply.Reply.error_code) {
		case 0:
			retCode = 0;
			break;

		case NWE_INSUFFICIENT_SPACE:
			retCode = -ENOSPC;
			break;

		case NWE_ACCESS_DENIED:
			retCode = -EACCES;
			break;

		default:
			retCode = -EIO;
			break;
		}
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("Novfs_Write_Page retCode=0x%x\n", retCode);

	return (retCode);
}

int Novfs_Write_Pages(HANDLE handle, struct data_list *DList, int DList_Cnt,
		      size_t bytes, loff_t offset, session_t session_id)
{
	WRITE_FILE_REQUEST cmd;
	WRITE_FILE_REPLY lreply;
	PWRITE_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	size_t len;

	DbgPrint
	    ("Novfs_Write_Pages: handle=0x%p Dlst=0x%p Dlcnt=%d bytes=%d offset=%lld session_id=0x%llx\n",
	     handle, DList, DList_Cnt, bytes, offset, session_id);

	DList[0].page = NULL;
	DList[0].offset = &lreply;
	DList[0].len = sizeof(lreply);
	DList[0].rwflag = DLWRITE;

	len = bytes;
	cmdlen = offsetof(WRITE_FILE_REQUEST, data);

	if (len) {
		cmd.command.command_type = VFS_COMMAND_WRITE_FILE;
		cmd.command.sequence_number = 0;
		cmd.command.session_id = session_id;

		cmd.handle = handle;
		cmd.len = len;
		cmd.offset = offset;

		retCode =
		    Queue_Daemon_Command(&cmd, cmdlen, DList, DList_Cnt,
					 (void *)&reply, &replylen,
					 INTERRUPTIBLE);
		if (!retCode) {
			if (reply) {
				memcpy(&lreply, reply, sizeof(lreply));
			}
			switch (lreply.Reply.error_code) {
			case 0:
				retCode = 0;
				break;

			case NWE_INSUFFICIENT_SPACE:
				retCode = -ENOSPC;
				break;

			case NWE_ACCESS_DENIED:
				retCode = -EACCES;
				break;

			default:
				retCode = -EIO;
				break;
			}
		}
		if (reply) {
			kfree(reply);
		}
	}
	DbgPrint("Novfs_Write_Pages retCode=0x%x\n", retCode);

	return (retCode);
}

int Novfs_Read_Stream(HANDLE ConnHandle, unsigned char *handle,
		      unsigned char __user *buffer, size_t *bytes,
		      loff_t *offset, session_t session_id)
{
	READ_STREAM_REQUEST cmd;
	PREAD_STREAM_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;
	size_t len;

	len = *bytes;
	*bytes = 0;

	if ((offsetof(READ_FILE_REPLY, data) + len) > MaxIoSize) {
		len = MaxIoSize - offsetof(READ_FILE_REPLY, data);
		len = (len / PAGE_SIZE) * PAGE_SIZE;
	}

	cmd.command.command_type = VFS_COMMAND_READ_STREAM;
	cmd.command.sequence_number = 0;
	cmd.command.session_id = session_id;

	cmd.connection = ConnHandle;
	memcpy(cmd.handle, handle, sizeof(cmd.handle));
	cmd.len = len;
	cmd.offset = *offset;

	retCode = Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);

	DbgPrint("%s: Queue_Daemon_Command 0x%x replylen=%d\n", __func__, retCode, replylen);

	if (reply) {
		retCode = 0;
		if (reply->Reply.error_code) {
			retCode = -EIO;
		} else {
			replylen -= offsetof(READ_STREAM_REPLY, data);
			if (replylen > 0) {
				replylen -= copy_to_user(buffer, reply->data, replylen);
				*bytes = replylen;
			}
		}
		kfree(reply);
	}

	DbgPrint("%s: *bytes=0x%x retCode=0x%x\n", __func__, *bytes, retCode);

	return (retCode);
}

int Novfs_Write_Stream(HANDLE ConnHandle, unsigned char * handle, const char __user *buffer,
		       size_t * bytes, loff_t * offset, session_t session_id)
{
	PWRITE_STREAM_REQUEST cmd;
	PWRITE_STREAM_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	size_t len;

	len = *bytes;
	cmdlen = len + offsetof(WRITE_STREAM_REQUEST, data);
	*bytes = 0;

	if (cmdlen > MaxIoSize) {
		cmdlen = MaxIoSize;
		len = cmdlen - offsetof(WRITE_STREAM_REQUEST, data);
	}

	DbgPrint("%s: cmdlen=%d len=%d\n", __func__, cmdlen, len);

	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		if (buffer && len)
			len -= copy_from_user(cmd->data, buffer, len);

		DbgPrint("%s: len=%d\n", __func__, len);

		cmd->command.command_type = VFS_COMMAND_WRITE_STREAM;
		cmd->command.sequence_number = 0;
		cmd->command.session_id = session_id;

		cmd->connection = ConnHandle;
		memcpy(cmd->handle, handle, sizeof(cmd->handle));
		cmd->len = len;
		cmd->offset = *offset;

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
		if (reply) {
			switch (reply->Reply.error_code) {
			case 0:
				retCode = 0;
				break;

			case NWE_INSUFFICIENT_SPACE:
				retCode = -ENOSPC;
				break;

			case NWE_ACCESS_DENIED:
				retCode = -EACCES;
				break;

			default:
				retCode = -EIO;
				break;
			}
			DbgPrint("%s: reply->bytesWritten=0x%lx\n", __func__, reply->bytesWritten);
			*bytes = reply->bytesWritten;
			kfree(reply);
		}
		kfree(cmd);
	}
	DbgPrint("%s: *bytes=0x%x retCode=0x%x\n", __func__, *bytes, retCode);

	return retCode;
}

int Novfs_Close_Stream(HANDLE ConnHandle, unsigned char * handle, session_t session_id)
{
	CLOSE_STREAM_REQUEST cmd;
	PCLOSE_STREAM_REPLY reply;
	unsigned long replylen = 0;
	int retCode;

	cmd.command.command_type = VFS_COMMAND_CLOSE_STREAM;
	cmd.command.sequence_number = 0;
	cmd.command.session_id = session_id;

	cmd.connection = ConnHandle;
	memcpy(cmd.handle, handle, sizeof(cmd.handle));

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, 0);
	if (reply) {
		retCode = 0;
		if (reply->Reply.error_code) {
			retCode = -EIO;
		}
		kfree(reply);
	}
	return (retCode);
}

int Novfs_Delete(unsigned char * Path, int DirectoryFlag, session_t session_id)
{
	PDELETE_FILE_REQUEST cmd;
	PDELETE_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	cmdlen = offsetof(DELETE_FILE_REQUEST, path) + pathlen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->command.command_type = VFS_COMMAND_DELETE_FILE;
		cmd->command.sequence_number = 0;
		cmd->command.session_id = session_id;

		cmd->isDirectory = DirectoryFlag;
		cmd->pathlength = pathlen;
		memcpy(cmd->path, Path, pathlen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
		if (reply) {
			retCode = 0;
			if (reply->Reply.error_code) {
				if ((reply->Reply.error_code & 0xFFFF) == 0x0006) {	/* Access Denied Error */
					retCode = -EACCES;
				} else {
					retCode = -EIO;
				}
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

int Novfs_Truncate_File_Ex(HANDLE handle, loff_t offset, session_t session_id)
{
	WRITE_FILE_REQUEST cmd;
	PWRITE_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;

	DbgPrint("%s: handle=0x%p offset=%lld\n", __func__, handle, offset);

	cmdlen = offsetof(WRITE_FILE_REQUEST, data);

	cmd.command.command_type = VFS_COMMAND_WRITE_FILE;
	cmd.command.sequence_number = 0;
	cmd.command.session_id = session_id;
	cmd.handle = handle;
	cmd.len = 0;
	cmd.offset = offset;

	retCode =
	    Queue_Daemon_Command(&cmd, cmdlen, NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);

	DbgPrint("%s: retCode=0x%x reply=0x%p\n", __func__, retCode,
		 reply);

	if (!retCode) {
		switch (reply->Reply.error_code) {
		case 0:
			retCode = 0;
			break;

		case NWE_INSUFFICIENT_SPACE:
			retCode = -ENOSPC;
			break;

		case NWE_ACCESS_DENIED:
			retCode = -EACCES;
			break;

		case NWE_FILE_IO_LOCKED:
			retCode = -EBUSY;
			break;

		default:
			retCode = -EIO;
			break;
		}
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("%s: retCode=%d\n", __func__, retCode);

	return (retCode);
}

int Novfs_Rename_File(int DirectoryFlag, unsigned char * OldName, int OldLen,
		      unsigned char * NewName, int NewLen, session_t session_id)
{
	RENAME_FILE_REQUEST cmd;
	PRENAME_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode;

	DbgPrint("%s: DirectoryFlag: %d OldName: %.*s NewName: %.*s session_id: 0x%llx\n", __func__,
		 DirectoryFlag, OldLen, OldName, NewLen, NewName, session_id);

	cmd.command.command_type = VFS_COMMAND_RENAME_FILE;
	cmd.command.sequence_number = 0;
	cmd.command.session_id = session_id;

	cmd.directoryFlag = DirectoryFlag;

	if (StripTrailingDots) {
		if ('.' == OldName[OldLen - 1])
			OldLen--;
		if ('.' == NewName[NewLen - 1])
			NewLen--;
	}

	cmd.newnameLen = NewLen;
	memcpy(cmd.newname, NewName, NewLen);

	cmd.oldnameLen = OldLen;
	memcpy(cmd.oldname, OldName, OldLen);

	retCode = Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0,
				       (void *)&reply, &replylen,
				       INTERRUPTIBLE);
	if (reply) {
		retCode = 0;
		if (reply->Reply.error_code)
			retCode = -ENOENT;
		kfree(reply);
	}
	return retCode;
}

int Novfs_Set_Attr(unsigned char * Path, struct iattr *Attr, session_t session_id)
{
	PSET_FILE_INFO_REQUEST cmd;
	PSET_FILE_INFO_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	cmdlen = offsetof(SET_FILE_INFO_REQUEST, path) + pathlen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->command.command_type = VFS_COMMAND_SET_FILE_INFO;
		cmd->command.sequence_number = 0;
		cmd->command.session_id = session_id;
		cmd->fileInfo.ia_valid = Attr->ia_valid;
		cmd->fileInfo.ia_mode = Attr->ia_mode;
		cmd->fileInfo.ia_uid = Attr->ia_uid;
		cmd->fileInfo.ia_gid = Attr->ia_uid;
		cmd->fileInfo.ia_size = Attr->ia_size;
		cmd->fileInfo.ia_atime = Attr->ia_atime.tv_sec;
		cmd->fileInfo.ia_mtime = Attr->ia_mtime.tv_sec;;
		cmd->fileInfo.ia_ctime = Attr->ia_ctime.tv_sec;;
/*
      cmd->fileInfo.ia_attr_flags = Attr->ia_attr_flags;
*/
		cmd->fileInfo.ia_attr_flags = 0;

		cmd->pathlength = pathlen;
		memcpy(cmd->path, Path, pathlen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
		if (reply) {
			switch (reply->Reply.error_code) {
			case 0:
				retCode = 0;
				break;

			case NWE_PARAM_INVALID:
				retCode = -EINVAL;
				break;

			case NWE_FILE_IO_LOCKED:
				retCode = -EBUSY;
				break;

			default:
				retCode = -EIO;
				break;
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

int Novfs_Get_File_Cache_Flag(unsigned char *path, session_t session_id)
{
	struct novfs_get_cache_flag_request *cmd;
	struct novfs_get_cache_flag_reply *reply = NULL;
	unsigned long replylen = 0;
	int cmdlen;
	int retCode = 0;
	int path_len;

	DbgPrint("%s: path = %s\n", __func__, path);

	if (path && *path) {
		path_len = strlen(path);
		if (StripTrailingDots) {
			if ('.' == path[path_len - 1])
				path_len--;
		}
		cmdlen = offsetof(struct novfs_get_cache_flag_request, path) + path_len;
		cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
		if (cmd) {
			cmd->command.command_type = VFS_COMMAND_GET_CACHE_FLAG;
			cmd->command.sequence_number = 0;
			cmd->command.session_id = session_id;
			cmd->path_len = path_len;
			memcpy(cmd->path, path, cmd->path_len);

			Queue_Daemon_Command(cmd, cmdlen, NULL, 0,
					     (void *)&reply, &replylen,
					     INTERRUPTIBLE);

			if (reply) {
				if (!reply->reply.error_code)
					retCode = reply->cache_flag;

				kfree(reply);
			}
			kfree(cmd);
		}
	}

	DbgPrint("%s: return %d\n", __func__, retCode);
	return (retCode);
}

/*
 *  Arguments:
 *      session_id, file handle, type of lock (read/write or unlock),
 *	    start of lock area, length of lock area
 *
 *  Returns:
 *      0 on success
 *      negative value on error
 *
 *  Abstract:
 *
 *  Notes: lock type - fcntl
 */
int Novfs_Set_File_Lock(session_t session_id, HANDLE handle,
			unsigned char fl_type, loff_t fl_start, loff_t fl_len)
{
	struct novfs_set_file_lock_request *cmd;
	struct novfs_set_file_lock_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode;

	retCode = -1;

	DbgPrint("%s: session_id: 0x%llx\n", __func__, session_id);

	cmd = Novfs_Malloc(sizeof(*cmd), GFP_KERNEL);

	if (cmd) {
		DbgPrint("%s: 2\n", __func__);

		cmd->command.command_type = VFS_COMMAND_SET_FILE_LOCK;
		cmd->command.sequence_number = 0;
		cmd->command.session_id = session_id;

		cmd->handle = handle;
		if (F_RDLCK == fl_type) {
			fl_type = 1;	// LockRegionExclusive
		} else if (F_WRLCK == fl_type) {
			fl_type = 0;	// LockRegionShared
		}

		cmd->fl_type = fl_type;
		cmd->fl_start = fl_start;
		cmd->fl_len = fl_len;

		DbgPrint("%s: 3\n", __func__);
		DbgPrint("%s: BEGIN dump arguments\n", __func__);
		DbgPrint("%s: Queue_Daemon_Command %d\n", __func__, cmd->command.command_type);
		DbgPrint("%s: cmd->handle   = 0x%p\n", __func__, cmd->handle);
		DbgPrint("%s: cmd->fl_type  = %u\n", __func__, cmd->fl_type);
		DbgPrint("%s: cmd->fl_start = 0x%X\n", __func__, cmd->fl_start);
		DbgPrint("%s: cmd->fl_len   = 0x%X\n", __func__, cmd->fl_len);
		DbgPrint("%s: sizeof(struct novfs_set_file_lock_request) = %u\n", __func__, sizeof(*cmd));
		DbgPrint("%s: END dump arguments\n", __func__);

		retCode = Queue_Daemon_Command(cmd, sizeof(*cmd),
					 NULL, 0, (void *)&reply, &replylen,
					 INTERRUPTIBLE);
		DbgPrint("%s: 4\n", __func__);

		if (reply) {
			DbgPrint("%s 5, error_code = %X\n", __func__, reply->reply.error_code);

			if (reply->reply.error_code) {
				retCode = reply->reply.error_code;
			}
			kfree(reply);
		}

		kfree(cmd);
	}

	DbgPrint("%s: 6\n", __func__);

	return (retCode);
}
