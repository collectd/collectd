#ifndef SETUP_H
#define SETUP_H

#ifdef HAVE_SYS_TIMES_H
# include <sys/times.h>
#endif /*HAVE_SYS_TIMES_H*/
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif /*TIME_WITH_SYS_TIME*/

#ifndef  TRUE
# define TRUE           1
#endif /*TRUE*/
#ifndef  FALSE
# define FALSE          0 
#endif /*FALSE*/

#ifndef  EXIT_SUCCESS
# define EXIT_SUCCESS   0
#endif /*EXIT_SUCESS*/
#ifndef  EXIT_FAILURE
# define EXIT_FAILURE   1
#endif /*EXIT_FAILURE*/ 

#endif/*SETUP_H*/
