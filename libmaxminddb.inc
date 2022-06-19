# -*- mode: mak; indent-tabs-mode: t; tab-width: 8 -*-
# $Id: libmaxminddb.inc,v 1.2 2022/06/05 11:08:39 cvsuser Exp $
# libmaxmindb support
#
#
# Copyright (c) 2022, Adam Young.
# All rights reserved.
#
# The applications are free software: you can redistribute it
# and/or modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, version 3.
#
# Redistributions of source code must retain the above copyright
# notice, and must be distributed with the license document above.
#
# Redistributions in binary form must reproduce the above copyright
# notice, and must include the license document above in
# the documentation and/or other materials provided with the
# distribution.
#
# The applications are distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# ==end==
#

INCMAXMINDDB=
DEFMAXMINDDB=
LIBMAXMINDDB=

################################################################################
# MingGW

ifeq (mingw,$(findstring mingw,"@TOOLCHAIN@"))


################################################################################
# Visual studio

else ifeq (vs,$(findstring vs,"@TOOLCHAIN@"))

ifeq ("$(MAXMINDDB)","")
MAXMINDDB	=../vcpkg_installed/x86-windows
ifeq ("$(wildcard $(MAXMINDDB)/include/maxminddb/*)","")
MAXMINDDB	=../../vcpkg/installed/x86-windows
ifeq ("$(wildcard $(MAXMINDDB)/include/maxminddb/*)","")
MAXMINDDB	=
endif
endif
endif

ifneq ("$(MAXMINDDB)","")
INCMAXMINDDB=	-I$(MAXMINDDB)/include
DEFMAXMINDDB=	-DHAVE_LIBMAXMINDDB
ifeq ("$(BUILD_TYPE)","release")
LIBMAXMINDDB=	$(MAXMINDDB)/lib/libmaxminddb.lib
else
LIBMAXMINDDB=	$(MAXMINDDB)/debug/lib/libmaxminddbd.lib
endif
endif

endif

#
