/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "socket_header.h"
#include <string>
#ifndef _WIN32
#include <memory.h>
#endif

bool LookupBlocking(std::string pServer, in_addr *dest)
{
	const char* host=pServer.c_str();
    unsigned int addr = inet_addr(host);
    if (addr != INADDR_NONE)
	{
        dest->s_addr = addr;
    }
	else
	{
		addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_socktype = SOCK_STREAM;

		addrinfo* hp;
		if(getaddrinfo(host, NULL, &hints, &hp)==0 && hp!=NULL)
		{
			if(hp->ai_addrlen>=sizeof(sockaddr_in))
			{
				memcpy(dest, &reinterpret_cast<sockaddr_in*>(hp->ai_addr)->sin_addr, sizeof(in_addr));
				freeaddrinfo(hp);
				return true;
			}
			else
			{
				freeaddrinfo(hp);
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	return true;
}




