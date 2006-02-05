/**
 * PING module
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
#include <ping.h>
#include <util.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>

#define  MAXPACKET   65535
#define  PKTSIZE     64 
#define  HDRLEN      ICMP_MINLEN
#define  DATALEN     (PKTSIZE-HDRLEN)
#define  MAXDATA     (MAXPKT-HDRLEN-TIMLEN)
#define  DEF_TIMEOUT 5

#include "private.h"

void
JOEfreeprotoent( struct protoent *p )
{
  char **a;
  free( p->p_name );
  if( p->p_aliases != NULL ){
    for( a = p->p_aliases; *a != NULL; a++ ){
      free( *a );
    }
  }
  free( p );
}

void
JOEfreehostent( struct hostent *h )
{
  char **p;

  free( h->h_name );
  if( h->h_aliases != NULL ){
    for( p = h->h_aliases; *p != NULL; ++p )
      free( *p );
    free( h->h_aliases );
  }
  if( h->h_addr_list != NULL ){
    for( p = h->h_addr_list; *p != NULL; ++p )
      free( *p );
    free (h->h_addr_list);
  }
  free( h );
}

static int 
in_checksum( u_short *buf, int len )
{
  register long sum = 0;
  u_short  answer = 0;

  while( len > 1 ){
    sum += *buf++;
    len -= 2;
  }

  if( len == 1 ){
    *( u_char* )( &answer ) = *( u_char* )buf;
    sum += answer;
  }
  sum = ( sum >> 16 ) + ( sum & 0xffff );
  sum += ( sum >> 16 );     
  answer = ~sum;     

  return ( answer );

} 

static int
send_ping( const char *host, struct sockaddr_in *taddr, struct ping_priv * datum )
{
  int len;
  int ss;
  unsigned char buf[ HDRLEN + DATALEN ];

  const int proto_buf_len = 1024;
  char   proto_buf[proto_buf_len];
  struct protoent *proto = NULL;
  struct protoent proto_datum;

  struct hostent  *hp = NULL;
  struct hostent  hent;
  int herrno;
  char hbf[9000];
#if defined(_AIX)
  char *aixbuf;
  char *probuf;
  int  rc;
#endif/*_AIX*/

  struct icmp     *icp;
  unsigned short  last;

  len = HDRLEN + DATALEN;

#if defined(__GLIBC__)
  /* for systems using GNU libc */
  getprotobyname_r("icmp", &proto_datum, proto_buf, proto_buf_len, &proto);
  if(( gethostbyname_r( host, &hent, hbf, sizeof(hbf), &hp, &herrno ) < 0 )){
    hp = NULL;
  }
#elif defined(sun)
  /* Solaris 5++ */
  proto = getprotobyname_r("icmp", &proto_datum, proto_buf, proto_buf_len);
  hp    = gethostbyname_r( host, &hent, hbf, sizeof(hbf), &herrno );
#elif defined(_AIX)
  aixbuf = (char*)xmalloc( 9000 );
  probuf = (char*)xmalloc( 9000 );
  rc  = getprotobyname_r( "icmp", &proto,
                        ( struct protoent_data *)(probuf + sizeof( struct protoent)));
  rc  = gethostbyname_r ( host, (struct hostent *)aixbuf,
                        (struct hostent_data *)(aixbuf + sizeof(struct hostent)));
  hp = (struct hostent*)aixbuf;
#elif ( defined(hpux) || defined(__osf__) )
  proto  = getprotobyname( "icmp" ); 
  hp     = gethostbyname( host );
  herrno = h_errno;
#else
  /* simply hoping that get*byname is thread-safe */
  proto  = getprotobyname( "icmp" ); 
  hp     = gethostbyname( host );
  herrno = h_errno;
#endif/*OS SPECIFICS*/
  
  if( proto == NULL ) {
    return -1;
  }

  if(hp != NULL ){
    memcpy( &taddr->sin_addr, hp->h_addr_list[0], sizeof( taddr->sin_addr ));
    taddr->sin_port = 0;
    taddr->sin_family = AF_INET;
  }
  else if( inet_aton( host, &taddr->sin_addr ) == 0 ){
    return -1;
  }

  last = ntohl( taddr->sin_addr.s_addr ) & 0xFF;
  if(( last == 0x00 ) || ( last == 0xFF )){
    return -1;
  }

  if(( datum->sock = socket( AF_INET, SOCK_RAW, proto->p_proto )) < 0 ){
#ifdef  DEBUG
  perror( "sock" );
#endif/*DEBUG*/
    return -2;
  }

  memset(buf, 0, sizeof(buf));
  icp = (struct icmp *)buf;
  icp->icmp_type  = ICMP_ECHO;
  icp->icmp_code  = 0;
  icp->icmp_cksum = 0;
  icp->icmp_id    = getpid() & 0xFFFF;
  icp->icmp_seq   = icp->icmp_id; /* FIXME this is not nice.. */
  icp->icmp_cksum = in_checksum((u_short *)icp, len );

  if(( ss = sendto( datum->sock, buf, sizeof( buf ), 0, 
     (struct sockaddr*)taddr, sizeof( *taddr ))) < 0 ){
#ifdef  DEBUG
  perror( "sock" );
#endif/*DEBUG*/
    return -2;
  }
  if( ss != len ){
#ifdef  DEBUG
  perror( "malformed packet" );
#endif/*DEBUG*/
    return -2;
  }

#if defined(_AIX)
  free( aixbuf );
  free( probuf );
#endif 
  /* JOEfreeprotoent( proto ); */
  /* JOEfreeprotoent( &proto_datum ); */
  /* JOEfreehostent( hp ); */
  /* JOEfreehostent( &hent ); */
  return 0;
}

static int 
recv_ping( struct sockaddr_in *taddr, struct ping_priv * datum )
{
  int len;
  socklen_t from;
  int nf, cc;
  unsigned char buf[ HDRLEN + DATALEN ];
  struct icmp        *icp;
  struct sockaddr_in faddr;
  struct timeval to;
  fd_set readset;

  to.tv_sec = datum->timo / 100000;
  to.tv_usec = ( datum->timo - ( to.tv_sec * 100000 ) ) * 10;

  FD_ZERO( &readset );
  FD_SET( datum->sock, &readset );
  /* we use select to see if there is any activity
     on the socket.  If not, then we've requested an
     unreachable network and we'll time out here. */
  if(( nf = select( datum->sock + 1, &readset, NULL, NULL, &to )) < 0 ){
    datum->rrt = -4;
#ifdef  DEBUG
    perror( "select" );
#endif/*DEBUG*/    
    return 0;
  }
  if( nf == 0 ){ 
    return -1; 
  }

  len = HDRLEN + DATALEN;
  from = sizeof( faddr ); 

  cc = recvfrom( datum->sock, buf, len, 0, (struct sockaddr*)&faddr, &from );
  if( cc < 0 ){
    datum->rrt = -4;
#ifdef  DEBUG
    perror( "recvfrom" );
#endif/*DEBUG*/    
    return 0;
  }

  icp = (struct icmp *)(buf + HDRLEN + DATALEN );
  if( faddr.sin_addr.s_addr != taddr->sin_addr.s_addr ){
    return 1;
  }
  /*****
  if( icp->icmp_id   != ( getpid() & 0xFFFF )){
    printf( "id: %d\n",  icp->icmp_id );
    return 1; 
  }
  *****/
  return 0;
}

int 
myping( const char *hostname, int t , struct ping_priv * datum)
{
  int err;
  int rrt;
  struct sockaddr_in sa;
  struct timeval mytime;
 
  datum->ident = getpid() & 0xFFFF;

  if( t == 0 ) datum->timo = 2; 
  else         datum->timo = t;

  datum->rrt = 0;
  
  (void) gettimeofday( &mytime, (struct timezone *)NULL); 
  if(( err = send_ping( hostname, &sa, datum)) < 0 ){
    close( datum->sock );
    return err;
  }
  do {
    rrt = elapsed_time( &mytime );
    if (datum->rrt < 0)
      return 0;
    datum->rrt = rrt;
    if (datum->rrt > datum->timo * 1000 ) {
      close( datum->sock );
      return 0;
    }
  } while( recv_ping( &sa, datum ));
  close( datum->sock ); 
 
  return 1;
}

int
pinghost( const char *hostname )
{
  struct ping_priv datum = ping_priv_default();
  return myping( hostname, 0, &datum );
}

int
pingthost( const char *hostname, int t )
{
  struct ping_priv datum = ping_priv_default();
  return myping( hostname, t, &datum );
}

int
tpinghost( const char *hostname )
{
  int ret;
  struct ping_priv datum = ping_priv_default();

  ret = myping( hostname, 0, &datum );
  if(ret > 0 )
    ret = datum.rrt;
  return ret;
} 

int
tpingthost( const char *hostname, int t )
{
  int ret;
  struct ping_priv datum = ping_priv_default();

  ret = myping( hostname, t, &datum );
  if(ret > 0 )
    ret = datum.rrt;
  return ret;
}

