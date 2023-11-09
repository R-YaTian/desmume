/*
	Copyright (C) 2007 Normmatt
	Copyright (C) 2007-2022 DeSmuME team
	Copyright (C) 2023 R-YaTian

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "FirmConfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NDSSystem.h"
#include "firmware.h"

#include "resource.h"
#include "CWindow.h"
#include "winutil.h"

static wchar_t nickname_buffer[11];
static wchar_t message_buffer[27];

const char firmLang[6][16]   = {"Japanese","English","French","German","Italian","Spanish"};
const char firmColor[16][16] = {"Gray","Brown","Red","Pink","Orange","Yellow","Lime Green",
                                "Green","Dark Green","Sea Green","Turquoise","Blue",
                                "Dark Blue","Dark Purple","Violet","Magenta"};
const char firmDay[31][16]   = {"1","2","3","4","5","6","7","8","9","10","11","12","13","14",
                                "15","16","17","18","19","20","21","22","23","24","25","26",
                                "27","28","29","30","31"};
const char firmMonth[12][16] = {"January","Feburary","March","April","May","June","July",
                                "August","September","October","November","December"};

static void WriteFirmConfig(const FirmwareConfig &fwConfig)
{
	char temp_str[79];
	wchar_t unicode_str[27];
	int i;

    WritePrivateProfileInt("Firmware","favColor", fwConfig.favoriteColor,IniName);
    WritePrivateProfileInt("Firmware","bMonth", fwConfig.birthdayMonth,IniName);
    WritePrivateProfileInt("Firmware","bDay",fwConfig.birthdayDay,IniName);
    WritePrivateProfileInt("Firmware","Language",fwConfig.language,IniName);

	for (i = 0; i < fwConfig.nicknameLength; i++) {
		unicode_str[i] = fwConfig.nickname[i];
	}
	unicode_str[i] = '\0';
	UTF16ToUTF8(unicode_str, temp_str);
    WritePrivateProfileString("Firmware", "nickName", temp_str, IniName);

	for (i = 0; i < fwConfig.messageLength; i++) {
		unicode_str[i] = fwConfig.message[i];
	}
	unicode_str[i] = '\0';
	UTF16ToUTF8(unicode_str, temp_str);
	WritePrivateProfileString("Firmware","Message", temp_str, IniName);

	NDS_GetFirmwareMACAddressAsStr(fwConfig, temp_str);
	WritePrivateProfileString("Firmware", "macAddress", temp_str, IniName);
}

BOOL CALLBACK FirmConfig_Proc(HWND dialog,UINT komunikat,WPARAM wparam,LPARAM lparam)
{
	FirmwareConfig &fwConfig = CommonSettings.fwConfig;
	int i;
	wchar_t unicode_str[27]{};
	char mac_buffer[13];

	switch(komunikat)
	{
		case WM_INITDIALOG:
			for(i=0;i<6;i++) SendDlgItemMessage(dialog,IDC_COMBO4,CB_ADDSTRING,0,(LPARAM)&firmLang[i]);
			for(i=0;i<12;i++) SendDlgItemMessage(dialog,IDC_COMBO2,CB_ADDSTRING,0,(LPARAM)&firmMonth[i]);
			for(i=0;i<16;i++) SendDlgItemMessage(dialog,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)&firmColor[i]);
			for(i=0;i<31;i++) SendDlgItemMessage(dialog,IDC_COMBO3,CB_ADDSTRING,0,(LPARAM)&firmDay[i]);
			SendDlgItemMessage(dialog,IDC_COMBO1,CB_SETCURSEL,fwConfig.favoriteColor,0);
			SendDlgItemMessage(dialog,IDC_COMBO2,CB_SETCURSEL,fwConfig.birthdayMonth-1,0);
			SendDlgItemMessage(dialog,IDC_COMBO3,CB_SETCURSEL,fwConfig.birthdayDay-1,0);
			SendDlgItemMessage(dialog,IDC_COMBO4,CB_SETCURSEL,fwConfig.language,0);
			SendDlgItemMessageW(dialog,IDC_EDIT1,EM_SETLIMITTEXT,10,0);
			SendDlgItemMessageW(dialog,IDC_EDIT2,EM_SETLIMITTEXT,26,0);
			SendDlgItemMessageW(dialog,IDC_EDIT3,EM_SETLIMITTEXT,12,0);
			SendDlgItemMessageW(dialog,IDC_EDIT1,EM_SETSEL,0,10);
			SendDlgItemMessageW(dialog,IDC_EDIT2,EM_SETSEL,0,26);
			SendDlgItemMessageW(dialog,IDC_EDIT3,EM_SETSEL,0,12);

			for (i = 0; i < fwConfig.nicknameLength; i++) {
				nickname_buffer[i] = fwConfig.nickname[i];
			}
			nickname_buffer[i] = '\0';
			SendDlgItemMessageW(dialog,IDC_EDIT1,WM_SETTEXT,0,(LPARAM)nickname_buffer);

			for (i = 0; i < fwConfig.messageLength; i++) {
				message_buffer[i] = fwConfig.message[i];
			}
			message_buffer[i] = '\0';
			SendDlgItemMessageW(dialog,IDC_EDIT2,WM_SETTEXT,0,(LPARAM)message_buffer);

			NDS_GetFirmwareMACAddressAsStr(fwConfig, mac_buffer);
			SendDlgItemMessage(dialog, IDC_EDIT3, WM_SETTEXT, 0, (LPARAM)mac_buffer);

			break;

		case WM_COMMAND:
			if((HIWORD(wparam) == BN_CLICKED) && (((int)LOWORD(wparam)) == IDOK))
			{
				int char_index;
				LRESULT res;
				fwConfig.favoriteColor = SendDlgItemMessage(dialog,IDC_COMBO1,CB_GETCURSEL,0,0);
				fwConfig.birthdayMonth = 1 + SendDlgItemMessage(dialog,IDC_COMBO2,CB_GETCURSEL,0,0);
				fwConfig.birthdayDay = 1 + SendDlgItemMessage(dialog,IDC_COMBO3,CB_GETCURSEL,0,0);
				fwConfig.language = SendDlgItemMessage(dialog,IDC_COMBO4,CB_GETCURSEL,0,0);

				*(DWORD *) unicode_str = 10;
				res = SendDlgItemMessageW(dialog,IDC_EDIT1,EM_GETLINE,0,(LPARAM)unicode_str);

				if (res > 0) {
					unicode_str[res] = '\0';
				} else {
					wcscpy(unicode_str, L"yopyop");
				}
				fwConfig.nicknameLength = wcslen(unicode_str);
				for (char_index = 0; char_index < fwConfig.nicknameLength; char_index++) {
					fwConfig.nickname[char_index] = unicode_str[char_index];
				}

				*(DWORD *) unicode_str = 26;
				res = SendDlgItemMessageW(dialog,IDC_EDIT2,EM_GETLINE,0,(LPARAM)unicode_str);
				if (res > 0) {
					unicode_str[res] = '\0';
					fwConfig.messageLength = wcslen(unicode_str);
				} else {
					fwConfig.messageLength = 0;
				}
				for (char_index = 0; char_index < fwConfig.messageLength; char_index++) {
					fwConfig.message[char_index] = unicode_str[char_index];
				}

				*(WORD*) mac_buffer = 13;
				res = SendDlgItemMessage(dialog, IDC_EDIT3, EM_GETLINE, 0, (LPARAM)mac_buffer);
				NDS_SetFirmwareMACAddressFromStr(fwConfig, mac_buffer);

				WriteFirmConfig(fwConfig);
				EndDialog(dialog,0);

				return 1;
			} else if((HIWORD(wparam) == BN_CLICKED) && (((int)LOWORD(wparam)) == IDCANCEL)) {
				EndDialog(dialog, 0);
				return 0;
            }
		    break;
	}
	return 0;
}
