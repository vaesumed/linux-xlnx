# Build vmlinux
# ---------------------------------------------------------------------------
# vmlinux is built from the objects selected by $(KBUILD_VMLINUX_INIT) and
# $(KBUILD_VMLINUX_MAIN). Most are built-in.o files from top-level directories
# in the kernel tree, others are specified in arch/$(ARCH)/Makefile.
# Ordering when linking is important, and $(KBUILD_VMLINUX_INIT) must be first.
#
# vmlinux
#   ^
#   |
#   +-< $(KBUILD_VMLINUX_INIT)
#   |   +--< init/version.o + more
#   |
#   +--< $(KBUILD_VMLINUX_MAIN)
#   |    +--< driver/built-in.o mm/built-in.o + more
#   |
#   +-< $(KBUILD_VMLINUX_EXTRA)
#
# vmlinux version (uname -v) cannot be updated during normal
# descending-into-subdirs phase since we do not yet know if we need to
# update vmlinux.
#
# System.map is generated to document addresses of all kernel symbols

MAKE=make

# We need access to CONFIG_ symbols
source .config
# Error out on error
set -e
# USe "make V=3" to debug this script
if [ "${KBUILD_VERBOSE}" = "3" ]; then
	set -x
fi
# Override MAKEFLAGS to avoid parrallel builds
MAKEFLAGS='--no-print-directory -Rr'
# Delete output files in case of error
trap cleanup SIGHUP SIGINT SIGQUIT SIGTERM ERR
cleanup()
{
	rm -f vmlinux.o
	rm -f .old_version
	rm -f .tmp_vmlinux*
	rm -f .tmp_kallsyms*
	rm -f vmlinux
	rm -f .tmp_System.map
	rm -f System.map
}
# non-verbose output
tell()
{
	printf "  %-7s %s\n" $1 $2
}

#####
# Generate System.map

# $NM produces the following output:
# f0081e80 T alloc_vfsmnt

#   The second row specify the type of the symbol:
#   A = Absolute
#   B = Uninitialised data (.bss)
#   C = Comon symbol
#   D = Initialised data
#   G = Initialised data for small objects
#   I = Indirect reference to another symbol
#   N = Debugging symbol
#   R = Read only
#   S = Uninitialised data for small objects
#   T = Text code symbol
#   U = Undefined symbol
#   V = Weak symbol
#   W = Weak symbol
#   Corresponding small letters are local symbols

# For System.map filter away:
#   a - local absolute symbols
#   N = Debugging symbol
#   U - undefined global symbols
#   w - local weak symbols

# readprofile starts reading symbols when _stext is found, and
# continue until it finds a symbol which is not either of 'T', 't',
# 'W' or 'w'. __crc_ are 'A' and placed in the middle
# so we just ignore them to let readprofile continue to work.
# (At least sparc64 has __crc_ in the middle).
mksysmap()
{
	$NM -n $1 | grep -v '\( [aNUw] \)\|\(__crc_\)\|\( \$[adt]\)'
}

# link vmlinux.o
tell LD vmlinux.o
${LD} ${LDFLAGS} ${LDFLAGS_vmlinux} -r -o vmlinux.o      \
	${KBUILD_VMLINUX_INIT}                           \
        --start-group ${KBUILD_VMLINUX_MAIN} --end-group \
	${KBUILD_VMLINUX_EXTRA}

# modpost vmlinux.o
${MAKE} -f ${srctree}/scripts/Makefile.modpost vmlinux.o

# Update version
tell GEN .version
if [ ! -r .version ]; then
	rm -f .version;
	echo 1 >.version;
else
	mv .version .old_version;
	expr 0$(cat .old_version) + 1 >.version;
fi;

# final build of init/
${MAKE} -f ${srctree}/scripts/Makefile.build obj=init

VMLINUX=vmlinux
if [ "${CONFIG_KALLSYMS}" = "y" ]; then
	VMLINUX=.tmp_vmlinux
fi

# First stage of fully linked vmlinux
tell LD ${VMLINUX}
${LD} ${LDFLAGS} ${LDFLAGS_vmlinux} -o ${VMLINUX} \
      -T ${KBUILD_VMLINUX_LDS} vmlinux.o

if [ "${CONFIG_KALLSYMS}" = "y" ]; then

	# Do an extra pass to link in kallsyms data
	${NM} -n .tmp_vmlinux | scripts/kallsyms > .tmp_kallsyms.S
        ${CC} ${KBUILD_AFLAGS} ${KBUILD_CPPFLAGS} -c -o .tmp_kallsyms.o \
	      .tmp_kallsyms.S
        # link in kalll symbols
	tell LD vmlinux
        ${LD} ${LDFLAGS} ${LDFLAGS_vmlinux} -o vmlinux \
         -T ${KBUILD_VMLINUX_LDS} vmlinux.o .tmp_kallsyms.o
fi

tell SYSMAP System.map
mksysmap vmlinux > System.map

# We made a new kernel - delete old version file
rm -f .old_version


