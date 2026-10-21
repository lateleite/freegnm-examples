# Package metadata.
TITLE=FreeGNM Indirect Sample
VERSION=1.00
TITLE_ID=FGNM00005
CONTENT_ID=IV0000-FGNM00005_00-INDIRECT00000000

# Root toolchain directory
TOOLCHAIN=$(OO_PS4_TOOLCHAIN)
# Shared source files directory path
SHARED_PATH=../shared

# Tools to be used
CC=clang
LD=ld.lld
GLSLC=glslc
PSBC=psbc
MKFSELF=$(TOOLCHAIN)/bin/linux/create-fself
MKGP4=$(TOOLCHAIN)/bin/linux/create-gp4
# HACK: linux PkgTool.Core doesn't run under muslc,
# use windows' tool as an alternative
PKGTOOL=wine64 $(TOOLCHAIN)/bin/windows/PkgTool.exe

# Objects to build
SRCS=\
	$(SHARED_PATH)/src/u/fs.c \
	$(SHARED_PATH)/src/u/utility.c \
	$(SHARED_PATH)/src/displayctx.c \
	$(SHARED_PATH)/src/font2d.c \
	$(SHARED_PATH)/src/gltfmodel.c \
	$(SHARED_PATH)/src/memalloc.c \
	$(SHARED_PATH)/src/misc.c \
	src/main.c \
	src/pass_model.c \
	src/pass_ui.c
OBJS=$(SRCS:%.c=%.o)
EXE=indirect

VERT_SRCS=\
	assets/misc/font.vert.glsl \
	assets/misc/indirect.vert.glsl
FRAG_SRCS=\
	assets/misc/clear.frag.glsl \
	assets/misc/font.frag.glsl \
	assets/misc/indirect.frag.glsl
VERT_SPVS=$(VERT_SRCS:%.glsl=%.spv)
VERT_PSBS=$(VERT_SRCS:%.glsl=%.sb)
FRAG_SPVS=$(FRAG_SRCS:%.glsl=%.spv)
FRAG_PSBS=$(FRAG_SRCS:%.glsl=%.sb)

# Custom tools flags
# cglm needs CGLM_ALL_UNALIGNED
# because buffers that use its objects may not be aligned
CFLAGS=-std=c11 -Wall -Wextra -Wpedantic -DCGLM_ALL_UNALIGNED \
	-DCGLM_FORCE_LEFT_HANDED -DCGLM_FORCE_DEPTH_ZERO_TO_ONE \
	-D_XOPEN_SOURCE=500 -I$(SHARED_PATH)/src
LDFLAGS=-lc -lkernel -lSceGnmDriver -lSceVideoOut -lgnm
GLSLCFLAGS=

# Asset and module directories.
ASSETS=\
	assets/fonts/Hack-Regular.ttf \
	assets/misc/venus.gltf \
	$(VERT_PSBS) \
	$(FRAG_PSBS)
LIBMODULES=$(wildcard sce_module/*)

all: $(CONTENT_ID).pkg

# compiler
%.o: %.c
	$(CC) --target=x86_64-ps4-elf -fPIC -c -isysroot $(TOOLCHAIN) \
		-isystem $(TOOLCHAIN)/include $(CFLAGS) -o $@ $<
# linker
$(EXE): $(OBJS)
	$(LD) -o $@ $(OBJS) -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x \
		--eh-frame-hdr -L$(TOOLCHAIN)/lib $(LDFLAGS) $(TOOLCHAIN)/lib/crt1.o \
		$(TOOLCHAIN)/lib/crti.o $(TOOLCHAIN)/lib/crtn.o

# GLSL shader compiler
%.vert.spv: %.vert.glsl
	$(GLSLC) -fshader-stage=vertex $(GLSLCFLAGS) $< -o $@
%.frag.spv: %.frag.glsl
	$(GLSLC) -fshader-stage=fragment $(GLSLCFLAGS) $< -o $@
# PSB/GCN shader compiler
%.vert.sb: %.vert.spv
	$(PSBC) -s vertex -f $< -o $@
%.frag.sb: %.frag.spv
	$(PSBC) -s fragment -f $< -o $@

# eboot creation
eboot.bin: $(EXE)
	$(MKFSELF) -in=$(EXE) -out=$(EXE).oelf -eboot=eboot.bin --paid 0x3800000000000011

sce_sys/param.sfo: Makefile
	$(PKGTOOL) sfo_new $@
	$(PKGTOOL) sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKGTOOL) sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(PKGTOOL) sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(PKGTOOL) sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(PKGTOOL) sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(PKGTOOL) sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(PKGTOOL) sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

pkg.gp4: eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png $(LIBMODULES) $(ASSETS)
	$(MKGP4) -out $@ --content-id=$(CONTENT_ID) --files "$^"

# final application package
$(CONTENT_ID).pkg: pkg.gp4
	$(PKGTOOL) pkg_build $< .

clean:
	rm -f $(CONTENT_ID).pkg pkg.gp4 pkg/sce_sys/param.sfo eboot.bin	$(EXE) \
		$(EXE).oelf $(OBJS) $(VERT_SPVS) $(FRAG_SPVS) \
		$(VERT_PSBS) $(FRAG_PSBS)
