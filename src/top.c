/**
 * collectd - src/top.c
 * Copyright (C) 2012  Cyril Feraudet
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Cyril Feraudet <cyril at feraudet.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#if KERNEL_LINUX
#include <pwd.h>
#include <unistd.h>
#elif KERNEL_LINUX || KERNEL_SOLARIS
#include <grp.h>
#else
#error "Platform not supported"
#endif
#if KERNEL_SOLARIS
#include <procfs.h>
#endif

typedef struct stat_s
{
  int pid; // %d 
  char comm[256]; // %s
  char state; // %c
  int ppid; // %d
  int pgrp; // %d
  int session; // %d
  int tty_nr; // %d
  int tpgid; // %d
  unsigned long flags; // %lu
  unsigned long minflt; // %lu
  unsigned long cminflt; // %lu
  unsigned long majflt; // %lu
  unsigned long cmajflt; // %lu
  unsigned long utime; // %lu
  unsigned long stime; // %lu
  long cutime; // %ld
  long cstime; // %ld
  long priority; // %ld
  long nice; // %ld
  long num_threads; // %ld
  long itrealvalue; // %ld
  unsigned long starttime; // %lu
  unsigned long vsize; // %lu
  long rss; // %ld
  unsigned long rlim; // %lu
  unsigned long startcode; // %lu
  unsigned long endcode; // %lu
  unsigned long startstack; // %lu
  unsigned long kstkesp; // %lu
  unsigned long kstkeip; // %lu
  unsigned long signal; // %lu
  unsigned long blocked; // %lu
  unsigned long sigignore; // %lu
  unsigned long sigcatch; // %lu
  unsigned long wchan; // %lu
  unsigned long nswap; // %lu
  unsigned long cnswap; // %lu
  int exit_signal; // %d
  int processor; // %d
} stat_t;

typedef struct status_s
{
  char Name[256]; // tcsh
  char State; // S (sleeping)
  unsigned long SleepAVG; //  98%
  unsigned long Tgid; //  20616
  unsigned long Pid; //  20616
  unsigned long PPid; //  20612
  unsigned long TracerPid; //  0
  unsigned long Uid[4]; //  418 418 418 418
  unsigned long Gid[4]; //  30  30  30  30
  unsigned long FDSize; //  64
  unsigned long Groups[16]; //  30 118 121 136 148 260 262 724 728 60045 60053 60072 600159 600217 600241 600245 
  unsigned long VmPeak; //     64732 kB
  unsigned long VmSize; //     64700 kB
  unsigned long VmLck; //         0 kB
  unsigned long VmHWM; //      1756 kB
  unsigned long VmRSS; //      1756 kB
  unsigned long VmData; //      1112 kB
  unsigned long VmStk; //       348 kB
  unsigned long VmExe; //       320 kB
  unsigned long VmLib; //      1496 kB
  unsigned long VmPTE; //        68 kB
  unsigned long StaBrk; //  0871a000 kB
  unsigned long Brk; //  0879b000 kB
  unsigned long StaStk; //  7fff6d0ccc70 kB
  unsigned long Threads; //  1
  unsigned long SigQ[2]; //  1/16368
  unsigned long SigPnd; //  0000000000000000
  unsigned long ShdPnd; //  0000000000000000
  unsigned long SigBlk; //  0000000000000002
  unsigned long SigIgn; //  0000000000384004
  unsigned long SigCgt; //  0000000009812003
  unsigned long CapInh; //  0000000000000000
  unsigned long CapPrm; //  0000000000000000
  unsigned long CapEff; //  0000000000000000
  unsigned long Cpus_allowed[8]; //  00000000,00000000,00000000,00000000,00000000,00000000,00000000,000000ff
  unsigned long Mems_allowed[2]; //  00000000,00000001
} status_t;

const char *statformat = "%d %s %c %d %d %d %d %d %lu %lu %lu %lu %lu %lu"
        " %lu %ld %ld %ld %ld %ld %ld %lu %lu %ld %lu %lu %lu %lu %lu %lu %lu"
        " %lu %lu %lu %lu %lu %lu %d %d";

#if KERNEL_LINUX

static int getStat (int pid, stat_t *s)
{

  char buf[256];
  FILE *proc;
  sprintf (buf, "/proc/%d/stat", pid);
  proc = fopen (buf, "r");
  if (proc)
    {
      if (39 == fscanf (proc, statformat,
                        &s->pid, s->comm, &s->state, &s->ppid, &s->pgrp, &s->session,
                        &s->tty_nr, &s->tpgid, &s->flags, &s->minflt, &s->cminflt,
                        &s->majflt, &s->cmajflt, &s->utime, &s->stime, &s->cutime,
                        &s->cstime, &s->priority, &s->nice, &s->num_threads, &s->itrealvalue,
                        &s->starttime, &s->vsize, &s->rss, &s->rlim, &s->startcode, &s->endcode,
                        &s->startstack, &s->kstkesp, &s->kstkeip, &s->signal, &s->blocked,
                        &s->sigignore, &s->sigcatch, &s->wchan, &s->nswap, &s->cnswap,
                        &s->exit_signal, &s->processor)
          )
        {
          fclose (proc);
          return 1;
        } else
        {
          fclose (proc);
          return 0;
        }
    } else
    {
      return 0;
    }
}

static int getStatus (int pid, status_t *s)
{
  int i;
  char name[256], *status;
  char buf[256];
  FILE *proc;
  sprintf (name, "/proc/%d/status", pid);
  proc = fopen (name, "r");
  if (proc)
    {
      status = fgets (buf, 256, proc);
      sscanf (buf, "Name:\t%s", s->Name);
      status = fgets (buf, 256, proc);
      sscanf (buf, "State:\t%c", &s->State);
      status = fgets (buf, 256, proc);
      if (sscanf (buf, "SleepAVG:\t%lu", &s->SleepAVG))
        {
          status = fgets (buf, 256, proc);
        }
      sscanf (buf, "Tgid:\t%lu", &s->Tgid);
      status = fgets (buf, 256, proc);
      sscanf (buf, "Pid:\t%lu", &s->Pid);
      status = fgets (buf, 256, proc);
      sscanf (buf, "PPid:\t%lu", &s->PPid);
      status = fgets (buf, 256, proc);
      sscanf (buf, "TracerPid:\t%lu", &s->TracerPid);
      status = fgets (buf, 256, proc);
      sscanf (buf, "Uid:\t%lu\t%lu\t%lu\t%lu", s->Uid, s->Uid + 1, s->Uid + 2, s->Uid + 3);
      status = fgets (buf, 256, proc);
      sscanf (buf, "Gid:\t%lu\t%lu\t%lu\t%lu", s->Gid, s->Gid + 1, s->Gid + 2, s->Gid + 3);
      status = fgets (buf, 256, proc);
      sscanf (buf, "FDSize:\t%lu", &s->FDSize);
      status = fgets (buf, 256, proc);
      for (i = 0; i < 16; i++)
        {
          s->Groups[i] = 0;
        }
      i = sscanf (buf, "Groups:\t%ld%ld%ld%ld%ld%ld%ld%ld%ld%ld%ld%ld%ld%ld%ld%ld",
                  s->Groups, s->Groups + 1, s->Groups + 2, s->Groups + 3, s->Groups + 4,
                  s->Groups + 5, s->Groups + 6, s->Groups + 7, s->Groups + 8, s->Groups + 9,
                  s->Groups + 10, s->Groups + 11, s->Groups + 12, s->Groups + 13, s->Groups + 14,
                  s->Groups + 15);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmPeak:\t%lu", &s->VmPeak);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmSize:\t%lu", &s->VmSize);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmLck:\t%lu", &s->VmLck);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmHWM:\t%lu", &s->VmHWM);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmRSS:\t%lu", &s->VmRSS);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmData:\t%lu", &s->VmData);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmStk:\t%lu", &s->VmStk);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmExe:\t%lu", &s->VmExe);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmLib:\t%lu", &s->VmLib);
      status = fgets (buf, 256, proc);
      sscanf (buf, "VmPTE:\t%lu", &s->VmPTE);
      status = fgets (buf, 256, proc);
      sscanf (buf, "StaBrk:\t%lx", &s->StaBrk);
      status = fgets (buf, 256, proc);
      sscanf (buf, "Brk:\t%lx", &s->Brk);
      status = fgets (buf, 256, proc);
      sscanf (buf, "StaStk:\t%lx", &s->StaStk);
      status = fgets (buf, 256, proc);
      sscanf (buf, "Threads:\t%lu", &s->Threads);
      status = fgets (buf, 256, proc);
      sscanf (buf, "SigQ:\t%lu/%lu", s->SigQ, s->SigQ + 1);
      status = fgets (buf, 256, proc);
      sscanf (buf, "SigPnd:\t%lx", &s->SigPnd);
      status = fgets (buf, 256, proc);
      sscanf (buf, "ShdPnd:\t%lx", &s->ShdPnd);
      status = fgets (buf, 256, proc);
      sscanf (buf, "SigBlk:\t%lx", &s->SigBlk);
      status = fgets (buf, 256, proc);
      sscanf (buf, "SigIgn:\t%lx", &s->SigIgn);
      status = fgets (buf, 256, proc);
      sscanf (buf, "SigCgt:\t%lx", &s->SigCgt);
      status = fgets (buf, 256, proc);
      sscanf (buf, "CapInh:\t%lx", &s->CapInh);
      status = fgets (buf, 256, proc);
      sscanf (buf, "CapPrm:\t%lx", &s->CapPrm);
      status = fgets (buf, 256, proc);
      sscanf (buf, "CapEff:\t%lx", &s->CapEff);
      status = fgets (buf, 256, proc);
      for (i = 0; i < 8; i++)
        {
          s->Cpus_allowed[i] = 0;
        }
      sscanf (buf, "Cpus_allowed:\t%lx,%lx,%lx,%lx,%lx,%lx,%lx,%lx",
              s->Cpus_allowed, s->Cpus_allowed + 1,
              s->Cpus_allowed + 2, s->Cpus_allowed + 3,
              s->Cpus_allowed + 4, s->Cpus_allowed + 5,
              s->Cpus_allowed + 6, s->Cpus_allowed + 7);
      status = fgets (buf, 256, proc);
      sscanf (buf, "Mems_allowed:\t%lx,%lx", &(s->Mems_allowed[0]), &(s->Mems_allowed[1]));
      fclose (proc);
      return 1;
    } else
    {
      return 0;
    }
}

#elif KERNEL_SOLARIS

static int getStat (int pid, stat_t *s)
{
  char f_status[64];
  char f_psinfo[64];
  char *buffer;

  pstatus_t *myStatus;
  psinfo_t *myInfo;

  sprintf (f_status, "/proc/%d/status", pid);
  sprintf (f_psinfo, "/proc/%d/psinfo", pid);

  buffer = malloc (sizeof (pstatus_t));
  memset (buffer, 0, sizeof (pstatus_t));
  read_file_contents (f_status, buffer, sizeof (pstatus_t));
  myStatus = (pstatus_t *) buffer;

  buffer = malloc (sizeof (psinfo_t));
  memset (buffer, 0, sizeof (psinfo_t));
  read_file_contents (f_psinfo, buffer, sizeof (psinfo_t));
  myInfo = (psinfo_t *) buffer;

  s->pid = myInfo->pr_pid;
  s->ppid = myInfo->pr_ppid;
  s->rss = myInfo->pr_rssize * 1024;
  s->stime = myStatus -> pr_stime.tv_sec;
  s->utime = myStatus -> pr_utime.tv_sec;

  sfree (myStatus);
  sfree (myInfo);

  return (1);
}

static int getStatus (int pid, status_t *s)
{

  char f_psinfo[64];
  char *buffer; 


  psinfo_t *myInfo;

  sprintf (f_psinfo, "/proc/%d/psinfo", pid);

  buffer = malloc (sizeof (psinfo_t));
  memset (buffer, 0, sizeof (psinfo_t));
  read_file_contents (f_psinfo, buffer, sizeof (psinfo_t));
  myInfo = (psinfo_t *) buffer;

  sstrncpy (s->Name, myInfo->pr_fname, sizeof (myInfo->pr_fname));  
  s->Uid[1] = myInfo->pr_euid;
  s->Gid[1] = myInfo->pr_egid;
  
  sfree (myInfo);
  return (1);
}
#endif

static int top_read (void)
{
  struct dirent **namelist;
  int n;
  n = scandir ("/proc", &namelist, 0, alphasort);
  if (n < 0)
    perror ("scandir");
  else
    {
      int hz;
      hz = sysconf (_SC_CLK_TCK);
      char *bufferout;
      notification_t notif;
      memset (&notif, '\0', sizeof (n));
      bufferout = malloc (n * sizeof (char) * 256);
      *bufferout = '\0';
      //printf("pid ppid    uid user gid group rss stime    utime  name\n");
      while (n--)
        {
          if (atoi (namelist[n]->d_name))
            {
              stat_t *stat;              
              stat = malloc (sizeof (stat_t));
              //if (getStat (atoi (namelist[n]->d_name), stat) == 0 || stat->ppid == 2 || stat->ppid == 0) {
              if (getStat (atoi (namelist[n]->d_name), stat) == 0)
                {
                  free (namelist[n]);
                  free (stat);
                  continue;
                }             
              status_t *status;
              status = malloc (sizeof (status_t));
              if (getStatus (atoi (namelist[n]->d_name), status) == 0)
                {
                  free (namelist[n]);
                  free (stat);
                  free (status);
                }              
              char buf[256];
              struct passwd *pwd;
              pwd = getpwuid (status->Uid[1]);              
              struct group *grp;
              grp = getgrgid (status->Gid[1]);
#if KERNEL_LINUX
              snprintf (buf, sizeof (buf), "%d %d %lu %s %lu %s %ld %ld %ld %s\n",
                        stat->pid, stat->ppid, status->Uid[1], pwd->pw_name, status->Gid[1],
                        grp->gr_name, stat->rss, stat->stime * 100 / hz,
                        stat->utime * 100 / hz, status->Name);
#elif KERNEL_SOLARIS
           snprintf (buf, sizeof (buf), "%d %d %lu %s %lu %s %ld %ld %ld %s\n",
                        stat->pid, stat->ppid, status->Uid[1], pwd->pw_name, status->Gid[1],
                        grp->gr_name, stat->rss, stat->stime,
                        stat->utime, status->Name);   
#endif
              free (namelist[n]);
              free (stat);
              free (status);
              strncat (bufferout, buf, sizeof (buf));
            }
        }      
      DEBUG("\n%s", bufferout);
      notif.severity = NOTIF_OKAY;
      notif.time = cdtime ();
      sstrncpy (notif.host, hostname_g, sizeof (notif.host));
      sstrncpy (notif.plugin, "top", sizeof (notif.plugin));
      sstrncpy (notif.type, "ps", sizeof (notif.type));
      sstrncpy (notif.plugin_instance, "", sizeof(notif.plugin_instance));
      sstrncpy (notif.message, bufferout, sizeof (notif.message));      
      plugin_dispatch_notification (&notif);      
      free (bufferout);
    }
  free (namelist);  
  return 0;
}

void module_register (void)
{
  plugin_register_read ("top", top_read);
}

