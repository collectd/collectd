/**
 * URL Processing
 *
 * Copyright (C) 2000, 2001, 2002 by
 * Jeffrey Fulmer - <jdfulmer@armstrong.com>
 * This file is distributed as part of Siege 
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
#ifdef  HAVE_CONFIG_H
# include <config.h>
#endif/*HAVE_CONFIG_H*/
 
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef STDC_HEADERS
# include <string.h>
#else
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr (), *strrchr ();
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif
#include <errno.h>
#include <url.h>
#include <util.h>
#include <setup.h>

/*HARDCODED ALERT*/
#define PLENGTH 84  /* length of the *prot[] array */

/** 
 * ALERT: using hardcoded array lengths below,
 * if you change this array, then redefine PLENGTH
 * 
 * Currently http(prot[25]) and https(prot[26]) are 
 * the only supported protocols.  But all w3c supported 
 * protocols are listed for URL evaluation.
 */  
static char *prot[] = {
  "about:",      "addrbook:",  "acap:",      "afp:",
  "afs:",        "callto:",    "chttp:",     "cid:",
  "clsid:",      "data:",      "date:",      "DAV:",
  "dns:",        "eid:",       "fax:",       "file:",
  "finger:",     "freenet:",   "ftp:",       "gopher:",
  "gsm:",        "h323:",      "h324:",      "hdl:",
  "hnews:",      "http:",      "https:",     "iioploc:",
  "ilu:",        "imap:",      "IOR:",       "irc:",
  "isbn:",       "java:",      "JavaRMI:",   "javascript:",
  "jdbc:",       "ldap:",      "lid:",       "lifn:",
  "livescript:", "lrq:",       "mailto:",    "mailserver:",
  "md5:",        "mid:",       "mocha:",     "modem:",
  "news:",       "nfs:",       "nntp:",      "opaquelocktoken:"
  "path:",       "phone:",     "pop:",       "pop3:",
  "printer:",    "prospero:",  "res:",       "rtsp:",
  "rvp:",        "rwhois:",    "rx:",        "sdp:",
  "sip:",        "shttp:",     "snews:",     "STANF:",
  "t120:",       "tel:",       "telephone:", "telnet:",
  "tip:",        "tn3270:",    "tv:",        "uuid:",
  "urn:",        "vemmi:",     "videotex:",  "view:",
  "wais:",       "whois++:",   "whodp:",     "z39.50r:",
  "z39.50s:"
  
};

/**
 * int value of the length of the protocol 
 * string passed to the function.
 */     
int
protocol_length( char *url )
{
  int x;
  /** 
   * hardcoded protocol length!! see explanation above...
   */
  for( x = 0; x < PLENGTH; x ++ ){ 
    if( strncasecmp( url, prot[x], strlen( prot[x] )) == 0 )
      return strlen( prot[x] );
  } 
  return 0;	
}

/**
 * If a person edits an html file on the
 * Microsoft platform and copies it to a 
 * UNIX server, we are left to deal with 
 * ^M chars messing with our minds...
 */
char *
trim( char *str )
{
  char *s=str;
 
  if( str == NULL ) return NULL;
 
  /* advance the ptr */
  while( *s ) s++;
 
  /* chomp the white space */
  while( str < s && isspace( *( s-1 )))
    *s-- = '\0';
 
  *s = '\0';
 
  return str; /* UNIX friendly */
} 

/**
 * boolean, returns true if the protocol is 
 * supported by siege, false if it is not.
 */ 
int
is_supported( char* url )
{
  if( strncasecmp( url, prot[25], strlen( prot[25] )) == 0 )
    return TRUE;
  if( strncasecmp( url, prot[26], strlen( prot[26] )) == 0 )
    #ifdef HAVE_SSL
      return TRUE;
    #else
      return FALSE;
    #endif /* HAVE_SSL */
  else
    return FALSE;
}

/**
 * get_protocol
 * returns protocol char*
 */
PROTOCOL
get_protocol( const char *url )
{
  if( strncasecmp( url, prot[25], strlen( prot[25] )) == 0 )
    return HTTP;
  if( strncasecmp( url, prot[26], strlen( prot[26] )) == 0 )
    #ifdef HAVE_SSL
      return HTTPS;
    #else
      return HTTP;
    #endif /* HAVE_SSL */
  else
    return UNSPRTD;
}

/**
 * get_default_port
 */
int
get_default_port( PROTOCOL p )
{
  if( p == HTTP )
    return 80;
  if( p == HTTPS )
    #ifdef HAVE_SSL
      return 443;
    #else
      return 80;
    #endif /* HAVE_SSL */
  else
    return 80; 
}

char *
url_encode( char *str )
{
  int size = 0;
  char *ch, *bk;
  char *p, *buf;
  static char unsafe[]     = "<>{}#%|\"\\^~[]`@:\033";
  static char char2hex[16] = "0123456789ABCDEF";

  bk = ch  = str;
  do{
    if( strchr( unsafe, *ch ))
      size += 2;
    ch++; size ++;
  } while( *ch );

  buf = (char*)malloc( size +1 );
  p   = buf;
  ch  = bk;
  do{
    if( strchr( unsafe, *ch )){
      const char c = *ch;
      *p++ = '%';
      *p++ = char2hex[(c >> 4) & 0xf];
      *p++ = char2hex[c & 0xf];
    }
    else{
      *p++ = *ch;
    }
    ch ++;
  } while( *ch );

  *p = '\0';
  return( buf );
}

/**
 * process_post_data
 * populates URL->postdata with POST information
 * returns int
 */
char *
process_post_data( char *datap )
{
  for( ; isspace(*datap); datap++ ){
    /* Advance past white space */
  }
  if( *datap == '<' ){
    /* Get Data from file */
    return NULL;
  }
  else{
    return datap;
  }
}

URL
build_url( char *url )
{
  URL U;                   /* defined in setup.h  */
  int one, two, thr, fou;  /* placement counters. */ 
  char *post_cmd=NULL;     /* POST directive for server */
  char *tmp;

  post_cmd = strstr( url, " POST" ); 
  if( post_cmd != NULL ){
    /* How do we deal with handling the multi-headed url_t arrays */
    U.calltype = URL_POST;
    *post_cmd = 0;
    post_cmd += 5;
    U.postdata = (char*)strdup( process_post_data( post_cmd ));
    U.postlen  = strlen( U.postdata );
  }
  else{
    U.calltype   = URL_GET;
    U.postdata   = NULL;
    U.posttemp   = NULL;
    U.postlen    = 0;
  }
  if(( one = protocol_length( url )) > 0 && is_supported( url ) == TRUE ){
    one += 2;
  }
  else if(( one = protocol_length( url )) > 0 && is_supported( url ) == FALSE ){
    U.protocol = UNSPRTD;
    one += 2;
    perror( "unsupported protocol" );
  }
  else{
    /* we are dealing with who knows what */
    tmp = (char*)strstr( url, "://" );
    if( tmp != NULL ){
      one = (strlen(url) - (strlen(tmp) - 3 ));
    }
    else{
      one = 0;  /* no specified protocol, assuming http: */
    }
  }

  two = one;
  while( url[two] && url[two] != ':' && url[two] != '/' ) two++; 

  if( url[two] == ':' ){
    fou = two;
    while( url[two] && url[two] != '/' ){
      two++;
    }
  }
  else{ fou = two; }
  if( url[two] == '/' ){ thr = two; }
  else                 { thr = strlen( url ); } 

  /* here we piece it all together */
  if( one == 0 ){
    U.protocol = HTTP;
  }
  else{
    U.protocol = get_protocol( url );
  }
  U.hostname   = (char*)strdup(substring( url, one, ( fou - one )));
  if( fou == two ){
    U.port = get_default_port( U.protocol );
  }
  else{
    U.port = atoi(substring( url, fou+1, (thr-(fou+1))));
  }
  if(( U.pathname = (char *)strdup(substring( url, thr, strlen( url )))) == NULL ){
    U.pathname = (char *)strdup( "/" ); 
  }
  U.pathname = (strlen(U.pathname)==0)?strcpy(U.pathname, "/"):U.pathname; 
  trim( U.pathname );

  free( url ); 
  return( U );
}

/**
 * add_url
 * parses char * then populates and 
 * returns a URL with appropriate data.
 */
URL
add_url( char *url )
{

  return build_url( url );
}


