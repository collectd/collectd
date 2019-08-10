/*
 * Some systems lack full limit definitions.
 */
#ifndef OPEN_MAX
# define OPEN_MAX 256
#endif

#ifndef HAVE_CLOSEFROM
void closefrom(int);
#endif
