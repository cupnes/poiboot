#include <efi.h>
#include <fb.h>
#include <common.h>

struct fb fb;

void init_fb(unsigned int mode_width, unsigned int mode_height)
{
	int mode = search_graphic_mode(mode_width, mode_height);
	if (mode >= 0) {
		unsigned long long status = GOP->SetMode(GOP, mode);
		assert(status, L"init_fb: GOP->SetMode()");
	}

	fb.base = GOP->Mode->FrameBufferBase;
	fb.size = GOP->Mode->FrameBufferSize;
	fb.hr = GOP->Mode->Info->HorizontalResolution;
	fb.vr = GOP->Mode->Info->VerticalResolution;
	fb.px_per_sl = GOP->Mode->Info->PixelsPerScanLine;
}

void dump_available_graphic_modes(void)
{
	puts(L"### dump available graphic modes ###\r\n");

	unsigned int mode_num;
	for (mode_num = 0; mode_num < GOP->Mode->MaxMode; mode_num++) {
		unsigned long long size_info;
		struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
		unsigned long long status = GOP->QueryMode(GOP, mode_num,
							   &size_info, &info);
		assert(status, L"GOP->QueryMode()");

		put_param(L"ModeNumber", mode_num);
		put_param(L"Version", info->Version);
		put_param(L"HorizontalResolution", info->HorizontalResolution);
		put_param(L"VerticalResolution", info->VerticalResolution);
		put_param(L"PixelFormat", info->PixelFormat);
		put_param(L"PixelsPerScanLine", info->PixelsPerScanLine);
		puts(L"\r\n");
	}
}

int search_graphic_mode(unsigned int hr, unsigned int vr)
{
	puts(L"## BEGIN: search_graphic_mode()\r\n");

	unsigned int mode_num;
	for (mode_num = 0; mode_num < GOP->Mode->MaxMode; mode_num++) {
		unsigned long long size_info;
		struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
		unsigned long long status = GOP->QueryMode(GOP, mode_num,
							   &size_info, &info);
		assert(status, L"GOP->QueryMode()");

		puts(L"## ");
		puth(mode_num, 3);
		puts(L": ");
		puth(info->HorizontalResolution, 8);
		puts(L", ");
		puth(info->VerticalResolution, 8);
		puts(L"\r\n");

		if ((info->HorizontalResolution == hr)
		    && (info->VerticalResolution == vr)) {
			puts(L"## FOUND: mode=");
			puth(mode_num, 8);
			puts(L"\r\n");
			puts(L"## END: search_graphic_mode(FOUND)\r\n");
			return mode_num;
		}
	}
	puts(L"## END: search_graphic_mode(Not found)\r\n");
	return -1;
}
