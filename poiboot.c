#include <efi.h>
#include <common.h>
#include <mem.h>
#include <fb.h>
#include <file.h>
#include <config.h>

#define KERNEL_FILE_NAME	L"kernel.bin"
#define FS_FILE_NAME	L"fs.img"

/* AP側のUEFI処理完了までの待ち時間(単位: マイクロ秒) */
#define WAIT_FOR_AP_USECS	100000 /* 100ms */

struct __attribute__((packed)) platform_info {
	struct fb fb;
	void *rsdp;
	void *fs_start;
	unsigned int nproc;
} pi;

struct ap_info {
	unsigned long long kernel_start;
	unsigned long long stack_space_start;
	struct EFI_SYSTEM_TABLE *system_table;
} ai;

void load_kernel(
	struct EFI_FILE_PROTOCOL *root, unsigned short *kernel_file_name);
unsigned char load_fs(
	struct EFI_FILE_PROTOCOL *root, unsigned short *fs_file_name);
void put_n_bytes(unsigned char *addr, unsigned int num);
void put_param(unsigned short *name, unsigned long long val);
void ap_main(void *_ai);

void efi_main(void *ImageHandle, struct EFI_SYSTEM_TABLE *SystemTable)
{
	efi_init(SystemTable);
	config_init();

	puts(L"Starting poiboot ...\r\n");

	/* ボリュームのルートディレクトリを開く */
	struct EFI_FILE_PROTOCOL *root;
	unsigned long long status = SFSP->OpenVolume(SFSP, &root);
	assert(status, L"SFSP->OpenVolume");

	/* コンフィグファイル・カーネルバイナリ・ファイルシステムイメージを
	 * 開き、コンフィグファイルの内容に従って
	 * カーネルバイナリとファイルシステムイメージをメモリへロードする */
	load_config(root, CONF_FILE_NAME);
	load_kernel(root, KERNEL_FILE_NAME);
	unsigned char has_fs = load_fs(root, FS_FILE_NAME);

	/* カーネルへ引数として渡す内容を変数に準備する */
	unsigned long long kernel_arg1 = (unsigned long long)ST;
	put_param(L"kernel_arg1", kernel_arg1);
	init_fb();
	pi.fb.base = fb.base;
	pi.fb.size = fb.size;
	pi.fb.hr = fb.hr;
	pi.fb.vr = fb.vr;
	pi.rsdp = find_efi_acpi_table();
	if (has_fs == TRUE)
		pi.fs_start = (void *)conf.fs_start;
	else
		pi.fs_start = NULL;
	unsigned long long nproc, nproc_en;
	status = MSP->GetNumberOfProcessors(MSP, &nproc, &nproc_en);
	assert(status, L"MSP->GetNumberOfProcessors");
	pi.nproc = nproc_en;
	unsigned long long kernel_arg2 = (unsigned long long)&pi;
	put_param(L"kernel_arg2", kernel_arg2);
	unsigned long long pnum;
	status = MSP->WhoAmI(MSP, &pnum);
	assert(status, L"MSP->WhoAmI");
	unsigned long long kernel_arg3 = pnum;
	put_param(L"kernel_arg3", kernel_arg3);

	/* 画面クリア */
	ST->ConOut->ClearScreen(ST->ConOut);

	/* APを起動 */
	if (conf.enable_ap) {
		ai.kernel_start = conf.kernel_start;
		ai.stack_space_start = conf.stack_base;
		ai.system_table = ST;
		status = MSP->StartupAllAPs(
			MSP, ap_main, 0, NULL, WAIT_FOR_AP_USECS, &ai, NULL);
	}

	/* UEFIのブートローダー向け機能を終了させる */
	exit_boot_services(ImageHandle);

	/* カーネルへ渡す引数設定(引数に使うレジスタへセットする) */
	unsigned long long _sb = conf.stack_base, _ks = conf.kernel_start;
	__asm__ ("	mov	%0, %%rdx\n"
		 "	mov	%1, %%rsi\n"
		 "	mov	%2, %%rdi\n"
		 "	mov	%3, %%rsp\n"
		 "	jmp	*%4\n"
		 ::"m"(kernel_arg3), "m"(kernel_arg2), "m"(kernel_arg1),
		  "m"(_sb), "m"(_ks));

	while (TRUE);
}

void load_kernel(
	struct EFI_FILE_PROTOCOL *root, unsigned short *kernel_file_name)
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
	safety_file_read(file_kernel, (void *)conf.kernel_start, kernel_size);
	puts(L"done\r\n");
	file_kernel->Close(file_kernel);

	ST->BootServices->SetMem(head.bss_start, head.bss_size, 0);

	puts(L"kernel body first 16 bytes: 0x");
	put_n_bytes((unsigned char *)conf.kernel_start, 16);
	puts(L"\r\n");
	puts(L"kernel body last 16 bytes: 0x");
	unsigned char *kernel_last =
		(unsigned char *)(conf.kernel_start + kernel_size - 16);
	put_n_bytes(kernel_last, 16);
	puts(L"\r\n");
}

unsigned char load_fs(
	struct EFI_FILE_PROTOCOL *root, unsigned short *fs_file_name)
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
	safety_file_read(file_fs, (void *)conf.fs_start, fs_size);
	puts(L"done\r\n");
	file_fs->Close(file_fs);

	puts(L"fs first 16 bytes: 0x");
	put_n_bytes((unsigned char *)conf.fs_start, 16);
	puts(L"\r\n");
	puts(L"fs last 16 bytes: 0x");
	unsigned char *fs_last =
		(unsigned char *)(conf.fs_start + fs_size - 16);
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

void ap_main(void *_ai)
{
	struct ap_info *ai = _ai;
	efi_init(ai->system_table);

	unsigned long long pnum;
	unsigned long long status = MSP->WhoAmI(MSP, &pnum);
	assert(status, L"MSP->WhoAmI");

	unsigned long long stack_base = ai->stack_space_start + (pnum * MB);

	unsigned long long kernel_arg1 = (unsigned long long)ST;
	unsigned long long kernel_arg2 = 0;
	unsigned long long kernel_arg3 = pnum;

	/* カーネルへ渡す引数設定(引数に使うレジスタへセットする) */
	unsigned long long _sb = stack_base, _ks = ai->kernel_start;
	__asm__ ("	mov	%0, %%rdx\n"
		 "	mov	%1, %%rsi\n"
		 "	mov	%2, %%rdi\n"
		 "	mov	%3, %%rsp\n"
		 "	jmp	*%4\n"
		 ::"m"(kernel_arg3), "m"(kernel_arg2), "m"(kernel_arg1),
		  "m"(_sb), "m"(_ks));

	while (TRUE);
}
