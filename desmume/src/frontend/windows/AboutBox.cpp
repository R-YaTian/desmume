/*
	Copyright (C) 2008 shash
	Copyright (C) 2008-2016 DeSmuME team

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

#include "AboutBox.h"

#include "version.h"

#include "resource.h"

#define ABOUT_TIMER_ID 110222
const wchar_t *team[] = {
	L"Original author\1",
	L"yopyop",
	L"",
	L"Current Team\1",
	L"Guillaume Duhamel",
	L"Normmatt",
	L"zeromus",
	L"rogerman",
	L"",
	L"Contributors\1",
	L"Bernat Muñoz (shash)",
	L"Allustar",
	L"amponzi",
	L"Anthony Molinaro",
	L"ape",
	L"Damien Nozay (damdoum)",
	L"delfare",
	L"Romain Vallet",
	L"snkmad",
	L"Theo Berkau",
	L"thoduv",
	L"Tim Seidel (Mighty Max)",
	L"Pascal Giard (evilynux)",
	L"Ben Jaques (masscat)",
	L"Jeff Bland",
	L"matusz",
	L"nitsuja",
	L"gocha",
	L"pa__",
	L"adelikat",
	L"hi-coder",
	L"WinterMute",
	L"pengvado",
	L"dormito",
	L"ldesnogue",
	L"mtheall",
	L"thelemonman",
	L"nash679",
	L"pokefan999",
	L"dottorleo",
	L"yki",
	L"Luigi__",
	L"CrazyMax",
	L"Riccardo Magliocchetti",
	L"CyberWarriorX",
	L"mic"
};

static HWND		gList = NULL;
static RECT		gRc = {0};
static s32		gPosY = 0;
const u32		size = ARRAY_SIZE(team);

BOOL CALLBACK ListProc(HWND Dlg, UINT msg,WPARAM wparam,LPARAM lparam)
{

	switch (msg)
	{
		case WM_PAINT:
			{
				PAINTSTRUCT ps = {0};
				
				HDC hDC = BeginPaint(Dlg, &ps);
				HDC hdcMem = CreateCompatibleDC(hDC);
				HBITMAP hbmMem = CreateCompatibleBitmap(hDC, gRc.right, gRc.bottom);
				HANDLE hOld   = SelectObject(hdcMem, hbmMem);
				SetBkMode(hdcMem, TRANSPARENT);
				SetTextAlign(hdcMem, TA_CENTER);
				u32 x = gRc.right / 2;
				FillRect(hdcMem, &gRc, (HBRUSH)COLOR_WINDOW);
				SetTextColor(hdcMem, RGB(255, 0, 0));
				for (u32 i = 0; i < size; i++)
				{
					s32 pos = gPosY+(i*20);
					if (pos > gRc.bottom) break;
					if (team[i][wcslen(team[i])-1] == 1)
					{
						SetTextColor(hdcMem, RGB(255, 0, 0));
						ExtTextOutW(hdcMem, x, pos, ETO_CLIPPED, &gRc, team[i], wcslen(team[i])-1, NULL);
					}
					else
					{
						SetTextColor(hdcMem, RGB(0, 0, 0));
						ExtTextOutW(hdcMem, x, pos, ETO_CLIPPED, &gRc, team[i], wcslen(team[i]), NULL);
					}
					if ((i == size-1) && (pos < (s32)(gRc.top - 20))) gPosY = gRc.bottom;
				}

				BitBlt(hDC, 0, 0, gRc.right, gRc.bottom, hdcMem, 0, 0, SRCCOPY);
				SelectObject(hdcMem, hOld);
				DeleteObject(hbmMem);
				DeleteDC(hdcMem);
				EndPaint(Dlg, &ps);
			}
			return TRUE;
	}
	return FALSE;
}

BOOL CALLBACK AboutBox_Proc (HWND dialog, UINT message,WPARAM wparam,LPARAM lparam)
{
	switch(message)
	{
		case WM_INITDIALOG: 
		{
			char buf[256] = {0};
			memset(&buf[0], 0, sizeof(buf));
			sprintf(buf, DESMUME_NAME "%s", EMU_DESMUME_VERSION_STRING());
			SetDlgItemText(dialog, IDC_TXT_VERSION, buf);
			char buf_tmp[16] = {0};
			memset(&buf_tmp[0], 0, sizeof(buf_tmp));
			GetDlgItemText(dialog, IDC_TXT_COMPILED, buf_tmp, 16);
			sprintf(buf, "%s %s - %s %s", buf_tmp, __DATE__, __TIME__, EMU_DESMUME_COMPILER_DETAIL());
			SetDlgItemText(dialog, IDC_TXT_COMPILED, buf);

			gList = GetDlgItem(dialog, IDC_AUTHORS_LIST);
			SetWindowLongPtr(gList, GWLP_WNDPROC, (LONG_PTR)ListProc);
			GetClientRect(gList, &gRc);
			gPosY = gRc.bottom;

			SetTimer(dialog, ABOUT_TIMER_ID, 20, (TIMERPROC) NULL);
			break;
		}
	
		case WM_COMMAND:
		{
			if((HIWORD(wparam) == BN_CLICKED)&&(((int)LOWORD(wparam)) == IDC_FERMER))
			{
				KillTimer(dialog, ABOUT_TIMER_ID);
				EndDialog(dialog,0);
				return 1;
			}
			break;
		}

		case WM_TIMER:
		{
			gPosY--;
			InvalidateRect(gList, &gRc, false);
			break;
		}
	}
	return 0;
}
