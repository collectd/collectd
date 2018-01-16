# Gluster-Collectd Plugin

Gluster collectd plugin is based on collectd python plugin. The gluster collectd plugin collects metrics about

  - glusterd, glusterfs process cpu and memory
  - io statistics and brick utilization 
  - volume statistics (heal entries)

# Prerequisites
Install the python plugin for collectd and also dependency packages. gluster collectd plugin is based on collectd python plugin. 
  - yum install collectd-python
  - pip install -r gluster_collectd/requirements.txt
# RPM Installation.
- #git clone https://github.com/vredara/collectd.git
- #cd src/gluster_collectd
- ./makerpm.sh
- rpm -ivh build/gluster_collectd.noarch.rpm

# Manual Installation (if rpm is not used)
 - mkdir /usr/lib64/collectd/gluster_collectd
 - copy gluster_collectd/src/*.py dir to /usr/lib64/collectd/gluster_collectd/ dir
 - copy gluster_collectd/conf/python.conf to /etc/collectd.d/ dir
 - copy gluster_collectd/types/types.db.gluster to /usr/share/collectd/types.db.gluster
### Run collectd plugin.
provide permissions to collectd process to create processes.
 - semanage permissive -a collectd_t
#service collectd restart 
if you are using write_log plugin then system journal will have gluster metrics.
