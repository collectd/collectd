/*
 * Copyright (c) 2015 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "plugin.h"
#include "wmi.h"

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <wbemidl.h>
#include <windows.h>
#include <wtypes.h>

static enum VARENUM variant_unsigned_integer_types[] =
{
    VT_UI1, VT_UI2, VT_UI4, VT_UI8, VT_UINT
};

static int variant_is_unsigned_integer (VARIANT *v)
{
    int i;
    for (i = 0; i < COUNTOF (variant_unsigned_integer_types); i++)
        if (v->vt == variant_unsigned_integer_types[i])
            return (1);
    return (0);
}

static enum VARENUM variant_signed_integer_types[] =
{
    VT_I1, VT_I2, VT_I4, VT_I8, VT_INT
};

static int variant_is_signed_integer(VARIANT *v)
{
    int i;
    for (i = 0; i < COUNTOF (variant_signed_integer_types); i++)
        if (v->vt == variant_signed_integer_types[i])
            return (1);
    return (0);
}

static uint64_t variant_get_unsigned (VARIANT *v)
{
    switch (v->vt)
    {
    case VT_UI1: return (v->bVal);
    case VT_UI2: return (v->uiVal);
    case VT_UI4: return (v->ulVal);
    case VT_UI8: return (v->ullVal);
    case VT_UINT: return (v->uintVal);
    default:
        ERROR ("wmi internal error: conversion problem at line %d", __LINE__);
        return (0);
    }
}

static int64_t variant_get_signed_integer (VARIANT *v)
{
    switch (v->vt)
    {
    case VT_I1: return (v->bVal);
    case VT_I2: return (v->iVal);
    case VT_I4: return (v->lVal);
    case VT_I8: return (v->llVal);
    case VT_INT: return (v->intVal);
    default:
        ERROR ("wmi internal error: conversion problem at line %d", __LINE__);
        return (0);
    }
}

static int variant_is_real (VARIANT *v)
{
    return (v->vt == VT_R4 || v->vt == VT_R8);
}


static double variant_get_real (VARIANT *v)
{
    // TODO
    switch (v->vt)
    {
    case VT_R4: return (v->fltVal);
    case VT_R8: return (v->dblVal);
    default:
        ERROR ("wmi internal error: conversion problem at line %d", __LINE__);
        return (0);
    }
}

int64_t variant_get_int64 (VARIANT *v)
{
    int64_t result = 0;

    if (variant_is_unsigned_integer (v))
        return (variant_get_unsigned (v));
    else if (variant_is_signed_integer (v))
        return (variant_get_signed_integer (v));
    else if (variant_is_real (v))
        return (variant_get_real (v));
    else if (v->vt == VT_BSTR)
    {
        char* str = wstrtostr (v->bstrVal);
        int status = sscanf (str, "%" SCNd64, &result);
        if (status <= 0)
            ERROR ("Cannot convert '%s' to int64.", str);
        free (str);

        return (result);
    }
    else
    {
        ERROR ("wmi error: Cannot convert from type %d to int64.",
                (int)v->vt);
        return (0);
    }
}

uint64_t variant_get_uint64 (VARIANT *v)
{
    uint64_t result = 0;

    if (variant_is_unsigned_integer (v))
        return (variant_get_unsigned (v));
    else if (variant_is_signed_integer (v))
        return (variant_get_signed_integer (v));
    else if (variant_is_real (v))
        return (variant_get_real (v));
    else if (v->vt == VT_BSTR)
    {
        char* str = wstrtostr (v->bstrVal);
        int status = sscanf (str, "%" SCNu64, &result);
        if (status <= 0)
            ERROR ("Cannot convert '%s' to uint64.", str);
        free (str);

       return (result);
    }
    else
    {
        ERROR ("wmi error: Cannot convert from type %d to int64.",
                (int)v->vt);
        return (0);
    }
}

double variant_get_double (VARIANT *v)
{
    double result = 0;

    if (variant_is_unsigned_integer (v))
        return (variant_get_unsigned (v));
    else if (variant_is_signed_integer (v))
        return (variant_get_signed_integer (v));
    else if (variant_is_real (v))
        return (variant_get_real (v));
    else if (v->vt == VT_BSTR)
    {
        char* str = wstrtostr (v->bstrVal);
        int status = sscanf (str, "%lf", &result);
        if (status <= 0)
            ERROR ("Cannot convert '%s' to double.", str);
        free (str);

        return (result);
    }
    else
    {
        ERROR ("wmi error: Cannot convert from type %d to double.",
                (int)v->vt);
        return (0);
    }
}

unsigned long long variant_get_ull (VARIANT *v)
{
    // TODO: this might be a corner case
    return (variant_get_uint64(v));
}
