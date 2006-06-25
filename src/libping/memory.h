/**
 * MEMORY header
 *
 * Copyright (C) 2001 Jeffrey Fulmer <jdfulmer@armstrong.com>
 * This file is part of LIBPING
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdlib.h>

/**
 * a controlled realloc
 */
void * xrealloc( void *, size_t );

/**
 * a controlled malloc
 */
void * xmalloc ( size_t );

/**
 * a controlled calloc
 */
void * xcalloc ( size_t, size_t );  


#endif /* MEMORY_H */
