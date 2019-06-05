#include <efi.h>
#include <common.h>
#include <mem.h>
#include <fb.h>
#include <file.h>

#define CONF_FILE_NAME	L"poiboot.conf"

#define KERNEL_FILE_NAME	L"kernel.bin"
#define FS_FILE_NAME	L"fs.img"

#define MB		1048576	/* 1024 * 1024 */

#define NPROCS	4

#define WAIT_FOR_AP_USECS	500000

struct __attribute__((packed)) platform_info {
	struct fb fb;
	void *rsdp;
	unsigned char pnum;
} pi[NPROCS];

struct ap_info {
	unsigned long long stack_base;
	unsigned long long kernel_start;
	unsigned long long system_table;
	unsigned long long platform_info;
	unsigned long long fs_start;
} ai;

void load_config(
	struct EFI_FILE_PROTOCOL *root, unsigned short *conf_file_name,
	unsigned long long *kernel_start, unsigned long long *stack_base,
	unsigned long long *fs_start);
void load_kernel(
	struct EFI_FILE_PROTOCOL *root, unsigned short *kernel_file_name,
	unsigned long long kernel_start);
unsigned char load_fs(
	struct EFI_FILE_PROTOCOL *root, unsigned short *fs_file_name,
	unsigned long long fs_start);
void put_n_bytes(unsigned char *addr, unsigned int num);
void put_param(unsigned short *name, unsigned long long val);
void ap_main(void *_ai);

void efi_main(void *ImageHandle, struct EFI_SYSTEM_TABLE *SystemTable)
{
	efi_init(SystemTable);

	puts(L"Starting poiboot ...\r\n");

	/* ボリュームのルートディレクトリを開く */
	struct EFI_FILE_PROTOCOL *root;
	unsigned long long status = SFSP->OpenVolume(SFSP, &root);
	assert(status, L"SFSP->OpenVolume");

	/* コンフィグファイル・カーネルバイナリ・ファイルシステムイメージを
	 * 開き、コンフィグファイルの内容に従って
	 * カーネルバイナリとファイルシステムイメージをメモリへロードする */
	unsigned long long kernel_start, stack_base, fs_start;
	load_config(root, CONF_FILE_NAME,
		    &kernel_start, &stack_base, &fs_start);
	load_kernel(root, KERNEL_FILE_NAME, kernel_start);
	unsigned char has_fs = load_fs(root, FS_FILE_NAME, fs_start);

	/* カーネルへ引数として渡す内容を変数に準備する */
	unsigned long long kernel_arg1 = (unsigned long long)ST;
	put_param(L"kernel_arg1", kernel_arg1);
	init_fb();
	pi[0].fb.base = fb.base;
	pi[0].fb.size = fb.size;
	pi[0].fb.hr = fb.hr;
	pi[0].fb.vr = fb.vr;
	pi[0].rsdp = find_efi_acpi_table();
	unsigned long long _pnum;
	status = MSP->WhoAmI(MSP, &_pnum);
	assert(status, L"MSP->WhoAmI");
	pi[0].pnum = (unsigned char)_pnum;
	unsigned long long kernel_arg2 = (unsigned long long)&pi[0];
	put_param(L"kernel_arg2", kernel_arg2);
	unsigned long long kernel_arg3;
	if (has_fs == TRUE)
		kernel_arg3 = fs_start;
	else
		kernel_arg3 = 0;
	put_param(L"kernel_arg3", kernel_arg3);

	/* APを起動 */
	ai.stack_base = stack_base;
	ai.kernel_start = kernel_start;
	ai.system_table = kernel_arg1;
	ai.platform_info = kernel_arg2;
	ai.fs_start = kernel_arg3;
	status = MSP->StartupAllAPs(
		MSP, ap_main, 0, NULL, WAIT_FOR_AP_USECS, &ai, NULL);

	/* UEFIのブートローダー向け機能を終了させる */
	exit_boot_services(ImageHandle);

	/* カーネルへ渡す引数設定(引数に使うレジスタへセットする) */
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

void load_config(
	struct EFI_FILE_PROTOCOL *root, unsigned short *conf_file_name,
	unsigned long long *kernel_start, unsigned long long *stack_base,
	unsigned long long *fs_start)
{
	struct EFI_FILE_PROTOCOL *file_conf;
	unsigned long long status = root->Open(
		root, &file_conf, conf_file_name, EFI_FILE_MODE_READ, 0);
	assert(status, L"Can't open config file.");

	struct config {
		char kernel_start[17];
		char fs_start[17];
	} conf;

	unsigned long long conf_size = sizeof(conf);
	put_param(L"conf_size", conf_size);

	puts(L"load conf ... ");
	safety_file_read(file_conf, (void *)&conf, conf_size);
	puts(L"done\r\n");
	file_conf->Close(file_conf);

	*kernel_start = hexstrtoull(conf.kernel_start);
	put_param(L"kernel_start", *kernel_start);
	*stack_base = *kernel_start + (1 * MB);
			/* stack_baseはスタックの底のアドレス(上へ伸びる) */
	put_param(L"stack_base", *stack_base);
	*fs_start = hexstrtoull(conf.fs_start);
	put_param(L"fs_start", *fs_start);
}

void load_kernel(
	struct EFI_FILE_PROTOCOL *root, unsigned short *kernel_file_name,
	unsigned long long kernel_start)
{
	struct EFI_FILE_PROTOCOL *file_kernel;
	unsigned long long status = root->Open(
		root, &file_kernel, kernel_file_name, EFI_FILE_MODE_READ, 0);
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
}

unsigned char load_fs(
	struct EFI_FILE_PROTOCOL *root, unsigned short *fs_file_name,
	unsigned long long fs_start)
{
	struct EFI_FILE_PROTOCOL *file_fs;
	unsigned long long status = root->Open(
		root, &file_fs, fs_file_name, EFI_FILE_MODE_READ, 0);
	if (!check_warn_error(status, L"root->Open(fs)")) {
		puts(L"fs load failure. skip.\r\n");
		return FALSE;
	}

	unsigned long long fs_size = get_file_size(file_fs);
	put_param(L"fs_size", fs_size);

	puts(L"load fs ... ");
	safety_file_read(file_fs, (void *)fs_start, fs_size);
	puts(L"done\r\n");
	file_fs->Close(file_fs);

	puts(L"fs first 16 bytes: 0x");
	put_n_bytes((unsigned char *)fs_start, 16);
	puts(L"\r\n");
	puts(L"fs last 16 bytes: 0x");
	unsigned char *fs_last =
		(unsigned char *)(fs_start + fs_size - 16);
	put_n_bytes(fs_last, 16);
	puts(L"\r\n");

	return TRUE;
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

void ap_main(void *_ai)
{
	struct ap_info *ai = _ai;
	efi_init((struct EFI_SYSTEM_TABLE *)ai->system_table);

	unsigned long long pnum;
	unsigned long long status = MSP->WhoAmI(MSP, &pnum);
	assert(status, L"MSP->WhoAmI");

	unsigned long long stack_base = ai->stack_base + (pnum * MB);

	unsigned long long kernel_start = ai->kernel_start;

	unsigned long long kernel_arg1 = ai->system_table;

	struct platform_info *_pi = (struct platform_info *)ai->platform_info;
	_pi += pnum;
	_pi->pnum = pnum;
	unsigned long long kernel_arg2 = (unsigned long long)_pi;

	unsigned long long kernel_arg3 = ai->fs_start;

	/* カーネルへ渡す引数設定(引数に使うレジスタへセットする) */
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
