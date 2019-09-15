#include <efi.h>
#include <file.h>
#include <config.h>
#include <common.h>

char conf_buf[CONF_BUF_SIZE];

struct config_list conf;

void config_init(void)
{
	conf.kernel_start = CONF_DEFAULT_KERNEL_START;
	conf.stack_base = CONF_DEFAULT_STACK_BASE;
	conf.fs_start = CONF_DEFAULT_FS_START;
	conf.enable_ap = CONF_DEFAULT_ENABLE_AP;
}

static void conf_set_val(char *name, char *val_str)
{
	if (!strcmp_char(name, CONF_NAME_KERNEL_START))
		conf.kernel_start = hexstrtoull(val_str);
	else if (!strcmp_char(name, CONF_NAME_STACK_BASE))
		conf.stack_base = hexstrtoull(val_str);
	else if (!strcmp_char(name, CONF_NAME_FS_START))
		conf.fs_start = hexstrtoull(val_str);
	else if (!strcmp_char(name, CONF_NAME_ENABLE_AP))
		conf.enable_ap = boolstrtouc(val_str);
}

static void conf_parser(char *buf, unsigned long long buf_size)
{
	enum PARSER_STATE {
		PS_GET_NAME,
		PS_GET_VAL,
		PS_SKIP
	};
	enum PARSER_STATE state = PS_GET_NAME;

	char name[CONF_MAX_NAME_LEN], val_str[CONF_MAX_VAL_LEN];
	unsigned char idx = 0;
	unsigned long long i;
	for (i = 0; i < buf_size; i++) {
		switch (state) {
		case PS_GET_NAME:
			switch (buf[i]) {
			case '\r':
			case '\n':
				idx = 0;
				break;

			case '=':
				name[idx] = '\0';
				idx = 0;
				state = PS_GET_VAL;
				break;

			default:
				name[idx++] = buf[i];
			}
			break;

		case PS_GET_VAL:
			if ((buf[i] != '\r') && (buf[i] != '\n'))
				val_str[idx++] = buf[i];
			else {
				val_str[idx] = '\0';
				conf_set_val(name, val_str);
				idx = 0;
				state = PS_GET_NAME;
			}
			break;

		case PS_SKIP:
			if ((buf[i] == '\r') || (buf[i] == '\n')) {
				idx = 0;
				state = PS_GET_NAME;
			}
			break;
		}
	}
	if (state == PS_GET_VAL) {
		val_str[idx] = '\0';
		conf_set_val(name, val_str);
	}
}

void load_config(
	struct EFI_FILE_PROTOCOL *root, unsigned short *conf_file_name)
{
	struct EFI_FILE_PROTOCOL *file_conf;
	unsigned long long no_conf_file = root->Open(
		root, &file_conf, conf_file_name, EFI_FILE_MODE_READ, 0);

	if (no_conf_file)
		puts(L"Can't open config file. Use default value.\r\n");
	else {
		unsigned long long conf_size = get_file_size(file_conf);
		assert(conf_size > CONF_BUF_SIZE, L"Config file too big.");

		puts(L"load conf ... ");
		safety_file_read(file_conf, conf_buf, conf_size);
		puts(L"done\r\n");
		file_conf->Close(file_conf);

		conf_parser(conf_buf, conf_size);
	}

	put_param(L"kernel_start", conf.kernel_start);
	put_param(L"stack_base", conf.stack_base);
	put_param(L"fs_start", conf.fs_start);
	put_param(L"enable_ap", conf.enable_ap);
}
