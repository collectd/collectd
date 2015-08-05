/**
 * @file bcon.h
 * @brief BCON (BSON C Object Notation) Declarations
 */

/*    Copyright 2009-2012 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#ifndef BCON_H_
#define BCON_H_

#include "bson.h"

#ifndef DOXYGEN_SHOULD_SKIP_THIS

MONGO_EXTERN_C_START

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

typedef union bcon {
    char *s;         /**< 02  e_name  string              Macro BS(v)  - UTF-8 string */ /* must be first to be default */
    char *Rs;        /**< 02  e_name  string              Macro BRS(v) - UTF-8 string reference interpolation */
    char **Ps;       /**< 02  e_name  string              Macro BPS(v) - UTF-8 string pointer interpolation */
    double f;        /**< 01  e_name  double              Macro BF(v)  - Floating point */
    double *Rf;      /**< 01  e_name  double              Macro BRF(v) - Floating point reference interpolation */
    double **Pf;     /**< 01  e_name  double              Macro BPF(v) - Floating point pointer interpolation */
    union bcon *D;   /**< 03  e_name  document            Macro BD(v)  - Embedded document interpolation */
    union bcon *RD;  /**< 03  e_name  document            Macro BRD(v) - Embedded document reference interpolation */
    union bcon **PD; /**< 03  e_name  document            Macro BPD(v) - Embedded document pointer interpolation */
    union bcon *A;   /**< 04  e_name  document            Macro BA(v)  - Array interpolation */
    union bcon *RA;  /**< 04  e_name  document            Macro BRA(v) - Array reference interpolation */
    union bcon **PA; /**< 04  e_name  document            Macro BPA(v) - Array pointer interpolation */
    char *o;         /**< 07  e_name  (byte*12)           Macro BO(v)  - ObjectId */
    char *Ro;        /**< 07  e_name  (byte*12)           Macro BRO(v) - ObjectId reference interpolation */
    char **Po;       /**< 07  e_name  (byte*12)           Macro BPO(v) - ObjectId pointer interpolation */
    bson_bool_t b;   /**< 08  e_name  00                  Macro BB(v)  - Boolean "false"
                          08  e_name  01                  Macro BB(v) - Boolean "true" */
    bson_bool_t *Rb; /**< 08  e_name  01                  Macro BRB(v) - Boolean reference interpolation */
    bson_bool_t **Pb;/**< 08  e_name  01                  Macro BPB(v) - Boolean pointer interpolation */
    time_t t;        /**< 09  e_name  int64               Macro BT(v)  - UTC datetime */
    time_t *Rt;      /**< 09  e_name  int64               Macro BRT(v) - UTC datetime reference interpolation */
    time_t **Pt;     /**< 09  e_name  int64               Macro BPT(v) - UTC datetime pointer interpolation */
    char *v;         /**< 0A  e_name                      Macro BNULL  - Null value */
    char *x;         /**< 0E  e_name  string              Macro BX(v)  - Symbol */
    char *Rx;        /**< 0E  e_name  string              Macro BRX(v) - Symbol reference interpolation */
    char **Px;       /**< 0E  e_name  string              Macro BPX(v) - Symbol pointer interpolation */
    int i;           /**< 10  e_name  int32               Macro BI(v)  - 32-bit Integer */
    int *Ri;         /**< 10  e_name  int32               Macro BRI(v) - 32-bit Integer reference interpolation */
    int **Pi;        /**< 10  e_name  int32               Macro BPI(v) - 32-bit Integer pointer interpolation */
    long l;          /**< 12  e_name  int64               Macro BL(v)  - 64-bit Integer */
    long *Rl;        /**< 12  e_name  int64               Macro BRL(v) - 64-bit Integer reference interpolation */
    long **Pl;       /**< 12  e_name  int64               Macro BPL(v) - 64-bit Integer pointer interpolation */
    void **Pv;       /*                                   generic pointer internal */
    /* "{" "}" */    /*   03  e_name  document            Embedded document */
    /* "[" "]" */    /*   04  e_name  document            Array */
                     /*   05  e_name  binary              Binary data */
                     /*   06  e_name                      undefined - deprecated */
                     /*   0B  e_name  cstring cstring     Regular expression */
                     /*   0C  e_name  string (byte*12)    DBPointer - Deprecated */
                     /*   0D  e_name  string              JavaScript code */
                     /*   0F  e_name  code_w_s            JavaScript code w/ scope  */
                     /*   11  e_name  int64               Timestamp */
                     /*   FF  e_name                      Min key */
                     /*   7F  e_name                      Max key */
} bcon;

/** BCON document terminator */
#define BEND "."

/** BCON internal 01 double Floating point type-specifier */
#define BTF ":_f:"
/** BCON internal 02 char* string type-specifier */
#define BTS ":_s:"
/** BCON internal 03 union bcon* Embedded document interpolation type-specifier */
#define BTD ":_D:"
/** BCON internal 04 union bcon* Array interpolation type-specifier */
#define BTA ":_A:"
/** BCON internal 07 char* ObjectId type-specifier */
#define BTO ":_o:"
/** BCON internal 08 int Boolean type-specifier */
#define BTB ":_b:"
/** BCON internal 09 int64 UTC datetime type-specifier */
#define BTT ":_t:"
/** BCON internal 0A Null type-specifier */
#define BTN ":_v:"
/** BCON internal 0E char* Symbol type-specifier */
#define BTX ":_x:"
/** BCON internal 10 int32 64-bit Integer type-specifier */
#define BTI ":_i:"
/** BCON internal 12 int64 64-bit Integer type-specifier */
#define BTL ":_l:"

/** BCON internal 01 double* Floating point reference interpolation type-specifier */
#define BTRF ":Rf:"
/** BCON internal 02 char* string reference interpolation type-specifier */
#define BTRS ":Rs:"
/** BCON internal 03 union bcon* Embedded document reference interpolation type-specifier */
#define BTRD ":RD:"
/** BCON internal 04 union bcon* Array reference interpolation type-specifier */
#define BTRA ":RA:"
/** BCON internal 07 char* ObjectId reference interpolation type-specifier */
#define BTRO ":Ro:"
/** BCON internal 08 int* Boolean reference interpolation type-specifier */
#define BTRB ":Rb:"
/** BCON internal 09 int64* UTC datetime reference interpolation type-specifier */
#define BTRT ":Rt:"
/** BCON internal 0E char* Symbol reference interpolation type-specifier */
#define BTRX ":Rx:"
/** BCON internal 10 int32* 23-bit Integer reference interpolation type-specifier */
#define BTRI ":Ri:"
/** BCON internal 12 int64* 64-bit Integer reference interpolation type-specifier */
#define BTRL ":Rl:"

/** BCON internal 01 double** Floating point pointer interpolation type-specifier */
#define BTPF ":Pf:"
/** BCON internal 02 char** string pointer interpolation type-specifier */
#define BTPS ":Ps:"
/** BCON internal 03 union bcon** Embedded document pointer interpolation type-specifier */
#define BTPD ":PD:"
/** BCON internal 04 union bcon** Array pointer interpolation type-specifier */
#define BTPA ":PA:"
/** BCON internal 07 char** ObjectId pointer interpolation type-specifier */
#define BTPO ":Po:"
/** BCON internal 08 int** Boolean pointer interpolation type-specifier */
#define BTPB ":Pb:"
/** BCON internal 09 int64** UTC datetime pointer interpolation type-specifier */
#define BTPT ":Pt:"
/** BCON internal 0E char** Symbol pointer interpolation type-specifier */
#define BTPX ":Px:"
/** BCON internal 10 int32** 23-bit Integer pointer interpolation type-specifier */
#define BTPI ":Pi:"
/** BCON internal 12 int64** 64-bit Integer pointer interpolation type-specifier */
#define BTPL ":Pl:"

/** BCON 01 double Floating point value */
#define BF(v) BTF, { .f = (v) }
/** BCON 02 char* string value */
#define BS(v) BTS, { .s = (v) }
/** BCON 03 union bcon* Embedded document interpolation value */
#define BD(v) BTD, { .D = (v) }
/** BCON 04 union bcon* Array interpolation value */
#define BA(v) BTA, { .A = (v) }
/** BCON 07 char* ObjectId value */
#define BO(v) BTO, { .o = (v) }
/** BCON 08 int Boolean value */
#define BB(v) BTB, { .b = (v) }
/** BCON 09 int64 UTC datetime value */
#define BT(v) BTT, { .t = (v) }
/** BCON 0A Null value */
#define BNULL BTN, { .v = ("") }
/** BCON 0E char* Symbol value */
#define BX(v) BTX, { .x = (v) }
/** BCON 10 int32 32-bit Integer value */
#define BI(v) BTI, { .i = (v) }
/** BCON 12 int64 64-bit Integer value */
#define BL(v) BTL, { .l = (v) }

/** BCON 01 double* Floating point interpolation value */
#define BRF(v) BTRF, { .Rf = (v) }
/** BCON 02 char* string interpolation value */
#define BRS(v) BTRS, { .Rs = (v) }
/** BCON 03 union bcon* Embedded document interpolation value */
#define BRD(v) BTRD, { .RD = (v) }
/** BCON 04 union bcon* Array interpolation value */
#define BRA(v) BTRA, { .RA = (v) }
/** BCON 07 char* ObjectId interpolation value */
#define BRO(v) BTRO, { .Ro = (v) }
/** BCON 08 int* Boolean interpolation value */
#define BRB(v) BTRB, { .Rb = (v) }
/** BCON 09 int64* UTC datetime  value */
#define BRT(v) BTRT, { .Rt = (v) }
/** BCON 0E char* Symbol interpolation value */
#define BRX(v) BTRX, { .Rx = (v) }
/** BCON 10 int32* 32-bit Integer interpolation value */
#define BRI(v) BTRI, { .Ri = (v) }
/** BCON 12 int64* 64-bit Integer interpolation value */
#define BRL(v) BTRL, { .Rl = (v) }

/** BCON 01 double** Floating point interpolation value */
#define BPF(v) BTPF, { .Pf = (v) }
/** BCON 02 char** string interpolation value */
#define BPS(v) BTPS, { .Ps = ((char**)v) }
/** BCON 03 union bcon** Embedded document interpolation value */
#define BPD(v) BTPD, { .PD = ((union bcon **)v) }
/** BCON 04 union bcon** Array interpolation value */
#define BPA(v) BTPA, { .PA = ((union bcon **)v) }
/** BCON 07 char** ObjectId interpolation value */
#define BPO(v) BTPO, { .Po = ((char**)v) }
/** BCON 08 int** Boolean interpolation value */
#define BPB(v) BTPB, { .Pb = (v) }
/** BCON 09 int64** UTC datetime  value */
#define BPT(v) BTPT, { .Pt = (v) }
/** BCON 0E char** Symbol interpolation value */
#define BPX(v) BTPX, { .Px = ((char**)v) }
/** BCON 10 int32** 32-bit Integer interpolation value */
#define BPI(v) BTPI, { .Pi = (v) }
/** BCON 12 int64** 64-bit Integer interpolation value */
#define BPL(v) BTPL, { .Pl = (v) }

/*
 * References on codes used for types
 *     http://en.wikipedia.org/wiki/Name_mangling
 *     http://www.agner.org/optimize/calling_conventions.pdf (page 25)
 */

typedef enum bcon_error_t {
    BCON_OK = 0, /**< OK return code */
    BCON_ERROR,  /**< ERROR return code */
    BCON_DOCUMENT_INCOMPLETE, /**< bcon document or nesting incomplete */
    BCON_BSON_ERROR /**< bson finish error */
} bcon_error_t;

extern char *bcon_errstr[]; /**< bcon_error_t text messages */

/**
 * Append a BCON object to a BSON object.
 *
 * @param b a BSON object
 * @param bc a BCON object
 */
MONGO_EXPORT bcon_error_t bson_append_bcon(bson *b, const bcon *bc);

/**
 * Generate a BSON object from a BCON object.
 *
 * @param b a BSON object
 * @param bc a BCON object
 */
MONGO_EXPORT bcon_error_t bson_from_bcon( bson *b, const bcon *bc );

/**
 * Print a string representation of a BCON object.
 *
 * @param bc the BCON object to print.
 */
MONGO_EXPORT void bcon_print( const bcon *bc );

#ifndef DOXYGEN_SHOULD_SKIP_THIS

MONGO_EXTERN_C_END

typedef enum bcon_token_t {
    Token_Default, Token_End, Token_Typespec,
    Token_OpenBrace, Token_CloseBrace, Token_OpenBracket, Token_CloseBracket,
    Token_EOD
} bcon_token_t;

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#endif
