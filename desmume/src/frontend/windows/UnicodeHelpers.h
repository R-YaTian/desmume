#ifndef _UNICODEHELPERS_H_INCLUDED_
#define _UNICODEHELPERS_H_INCLUDED_

#ifdef _MSC_VER
#pragma once
#endif  // _MSC_VER

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define fopen _fopen_
#define fopen_s _fopen_s_
#define WritePrivateProfileStringW _WritePrivateProfileString_

extern void UTF8ToUTF16(const char* utf8String, wchar_t* utf16String);
extern void UTF16ToUTF8(const wchar_t* utf16String, char* utf8String);

inline FILE* __cdecl _fopen_(const char* path, const char* mode) {
	wchar_t unicode_path[1024] = {0};
	wchar_t unicode_mode[32] = {0};
	UTF8ToUTF16(path, unicode_path);
	mbstowcs_s(NULL, unicode_mode, mode, _TRUNCATE);
	return _wfopen(unicode_path, unicode_mode);
}

inline errno_t __cdecl _fopen_s_(FILE** stream, const char* path, const char* mode) {
	wchar_t unicode_path[1024] = {0};
	wchar_t unicode_mode[32] = {0};
	UTF8ToUTF16(path, unicode_path);
	mbstowcs_s(NULL, unicode_mode, mode, _TRUNCATE);
	return _wfopen_s(stream, unicode_path, unicode_mode);
}

inline bool __stdcall _WritePrivateProfileString_(const wchar_t* lpAppName, const wchar_t* lpKeyName, const wchar_t* lpString, const wchar_t* lpFileName) {
	char AppName[128] = {0};
	char KeyName[128] = {0};
	char String[1024] = {0};
	char FileName[260] = {0};
	wcstombs_s(NULL, AppName, lpAppName, _TRUNCATE);
	wcstombs_s(NULL, KeyName, lpKeyName, _TRUNCATE);
	UTF16ToUTF8(lpString, String);
	UTF16ToUTF8(lpFileName, FileName);
	return WritePrivateProfileStringA(AppName, KeyName, String, FileName);
}

#endif
