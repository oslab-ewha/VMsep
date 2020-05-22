#pragma once

#include <ntddk.h>

LPWSTR libdrv_strdupW(LPCWSTR cwstr);

int libdrv_snprintf(char *buf, int size, const char *fmt, ...);
int libdrv_snprintfW(PWCHAR buf, int size, LPCWSTR fmt, ...);
int libdrv_asprintfW(PWCHAR *pbuf, LPCWSTR fmt, ...);
