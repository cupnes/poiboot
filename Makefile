CC      = x86_64-w64-mingw32-gcc
OBJCOPY = x86_64-w64-mingw32-objcopy
CFLAGS  = -Wall -Wextra
CFLAGS += -nostdinc -nostdlib -fno-builtin -fno-common
CFLAGS += -Wl,--subsystem,10

poiboot.efi: poiboot.o libuefi/libuefi.a
	$(CC) $(CFLAGS) -e efi_main -o $@ $+

poiboot.o: poiboot.c
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

libuefi/libuefi.a:
	make -C libuefi CC=$(CC) CFLAGS="$(CFLAGS)"

deploy: poiboot.efi
	mkdir -p ../fs/EFI/BOOT
	cp $< ../fs/EFI/BOOT/BOOTX64.EFI

clean:
	rm -f *~ *.o *.efi
	make -C libuefi clean

.PHONY: clean
