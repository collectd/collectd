PostreSQL example databases:

    collectd_insert.sql
    collectd_insert2.sql

The first database layout, from Sebastian 'tokkee' Harl, is like this:

```
+-------------------+  +----------------+
|Identifiers        |  |values          |
+-------------------+  +----------------+
|ID          int   <-- >ID        int   |
|plugin      text   |  |tstamp    time  |
|plugin_inst text   |  |name      text  |
|type        text   |  |value     double|
|type_inst   text   |  |                |
+-------------------+  +----------------+
```

The ID connects the two tables. The plugin, plugin_inst, type and type_inst
create a so-called identifier. The timestamp, name and value get inserted into
the value table.

collectd/postgresql calls the collectd_insert function.
```
	collectd_insert(timestamp with time zone,	-- tstamp
			character varying,		-- host
			character varying,		-- plugin
			character varying,		-- plugin_inst
			character varying,		-- type
			character varying,		-- type_inst
			character varying[],		-- value_name
			character varying[],		-- type_name
			double precision[])		-- values
```

This seems to represent the user_data_t/notification_t structure.
https://github.com/collectd/collectd/blob/ef1e157de1a4f2cff10f6f902002066d0998232c/src/daemon/plugin.h#L172

Let's take the ping plugin as an example. It collects 3 values: ping, ping_stddev, ping_droprate.

The current structure creates 3 identifiers and 3 lines for each entry. The identifiers get reused. It reports "192.168.200.123" as type.

To draw a diagram with e.g. grafana I would like all 3 values near each other for that host that I am pinging. See the graph in the wiki. The current setup must join through all collected values to scrap the ping values out of it. Each value must do the same again because it has another identifier.


Description:
------------

Second database layout is done on postgresql 15, by Georg Gast.

It has some advantages over first one: The data has much higher data locality as it stays in one table and has fewer text columns.
This leads to much smaller table spaces. In my case the first setup created about 300 MB per day. The new setup creates about 50 MB with better data-locality.
You can also think about changing the datatype of the plugin_$plugin table to real. Consider whether you really need the double precision compared to real as latter would cut the needed space in half.

```
+--------------------+  +--------------------+
|Instance            |  |plugin_ping         |
+--------------------+  +--------------------+
|ID          int    <-- >ID            int   |
|plugin      text    |  |tstamp        time  |
|plugin_inst text    |  |ping          double|
|                    |  |ping_stddev   double|
|                    |  |ping_droprate double|
|                    |  |                    |
+--------------------+  +--------------------+
```

The instance ID get reused. The plugin data get its own table. All relevant measurement values are on one line. Getting the data out is much easier.

The type, type_inst and value_name get used to create the name of the value column. The impl_location() function handles this "data anomaly" like the ping plugin.


Sample configuration:
---------------------
```

<Plugin postgresql>
    <Writer sqlstore>
        Statement "SELECT collectd_insert($1, $2, $3, $4, $5, $6, $7, $8, $9);"
    </Writer>
    <Database collectd>
        Host "127.0.0.1"
        Port 5432
        User collector
        Password "mypassword"
        SSLMode "prefer"
        Writer sqlstore
    </Database>
</Plugin>
```
Please make sure that your database user (in this collector) has the rights to create tables, insert and update. The user that drops data must have the delete right.

Function description:
---------------------
There is one sad thing about collectd. The times that are submitted do not match 100%, so there is an epsilon (0.5 sec) that is used to check to what row a value belongs to.
1. The procedure collectd_insert() inserts the values into the incoming table and realigns the timestamps. It also creates the instances in the instance table.
2. The collected data gets moved from the incoming table to the destination tables by the procedure move_data_to_table().

The function impl_location() removes some data anomalies that are there when the data get submitted. There is a default that matches most cases. The plugins cpufreq, ping and memory get their names, plugin_inst get adjusted.

The plugin_$plugin table (e.g. plugin_apache) then contains all the data for that plugin.  If the column is not yet present it is added by this function.
The procedure move_data_to_table() must be called periodically. In my case by a cron job.

```
root@www-collectd:/etc/cron.d# cat collectd
# Example of job definition:
# .---------------- minute (0 - 59)
# |  .------------- hour (0 - 23)
# |  |  .---------- day of month (1 - 31)
# |  |  |  .------- month (1 - 12) OR jan,feb,mar,apr ...
# |  |  |  |  .---- day of week (0 - 6) (Sunday=0 or 7) OR sun,mon,tue,wed,thu,fri,sat
# |  |  |  |  |
# *  *  *  *  * user-name command to be executed
  0  0  1  *  * root      /opt/collectd/etc/clean-tables.sh
*/1  *  *  *  * root      /opt/collectd/etc/move-data.sh
```

The procedure collectd_cleanup() is the maintenance function. It has as an argument the number of days where to keep the data. It can be called by pgagent or a similar mechanism like "CALL collectd_cleanup(180)". This delete all data that is older than 180 days.


My tested plugins are:
- apache
- cpu
- cpufreq
- df
- disk
- entropy
- interface
- irq
- load
- memory
- network
- openvpn
- ping
- postgresql
- processes
- sensors
- thermal
- uptime
- users
- wireless

