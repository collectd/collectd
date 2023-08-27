-- collectd - contrib/postgresql/collectd_insert2.sql
-- Copyright (C) 2012 Sebastian 'tokkee' Harl
-- Copyright (C) 2023 Georg Gast
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions
-- are met:
--
-- - Redistributions of source code must retain the above copyright
--   notice, this list of conditions and the following disclaimer.
--
-- - Redistributions in binary form must reproduce the above copyright
--   notice, this list of conditions and the following disclaimer in the
--   documentation and/or other materials provided with the distribution.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
-- POSSIBILITY OF SUCH DAMAGE.

-- Motivation for that second possible postgresql layout:
-- ------------------------------------------------------
--
-- The first layout from Sebastian 'tokkee' Harl is like that:
--
-- ```
-- +-------------------+  +----------------+
-- |Identifiers        |  |values          |
-- +-------------------+  +----------------+
-- |ID          int   <-- >ID        int   |
-- |plugin      text   |  |tstamp    time  |
-- |plugin_inst text   |  |name      text  |
-- |type        text   |  |value     double|
-- |type_inst   text   |  |                |
-- +-------------------+  +----------------+
-- ```
--
-- The ID connects the two tables. The plugin, plugin_inst, type and tpye_inst
-- create s so called identifier. The timestamp, name and value get inserted into
-- the value table.
--
-- collectd/postgresql calles the collectd_insert function.
-- ```
-- 	collectd_insert(timestamp with time zone,	-- tstamp
-- 			character varying,		-- host
-- 			character varying,		-- plugin
-- 			character varying,		-- plugin_inst
-- 			character varying,		-- type
-- 			character varying,		-- type_inst
-- 			character varying[],		-- value_name
-- 			character varying[],		-- type_name
-- 			double precision[])		-- values
-- ```
--
-- This seems to represents the user_data_t/notification_t structure.
-- https://github.com/collectd/collectd/blob/ef1e157de1a4f2cff10f6f902002066d0998232c/src/daemon/plugin.h#L172
--
-- Lets take the ping plugin as an example. It collects 3 values: ping, ping_stddev, ping_droprate.
--
-- The current structure creates 3 identifiers and 3 lines for each entry. The identifiers get reused. It reports "192.168.myping.ip" as type.
--
-- To draw a diagram with e.g. grafana i would like all 3 values near each other for that host that i am pinging. See the graph in the wiki. The current setup must join through all collected values to scrap the ping values out of it. Each value must do the same again because it has an other identifier.
--
-- This second setup creates two tables:
--
-- ```
-- +--------------------+  +--------------------+
-- |Instance            |  |plugin_ping         |
-- +--------------------+  +--------------------+
-- |ID          int    <-- >ID            int   |
-- |plugin      text    |  |tstamp        time  |
-- |plugin_inst text    |  |ping          double|
-- |                    |  |ping_stddev   double|
-- |                    |  |ping_droprate double|
-- |                    |  |                    |
-- +--------------------+  +--------------------+
-- ```
--
-- The instance ID get reused. The plugin data get its own table. All relevant measurement values are on one line. Get out the data is much more easy.
--
-- What could get argued is that i must admit, maybe take the creation of the instance table, sequence out of the collectd_insert function.
--
-- The type, type_inst and value_name get used to create the name of the value volumn. The impl_location() function handles this "data anomalies" like the ping plugin.
--
-- Description:
-- ------------
--
-- My development was done on postgresql 15.
--
-- It has some advantages: The data has much higher data locality as it stays in one table and much less unneeded text columns.
-- This leads to much smaller table spaces. In my case the first setup created about 300 MB per day. The new setup about 50 MB with the advantage of depending data near each other.
-- You can also think about changing the datatype of the plugin_$plugin table to real. Just think if you realy need the double precission that double vs real. This just cuts the needed space in half.
--
-- Sample configuration:
-- ---------------------
-- ```
--
-- <Plugin postgresql>
--     <Writer sqlstore>
--         Statement "SELECT collectd_insert($1, $2, $3, $4, $5, $6, $7, $8, $9);"
--     </Writer>
--     <Database collectd>
--         Host "127.0.0.1"
--         Port 5432
--         User collector
--         Password "mypassword"
--         SSLMode "prefer"
--         Writer sqlstore
--     </Database>
-- </Plugin>
-- ```
-- Please make sure that your database user (in this collector) has the rights to create tables, insert and update. The user that drops data must have the delete right.
--
-- Function description:
-- ---------------------
-- The function collectd_insert() creates all tables and columns by itself.
-- 1. The instance table consists of host/plugin/plugin_inst
-- 2. The plugin_$plugin table (e.g. plugin_apache) contain all data for that plugin. The function collectd_insert() inserts the value into the column that its type/type_inst/name determines. There is one sad thing about collectd. The times that are submitted dont match 100%, so there is a epsilon (0.5 sec) that is used to check to what row a value belongs. If the column is not yet present it is added by this function.
--
-- The function impl_location() removes some data anomalies that are there when the data get submitted. There is a default that matches most cases. The plugins cpufreq, ping and memory get their names, plugin_inst get adjusted.
--
-- My tested plugins are:
-- - apache
-- - cpu
-- - cpufreq
-- - df
-- - disk
-- - entropy
-- - interface
-- - irq
-- - load
-- - memory
-- - network
-- - openvpn
-- - ping
-- - postgresql
-- - processes
-- - sensors
-- - thermal
-- - uptime
-- - users
--
-- The procedure collectd_cleanup() is the maintainance function. It has as an argument the number of days where to keep the data. It can be called by pgagent or a similar mechanism like "CALL collectd_cleanup(180)". This delete all data that is older than 180 days.
--

CREATE PROCEDURE collectd_cleanup(IN days_to_keep integer)
    LANGUAGE plpgsql
    AS $$
DECLARE
	l RECORD;
	stmt text;
BEGIN
	for l in
		SELECT plugin
		FROM instance
		GROUP BY plugin
		ORDER BY plugin
	LOOP
		stmt = format('DELETE FROM plugin_%I WHERE now() - tstamp > ''%s days''::interval',
					 l.plugin, days_to_keep);
		RAISE INFO 'Stmt: %' , stmt;
		PERFORM stmt;
	END LOOP;
END;
$$;


CREATE FUNCTION collectd_insert(timestamp with time zone, character varying, character varying, character varying, character varying, character varying, character varying[], character varying[], double precision[]) RETURNS void
    LANGUAGE plpgsql
    AS $_$
DECLARE
    p_time alias for $1;
    p_host alias for $2;
    p_plugin alias for $3;
    p_plugin_inst alias for $4;
    p_type alias for $5;
    p_type_inst alias for $6;
    p_value_names alias for $7;
    -- don't use the type info; for 'StoreRates true' it's 'gauge' anyway
    -- p_type_names alias for $8;
    p_values alias for $9;
    ds_id integer;
    i integer;

	l RECORD;
	stmt text;
	epsilon interval;
	tstamp_l timestamp with time zone;
	tstamp_h timestamp with time zone;

BEGIN
	epsilon = '0.5 seconds'::interval;
	SELECT p_time - epsilon INTO tstamp_l;
	SELECT p_time + epsilon INTO tstamp_h;

	CREATE TABLE IF NOT EXISTS instance
	(
		id	bigint NOT NULL UNIQUE PRIMARY KEY,
		host text NOT NULL,
		plugin text NOT NULL,
		plugin_inst text NOT NULL,

	    CONSTRAINT instance_uniq UNIQUE (host, plugin, plugin_inst)
	);

	CREATE SEQUENCE IF NOT EXISTS instance_id_seq
		INCREMENT 1
		START 1
		MINVALUE 1
		MAXVALUE 9223372036854775807
		CACHE 1
		OWNED BY instance.id;

	ALTER TABLE instance ALTER COLUMN id SET DEFAULT nextval('instance_id_seq'::regclass);

	i := 1;
    LOOP
        EXIT WHEN i > array_upper(p_value_names, 1);

		if p_values[i]  = 'NaN'::double precision THEN
			i := i + 1;
			continue;
		end if;

		SELECT * FROM impl_location(p_plugin, p_plugin_inst, p_type, p_type_inst, p_value_names[i])
		INTO l;

		-- create the plugin table
		stmt = format(
			'
				CREATE TABLE IF NOT EXISTS %I
				(
					id	bigint NOT NULL,
					tstamp timestamp with time zone NOT NULL,

					FOREIGN KEY (id) REFERENCES instance (id)
				);
			', l.tbl);
		EXECUTE stmt;

		-- RAISE INFO 'L1';

		-- create the tstamp_id_idx
		stmt = format(
			'
				CREATE INDEX IF NOT EXISTS %s_tstamp_id_idx
				ON %I USING brin
				(tstamp,id);
			',l.tbl, l.tbl);
		EXECUTE stmt;
		-- RAISE INFO 'L2';

		-- add the column to the table
		stmt = format(
			'ALTER table %I ADD COLUMN IF NOT EXISTS %I double precision DEFAULT NULL;',
				l.tbl, l.tbl_col);
		EXECUTE stmt;
		-- RAISE INFO 'L3';

		-- insert the instance if it doesnt exists
		INSERT INTO instance (host,plugin,plugin_inst)
			SELECT p_host, p_plugin,l.corr_plugin_inst
			WHERE NOT EXISTS
			(
				SELECT 1 FROM instance
				WHERE host = p_host AND plugin = p_plugin AND plugin_inst = l.corr_plugin_inst
			);
		-- RAISE INFO 'L4';

		-- get the id from the instance. I tmust exist now
		SELECT id INTO ds_id
        FROM instance
        WHERE host = p_host AND plugin = p_plugin AND plugin_inst = l.corr_plugin_inst;

		-- RAISE INFO 'id=%', ds_id;

		-- insert or update the values (anti-join: TAOP book)
		--INSERT into l.tbl
		stmt = format(
			'
				WITH upd AS
				(
					UPDATE %I
					SET %I=%L
					WHERE 	%L <= tstamp AND	tstamp < %L
							AND id=%L
					returning 1
				),
				ins AS (
					INSERT INTO %I (tstamp,id,%I)
					SELECT %L,%L,%L
					WHERE NOT EXISTS
					(
						SELECT 1 FROM %I
						WHERE 	%L <= tstamp AND	tstamp < %L
								AND id=%L
					)
					returning 1
				)
				select (select count(*) from upd) as updates,
					   (select count(*) from ins) as inserts;

			',	l.tbl,
				l.tbl_col, p_values[i],
				tstamp_l, tstamp_h,
				ds_id,

				l.tbl, l.tbl_col,
				p_time, ds_id, p_values[i],
				l.tbl,
				tstamp_l, tstamp_h, ds_id
			);
		-- RAISE INFO 'L5: %', stmt;
		EXECUTE stmt;

		-- RAISE INFO 'L6';

		-- continue the loop
        i := i + 1;
    END LOOP;


END;
$_$;


CREATE FUNCTION impl_location(v_plugin text, v_plugin_inst text, v_type text, v_type_inst text, v_name text) RETURNS TABLE(tbl text, corr_plugin_inst text, tbl_col text)
    LANGUAGE plpgsql
    AS $$
DECLARE
	v_tbl				text;
	v_corr_plugin_inst	text;
	v_tblcol			text;
BEGIN
	-- bring the data anaomalies into shape
	CASE
		WHEN (v_plugin = 'cpufreq' AND v_type='percent') THEN
			v_tbl		 		= 'plugin_' || v_plugin;
			v_corr_plugin_inst	= v_type || '_' || v_plugin_inst || '_' || v_type_inst;
			v_tblcol 			= v_type;
		WHEN (v_plugin = 'cpufreq' AND v_type='cpufreq') THEN
			v_tbl		 		= 'plugin_' || v_plugin;
			v_corr_plugin_inst	= v_type || '_' || v_plugin_inst;
			v_tblcol 			= v_type;
		WHEN (v_plugin = 'ping') THEN
			v_tbl		 		= 'plugin_' || v_plugin;
			v_corr_plugin_inst	= v_type_inst;
			v_tblcol 			= v_type;
		WHEN (v_plugin = 'memory') THEN
			v_tbl		 		= 'plugin_' || v_plugin;
			v_corr_plugin_inst	= NULL;
			v_tblcol 			= v_type_inst;
		ELSE
			-- default case
			v_tbl		 		= 'plugin_' || v_plugin;
			v_corr_plugin_inst	= v_plugin_inst;
			v_tblcol 			= v_type ||
									COALESCE( '_' || v_type_inst, '') ||
									CASE WHEN v_name='''value''' OR v_name='value' THEN
										''
									ELSE
										'_' || v_name
									END CASE;
	END CASE;

	-- replace unwanted chars
	v_tbl = replace(v_tbl, '''' ,'');
	v_corr_plugin_inst = replace(v_corr_plugin_inst, '''' ,'');
	v_tblcol = replace(v_tblcol, '''' ,'');

	v_tbl = replace(v_tbl, '-' ,'_');
	v_corr_plugin_inst = replace(v_corr_plugin_inst, '-' ,'_');
	v_tblcol = replace(v_tblcol, '-' ,'_');


	RETURN QUERY
		SELECT	v_tbl, COALESCE(v_corr_plugin_inst,''), v_tblcol;

END;

$$;
