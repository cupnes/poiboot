#include <efi.h>
#include <common.h>
#include <mem.h>
#include <fb.h>
#include <file.h>

#define CONF_FILE_NAME	L"poiboot.conf"

#define KERNEL_FILE_NAME	L"kernel.bin"
#define APPS_FILE_NAME	L"fs.img"

#define MB		1048576	/* 1024 * 1024 */

void put_n_bytes(unsigned char *addr, unsigned int num);
void put_param(unsigned short *name, unsigned long long val);

void efi_main(void *ImageHandle, struct EFI_SYSTEM_TABLE *SystemTable)
{
	unsigned long long status;
	unsigned char has_apps = TRUE;

	efi_init(SystemTable);

	puts(L"Starting OS5 UEFI bootloader ...\r\n");

	struct EFI_FILE_PROTOCOL *root;
	status = SFSP->OpenVolume(SFSP, &root);
	assert(status, L"SFSP->OpenVolume");


	/* load config file */
	struct EFI_FILE_PROTOCOL *file_conf;
	status = root->Open(
		root, &file_conf, CONF_FILE_NAME, EFI_FILE_MODE_READ, 0);
	assert(status, L"Can't open config file.");

	struct config {
		char kernel_start[17];
		char apps_start[17];
	} conf;

	unsigned long long conf_size = sizeof(conf);
	put_param(L"conf_size", conf_size);

	puts(L"load conf ... ");
	safety_file_read(file_conf, (void *)&conf, conf_size);
	puts(L"done\r\n");
	file_conf->Close(file_conf);

	unsigned long long kernel_start = hexstrtoull(conf.kernel_start);
	put_param(L"kernel_start", kernel_start);
	unsigned long long stack_base = kernel_start + (1 * MB);
			/* stack_baseはスタックの底のアドレス(上へ伸びる) */
	put_param(L"stack_base", stack_base);
	unsigned long long apps_start = hexstrtoull(conf.apps_start);
	put_param(L"apps_start", apps_start);


	/* load the kernel */
	struct EFI_FILE_PROTOCOL *file_kernel;
	status = root->Open(
		root, &file_kernel, KERNEL_FILE_NAME, EFI_FILE_MODE_READ, 0);
	assert(status, L"root->Open(kernel)");

	unsigned long long kernel_size = get_file_size(file_kernel);
	put_param(L"kernel_size", kernel_size);

	struct header {
		void *bss_start;
		unsigned long long bss_size;
	} head;

	unsigned long long head_size = sizeof(head);
	puts(L"load kernel head ... ");
	safety_file_read(file_kernel, (void *)&head, head_size);
	puts(L"done\r\n");

	kernel_size -= sizeof(head);
	puts(L"load kernel body ... ");
	safety_file_read(file_kernel, (void *)kernel_start, kernel_size);
	puts(L"done\r\n");
	file_kernel->Close(file_kernel);

	ST->BootServices->SetMem(head.bss_start, head.bss_size, 0);

	puts(L"kernel body first 16 bytes: 0x");
	put_n_bytes((unsigned char *)kernel_start, 16);
	puts(L"\r\n");
	puts(L"kernel body last 16 bytes: 0x");
	unsigned char *kernel_last =
		(unsigned char *)(kernel_start + kernel_size - 16);
	put_n_bytes(kernel_last, 16);
	puts(L"\r\n");


	/* load the applications */
	struct EFI_FILE_PROTOCOL *file_apps;
	status = root->Open(
		root, &file_apps, APPS_FILE_NAME, EFI_FILE_MODE_READ, 0);
	if (!check_warn_error(status, L"root->Open(apps)")) {
		puts(L"apps load failure. skip.\r\n");
		has_apps = FALSE;
	}

	if (has_apps) {
		unsigned long long apps_size = get_file_size(file_apps);
		put_param(L"apps_size", apps_size);

		puts(L"load apps ... ");
		safety_file_read(file_apps, (void *)apps_start, apps_size);
		puts(L"done\r\n");
		file_apps->Close(file_apps);

		puts(L"apps first 16 bytes: 0x");
		put_n_bytes((unsigned char *)apps_start, 16);
		puts(L"\r\n");
		puts(L"apps last 16 bytes: 0x");
		unsigned char *apps_last =
			(unsigned char *)(apps_start + apps_size - 16);
		put_n_bytes(apps_last, 16);
		puts(L"\r\n");
	}


	unsigned long long kernel_arg1 = (unsigned long long)ST;
	put_param(L"kernel_arg1", kernel_arg1);
	init_fb();
	unsigned long long kernel_arg2 = (unsigned long long)&fb;
	put_param(L"kernel_arg2", kernel_arg2);
	unsigned long long kernel_arg3;
	if (has_apps == TRUE)
		kernel_arg3 = apps_start;
	else
		kernel_arg3 = 0;
	put_param(L"kernel_arg3", kernel_arg3);

	exit_boot_services(ImageHandle);

	unsigned long long _sb = stack_base, _ks = kernel_start;
	__asm__ ("	mov	%0, %%rdx\n"
		 "	mov	%1, %%rsi\n"
		 "	mov	%2, %%rdi\n"
		 "	mov	%3, %%rsp\n"
		 "	jmp	*%4\n"
		 ::"m"(kernel_arg3), "m"(kernel_arg2), "m"(kernel_arg1),
		  "m"(_sb), "m"(_ks));

	while (TRUE);
}

void put_n_bytes(unsigned char *addr, unsigned int num)
{
	unsigned int i;
	for (i = 0; i < num; i++) {
		puth(*addr++, 2);
		putc(L' ');
	}
}

void put_param(unsigned short *name, unsigned long long val)
{
	puts(name);
	puts(L": 0x");
	puth(val, 16);
	puts(L"\r\n");
}
