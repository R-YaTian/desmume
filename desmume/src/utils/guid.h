/*
	Copyright (C) 2008-2009 DeSmuME team

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

#ifndef _guid_h_
#define _guid_h_

#include <string>
#include <cstdio>
#include "../types.h"

template<typename T, int N>
struct ValueArray
{
	T data[N];
	T &operator[](int index) { return data[index]; }
	static const int size = N;
	bool operator!=(ValueArray<T,N> &other) { return !operator==(other); }
	bool operator==(ValueArray<T,N> &other)
	{
		for(int i=0;i<size;i++)
			if(data[i] != other[i])
				return false;
		return true;
	}
};

struct Desmume_Guid : public ValueArray<u8,16>
{
	void newGuid();
	std::string toString();
	static Desmume_Guid fromString(std::string str);
	static u8 hexToByte(char** ptrptr);
	void scan(std::string& str);
};


#endif
