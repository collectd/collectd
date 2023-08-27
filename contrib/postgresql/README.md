Motivation for that second possible postgresql layout:
------------------------------------------------------

The first layout from Sebastian 'tokkee' Harl is like that:

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

The ID connects the two tables. The plugin, plugin_inst, type and tpye_inst
create s so called identifier. The timestamp, name and value get inserted into
the value table.

collectd/postgresql calles the collectd_insert function.
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

This seems to represents the user_data_t/notification_t structure.
https://github.com/collectd/collectd/blob/ef1e157de1a4f2cff10f6f902002066d0998232c/src/daemon/plugin.h#L172

Lets take the ping plugin as an example. It collects 3 values: ping, ping_stddev, ping_droprate.

The current structure creates 3 identifiers and 3 lines for each entry. The identifiers get reused. It reports "192.168.myping.ip" as type.

To draw a diagram with e.g. grafana i would like all 3 values near each other for that host that i am pinging. See the graph in the wiki. The current setup must join through all collected values to scrap the ping values out of it. Each value must do the same again because it has an other identifier.

This second setup creates two tables:

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

The instance ID get reused. The plugin data get its own table. All relevant measurement values are on one line. Get out the data is much more easy.

What could get argued is that i must admit, maybe take the creation of the instance table, sequence out of the collectd_insert function.

The type, type_inst and value_name get used to create the name of the value volumn. The impl_location() function handles this "data anomalies" like the ping plugin.

Description:
------------

My development was done on postgresql 15.

It has some advantages: The data has much higher data locality as it stays in one table and much less unneeded text columns.
This leads to much smaller table spaces. In my case the first setup created about 300 MB per day. The new setup about 50 MB with the advantage of depending data near each other.
You can also think about changing the datatype of the plugin_$plugin table to real. Just think if you realy need the double precission that double vs real. This just cuts the needed space in half.

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
The function collectd_insert() creates all tables and columns by itself.
1. The instance table consists of host/plugin/plugin_inst
2. The plugin_$plugin table (e.g. plugin_apache) contain all data for that plugin. The function collectd_insert() inserts the value into the column that its type/type_inst/name determines. There is one sad thing about collectd. The times that are submitted dont match 100%, so there is a epsilon (0.5 sec) that is used to check to what row a value belongs. If the column is not yet present it is added by this function.

The function impl_location() removes some data anomalies that are there when the data get submitted. There is a default that matches most cases. The plugins cpufreq, ping and memory get their names, plugin_inst get adjusted.

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

The procedure collectd_cleanup() is the maintainance function. It has as an argument the number of days where to keep the data. It can be called by pgagent or a similar mechanism like "CALL collectd_cleanup(180)". This delete all data that is older than 180 days.
