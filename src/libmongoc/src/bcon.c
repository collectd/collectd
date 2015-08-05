/* bcon.c */

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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include "bcon.h"

#ifndef NOT_REACHED
#define NOT_REACHED 0
#endif

#define ARRAY_INDEX_BUFFER_SIZE 9

char *bcon_errstr[] = {
    "OK",
    "ERROR",
    "bcon document or nesting incomplete",
    "bson finish error"
};

bcon_error_t bson_append_bcon_array(bson *b, const bcon *bc);

/* should be static, but it used by test files */
bcon_token_t bcon_token(char *s);

bcon_token_t bcon_token(char *s) {
    if (s == 0) return Token_EOD;
    switch (s[0]) {
    case ':': if (s[1] != '\0' && s[2] != '\0' && s[3] != '\0' && s[4] == '\0' &&
                      s[3] == ':' && (s[1] == '_' || s[1] == 'P' || s[1] == 'R'))
            return Token_Typespec; break;
    case '{': if (s[1] == '\0') return Token_OpenBrace; break;
    case '}': if (s[1] == '\0') return Token_CloseBrace; break;
    case '[': if (s[1] == '\0') return Token_OpenBracket; break;
    case ']': if (s[1] == '\0') return Token_CloseBracket; break;
    case '.': if (s[1] == '\0') return Token_End; break;
    }
    return Token_Default;
}

static bcon_error_t bson_bcon_key_value(bson *b, const char *key, const char *typespec, const bcon bci) {
    bcon_error_t ret = BCON_OK;
    bson_oid_t oid;
    char ptype = typespec ? typespec[1] : '_';
    char utype = typespec ? typespec[2] : '_';
    switch (ptype) {
    case '_': /* kv(b, key, utype, bci) */
        switch (utype) {
        case '_': /* fall through */
        case 's': bson_append_string( b, key, bci.s ); break; /* common case */
        case 'f': bson_append_double( b, key, bci.f ); break;
        case 'D':
            bson_append_start_object( b, key );
            ret = bson_append_bcon( b, bci.D );
            bson_append_finish_object( b );
            break;
        case 'A':
            bson_append_start_array( b, key );
            ret = bson_append_bcon_array( b, bci.A );
            bson_append_finish_array( b );
            break;
        case 'o': if (*bci.o == '\0') bson_oid_gen( &oid ); else bson_oid_from_string( &oid, bci.o ); bson_append_oid( b, key, &oid ); break;
        case 'b': bson_append_bool( b, key, bci.b ); break;
        case 't': bson_append_time_t( b, key, bci.t ); break;
        case 'v': bson_append_null( b, key ); break; /* void */
        case 'x': bson_append_symbol( b, key, bci.x ); break;
        case 'i': bson_append_int( b, key, bci.i ); break;
        case 'l': bson_append_long( b, key, bci.l ); break;
        default: printf("\nptype:'%c' utype:'%c'\n", ptype, utype); assert(NOT_REACHED); break;
        }
        break;
    case 'R': /* krv(b, key, utype, bci) */
        switch (utype) {
        case 'f': bson_append_double( b, key, *bci.Rf ); break;
        case 's': bson_append_string( b, key, bci.Rs ); break;
        case 'D':
            bson_append_start_object( b, key );
            ret = bson_append_bcon( b, bci.RD );
            bson_append_finish_object( b );
            break;
        case 'A':
            bson_append_start_array( b, key );
            ret = bson_append_bcon_array( b, bci.RA );
            bson_append_finish_array( b );
            break;
        case 'o': if (*bci.o == '\0') bson_oid_gen( &oid ); else bson_oid_from_string( &oid, bci.o ); bson_append_oid( b, key, &oid ); break;
        case 'b': bson_append_bool( b, key, *bci.Rb ); break;
        case 't': bson_append_time_t( b, key, *bci.Rt ); break;
        case 'x': bson_append_symbol( b, key, bci.Rx ); break;
        case 'i': bson_append_int( b, key, *bci.Ri ); break;
        case 'l': bson_append_long( b, key, *bci.Rl ); break;
        default: printf("\nptype:'%c' utype:'%c'\n", ptype, utype); assert(NOT_REACHED); break;
        }
        break;
    case 'P': /* kpv(b, key, utype, bci) */
        if (*bci.Pv != 0) {
            switch (utype) {
            case 'f': bson_append_double( b, key, **bci.Pf ); break;
            case 's': bson_append_string( b, key, *bci.Ps ); break;
            case 'D':
                bson_append_start_object( b, key );
                ret = bson_append_bcon( b, *bci.PD );
                bson_append_finish_object( b );
                break;
            case 'A':
                bson_append_start_array( b, key );
                ret = bson_append_bcon_array( b, *bci.PA );
                bson_append_finish_array( b );
                break;
            case 'o': if (**bci.Po == '\0') bson_oid_gen( &oid );
                else bson_oid_from_string( &oid, *bci.Po );
                bson_append_oid( b, key, &oid );
                break;
            case 'b': bson_append_bool( b, key, **bci.Pb ); break;
            case 't': bson_append_time_t( b, key, **bci.Pt ); break;
            case 'x': if (*bci.Px != 0) bson_append_symbol( b, key, *bci.Px ); break;
            case 'i': bson_append_int( b, key, **bci.Pi ); break;
            case 'l': bson_append_long( b, key, **bci.Pl ); break;
            default: printf("\nptype:'%c' utype:'%c'\n", ptype, utype); assert(NOT_REACHED); break;
            }
        }
        break;
    default:
        printf("\nptype:'%c' utype:'%c'\n", ptype, utype); assert(NOT_REACHED);
        break;
    }
    return ret;
}

typedef enum bcon_state_t {
    State_Element, State_DocSpecValue, State_DocValue,
    State_ArraySpecValue, State_ArrayValue
} bcon_state_t;

#define DOC_STACK_SIZE 1024
#define ARRAY_INDEX_STACK_SIZE 1024

#define DOC_PUSH_STATE(return_state) ( doc_stack[doc_stack_pointer++] = (return_state) )
#define DOC_POP_STATE ( state = doc_stack[--doc_stack_pointer] )
#define ARRAY_PUSH_RESET_INDEX_STATE(return_state) ( array_index_stack[array_index_stack_pointer++] = array_index, array_index = 0, DOC_PUSH_STATE(return_state) )
#define ARRAY_POP_INDEX_STATE ( array_index = array_index_stack[--array_index_stack_pointer], DOC_POP_STATE )

#define ARRAY_KEY_STRING(l) (bson_numstr(array_index_buffer, (int)(l)), array_index_buffer)

/*
 * simplified FSM to parse BCON structure, uses stacks for sub-documents and sub-arrays
 */
static bcon_error_t bson_append_bcon_with_state(bson *b, const bcon *bc, bcon_state_t start_state) {
    bcon_error_t ret = BCON_OK;
    bcon_state_t state = start_state;
    char *key = 0;
    char *typespec = 0;
    unsigned char doc_stack[DOC_STACK_SIZE];
    size_t doc_stack_pointer = 0;
    size_t array_index = 0;
    size_t array_index_stack[ARRAY_INDEX_STACK_SIZE];
    size_t array_index_stack_pointer = 0;
    char array_index_buffer[ARRAY_INDEX_BUFFER_SIZE]; /* max BSON size */
    int end_of_data;
    const bcon *bcp;
    for (end_of_data = 0, bcp = bc; ret == BCON_OK && !end_of_data; bcp++) {
        bcon bci = *bcp;
        char *s = bci.s;
        switch (state) {
        case State_Element:
            switch (bcon_token(s)) {
            case Token_CloseBrace:
                bson_append_finish_object( b );
                DOC_POP_STATE; /* state = ...; */
                break;
            case Token_End:
                end_of_data = 1;
                break;
            default:
                key = s;
                state = State_DocSpecValue;
                break;
            }
            break;
        case State_DocSpecValue:
            switch (bcon_token(s)) {
            case Token_Typespec:
                typespec = s;
                state = State_DocValue;
                break;
            case Token_OpenBrace:
                bson_append_start_object( b, key );
                DOC_PUSH_STATE(State_Element);
                state = State_Element;
                break;
            case Token_OpenBracket:
                bson_append_start_array( b, key );
                ARRAY_PUSH_RESET_INDEX_STATE(State_Element);
                state = State_ArraySpecValue;
                break;
            case Token_End:
                end_of_data = 1;
                break;
            default:
                ret = bson_bcon_key_value(b, key, typespec, bci);
                state = State_Element;
                break;
            }
            break;
        case State_DocValue:
            ret = bson_bcon_key_value(b, key, typespec, bci);
            state = State_Element;
            typespec = 0;
            break;
        case State_ArraySpecValue:
            switch (bcon_token(s)) {
            case Token_Typespec:
                typespec = s;
                state = State_ArrayValue;
                break;
            case Token_OpenBrace:
                key = ARRAY_KEY_STRING(array_index++);
                bson_append_start_object( b, key );
                DOC_PUSH_STATE(State_ArraySpecValue);
                state = State_Element;
                break;
            case Token_OpenBracket:
                key = ARRAY_KEY_STRING(array_index++);
                bson_append_start_array( b, key );
                ARRAY_PUSH_RESET_INDEX_STATE(State_ArraySpecValue);
                /* state = State_ArraySpecValue; */
                break;
            case Token_CloseBracket:
                bson_append_finish_array( b );
                ARRAY_POP_INDEX_STATE; /* state = ...; */
                break;
            case Token_End:
                end_of_data = 1;
                break;
            default:
                key = ARRAY_KEY_STRING(array_index++);
                ret = bson_bcon_key_value(b, key, typespec, bci);
                /* state = State_ArraySpecValue; */
                break;
            }
            break;
        case State_ArrayValue:
            key = ARRAY_KEY_STRING(array_index++);
            ret = bson_bcon_key_value(b, key, typespec, bci);
            state = State_ArraySpecValue;
            typespec = 0;
            break;
        default: assert(NOT_REACHED); break;
        }
    }
    return state == start_state ? BCON_OK : BCON_DOCUMENT_INCOMPLETE;
}

bcon_error_t bson_append_bcon(bson *b, const bcon *bc) {
    return bson_append_bcon_with_state(b, bc, State_Element);
}

bcon_error_t bson_append_bcon_array(bson *b, const bcon *bc) {
    return bson_append_bcon_with_state(b, bc, State_ArraySpecValue);
}

/**
 * Generate BSON from BCON
 * @param b a BSON object
 * @param bc a BCON object
 * match with bson_destroy
 */
bcon_error_t bson_from_bcon(bson *b, const bcon *bc) {
    bcon_error_t ret = BSON_OK;
    bson_init( b );
    ret = bson_append_bcon_with_state( b, bc, State_Element );
    if (ret != BCON_OK) return ret;
    ret = bson_finish( b );
    return ( ret == BSON_OK ? BCON_OK : BCON_BSON_ERROR );
}

void bcon_print(const bcon *bc) { /* prints internal representation, not JSON */
    char *typespec = 0;
    char *delim = "";
    int end_of_data;
    bcon *bcp;
    putchar('{');
    for (end_of_data = 0, bcp = (bcon*)bc; !end_of_data; bcp++) {
        bcon bci = *bcp;
        char *typespec_next = 0;
        if (typespec) {
            switch (typespec[1]) {
            case '_':
                switch (typespec[2]) {
                case 'f': printf("%s%f", delim, bci.f); break;
                case 's': printf("%s\"%s\"", delim, bci.s); break;
                case 'D': printf("%sPD(0x%lx,..)", delim, (unsigned long)bci.D); break;
                case 'A': printf("%sPA(0x%lx,....)", delim, (unsigned long)bci.A); break;
                case 'o': printf("%s\"%s\"", delim, bci.o); break;
                case 'b': printf("%s%d", delim, bci.b); break;
                case 't': printf("%s%ld", delim, (long)bci.t); break;
                case 'v': printf("%s\"%s\"", delim, bci.v); break;
                case 'x': printf("%s\"%s\"", delim, bci.x); break;
                case 'i': printf("%s%d", delim, bci.i); break;
                case 'l': printf("%s%ld", delim, bci.l); break;
                default: printf("\ntypespec:\"%s\"\n", typespec); assert(NOT_REACHED); break;
                }
                break;
            case 'R':
                switch (typespec[2]) {
                case 'f': printf("%sRf(0x%lx,%f)", delim, (unsigned long)bci.Rf, *bci.Rf); break;
                case 's': printf("%sRs(0x%lx,\"%s\")", delim, (unsigned long)bci.Rs, bci.Rs); break;
                case 'D': printf("%sRD(0x%lx,..)", delim, (unsigned long)bci.RD); break;
                case 'A': printf("%sRA(0x%lx,....)", delim, (unsigned long)bci.RA); break;
                case 'o': printf("%sRo(0x%lx,\"%s\")", delim, (unsigned long)bci.Ro, bci.Ro); break;
                case 'b': printf("%sRb(0x%lx,%d)", delim, (unsigned long)bci.Rb, *bci.Rb); break;
                case 't': printf("%sRt(0x%lx,%ld)", delim, (unsigned long)bci.Rt, (long)*bci.Rt); break;
                case 'x': printf("%sRx(0x%lx,\"%s\")", delim, (unsigned long)bci.Rx, bci.Rx); break;
                case 'i': printf("%sRi(0x%lx,%d)", delim, (unsigned long)bci.Ri, *bci.Ri); break;
                case 'l': printf("%sRl(0x%lx,%ld)", delim, (unsigned long)bci.Rl, *bci.Rl); break;
                default: printf("\ntypespec:\"%s\"\n", typespec); assert(NOT_REACHED); break;
                }
                break;
            case 'P':
                switch (typespec[2]) {
                case 'f': printf("%sPf(0x%lx,0x%lx,%f)", delim, (unsigned long)bci.Pf, (unsigned long)(bci.Pf ? *bci.Pf : 0), bci.Pf && *bci.Pf ? **bci.Pf : 0.0); break;
                case 's': printf("%sPs(0x%lx,0x%lx,\"%s\")", delim, (unsigned long)bci.Ps, (unsigned long)(bci.Ps ? *bci.Ps : 0), bci.Ps && *bci.Ps ? *bci.Ps : ""); break;
                case 'D': printf("%sPD(0x%lx,0x%lx,..)", delim, (unsigned long)bci.PD, (unsigned long)(bci.PD ? *bci.PD : 0)); break;
                case 'A': printf("%sPA(0x%lx,0x%lx,....)", delim, (unsigned long)bci.PA, (unsigned long)(bci.PA ? *bci.PA : 0)); break;
                case 'o': printf("%sPo(0x%lx,0x%lx,\"%s\")", delim, (unsigned long)bci.Po, (unsigned long)(bci.Po ? *bci.Po : 0), bci.Po && *bci.Po ? *bci.Po : ""); break;
                case 'b': printf("%sPb(0x%lx,0x%lx,%d)", delim, (unsigned long)bci.Pb, (unsigned long)(bci.Pb ? *bci.Pb : 0), bci.Pb && *bci.Pb ? **bci.Pb : 0); break;
                case 't': printf("%sPt(0x%lx,0x%lx,%ld)", delim, (unsigned long)bci.Pt, (unsigned long)(bci.Pt ? *bci.Pt : 0), bci.Pt && *bci.Pt ? (long)**bci.Pt : 0); break;
                case 'x': printf("%sPx(0x%lx,0x%lx,\"%s\")", delim, (unsigned long)bci.Px, (unsigned long)(bci.Px ? *bci.Px : 0), bci.Px && *bci.Px ? *bci.Px : ""); break;
                case 'i': printf("%sPi(0x%lx,0x%lx,%d)", delim, (unsigned long)bci.Pi, (unsigned long)(bci.Pi ? *bci.Pi : 0), bci.Pi && *bci.Pi ? **bci.Pi : 0); break;
                case 'l': printf("%sPl(0x%lx,0x%lx,%ld)", delim, (unsigned long)bci.Pl, (unsigned long)(bci.Pl ? *bci.Pl : 0), bci.Pl && *bci.Pl ? **bci.Pl : 0); break;

                default: printf("\ntypespec:\"%s\"\n", typespec); assert(NOT_REACHED); break;
                }
                break;
            default:
                printf("\ntypespec:\"%s\"\n", typespec); assert(NOT_REACHED);
                break;
            }
        }
        else {
            char *s = bci.s;
            switch (s[0]) {
            case '.':
                end_of_data = (s[1] == '\0');
                break;
            case ':':
                typespec_next = bcon_token(s) == Token_Typespec ? s : 0;
                break;
            }
            printf("%s\"%s\"", delim, s);
        }
        typespec = typespec_next;
        delim = ",";
    }
    putchar('}');
}
