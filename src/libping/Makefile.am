##
## lib/Makefile.am
##
## Copyright (C) 2000 Jeffrey Fulmer <jdfulmer@armstrong.com>
## This file is part of libping
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
##

AUTOMAKE_OPTIONS = foreign no-dependencies

noinst_LTLIBRARIES    = libping.la 
 
libping_la_LDFLAGS = -version-info 1:0:0

libping_la_SOURCES =  \
echo.c     echo.h     \
http.c     http.h     \
memory.c   memory.h   \
ping.c     ping.h     \
private.c  private.h  \
pop3.c     pop3.h     \
sock.c     sock.h     \
smtp.c     smtp.h     \
url.c      url.h      \
util.c     util.h     \
linux.h    setup.h

