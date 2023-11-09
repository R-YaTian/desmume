/*
	Copyright (C) 2009-2022 DeSmuME team
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

#include "OpenArchive.h"

#include <windows.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <map>
#include <vector>
#include <algorithm>
#include <mmsystem.h>

#include "driver.h"

#include "resource.h"
#include "main.h"
#include "utils/decrypt/header.h"
#include "utils/xstring.h"
#include "winutil.h"

static wchar_t Str_Tmp[1024];

LRESULT CALLBACK ArchiveFileChooser(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
static int s_archiveFileChooserResult = -1;

static HWND s_parentHWND = NULL;
void SetArchiveParentHWND(void* hwnd) { s_parentHWND = (HWND)hwnd; }
static HWND GetArchiveParentHWND() { return s_parentHWND ? s_parentHWND : MainWindow->getHWnd(); }

struct ArchiveFileChooserInfo
{
	ArchiveFileChooserInfo(ArchiveFile& theArchive, const char** ignoreExtensions, int& numIgnoreExtensions) : archive(theArchive)
	{
tryagain:
		int numItems = archive.GetNumItems();
		for(int i = 0; i < numItems; i++)
		{
			if(archive.GetItemSize(i))
			{
				//bullshit time. convert to system locale
				char name[MAX_PATH];
				WideCharToMultiByte(CP_ACP,0,archive.GetItemNameW(i),-1,name,ARRAY_SIZE(name), NULL, NULL);

				const char* ext = strrchr(name, '.');
				bool ok = true;
				if(ext++)
				{
					for(int j = 0; j < numIgnoreExtensions; j++)
					{
						const char* ext2 = ignoreExtensions[j];
						const char* wild = strchr(ext2, '*');
						if(!wild)
						{
							if(!_stricmp(ext, ext2))
							{
								ok = false;
								break;
							}
						}
						else // very limited (end only) wildcard support
						{
							if(!_strnicmp(ext, ext2, wild - ext2))
							{
								ok = false;
								break;
							}
						}
					}
				}
				if(ok)
				{
					ArchiveFileChooserInfo::FileInfo fi = { name, i };
					files.push_back(fi);
				}
			}
		}

		if(files.empty() && numIgnoreExtensions)
		{
			// try again without any exclusions if we excluded everything in the archive
			numIgnoreExtensions = 0;
			goto tryagain;
		}

		// strip away prefix paths that are common to all the files
		bool stripping = !files.empty();
		while(stripping)
		{
			const char* firstName = files[0].name.c_str();
			const char* slash = strchr(firstName, '\\');
			const char* slash2 = strchr(firstName, '/');
			slash = std::max(slash, slash2);
			if(!slash++)
				break;
			for(size_t i = 1; i < files.size(); i++)
				if(strncmp(firstName, files[i].name.c_str(), slash - firstName))
					stripping = false;
			if(stripping)
				for(size_t i = 0; i < files.size(); i++)
					files[i].name = files[i].name.substr(slash - firstName, files[i].name.length() - (slash - firstName));
		}

		// sort by filename
		std::sort(files.begin(), files.end(), FileInfo::Sort);
	}

	struct FileInfo
	{
		std::string name;
		int itemIndex;

		static bool Sort(const FileInfo& elem1, const FileInfo& elem2)
		{
			int comp = elem1.name.compare(elem2.name);
			return comp ? (comp < 0) : (elem1.itemIndex < elem2.itemIndex);
		}
	};

	ArchiveFile& archive;
	std::vector<FileInfo> files;
};

int ChooseItemFromArchive(ArchiveFile& archive, bool autoChooseIfOnly1, const char** ignoreExtensions, int numIgnoreExtensions)
{
	int prevNumIgnoreExtensions = numIgnoreExtensions;

	// prepare a list of files to choose from the archive
	ArchiveFileChooserInfo info (archive, ignoreExtensions, numIgnoreExtensions);

	// based on our list, decide which item in the archive to choose

	// check if there's nothing
	if(info.files.size() < 1)
	{
		MessageBox(GetArchiveParentHWND(), STRA(ID_BOX_MSG18).c_str(), STRA(ID_BOX_MSG19).c_str(), MB_OK | MB_ICONWARNING);
		return -1;
	}

	// if there's only 1 item, choose it
	if(info.files.size() == 1 && autoChooseIfOnly1 && numIgnoreExtensions == prevNumIgnoreExtensions)
		return info.files[0].itemIndex;

	// bring up a dialog to choose the index if there's more than 1
	DialogBoxParam(hAppInst, MAKEINTRESOURCE(IDD_ARCHIVEFILECHOOSER), GetArchiveParentHWND(), (DLGPROC) ArchiveFileChooser,(LPARAM) &info);
	return s_archiveFileChooserResult;
}

#define DEFAULT_EXTENSION ".tmp"
#define DEFAULT_CATEGORY "desmume"

static struct TempFiles
{
	struct TemporaryFile
	{
		TemporaryFile(const char* cat, const char* ext)
		{
			if(!ext || !*ext) ext = DEFAULT_EXTENSION;
			if(!cat || !*cat) cat = DEFAULT_CATEGORY;
			category = cat;

			wchar_t tempPath[1024];
			GetTempPathW(1024, tempPath);
			//GetTempFileName(tempPath, cat, 0, filename, ext); // alas

			wchar_t* const fname = tempPath + wcslen(tempPath);
			unsigned short start = (unsigned short)(timeGetTime() & 0xFFFF);
			unsigned short n = start + 1;
			while(n != start)
			{
				_snwprintf(fname, 1024 - (fname - tempPath), L"%hs%04X%hs", cat, n, ext);
				FILE* file = _wfopen(tempPath, L"wb");
				if(file)
				{
					// mark the temporary file as read-only and (whatever this does) temporary
					DWORD attributes = GetFileAttributesW(tempPath);
					attributes |= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_TEMPORARY;
					SetFileAttributesW(tempPath, attributes);

					fclose(file);

					// add it to our registry of files that need to be deleted, in case we fail to terminate properly
					TempFiles::AddEntryToGarbageRegistry(tempPath);

					break;
				}
				n++;
			}
			strcpy(filename, wcstombs((std::wstring)tempPath).c_str());
		}
		TemporaryFile(const TemporaryFile& copy)
		{
			strcpy(filename, copy.filename);
			category = copy.category;
		}
		TemporaryFile()
		{
			filename[0] = 0;
			category.resize(0);
		}
		bool Delete(bool returnFalseOnRegistryRemovalFailure=false)
		{
			if(!*filename)
				return true; // guess it already didn't exist

			wchar_t unicode_fname[1024];
			UTF8ToUTF16(filename, unicode_fname);

			// remove read-only attribute so Windows will let us delete it
			// (our temporary files are read-only to discourage other apps from tampering)
			DWORD attributes = GetFileAttributesW(unicode_fname);
			if(attributes & FILE_ATTRIBUTE_READONLY)
				SetFileAttributesW(unicode_fname, attributes & ~FILE_ATTRIBUTE_READONLY);

			char ansi_buf[1024];
			UTF8ToANSI(filename, ansi_buf); // I think _unlink func only support ANSI?
			if(_unlink(ansi_buf) == 0 || errno != EACCES)
			{
				// remove it from our registry of files that need to be deleted, to reduce accumulation
				bool removed = TempFiles::RemoveEntryFromGarbageRegistry(unicode_fname);

				*filename = '\0';
				return removed || !returnFalseOnRegistryRemovalFailure; // successfully deleted or already didn't exist, return true unless registry removal failure notification was requested and that failed
			}

			// restore read-only if we couldn't delete it (not sure if this ever succeeds or matters though)
			if(attributes & FILE_ATTRIBUTE_READONLY)
				SetFileAttributesW(unicode_fname, attributes);

			return false; // failed to delete read-only or in-use file
		}
		char filename[1024];
		std::string category;
	};

	std::vector<TemporaryFile> tempFiles;

	const char* GetFile(const char* category, const char* extension)
	{
		tempFiles.push_back(TemporaryFile(category, extension));
		return tempFiles.back().filename;
	}

	void ReleaseFile(const char* filename)
	{
		for(int i = (int)tempFiles.size()-1; i >= 0; i--)
		{
			if(!strcmp(filename, tempFiles[i].filename))
			{
				if(tempFiles[i].Delete())
					tempFiles.erase(tempFiles.begin() + i);
			}
		}
	}

	void ReleaseCategory(const char* cat, const char* exceptionFilename)
	{
		for(int i = (int)tempFiles.size()-1; i >= 0; i--)
		{
			if(!strcmp(cat, tempFiles[i].category.c_str()) &&
				(!exceptionFilename ||
				  strcmp(exceptionFilename, tempFiles[i].filename)))
			{
				if(tempFiles[i].Delete())
					tempFiles.erase(tempFiles.begin() + i);
			}
		}
	}

	// delete all temporary files on shutdown
	~TempFiles()
	{
		for(size_t i = 0; i < tempFiles.size(); i++)
		{
			tempFiles[i].Delete();
		}

		TempFiles::CleanOutGarbageRegistry();
	}

	// run this on startup to delete any files that we failed to delete last time
	// in case we crashed or were forcefully terminated
	TempFiles()
	{
		TempFiles::CleanOutGarbageRegistry();
	}

	static void AddEntryToGarbageRegistry(const wchar_t* filename)
	{
		wchar_t gbgFile[1024];
		GetTempPathW(1024, gbgFile);
		wcscat(gbgFile, L"DesmumeTempFileRecords");
		wchar_t key[64];
		int i = 0;
		while(true)
		{
			swprintf(key, L"File%d", i);
			GetPrivateProfileStringW(L"Files", key, L"", Str_Tmp, 1024, gbgFile);
			if(!*Str_Tmp)
				break;
			i++;
		}
		WritePrivateProfileStringW(L"Files", key, filename, gbgFile);
	}
	static bool RemoveEntryFromGarbageRegistry(const wchar_t* filename)
	{
		wchar_t gbgFile[1024];
		GetTempPathW(1024, gbgFile);
		wcscat(gbgFile, L"DesmumeTempFileRecords");
		wchar_t key[64];
		int i = 0;
		int deleteSlot = -1;
		while(true)
		{
			swprintf(key, L"File%d", i);
			GetPrivateProfileStringW(L"Files", key, L"", Str_Tmp, 1024, gbgFile);
			if(!*Str_Tmp)
				break;
			if(!wcscmp(Str_Tmp, filename))
				deleteSlot = i;
			i++;
		}
		--i;
		if(i >= 0 && deleteSlot >= 0)
		{
			if(i != deleteSlot)
			{
				swprintf(key, L"File%d", i);
				GetPrivateProfileStringW(L"Files", key, L"", Str_Tmp, 1024, gbgFile);
				swprintf(key, L"File%d", deleteSlot);
				WritePrivateProfileStringW(L"Files", key, Str_Tmp, gbgFile);
			}
			swprintf(key, L"File%d", i);
			if(0 == WritePrivateProfileStringW(L"Files", key, NULL, gbgFile))
				return false;
		}
		if (i <= 0 && deleteSlot == 0)
		{
			char ansi_buf[1024];
			int nRetLen = WideCharToMultiByte(CP_ACP, 0, gbgFile, -1, NULL, 0, NULL, NULL);
			WideCharToMultiByte(CP_ACP, 0, gbgFile, -1, ansi_buf, nRetLen, NULL, NULL);
			_unlink(ansi_buf);
		}
		return true;
	}

private:
	static void CleanOutGarbageRegistry()
	{
		wchar_t gbgFile[1024 + 64];
		GetTempPathW(1024, gbgFile);
		wcscat(gbgFile, L"DesmumeTempFileRecords");

		wchar_t key[64];
		int i = 0;
		while(true)
		{
			swprintf(key, L"File%d", i);
			GetPrivateProfileStringW(L"Files", key, L"", Str_Tmp, 1024, gbgFile);
			if(!*Str_Tmp)
				break;
			TemporaryFile temp;
			strcpy(temp.filename, wcstombs((std::wstring) Str_Tmp).c_str());
			if(!temp.Delete(true))
				i++;
		}
	}

} s_tempFiles;


const char* GetTempFile(const char* category, const char* extension)
{
	return s_tempFiles.GetFile(category, extension);
}
void ReleaseTempFile(const char* filename)
{
	s_tempFiles.ReleaseFile(filename);
}
void ReleaseTempFileCategory(const char* cat, const char* exceptionFilename)
{
	if(!cat || !*cat) cat = DEFAULT_CATEGORY;
	s_tempFiles.ReleaseCategory(cat, exceptionFilename);
}


// example input Name:          "C:\games.zip"
// example output LogicalName:  "C:\games.zip|Sonic.nds"
// example output PhysicalName: "C:\Documents and Settings\User\Local Settings\Temp\Desmume\dec3.tmp"
// assumes arguments are character buffers with 1024 bytes each
bool ObtainFile(const char* Name, char *const & LogicalName, char *const & PhysicalName, const char* category, const char** ignoreExtensions, int numIgnoreExtensions)
{
	char ArchivePaths [1024];
	strcpy(LogicalName, Name);
	strcpy(PhysicalName, Name);
	strcpy(ArchivePaths, Name);
	char* bar = strchr(ArchivePaths, '|');
	if(bar)
	{
		PhysicalName[bar - ArchivePaths] = 0; // doesn't belong in the physical name
		LogicalName[bar - ArchivePaths] = 0; // we'll reconstruct the logical name as we go
		*bar++ = 0; // bar becomes the next logical archive path component
	}

	while(true)
	{
		//before sending to FEX, see if we're known to be an NDS file
		//(this will stop games beginning with the name ZOO from being mis-recognized as a zoo file)
		FILE* inf = _wfopen(mbstowcs((std::string)PhysicalName).c_str(),L"rb");
		if(!inf) return false;
		u8 bytes512[512];
		bool got512 = fread(bytes512,1,512,inf)==512;
		fclose(inf);
		if(got512 && DetectAnyRom(bytes512))
			return true;

		ArchiveFile archive (PhysicalName);
		if(!archive.IsCompressed())
		{
			return archive.GetNumItems() > 0;
		}
		else
		{
			int item = -1;
			bool forceManual = false;
			if(bar && *bar) // try following the in-archive part of the logical path
			{
				char* bar2 = strchr(bar, '|');
				if(bar2) *bar2++ = 0;
				int numItems = archive.GetNumItems();
				for(int i = 0; i < numItems; i++)
				{
					if(archive.GetItemSize(i))
					{
						const char* itemName = archive.GetItemName(i);
						if(!_stricmp(itemName, bar))
						{
							item = i; // match found, now we'll auto-follow the path
							break;
						}
					}
				}
				if(item < 0)
				{
					forceManual = true; // we don't want it choosing something else without user permission
					bar = NULL; // remaining archive path is invalid
				}
				else
					bar = bar2; // advance to next archive path part
			}
			if(item < 0)
				item = ChooseItemFromArchive(archive, !forceManual, ignoreExtensions, numIgnoreExtensions);

			const char* itemName = archive.GetItemName(item);
			const char* TempFileName = s_tempFiles.GetFile(category, strrchr(itemName, '.'));
			if(!archive.ExtractItem(item, TempFileName))
				s_tempFiles.ReleaseFile(TempFileName);
			s_tempFiles.ReleaseFile(PhysicalName);
			strcpy(PhysicalName, TempFileName);

			const wchar_t* itemNameW = archive.GetItemNameW(item);

			//convert the itemname to utf8
			char itemname_utf8[MAX_PATH*4];
			WideCharToMultiByte(CP_UTF8,0,itemNameW,-1,itemname_utf8,ARRAY_SIZE(itemname_utf8),NULL,NULL);

			//strcat(LogicalName,itemname_utf8);
			_snprintf(LogicalName + strlen(LogicalName), 1024 - (strlen(LogicalName)+1), "|%s", itemname_utf8);
		}
	}
}


struct ControlLayoutInfo
{
	int controlID;
	
	enum LayoutType // what to do when the containing window resizes
	{
		NONE, // leave the control where it was
		RESIZE_END, // resize the control
		MOVE_START, // move the control
	};
	LayoutType horizontalLayout;
	LayoutType verticalLayout;
};

struct ControlLayoutState
{
	int x,y,width,height;
	bool valid;
	ControlLayoutState() : valid(false) {}
};

static ControlLayoutInfo controlLayoutInfos [] = {
	{IDC_LIST1, ControlLayoutInfo::RESIZE_END, ControlLayoutInfo::RESIZE_END},
	{IDOK,      ControlLayoutInfo::MOVE_START, ControlLayoutInfo::MOVE_START},
	{IDCANCEL, ControlLayoutInfo::MOVE_START, ControlLayoutInfo::MOVE_START},
};

static const int numControlLayoutInfos = sizeof(controlLayoutInfos)/sizeof(*controlLayoutInfos);
static ControlLayoutState s_layoutState [numControlLayoutInfos];
static int s_windowWidth = 182, s_windowHeight = 113;

LRESULT CALLBACK ArchiveFileChooser(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	RECT r, r2;
	int dx1, dy1, dx2, dy2;
	static std::map<int,int> s_listToItemsMap;

	switch(uMsg)
	{
		case WM_INITDIALOG:
		{
			GetWindowRect(MainWindow->getHWnd(), &r);
			dx1 = (r.right - r.left) / 2;
			dy1 = (r.bottom - r.top) / 2;

			GetWindowRect(hDlg, &r2);
			dx2 = (r2.right - r2.left) / 2;
			dy2 = (r2.bottom - r2.top) / 2;

			//SetWindowPos(hDlg, NULL, max(0, r.left + (dx1 - dx2)), max(0, r.top + (dy1 - dy2)), NULL, NULL, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
			SetWindowPos(hDlg, NULL, r.left, r.top, NULL, NULL, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

			ArchiveFileChooserInfo& info = *(ArchiveFileChooserInfo*) lParam;
			std::vector<ArchiveFileChooserInfo::FileInfo>& files = info.files;
			ArchiveFile& archive = info.archive;

			std::string title = STRA(ID_DLG_STR35);
			title += archive.GetArchiveTypeName();
			title += STRA(ID_DLG_STR36);
			SetWindowText(hDlg, title.c_str());

			// populate list
			for(size_t i = 0; i < files.size(); i++)
			{
				int listIndex = SendDlgItemMessage(hDlg, IDC_LIST1, LB_ADDSTRING, (WPARAM) 0, (LONG) (LPTSTR) files[i].name.c_str());
				s_listToItemsMap[listIndex] = files[i].itemIndex;
			}

			SendDlgItemMessage(hDlg, IDC_LIST1, LB_SETCURSEL, (WPARAM) 0, (LPARAM) 0);

			{
				RECT r3;
				GetClientRect(hDlg, &r3);
				s_windowWidth = r3.right - r3.left;
				s_windowHeight = r3.bottom - r3.top;
			}

			return true;
		}	break;

		case WM_SIZING:
		{
			// enforce a minimum window size

			LPRECT r = (LPRECT) lParam;
			int minimumWidth = 281;
			int minimumHeight = 117;
			if(r->right - r->left < minimumWidth)
				if(wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
					r->left = r->right - minimumWidth;
				else
					r->right = r->left + minimumWidth;
			if(r->bottom - r->top < minimumHeight)
				if(wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
					r->top = r->bottom - minimumHeight;
				else
					r->bottom = r->top + minimumHeight;
			return TRUE;
		}

		case WM_SIZE:
		{
			// resize or move controls in the window as necessary when the window is resized

			int prevDlgWidth = s_windowWidth;
			int prevDlgHeight = s_windowHeight;

			int dlgWidth = LOWORD(lParam);
			int dlgHeight = HIWORD(lParam);

			int deltaWidth = dlgWidth - prevDlgWidth;
			int deltaHeight = dlgHeight - prevDlgHeight;

			for(int i = 0; i < numControlLayoutInfos; i++)
			{
				ControlLayoutInfo layoutInfo = controlLayoutInfos[i];
				ControlLayoutState& layoutState = s_layoutState[i];

				HWND hCtrl = GetDlgItem(hDlg,layoutInfo.controlID);

				int x,y,width,height;
				if(layoutState.valid)
				{
					x = layoutState.x;
					y = layoutState.y;
					width = layoutState.width;
					height = layoutState.height;
				}
				else
				{
					RECT r;
					GetWindowRect(hCtrl, &r);
					POINT p = {r.left, r.top};
					ScreenToClient(hDlg, &p);
					x = p.x;
					y = p.y;
					width = r.right - r.left;
					height = r.bottom - r.top;
				}

				switch(layoutInfo.horizontalLayout)
				{
					case ControlLayoutInfo::RESIZE_END: width += deltaWidth; break;
					case ControlLayoutInfo::MOVE_START: x += deltaWidth; break;
					default: break;
				}
				switch(layoutInfo.verticalLayout)
				{
					case ControlLayoutInfo::RESIZE_END: height += deltaHeight; break;
					case ControlLayoutInfo::MOVE_START: y += deltaHeight; break;
					default: break;
				}

				SetWindowPos(hCtrl, 0, x,y, width,height, 0);

				layoutState.x = x;
				layoutState.y = y;
				layoutState.width = width;
				layoutState.height = height;
				layoutState.valid = true;
			}

			s_windowWidth = dlgWidth;
			s_windowHeight = dlgHeight;

			RedrawWindow(hDlg, NULL, NULL, RDW_INVALIDATE);
		}
		break;

		case WM_COMMAND:
			switch(LOWORD(wParam))
			{
				case IDC_LIST1:
					if(HIWORD(wParam) == LBN_DBLCLK)
						SendMessage(hDlg, WM_COMMAND, IDOK, 0);
					return TRUE;

				case IDOK:
				{	
					int listIndex = SendDlgItemMessage(hDlg, IDC_LIST1, LB_GETCURSEL, (WPARAM) 0, (LPARAM) 0);
					s_archiveFileChooserResult = s_listToItemsMap[listIndex];
					s_listToItemsMap.clear();
					EndDialog(hDlg, false);
				}	return TRUE;

				case IDCANCEL:
					s_archiveFileChooserResult = -1;
					s_listToItemsMap.clear();
					EndDialog(hDlg, false);
					return TRUE;
			}

		case WM_CLOSE:
			s_archiveFileChooserResult = -1;
			s_listToItemsMap.clear();
			EndDialog(hDlg, false);
			return TRUE;
	}

	return false;
}
