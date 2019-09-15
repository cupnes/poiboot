#pragma once

#define CONF_BUF_SIZE	4096
#define CONF_FILE_NAME	L"poiboot.conf"

#define CONF_MAX_NAME_LEN	100
#define CONF_MAX_VAL_LEN	100

#define CONF_NAME_KERNEL_START	"kernel_start"
#define CONF_DEFAULT_KERNEL_START	0x0000000000110000

#define CONF_NAME_STACK_BASE	"stack_base"
#define CONF_DEFAULT_STACK_BASE	0x0000000000210000

#define CONF_NAME_FS_START	"fs_start"
#define CONF_DEFAULT_FS_START	0x0000000100000000

struct config_list {
	unsigned long long kernel_start;
	unsigned long long stack_base;
	unsigned long long fs_start;
};

extern struct config_list conf;

void config_init(void);

void load_config(
	struct EFI_FILE_PROTOCOL *root, unsigned short *conf_file_name);
