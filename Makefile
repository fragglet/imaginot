
# makefile for OpenWatcom wmake
# To invoke: wmake -f makefile.wat

SOURCE_DIRS = lib
CFLAGS = -I. -q -onx -w3
LDFLAGS = -q

OBJS = bld\fakedisk.o bld\imaginot.o bld\doomnet.o bld\protocol.o &
       bld\crc32.o bld\common.lib
EXE = imaginot.exe

all: $(EXE)

bld\common.lib: bld\flag.o bld\log.o bld\dos.o bld\ints.o
	wlib -q -n $@ +bld\flag.o +bld\log.o +bld\dos.o +bld\ints.o
imaginot.exe: $(OBJS)
	wcl -q -fe=$@ $(OBJS)

.EXTENSIONS:
.EXTENSIONS: .exe .o .asm .c

.c: $(SOURCE_DIRS)
.asm: $(SOURCE_DIRS)

.c.o:
	wcc $(CFLAGS) -fo$@ $<
.asm.o:
	wasm -q -fo=$@ $<

clean: .always
	del bld\*.o
	del bld\*.lib
	del $(EXE)

rel: $(EXE) .always
	!copy dist\*.* rel\
	vsetargs rel\ipxsetup.exe imaginot sopwith2 -db
	vsetargs rel\udpsetup.exe imaginot sopwith2 -db
	!copy imaginot.exe rel\
