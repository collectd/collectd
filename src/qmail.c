/**
 * src/qmail_queue.c
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
 *   Alessandro Iurlano <alessandro.iurlano@gmail.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"       

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>



static char * qmail_base_dir;

static const char *config_keys[] =
{
        "QmailDir"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);


static void qmail_queue_submit (gauge_t queued_messages, gauge_t todo_messages)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = queued_messages;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE (values);
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, "qmail_queue");
  strncpy (vl.plugin_instance, "messages",
          sizeof (vl.plugin_instance));


  plugin_dispatch_values ("gauge", &vl);

  values[0].gauge = todo_messages;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE (values);
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, "qmail_queue");
  strncpy (vl.plugin_instance, "todo",
          sizeof (vl.plugin_instance));

  plugin_dispatch_values ("gauge", &vl);
}

#define MAX_PATH_LEN 4096

static int count_files_in_dir(char * path) {
  char *buf, *ebuf, *cp;
  off_t base;
  size_t bufsize;
  int fd, nbytes;
  struct stat sb;
  struct dirent *dp;
  int count=0;

  if ((fd = open(path, O_RDONLY)) < 0) {
    ERROR("cannot open %s", path);
    return -1;
  }
  if (fstat(fd, &sb) < 0) {
    ERROR("fstat");
    return -1;
  }
  bufsize = sb.st_size;
  if (bufsize < sb.st_blksize)
    bufsize = sb.st_blksize;
  if ((buf = malloc(bufsize)) == NULL) {
    ERROR("cannot malloc %lu bytes", (unsigned long)bufsize);
    return -1;
  }
  while ((nbytes = getdirentries(fd, buf, bufsize, &base)) > 0) {
    ebuf = buf + nbytes;
    cp = buf;
    while (cp < ebuf) {
      dp = (struct dirent *)cp;
      if ( (dp->d_fileno!=0) && (dp->d_type!=DT_DIR) ) {
	count++;
      }
      /*
	if (dp->d_fileno != 0)
	printf("%s\n", dp->d_name);*/
      cp += dp->d_reclen;
    }
  }
  if (nbytes < 0) {
    ERROR("getdirentries");
    return -1;
  }
  free(buf);
  close(fd);

  return count;
}
static int count_files_in_tree(char * path) {

  char *buf, *ebuf, *cp;
  off_t base;
  size_t bufsize;
  int fd, nbytes;
  struct stat sb;
  struct dirent *dp;
  int files_in_tree=0;
  int path_len=strlen(path);
  if ((fd = open(path, O_RDONLY)) < 0) {
    WARNING("cannot open %s", path);
    return -1;
  }
  if (fstat(fd, &sb) < 0) {
    WARNING( "fstat");
    return -1;
  }
  bufsize = sb.st_size;
  if (bufsize < sb.st_blksize)
    bufsize = sb.st_blksize;
  if ((buf = malloc(bufsize)) == NULL) {
    ERROR("cannot malloc %lu bytes", (unsigned long)bufsize);
    return -1;
  }
  while ((nbytes = getdirentries(fd, buf, bufsize, &base)) > 0) {
    ebuf = buf + nbytes;
    cp = buf;
    while (cp < ebuf) {
      int ret_value;
      dp = (struct dirent *)cp;
      //WARNING("Looking file %s\n", dp->d_name);
      if ((dp->d_fileno!=0) && (dp->d_type==DT_DIR) && strcmp(dp->d_name,".") && strcmp(dp->d_name,"..") ) {
	snprintf(path+path_len,MAX_PATH_LEN-path_len,"%s",dp->d_name);
	ret_value=count_files_in_dir(path);
	if (ret_value!=-1)
	  files_in_tree+=ret_value;
	else
	  return -1;
      }
      /*
	if (dp->d_fileno != 0)
	printf("%s\n", dp->d_name);*/
      cp += dp->d_reclen;
    }
  }
  if (nbytes < 0) {
    ERROR("getdirentries");
    return -1;
  }
  free(buf);
  close(fd);
  return files_in_tree;  
}

static int queue_len_read (void)
{
  char path[MAX_PATH_LEN];
  int path_len;
  //  struct dirent *root_entry, *node_entry;
  int messages_in_queue, messages_todo;
  
  messages_in_queue=0;
  messages_todo=0;
  snprintf(path,MAX_PATH_LEN,"%s/queue/mess/",qmail_base_dir);
  //WARNING("PATH TO READ: %s\n",path);
  path_len=strlen(path);
  if (path[path_len]!='/') {
    strcat(path,"/");
    path_len=strlen(path);
  }

  messages_in_queue=count_files_in_tree(path);

  snprintf(path,MAX_PATH_LEN,"%s/queue/todo/",qmail_base_dir);
  //WARNING("PATH TO READ: %s\n",path);
  messages_todo=count_files_in_tree(path);
  if ( (messages_todo!=-1) && (messages_in_queue!=-1) ) {
    qmail_queue_submit(messages_in_queue,messages_todo);
    return 0;
  }
  else return -1;
}

static int qmail_config (const char *key, const char *val)
{
   if (strcasecmp ("QmailDir", key) == 0)
     {
        free(qmail_base_dir);
        qmail_base_dir=strdup(val);
	WARNING("Setting Qmail base dir to %s\n", qmail_base_dir);
     }
}

void module_register (void)
{
  qmail_base_dir=strdup("/var/qmail");
  plugin_register_config ("qmail_queue", qmail_config,
                        config_keys, config_keys_num);
 
  plugin_register_read ("qmail_queue", queue_len_read);
} /* void module_register */
