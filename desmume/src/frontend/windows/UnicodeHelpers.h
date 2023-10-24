#ifndef _UNICODEHELPERS_H_INCLUDED_
#define _UNICODEHELPERS_H_INCLUDED_

#ifdef _MSC_VER
#pragma once
#endif  // _MSC_VER

#include <stdio.h>
#include <stdlib.h>

#define fopen _fopen_
#define fopen_s _fopen_s_

extern void UTF8ToUTF16(const char* utf8String, wchar_t* utf16String);

inline FILE* __cdecl _fopen_(const char* path, const char* mode) {
	wchar_t unicode_path[1024] = {0};
	wchar_t unicode_mode[32] = {0};
	UTF8ToUTF16(path, unicode_path);
	mbstowcs(unicode_mode, mode, _TRUNCATE);
	return _wfopen(unicode_path, unicode_mode);
}

inline errno_t __cdecl _fopen_s_(FILE** stream, const char* path, const char* mode) {
	wchar_t unicode_path[1024] = {0};
	wchar_t unicode_mode[32] = {0};
	UTF8ToUTF16(path, unicode_path);
	mbstowcs(unicode_mode, mode, _TRUNCATE);
	return _wfopen_s(stream, unicode_path, unicode_mode);
}

#endif
