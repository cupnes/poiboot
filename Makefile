FS_DIR  = ../fs
CC      = x86_64-w64-mingw32-gcc
OBJCOPY = x86_64-w64-mingw32-objcopy
CFLAGS  = -Wall -Wextra
CFLAGS += -nostdinc -nostdlib -fno-builtin -fno-common
CFLAGS += -Wl,--subsystem,10
ifdef NO_GRAPHIC
	QEMU_ADDITIONAL_ARGS += --nographic
endif
ifdef SMP
	QEMU_ADDITIONAL_ARGS += -smp ${SMP}
endif

poiboot.efi: poiboot.o libuefi/libuefi.a
	$(CC) $(CFLAGS) -e efi_main -o $@ $+

poiboot.o: poiboot.c
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

libuefi/libuefi.a:
	make -C libuefi CC=$(CC) CFLAGS="$(CFLAGS)"

deploy: poiboot.efi
	mkdir -p $(FS_DIR)/EFI/BOOT
	cp poiboot_default.conf $(FS_DIR)/poiboot.conf
	cp $< $(FS_DIR)/EFI/BOOT/BOOTX64.EFI

run: deploy
	qemu-system-x86_64 -m 4G -enable-kvm \
	-drive if=pflash,format=raw,readonly,file=$(HOME)/OVMF/OVMF_CODE.fd \
	-drive if=pflash,format=raw,file=$(HOME)/OVMF/OVMF_VARS.fd \
	-drive dir=$(FS_DIR),driver=vvfat,rw=on \
	$(QEMU_ADDITIONAL_ARGS)

clean:
	rm -f *~ include/*~ *.o *.efi
	make -C libuefi clean

.PHONY: deploy run clean
