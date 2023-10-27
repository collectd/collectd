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

------------------------------------------------------------------
-- IMPORTANT           Please read the README.md
------------------------------------------------------------------

--CREATE EXTENSION tablefunc;

CREATE TABLE IF NOT EXISTS instance
(
    id bigserial,
    host text COLLATE pg_catalog."default" NOT NULL,
    plugin text COLLATE pg_catalog."default" NOT NULL,
    plugin_inst text COLLATE pg_catalog."default" NOT NULL,
    CONSTRAINT instance_pkey PRIMARY KEY (id),
    CONSTRAINT instance_uniq UNIQUE (host, plugin, plugin_inst)
);


CREATE TABLE IF NOT EXISTS incoming
(
    tstamp timestamp with time zone,
    id bigint,
    tbl text COLLATE pg_catalog."default",
    tbl_col text COLLATE pg_catalog."default",
    val real,
    CONSTRAINT incoming_id_fkey FOREIGN KEY (id)
        REFERENCES public.instance (id) MATCH SIMPLE
        ON UPDATE NO ACTION
        ON DELETE NO ACTION
);





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


CREATE OR REPLACE PROCEDURE collectd_insert(
	IN timestamp with time zone,
	IN character varying,
	IN character varying,
	IN character varying,
	IN character varying,
	IN character varying,
	IN character varying[],
	IN character varying[],
	IN double precision[])
LANGUAGE 'plpgsql'
AS $BODY$
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

	-- working variables
	i integer;
	line RECORD;
	ds_id integer;
	epsilon interval;
	tstamp_l timestamp with time zone;
	tstamp_h timestamp with time zone;

	tstamp_t timestamp with time zone;
BEGIN

	epsilon = '0.5 seconds'::interval;
	SELECT p_time - epsilon INTO tstamp_l;
	SELECT p_time + epsilon INTO tstamp_h;

	----------------------------------------------------------------------
	-- Insert data
	----------------------------------------------------------------------
	i := 1;
    LOOP
        EXIT WHEN i > array_upper(p_value_names, 1);

		SELECT * INTO line FROM impl_location(p_plugin, p_plugin_inst, p_type, p_type_inst, p_value_names[i]);

		SELECT impl_instance_id(p_host,p_plugin,line.corr_plugin_inst) INTO ds_id;

		SELECT tstamp INTO tstamp_t
		FROM incoming
		WHERE tstamp_l <= tstamp AND tstamp < tstamp_h
		AND id = ds_id;

		INSERT INTO incoming	(tstamp, id, 	tbl, 		tbl_col, 		val)
		VALUES 					(COALESCE(tstamp_t,p_time),
										ds_id,	line.tbl,	line.tbl_col, 	p_values[i]);

		-- continue the loop
        i := i + 1;
    END LOOP;


END;
$BODY$;

CREATE OR REPLACE FUNCTION impl_location(
	v_plugin text,
	v_plugin_inst text,
	v_type text,
	v_type_inst text,
	v_name text)
    RETURNS TABLE(tbl text, corr_plugin_inst text, tbl_col text)
    LANGUAGE 'plpgsql'
    COST 1
    VOLATILE PARALLEL SAFE
    ROWS 1

AS $BODY$
DECLARE
	v_tbl				text;
	v_corr_plugin_inst	text;
	v_tblcol			text;
BEGIN
	-- bring the data anomalies into shape
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

$BODY$;


CREATE OR REPLACE PROCEDURE public.move_data_to_table()
LANGUAGE 'plpgsql'
AS $BODY$
DECLARE
	-- working variables
	line RECORD;
	test RECORD;
	stmt text;
	stmt_ins text;
	stmt_ct text;
	stmt_cp text;
	cols RECORD;
BEGIN

	----------------------------------------------------------------------
	-- create the tables and indices
	----------------------------------------------------------------------
	FOR line IN
		SELECT DISTINCT tbl FROM incoming
	LOOP
		EXECUTE '
			SELECT 1 FROM
				pg_tables
			WHERE
				schemaname = $1 AND
				tablename  = $2
			' INTO test USING 'public', line.tbl;

		IF test ISNULL THEN
			RAISE INFO 'Create %s', line.tbl;
			stmt = format(
				'
					CREATE TABLE IF NOT EXISTS %I
					(
						id	smallint NOT NULL,
						tstamp timestamp with time zone NOT NULL,

						FOREIGN KEY (id) REFERENCES instance (id)
					);
				', line.tbl);
			EXECUTE stmt;

			-- Create the tstamp_id_idx
			stmt = format(
				'
					CREATE INDEX IF NOT EXISTS %s_tstamp_id_idx
					ON %I USING brin
					(tstamp,id);
				',line.tbl, line.tbl);
			EXECUTE stmt;
		END IF;
	END LOOP;

	----------------------------------------------------------------------
	-- create the coluns in the table
	----------------------------------------------------------------------
	FOR line IN
		SELECT DISTINCT tbl, tbl_col FROM incoming
		ORDER BY tbl
	LOOP
		EXECUTE
			'	SELECT 1
				FROM information_schema.columns
				WHERE table_name=$1 and column_name=$2;
			' INTO test USING line.tbl, line.tbl_col;

		IF test ISNULL THEN
			stmt = format(
				'ALTER table %I ADD COLUMN IF NOT EXISTS %I real DEFAULT NULL;',
				line.tbl, line.tbl_col);
			EXECUTE stmt;
		END IF;
	END LOOP;

	----------------------------------------------------------------------
	-- Now as all tables and all columns exists, move the data
	-- table by table and not line by line into the target tables
	----------------------------------------------------------------------
	FOR line IN
		SELECT DISTINCT tbl FROM incoming
		ORDER BY tbl
	LOOP
		----------------------------------------------------------------------
		-- build the crosstab and insert statement
		----------------------------------------------------------------------
		stmt_ins = 'INSERT INTO ' || line.tbl || ' (tstamp, id ';
		stmt_ct = format('
			SELECT *
			FROM crosstab(
				''select tstamp, id, tbl_col, val
				from incoming
				where tbl=''%L''
				order by 1,2'',

				''SELECT DISTINCT tbl_col FROM incoming WHERE tbl=''%L'' ''
						)
			AS ct(row_name timestamp with time zone, id smallint  ', line.tbl,line.tbl);

		FOR cols IN
			SELECT DISTINCT tbl_col from incoming WHERE tbl = line.tbl
		LOOP
			stmt_ct = stmt_ct || ', "' || cols.tbl_col || '" real ';
			stmt_ins = stmt_ins || ', "' || cols.tbl_col || '" ';
		END LOOP;
		stmt_ct = stmt_ct || ');';
		stmt_ins = stmt_ins || ') ';
		stmt_cp = stmt_ins || ' ' || stmt_ct;
		--RAISE INFO 'ct %', stmt_ct;
		--RAISE INFO 'ins %', stmt_ins;
		--RAISE INFO 'total %', stmt_cp;

		----------------------------------------------------------------------
		-- Insert into the target table
		----------------------------------------------------------------------
		EXECUTE stmt_cp;
	END LOOP;

	DELETE FROM incoming;
END;
$BODY$;

