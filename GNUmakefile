# Installation paths
.DEFAULT_GOAL := all

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/share/trealla
MANDIR ?= $(PREFIX)/share/man

EMBED ?= 1
FREESTANDING_BASE_LIBS = builtins error lists iso_ext gensym si
EMBED_LIBS ?= $(FREESTANDING_BASE_LIBS)

PYTHON ?= python3
QEMU_RISCV_CC ?= riscv64-unknown-elf-gcc
QEMU_RISCV_AR ?= riscv64-unknown-elf-ar
QEMU_RISCV_SIZE ?= riscv64-unknown-elf-size
QEMU_RISCV ?= qemu-system-riscv32
QEMU_RISCV_ELF = ports/qemu-riscv32/trealla.elf
ifdef IDF_PATH
IDF_PY ?= $(IDF_PATH)/tools/idf.py
else
IDF_PY ?= idf.py
endif
RPI4_CC ?= aarch64-none-elf-gcc
RPI4_AR ?= aarch64-none-elf-ar
RPI4_OBJCOPY ?= aarch64-none-elf-objcopy
RPI4_SIZE ?= aarch64-none-elf-size
QEMU_RPI4 ?= qemu-system-aarch64
RPI4_ELF = ports/rpi4/trealla.elf
RPI4_IMG = ports/rpi4/kernel8.img
RPI4_MAP = ports/rpi4/trealla.map
RPI4_OBJ = ports/rpi4/boot.o ports/rpi4/mmu.o ports/rpi4/platform.o \
	ports/rpi4/syscalls.o ports/rpi4/fault.o ports/rpi4/mailbox.o \
	ports/rpi4/font8x8.o ports/rpi4/fb.o ports/rpi4/board.o
# Networking is opt-in for this port: `make rpi4 RPI4_NET=1`. The default
# image has no GENET code at all, because QEMU - which is what CI boots - has
# no GENET to talk to.

ifdef RPI4_NET
RPI4_OBJ += ports/rpi4/genet.o
RPI4_NET_OBJ = src/net/net.o src/net/bif_net_stack.o ports/rpi4/bif_genet.o
RPI4_NET_CFLAGS = -DRPI4_NET=1
endif
RPI4_CFLAGS = -mcpu=cortex-a72 -ffunction-sections -fdata-sections \
	-Isrc/net -Iports/rpi4 $(RPI4_NET_CFLAGS)
# What the image boots: the acceptance harness and its smoke program by
# default, overridden by `make rpi4-app main=<program.pl>`.
RPI4_PROGRAM ?= ports/rpi4/program.pl
RPI4_APP ?= samples/freestanding.c
RPI4_LDFLAGS = -nostartfiles -Tports/rpi4/rpi4.ld -Wl,--gc-sections \
	-Wl,--no-warn-rwx-segments -Wl,-Map=$(RPI4_MAP) -lm
ESP32S3_CC ?= xtensa-esp32s3-elf-gcc
ESP32S3_AR ?= xtensa-esp32s3-elf-ar
PICOLIBC_SPECS ?= $(shell $(QEMU_RISCV_CC) --print-file-name=picolibc.specs 2>/dev/null)
QEMU_RISCV_CFLAGS = -specs=$(PICOLIBC_SPECS) -march=rv32imac -mabi=ilp32 \
	-mcmodel=medany -ffunction-sections -fdata-sections
QEMU_RISCV_LDFLAGS = --oslib=semihost \
	-march=rv32imac -mabi=ilp32 -mcmodel=medany \
	-Tports/qemu-riscv32/picolibc.ld -Wl,--gc-sections -lm
# Captured eagerly (before WASI/WIN below reassign CC to a cross-compiler)
# so a plain `make CC=clang` also builds util/bin2c with clang, matching
# https://github.com/trealla-prolog/trealla/issues/1123
HOST_CC := $(CC)

# A source export with no .git directory (a GitHub release tarball, a
# distro's source package, ...) can't run `git describe`, so fall back
# to the version git-archive stamps into .tarball-version via the
# export-subst attribute in .gitattributes. If that substitution never
# ran either - the file still holds its literal $Format placeholder, or
# is missing entirely - settle for "unknown" rather than an empty
# -DVERSION.
GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags 2>/dev/null)
ifeq ($(GIT_VERSION),)
TARBALL_VERSION := $(shell sed -n '1p' .tarball-version 2>/dev/null)
ifeq ($(TARBALL_VERSION),)
GIT_VERSION := unknown
else ifneq ($(filter $$Format%,$(TARBALL_VERSION)),)
GIT_VERSION := unknown
else
GIT_VERSION := $(TARBALL_VERSION)
endif
endif
GIT_VERSION := "$(GIT_VERSION)"

CFLAGS = -MMD -MP -Isrc -I/usr/local/include -DVERSION='$(GIT_VERSION)' \
	-DDEFAULT_LIBRARY_PATH='"$(LIBDIR)/library"' \
	-O3 $(OPT) \
	-Wall -Wextra \
	-Wno-unused-but-set-variable \
	-Wno-unused-parameter \
	-Wno-unused-variable     \
	-Wno-unused-function
ifndef NO_GNU_SOURCE
CFLAGS += -D_GNU_SOURCE
endif
CFLAGS += $(TARGET_CFLAGS)

ifeq ($(EMBED), 1)
CFLAGS += -DEMBED=1
endif

# libtrealla.a is only useful to an embedder if its objects can be linked
# into a shared object - a Python extension module, say - and on ELF
# targets that needs -fPIC, not the -fPIE a normal executable build would
# settle for. Costs a little register pressure on x86-64 and nothing
# measurable on arm64. NOPIC=1 opts out where the flag is meaningless
# (WASI, Windows) or unsupported (cosmocc).

ifndef NOPIC
CFLAGS += -fPIC
endif

ifeq ($(INDEX_PROFILE), 1)
CFLAGS += -DINDEX_PROFILE
endif

LDFLAGS = -L/usr/local/lib -lm

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), FreeBSD)
LDFLAGS += -lrt
endif

ifeq ($(UNAME_S), SunOS)
LDFLAGS += -lsocket -lnsl
endif

ifdef TRIBBLIX
CFLAGS += -m64
LDFLAGS += -L/usr/lib/amd64
endif

ifeq ($(UNAME_S), Haiku)
LDFLAGS += -lnetwork
endif

# Which Homebrew OpenSSL keg to build against. Both -I and -L have to name
# the same one: pointing only the include path at a different major version
# links the headers of one against the library of another, and nothing
# complains until runtime. Override as `make OPENSSL=openssl@4`.

OPENSSL ?= openssl@3

ifdef HOMEBREW_PREFIX
LDFLAGS += -L$(HOMEBREW_PREFIX)/opt/libffi/lib -L$(HOMEBREW_PREFIX)/opt/$(OPENSSL)/lib
CFLAGS += -I$(HOMEBREW_PREFIX)/opt/libffi/include -I$(HOMEBREW_PREFIX)/opt/$(OPENSSL)/include
endif

ifdef WASI
CFLAGS += -std=c11 -Isrc/wasm \
	-D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL \
	-D_WASI_EMULATED_PROCESS_CLOCKS
LDFLAGS += -lwasi-emulated-mman -lwasi-emulated-signal \
	-lwasi-emulated-process-clocks -Wl,--stack-first \
	-Wl,-zstack-size=8388608 -Wl,--initial-memory=134217728 \
	-o tpl.wasm
NOPIC = 1
NOLIB = 1
NOFFI = 1
NOSSL = 1
NOTHREADS = 1
ifdef WASI_CC
CC = $(WASI_CC)
endif
endif

ifdef WIN
ifndef EDITLINE
ifndef READLINE
ISOCLINE = 1
endif
endif
NOPIC = 1
CC = x86_64-w64-mingw32-gcc
LDFLAGS += -lws2_32
ifndef NOFFI
endif
endif

ifdef FREESTANDING
CFLAGS += -DTPL_FREESTANDING=1 -DUSE_MMAP=0
override EMBED_LIBS := $(FREESTANDING_BASE_LIBS) $(filter-out $(FREESTANDING_BASE_LIBS),$(EMBED_LIBS))
PROGRAM ?= samples/freestanding.pl
FREESTANDING_MAIN ?= samples/freestanding.c
PLATFORM_OBJ ?= src/platform/hosted.o
NOFFI = 1
NOSSL = 1
NOTHREADS = 1
NOTTY = 1
NONETWORK = 1
override ISOCLINE =
override READLINE =
override EDITLINE =
endif

ifdef ISOCLINE
CFLAGS += -DUSE_ISOCLINE=1
endif

ifdef READLINE
CFLAGS += -DUSE_READLINE=1 -I$(HOMEBREW_PREFIX)/opt/readline/include
LDFLAGS += -lreadline -L$(HOMEBREW_PREFIX)/opt/readline/lib
endif

ifdef EDITLINE
CFLAGS += -DUSE_EDITLINE=1
LDFLAGS += -ledit
endif

ifdef WASI
CFLAGS += -DUSE_ISOCLINE=1
endif

ifndef ISOCLINE
ifndef EDITLINE
ifndef READLINE
ifndef WASI
ifndef WIN
ifndef NOTTY
CFLAGS += -DUSE_EDITLINE=1
LDFLAGS += -ledit
endif
endif
endif
endif
endif
endif

ifndef NOFFI
CFLAGS += -DUSE_FFI=1 -I/usr/local/opt/libffi/include
LDFLAGS += -lffi
ifeq ($(filter OpenBSD Haiku,$(UNAME_S)),)
LDFLAGS += -ldl
endif
endif

ifndef NOSSL
CFLAGS += -DUSE_OPENSSL=1
LDFLAGS += -lssl -lcrypto
endif

ifndef NOTHREADS
CFLAGS += -DUSE_THREADS=1 -pthread
LDFLAGS += -pthread
# 64-bit atomics are not inlined everywhere: 32-bit ARM emits calls to
# __atomic_fetch_add_8 and friends, which live in libatomic. Link the static
# archive directly (rather than -latomic) so we don't depend on libatomic.so
# being on the runtime linker's search path - on illumos-family systems
# (Solaris, OpenIndiana, Tribblix) gcc's own libatomic.so isn't always
# registered with the system dynamic linker.
#
# Ask the compiler instead of sniffing its --version banner. That check
# wanted the word "gcc" and Debian's default cc - which IS gcc - announces
# itself as "cc (Debian ...)", so it failed on exactly the platform that
# needs the library. A compiler without libatomic returns the name unchanged
# and the wildcard finds nothing, which is the right answer for clang too.
LIBATOMIC := $(shell $(CC) -print-file-name=libatomic.a 2>/dev/null)
ifneq ($(wildcard $(LIBATOMIC)),)
LDFLAGS += $(LIBATOMIC)
endif
endif

ifdef LTO
CFLAGS += -flto=$(LTO)
LDFLAGS += -flto=$(LTO)
endif

ifndef WASMOPT
WASMOPT = wasm-opt
endif

SRCOBJECTS = tpl.o \
	src/allocator.o \
	src/base64.o \
	src/bif_atts.o \
	src/bif_bboard.o \
	src/bif_control.o \
	src/bif_csv.o \
	src/bif_database.o \
	$(BIF_FFI_OBJECT) \
	src/bif_format.o \
	src/bif_functions.o \
	src/bif_misc.o \
	$(BIF_NET_OBJECT) \
	$(BIF_OS_OBJECT) \
	$(PORT_BIFS_OBJECT) \
	$(BIF_POSIX_OBJECT) \
	src/bif_predicates.o \
	src/bif_sort.o \
	src/bif_sregex.o \
	src/bif_streams.o \
	src/bif_dcgs.o \
	src/bif_tabling.o \
	src/bif_tasks.o \
	src/bif_threads.o \
	src/bif_uri.o \
	src/compile.o \
	src/files.o \
	src/heap.o \
	$(HISTORY_OBJECT) \
	$(LIBRARY_REGISTRY_OBJECT) \
	src/list.o \
	src/module.o \
	$(NETWORK_OBJECT) \
	src/parser.o \
	src/print.o \
	src/prolog.o \
	src/query.o \
	src/skiplist.o \
	src/terms.o \
	src/toplevel.o \
	src/unify.o \
	src/utf8.o \
	src/version.o

ifdef NONETWORK
BIF_NET_OBJECT = src/bif_net_none.o
NETWORK_OBJECT = src/network_none.o
else
BIF_NET_OBJECT = src/bif_net.o
NETWORK_OBJECT = src/network.o
endif

# Without FFI the real table is 1000 reserved slots of 680 bytes that
# nothing can ever fill - 680KB of BSS. The stub is one empty entry.

ifdef NOFFI
BIF_FFI_OBJECT = src/bif_ffi_none.o
else
BIF_FFI_OBJECT = src/bif_ffi.o
endif

ifdef FREESTANDING
BIF_OS_OBJECT = src/bif_os_none.o
BIF_POSIX_OBJECT = src/bif_posix_none.o
else
BIF_OS_OBJECT = src/bif_os.o
BIF_POSIX_OBJECT = src/bif_posix.o
endif

# A port that exposes board hardware to Prolog overrides this with its own
# builtin table; every other build gets the empty one.
#
# LINUX_GPIO=1 fills it on a hosted build with the GPIO character-device
# builtins, giving the same gpio_* and delay_ms predicates as the Raspberry
# Pi 4 freestanding port so the same Prolog runs on both. Opt-in, because a
# build that never touches a pin should not carry them. Not Pi-specific: it
# wraps the kernel ABI, so it serves any Linux board with a gpiochip.

ifdef LINUX_GPIO
ifneq ($(UNAME_S),Linux)
$(error LINUX_GPIO=1 needs Linux and its GPIO character device)
endif
PORT_BIFS_OBJECT = src/bif_gpio_linux.o
endif

# NETSTACK=1 puts the udp_* builtins over src/net's own IPv4/UDP stack in a
# hosted build, with an AF_PACKET netif standing in for a driver. That is how
# the freestanding networking path is exercised without a board: the same
# builtins a port would expose, over the same stack, against a real peer.

ifdef NETSTACK
ifneq ($(UNAME_S),Linux)
$(error NETSTACK=1 needs Linux and AF_PACKET)
endif
PORT_BIFS_OBJECT = src/net/bif_net_stack.o src/net/net.o \
	src/net/netif_afpacket.o src/net/port_bifs_netstack.o
CFLAGS += -Isrc/net
endif

PORT_BIFS_OBJECT ?= src/port_bifs_none.o

ifdef NOTTY
HISTORY_OBJECT = src/history_none.o
else
HISTORY_OBJECT = src/history.o
endif

ifdef FREESTANDING
LIBRARY_REGISTRY_OBJECT = library/embedded_registry.o
else
LIBRARY_REGISTRY_OBJECT = src/library.o
endif

LIBOBJECTS =

ifeq ($(EMBED), 1)
ifdef FREESTANDING
LIBOBJECTS += $(addprefix library/,$(addsuffix .o,$(EMBED_LIBS)))
else
LIBOBJECTS +=  \
	library/abnf.o \
	library/aggregate.o \
	library/arithmetic.o \
	library/assoc.o \
	library/atts.o \
	library/builtins.o \
	library/charsio.o \
	library/actors/threads.o \
	library/actors/tasks.o \
	library/concurrent.o \
	library/clpb.o \
	library/clpz.o \
	library/curl.o \
	library/debug.o \
	library/dif.o \
	library/error.o \
	library/format.o \
	library/freeze.o \
	library/gensym.o \
	library/gsl.o \
	library/http.o \
	library/iso_ext.o \
	library/json.o \
	library/lambda.o \
	library/lists.o \
	library/ordsets.o \
	library/pairs.o \
	library/pio.o \
	library/random.o \
	library/raylib.o \
	library/rbtrees.o \
	library/quads.o \
	library/reif.o \
	library/si.o \
	library/tabling.o \
	library/tftp.o \
	library/sqlite3.o \
	library/socket.o \
	library/sockets.o \
	library/syslog.o \
	library/filesex.o \
	library/time.o \
	library/tty.o \
	library/ugraphs.o \
	library/uri.o \
	library/uuid.o \
	library/when.o \
	library/yall.o
endif

# Janus is opt-in: `make janus`, never a plain `make`. Inside the EMBED
# block because that is where the embedded library list lives.

ifdef JANUS
LIBOBJECTS += library/janus.o
endif
endif

SRCOBJECTS += src/imath/imath.o
SRCOBJECTS += src/imath/imrat.o
SRCOBJECTS += src/sre/re.o

ifdef ISOCLINE
SRCOBJECTS += src/isocline/src/isocline.o
endif

OBJECTS = $(SRCOBJECTS) $(LIBOBJECTS) $(PLATFORM_OBJ)

# Everything except tpl.o, which carries main(). This is the whole engine,
# and it is what an embedder links against.

# libtrealla.a and the embedder demo that smoke-tests it. Both are for
# native builds that produce a linkable library, so NOLIB=1 turns them off
# where that is not what is being built:
#
#   WASI    the LDFLAGS already carry '-o tpl.wasm'
#   cosmo   cosmocc builds fat output from a parallel .aarch64 object tree
#           and wants a matching .aarch64/libtrealla.a beside the archive,
#           which plain ar does not produce - linking the demo against one
#           fails with "linker input missing concomitant" - and an APE
#           build has no embedder to serve anyway
#
# The demo is skipped for a Windows cross-build as well, since there is no
# native loader to run it with.

LIBTREALLA =
LIBTREALLA_OBJECTS = $(filter-out tpl.o $(PLATFORM_OBJ),$(OBJECTS))
SAMPLES =

ifndef NOLIB
LIBTREALLA = libtrealla.a

ifndef WIN
SAMPLES += samples/embed samples/allocator samples/oom
endif

ifdef FREESTANDING
SAMPLES = samples/freestanding
endif
endif

library/%.c: library/%.pl util/bin2c
	echo '#include <stddef.h>' > $@
	./util/bin2c $< >> $@

.PHONY: FORCE

library/embedded_registry.c: util/embed_registry FORCE
	./util/embed_registry $(EMBED_LIBS) > $@

program.c: $(PROGRAM) util/bin2c
	echo '#include <stddef.h>' > $@
	./util/bin2c $(PROGRAM) program_pl >> $@

program.o: program.c
	$(CC) $(CFLAGS) -o $@ -c $<

all: tpl $(LIBTREALLA) $(SAMPLES)

.PHONY: cosmo

cosmo:
	$(MAKE) clean
	$(MAKE) CC=cosmocc NOTHREADS=1 ISOCLINE=1 NOSSL=1 NOFFI=1 NOPIC=1 NOLIB=1

# Header dependencies are generated by the compiler (-MMD -MP) rather
# than listed by hand below: the hand-written rules had no entry for
# tpl.o at all, so editing internal.h left a stale tpl.o linked against
# the previous struct layout.

-include $(OBJECTS:.o=.d)

tpl: $(OBJECTS) README.md LICENSE
	rm src/version.o
	$(CC) $(CFLAGS) -o src/version.o -c src/version.c
	$(CC) $(CFLAGS) -o tpl $(OBJECTS) $(OPT) $(LDFLAGS)

# Order-only on tpl: the recipe above deletes and rebuilds src/version.o
# to re-stamp the git version at link time, so archiving in parallel with
# it would be a race for that one object.

ifdef FREESTANDING
$(LIBTREALLA): $(LIBTREALLA_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $(LIBTREALLA_OBJECTS)
else
$(LIBTREALLA): $(LIBTREALLA_OBJECTS) | tpl
	rm -f $@
	$(AR) rcs $@ $(LIBTREALLA_OBJECTS)
endif

# Links exactly the way an embedder would, so it catches a libtrealla.a
# that does not stand on its own.

samples/embed: samples/embed.c $(LIBTREALLA)
	$(CC) $(CFLAGS) -o $@ $< $(LIBTREALLA) $(OPT) $(LDFLAGS)

samples/allocator: samples/allocator.c $(LIBTREALLA)
	$(CC) $(CFLAGS) -o $@ $< $(LIBTREALLA) $(OPT) $(LDFLAGS)

samples/oom: samples/oom.c $(LIBTREALLA)
	$(CC) $(CFLAGS) -o $@ $< $(LIBTREALLA) $(OPT) $(LDFLAGS)

samples/freestanding: $(FREESTANDING_MAIN) program.o $(LIBTREALLA) $(PLATFORM_OBJ)
	$(CC) $(CFLAGS) -o $@ $< program.o $(LIBTREALLA) $(PLATFORM_OBJ) $(OPT) $(LDFLAGS)

.PHONY: freestanding freestanding-smoke port-template-smoke

freestanding:
	$(MAKE) clean
	$(MAKE) FREESTANDING=1 freestanding-smoke

freestanding-smoke: samples/freestanding
	./samples/freestanding
	@if nm -u samples/freestanding | grep -E '(_| )(connect|socket|getaddrinfo|fork|posix_spawn[A-Za-z_]*|mmap|dlopen|pthread_[A-Za-z_]*|readline|el_init|icl_init)$$'; then \
		echo "freestanding smoke image imports a disabled hosted service"; exit 1; \
	fi

port-template-smoke:
	$(MAKE) clean
	$(MAKE) FREESTANDING=1 \
		'PLATFORM_OBJ=ports/template/platform.o ports/template/hosted-board.o' \
		samples/freestanding
	./samples/freestanding

.PHONY: qemu-riscv32 qemu-riscv32-smoke

qemu-riscv32:
	@command -v $(QEMU_RISCV_CC) >/dev/null || { \
		echo "missing $(QEMU_RISCV_CC) (install a RISC-V bare-metal GCC toolchain)"; exit 1; \
	}
	@test -f "$(PICOLIBC_SPECS)" || { \
		echo "missing Picolibc for $(QEMU_RISCV_CC)"; exit 1; \
	}
	$(MAKE) clean
	$(MAKE) FREESTANDING=1 NOPIC=1 \
		CC=$(QEMU_RISCV_CC) AR=$(QEMU_RISCV_AR) HOST_CC=$(HOST_CC) \
		PLATFORM_OBJ=ports/qemu-riscv32/platform.o \
		'TARGET_CFLAGS=$(QEMU_RISCV_CFLAGS)' 'LDFLAGS=$(QEMU_RISCV_LDFLAGS)' \
		samples/freestanding
	cp samples/freestanding $(QEMU_RISCV_ELF)
	$(QEMU_RISCV_SIZE) $(QEMU_RISCV_ELF)

qemu-riscv32-smoke: qemu-riscv32
	$(PYTHON) util/qemu_smoke.py $(QEMU_RISCV) $(QEMU_RISCV_ELF) $(QEMU_RISCV_SIZE)

.PHONY: rpi4 rpi4-app rpi4-smoke rpi4-screen

# boot.S needs the same driver flags as the C files; the built-in .S rule
# would use ASFLAGS and miss them.

ports/rpi4/%.o: ports/rpi4/%.S
	$(CC) $(CFLAGS) -o $@ -c $<

rpi4:
	@command -v $(RPI4_CC) >/dev/null || { \
		echo "missing $(RPI4_CC) (install an AArch64 bare-metal GCC toolchain)"; exit 1; \
	}
	$(MAKE) clean
	$(MAKE) FREESTANDING=1 NOPIC=1 \
		CC=$(RPI4_CC) AR=$(RPI4_AR) HOST_CC=$(HOST_CC) \
		'PLATFORM_OBJ=$(RPI4_OBJ)' \
		'PORT_BIFS_OBJECT=ports/rpi4/bif_gpio.o ports/rpi4/bif_fb.o ports/rpi4/port_bifs.o $(RPI4_NET_OBJ)' \
		'PROGRAM=$(RPI4_PROGRAM)' 'FREESTANDING_MAIN=$(RPI4_APP)' \
		'TARGET_CFLAGS=$(RPI4_CFLAGS)' 'LDFLAGS=$(RPI4_LDFLAGS)' \
		samples/freestanding
	cp samples/freestanding $(RPI4_ELF)
	$(RPI4_OBJCOPY) -O binary $(RPI4_ELF) $(RPI4_IMG)
	$(RPI4_SIZE) $(RPI4_ELF)

# Semihosting is a QEMU-only exit path, so the smoke image is built with it
# and a flashable image is not.

# The bare-metal equivalent of `make compile main=...`: build a kernel image
# that boots straight into one Prolog program.

rpi4-app:
	@test -n "$(main)" || { \
		echo "usage: make rpi4-app main=<program.pl>"; exit 1; \
	}
	@test -f "$(main)" || { echo "no such program: $(main)"; exit 1; }
	$(MAKE) 'RPI4_PROGRAM=$(main)' 'RPI4_APP=samples/freestanding_app.c' rpi4

rpi4-smoke:
	@$(QEMU_RPI4) -M help | grep -q '^raspi4b ' || { \
		echo "$(QEMU_RPI4) has no raspi4b machine (needs QEMU 9.0 or newer)"; exit 1; \
	}
	$(MAKE) 'RPI4_CFLAGS=$(RPI4_CFLAGS) -DRPI4_SEMIHOSTING=1' rpi4
	$(PYTHON) util/rpi4_smoke.py $(QEMU_RPI4) $(RPI4_ELF) $(RPI4_SIZE)

# Boots the image QEMU can screenshot - the one without semihosting, which
# parks instead of exiting - and reads the console back off the screen.
rpi4-screen:
	@$(QEMU_RPI4) -M help | grep -q '^raspi4b ' || { \
		echo "$(QEMU_RPI4) has no raspi4b machine (needs QEMU 9.0 or newer)"; exit 1; \
	}
	$(MAKE) rpi4
	$(PYTHON) util/rpi4_screen.py $(QEMU_RPI4) $(RPI4_ELF) ports/rpi4/screen

.PHONY: arduino-nano-esp32 arduino-nano-esp32-lib

arduino-nano-esp32-lib:
	@command -v $(IDF_PY) >/dev/null || { \
		echo "missing $(IDF_PY) (activate ESP-IDF 6.0.2 or newer)"; exit 1; \
	}
	@command -v $(ESP32S3_CC) >/dev/null || { \
		echo "missing $(ESP32S3_CC) (activate the ESP32-S3 toolchain)"; exit 1; \
	}
	@command -v $(ESP32S3_AR) >/dev/null || { \
		echo "missing $(ESP32S3_AR) (activate the ESP32-S3 toolchain)"; exit 1; \
	}
	$(MAKE) clean
	$(MAKE) FREESTANDING=1 NOPIC=1 CC=$(ESP32S3_CC) AR=$(ESP32S3_AR) \
		HOST_CC=$(HOST_CC) NO_GNU_SOURCE=1 \
		'TARGET_CFLAGS=-mlongcalls -specs=picolibc.specs -I$(IDF_PATH)/components/xtensa/esp32s3/include' \
		$(LIBTREALLA)

arduino-nano-esp32: arduino-nano-esp32-lib
	@test -f ports/arduino-nano-esp32/sdkconfig || \
		{ cd ports/arduino-nano-esp32 && $(IDF_PY) set-target esp32s3; }
	cd ports/arduino-nano-esp32 && $(IDF_PY) build

util/bin2c: util/bin2c.c
	$(HOST_CC) -o util/bin2c util/bin2c.c

util/embed_registry: util/embed_registry.c
	$(HOST_CC) -o util/embed_registry util/embed_registry.c

profile:
	$(MAKE) 'OPT=$(OPT) -O0 -pg -DDEBUG'

debug:
	$(MAKE) 'OPT=$(OPT) -fsanitize=address -O0 -g -DDEBUG'

# No sanitizer: macOS's `leaks` tool refuses to inspect an ASan binary
# at all. Build this, then: leaks --atExit -- ./tpl -q -f -g halt file.pl
leakcheck:
	$(MAKE) 'OPT=$(OPT) -O0 -g -DDEBUG'

sanitize:
	$(MAKE) 'OPT=$(OPT) -fsanitize=undefined,integer,address -O0 -g -DDEBUG'

release:
	$(MAKE) 'OPT=$(OPT) -DNDEBUG'

# `library` is a build directory as well as a source one: by the time
# install runs, `all` has filled it with the generated C, objects and
# depfiles for the embedded modules. Only the .pl sources belong in an
# installed tree, so prune everything else back out after the copy -
# a whitelist, because the set of artefacts grows with the build flags.
# The second find clears up behind the first: a cosmo build leaves a
# parallel .aarch64 object directory per module, which is an empty
# directory once its objects are gone. rmdir only ever removes empty
# directories, so the non-empty ones it is handed just fail harmlessly.

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/include
	cp tpl $(DESTDIR)$(BINDIR)/tpl
	cp -r library $(DESTDIR)$(LIBDIR)/
	find $(DESTDIR)$(LIBDIR)/library -type f ! -name '*.pl' -exec rm -f {} +
	find $(DESTDIR)$(LIBDIR)/library -depth -type d -exec rmdir {} + 2>/dev/null || true
	cp man/trealla.1 $(DESTDIR)$(MANDIR)/man1/trealla.1
	cp $(LIBTREALLA) $(DESTDIR)$(PREFIX)/lib/$(LIBTREALLA)
	cp src/trealla.h $(DESTDIR)$(PREFIX)/include/trealla.h
	chmod 755 $(DESTDIR)$(BINDIR)/tpl
	chmod 644 $(DESTDIR)$(MANDIR)/man1/trealla.1
	chmod 644 $(DESTDIR)$(PREFIX)/lib/$(LIBTREALLA)
	chmod 644 $(DESTDIR)$(PREFIX)/include/trealla.h

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/tpl
	rm -f $(DESTDIR)$(MANDIR)/man1/trealla.1
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIBTREALLA)
	rm -f $(DESTDIR)$(PREFIX)/include/trealla.h
	rm -rf $(DESTDIR)$(LIBDIR)

install-strip: install
	strip $(DESTDIR)$(BINDIR)/tpl

tpl.wasm:
	$(MAKE) WASI=1 'OPT=$(OPT) -DNDEBUG'

wasm: tpl.wasm
	$(WASMOPT) --enable-bulk-memory tpl.wasm -o tpl-opt.wasm -O4
	mv tpl-opt.wasm tpl.wasm

compile: util/bin2c
	echo '#include <stddef.h>' > main.c
	./util/bin2c $(main) main_pl >> main.c
	rm -f src/library.o
	$(CC) $(CFLAGS) -o main.o -c main.c
	$(CC) $(CFLAGS) -DUSE_MAIN=1 -o src/library.o -c src/library.c
	$(CC) $(CFLAGS) -o tpl $(OBJECTS) main.o $(OPT) $(LDFLAGS)
	rm -f main.c main.o src/library.o

# Janus: the Prolog-Python interface, off unless asked for. See
# docs/janus-design.md.
#
# -DUSE_JANUS goes on one object and never on CFLAGS. Make does not track
# flag changes, so a global define would let a plain `make`'s src/library.o
# satisfy this target - linking library/janus.o in while g_libs[] still has
# no janus entry, and leaving use_module(library(janus)) to fail from a
# tree that just built it. Deleting that object either side of the link is
# what the USE_MAIN handling in `compile:` above does, for the same reason.
#
# Carries no Python dependency: library/janus.pl is pure Prolog and finds
# libpython by dlopen at run time.

janus:
	$(MAKE) JANUS=1 janus-tpl

janus-tpl: $(OBJECTS)
	rm -f src/library.o
	$(CC) $(CFLAGS) -DUSE_JANUS=1 -o src/library.o -c src/library.c
	$(CC) $(CFLAGS) -o tpl $(OBJECTS) $(OPT) $(LDFLAGS)
	rm -f src/library.o

reference: tpl
	$(PYTHON) util/gen_reference.py --in-place README.md

# Regenerate library/raylib.pl from the installed raylib.h. Needs the
# headers, not the library, and is not part of a normal build: the
# generated file is checked in. RAYLIB_H overrides header discovery.

raylib:
	$(PYTHON) util/gen_raylib.py --verify
	$(PYTHON) util/gen_raylib.py --in-place

# The IPv4/UDP stack is device-agnostic, so it is tested against a netif that
# is not a device: no board, no emulator, just `make test` on any platform.
# It is not linked into tpl - nothing calls it yet - so this builds it alone.

tests/net/net_test: tests/net/net_test.c src/net/net.c src/net/net.h src/net/netif.h
	$(CC) $(CFLAGS) -Isrc/net -o $@ tests/net/net_test.c src/net/net.c $(OPT)

.PHONY: net-test net-interop

net-test: tests/net/net_test
	./tests/net/net_test

# The interoperability half: the same stack behind a netif backed by a real
# AF_PACKET socket, with Linux on the other end of a veth pair. Kept out of
# `make test` because it needs root and Linux; net-test needs neither.

tests/net/net_interop: tests/net/netif_afpacket.c src/net/net.c src/net/netif_afpacket.c src/net/net.h src/net/netif.h
	$(CC) $(CFLAGS) -Isrc/net -o $@ tests/net/netif_afpacket.c src/net/net.c src/net/netif_afpacket.c $(OPT)

net-interop: tests/net/net_interop
	./tests/net/interop.sh

test: net-test
	@if test -x samples/allocator; then ./samples/allocator; fi
	@if test -x samples/oom; then ./samples/oom; fi
	./tests/run.sh

misc:
	./tests/run_misc.sh

# Phase 0 acceptance for library(janus). Needs a `make janus` binary -
# a default build has no janus module, which is the point - so this is
# deliberately not reachable from `make test`.

# The Python -> Prolog half: a CPython extension module linking
# libtrealla.a. Separate from `make janus`, and the ONLY part of the
# project that needs Python headers rather than a libpython to dlopen -
# which is why it is not reachable from a plain `make` either.
#
# -bundle on macOS, -shared elsewhere; the module resolves the CPython
# symbols from the interpreter that loads it, so libpython is not linked.

PYINCS = $(shell python3-config --includes)

ifeq ($(shell uname -s),Darwin)
PYLDFLAGS = -bundle -undefined dynamic_lookup
else
PYLDFLAGS = -shared
endif

janus-py: $(LIBTREALLA)
	$(CC) $(CFLAGS) $(PYINCS) $(PYLDFLAGS) -o janus_trealla.so \
		src/janus_py.c $(LIBTREALLA) $(OPT) $(LDFLAGS)

janus-py-test:
	@test -f janus_trealla.so || \
		{ echo "janus_trealla.so is not built - run 'make janus-py' first"; exit 1; }
	@python3 tests/janus/test_janus_py.py

# Both janus test targets run against whatever ./tpl is, and a default
# build has no janus module - which shows up as every phase failing at
# once, a long way from the cause. Say so instead.

JANUS_BUILT = ./tpl -q -g "(catch(use_module(library(janus)),_,fail)->halt(0);halt(1))" \
	2>/dev/null || { echo "this ./tpl has no janus module - run 'make janus' first"; exit 1; }

# Phase 7. Drives the Python fixtures from SWI's swipy package, which are
# third-party and not vendored, so this is separate from janus-test and
# reports a skip rather than a failure when they are absent. Point
# JANUS_XSB_TESTS at them if they are somewhere unusual.

janus-conformance:
	@$(JANUS_BUILT)
	@./tpl -q -f tests/janus/conformance.pl -g "main,halt" </dev/null

# The per-test results are shown as they run; the diff against
# run.expected is the check, and only appears when something differs.

janus-test:
	@$(JANUS_BUILT)
	@./tests/janus/run.sh > tmp.janus.out 2>&1; \
	cat tmp.janus.out; \
	if diff -a --strip-trailing-cr tests/janus/run.expected tmp.janus.out \
		> tmp.janus.diff 2>&1; then \
		rm -f tmp.janus.out tmp.janus.diff; \
		echo; echo "janus: ok"; \
	else \
		echo; \
		echo "janus: FAILED - differs from tests/janus/run.expected:"; \
		cat tmp.janus.diff; \
		rm -f tmp.janus.out tmp.janus.diff; \
		exit 1; \
	fi

slow:
	./tests/run_slow.sh

valgrind:
	./tests/run_valgrind.sh

# Same leak-check contract, whichever tool the platform actually has:
# valgrind everywhere valgrind runs, macOS's own `leaks` where it doesn't
# (no Apple Silicon support in any current valgrind release). Build
# with `make leakcheck` first - neither tool can see through ASan.
leaks:
ifeq ($(UNAME_S), Darwin)
	./tests/run_leaks.sh
else
	./tests/run_valgrind.sh
endif

clean:
	rm -f tpl tpl.aarch64.elf tpl.com.dbg tpl.wasm $(LIBTREALLA) \
		src/*.o src/imath/*.o src/isocline/src/*.o src/sre/*.o \
		src/platform/*.o src/*.d src/imath/*.d src/isocline/src/*.d src/sre/*.d \
		src/platform/*.d library/*.d *.d \
		library/*.o library/*.c library/actors/*.o library/actors/*.c library/actors/*.d \
		*.o program.c samples/*.o samples/*.so \
		samples/embed samples/allocator samples/oom samples/oom.tmp samples/freestanding samples/*.d samples/embed_demo.pl \
		janus_trealla.so tmp.janus.out tmp.janus.diff \
		vgcore.* *.core core core.* *.exe gmon.* \
		samples/*.xwam util/bin2c util/embed_registry util/bin2c.aarch64.elf util/bin2c.com.dbg
	rm -f ports/qemu-riscv32/*.o ports/qemu-riscv32/*.d $(QEMU_RISCV_ELF)
	rm -f ports/template/*.o ports/template/*.d
	rm -f tests/net/net_test tests/net/net_interop tests/net/*.o tests/net/*.d src/net/*.o src/net/*.d
	rm -f ports/rpi4/*.o ports/rpi4/*.d $(RPI4_ELF) $(RPI4_IMG) $(RPI4_MAP)
	rm -rf samples/embed.dSYM samples/allocator.dSYM samples/oom.dSYM samples/freestanding.dSYM
	rm -f *.itf *.po *.xwam samples/*.itf samples/*.po
