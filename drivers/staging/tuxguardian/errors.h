#ifndef __HAS_ERRORS_H
#define __HAS_ERRORS_H


#include <linux/errno.h>


struct sock_error_to_string
{
    int err;
    char *explain;
};


struct sock_error_to_string error_to_string[] = {
  { EPERM, "Not owner" },
  { ENOENT, "No such file" },
  { ESRCH, "No such process" },
  { EINTR, "Interrupted system" },
  { EIO, "I/O error" },
  { ENXIO, "No such device" },
  { E2BIG, "Argument list too long" },
  { ENOEXEC, "Exec format error" },
  { EBADF, "Bad file number" },
  { ECHILD, "No children" },
  { EAGAIN, "No more processes" },
  { ENOMEM, "Not enough core" },
  { EACCES, "Permission denied" },
  { EFAULT, "Bad address" },
  { ENOTBLK, "Block device required" },
  { EBUSY, "Mount device busy" },
  { EEXIST, "File exists" },
  { EXDEV, "Cross-device link" },
  { ENODEV, "No such device" },
  { ENOTDIR, "Not a directory" },
  { EISDIR, "Is a directory" },
  { EINVAL, "Invalid argument" },
  { ENFILE, "File table overflow" },
  { EMFILE, "Too many open file" },
  { ENOTTY, "Not a typewriter" },
  { ETXTBSY, "Text file busy" },
  { EFBIG, "File too large" },
  { ENOSPC, "No space left on" },
  { ESPIPE, "Illegal seek" },
  { EROFS, "Read-only file system" },
  { EMLINK, "Too many links" },
  { EPIPE, "Broken pipe" },
  { EWOULDBLOCK, "Operation would block" },
  { EINPROGRESS, "Operation now in progress" },
  { EALREADY, "Operation already in progress" },
  { ENOTSOCK, "Socket operation on" },
  { EDESTADDRREQ, "Destination address required" },
  { EMSGSIZE, "Message too long" },
  { EPROTOTYPE, "Protocol wrong type" },
  { ENOPROTOOPT, "Protocol not available" },
  { EPROTONOSUPPORT, "Protocol not supported" },
  { ESOCKTNOSUPPORT, "Socket type not supported" },
  { EOPNOTSUPP, "Operation not supported" },
  { EPFNOSUPPORT, "Protocol family not supported" },
  { EAFNOSUPPORT, "Address family not supported" },
  { EADDRINUSE, "Address already in use" },
  { EADDRNOTAVAIL, "Can't assign requested address" },
  { ENETDOWN, "Network is down" },
  { ENETUNREACH, "Network is unreachable" },
  { ENETRESET, "Network dropped connection" },
  { ECONNABORTED, "Software caused connection" },
  { ECONNRESET, "Connection reset by peer" },
  { ENOBUFS, "No buffer space available" },
  { EISCONN, "Socket is already connected" },
  { ENOTCONN, "Socket is not connected" },
  { ESHUTDOWN, "Can't send after shutdown" },
  { ETOOMANYREFS, "Too many references" },
  { ETIMEDOUT, "Connection timed out" },
  { ECONNREFUSED, "Connection refused" },
  { ELOOP, "Too many levels of nesting" },
  { ENAMETOOLONG, "File name too long" },
  { EHOSTDOWN, "Host is down" },
  { EHOSTUNREACH, "No route to host" },
  { ENOTEMPTY, "Directory not empty" },
  { EUSERS, "Too many users" },
  { EDQUOT, "Disc quota exceeded" },
  { ESTALE, "Stale NFS file handle" },
  { EREMOTE, "Too many levels of remote in the path" },
  { ENOSTR, "Device is not a stream" },
  { ETIME, "Timer expired" },
  { ENOSR, "Out of streams resources" },
  { ENOMSG, "No message" },
  { EBADMSG, "Trying to read unreadable message" },
  { EIDRM, "Identifier removed" },
  { EDEADLK, "Deadlock condition" },
  { ENOLCK, "No record locks available" },
  { ENONET, "Machine is not on network" },
  { ENOLINK, "The link has been severed" },
  { EADV, "ADVERTISE error" },
  { ESRMNT, "SRMOUNT error" },
  { ECOMM, "Communication error" },
  { EPROTO, "Protocol error" },
  { EMULTIHOP, "Multihop attempted" },
  { EDOTDOT, "Cross mount point" },
  { EREMCHG, "Remote address change" }
};



struct sock_proto_to_string
{
    int protonumber;
    char *description;
};

static struct sock_proto_to_string proto_to_string[] = {
  { IPPROTO_IP, "Dummy protocol for TCP" },
  { IPPROTO_ICMP, "Internet Control Message Protocol" },
  { IPPROTO_IGMP, "Internet Group Management Protocol" },
  { IPPROTO_IPIP, "IPIP tunnels (older KA9Q tunnels use 94)" },
  { IPPROTO_TCP, "Transmission Control Protocol" },
  { IPPROTO_EGP, "Exterior Gateway Protocol" },
  { IPPROTO_PUP, "PUP protocol" },
  { IPPROTO_UDP, "User Datagram Protocol" },
  { IPPROTO_IDP, "XNS IDP protocol" },
  { IPPROTO_RSVP, "RSVP protocol" },
  { IPPROTO_GRE, "Cisco GRE tunnels (rfc 1701,1702" },
  { IPPROTO_IPV6, "IPv6-in-IPv4 tunnelling" },
  { IPPROTO_ESP, "Encapsulation Security Payload protocol" },
  { IPPROTO_AH, "Authentication Header protocol" },
  { IPPROTO_PIM, "Protocol Independent Multicast" },
  { IPPROTO_COMP, "Compression Header protocol" },
  { IPPROTO_SCTP, "Stream Control Transport Protocol" },
  { IPPROTO_RAW, "Raw IP packets" }
};




#endif
