-- collectd - contrib/postgresql/collectd_insert.sql
-- Copyright (C) 2012 Sebastian 'tokkee' Harl
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

-- Description:
-- ------------
--
-- This is a sample database setup that may be used to write data collected by
-- collectd to a PostgreSQL database. We're using two tables, 'identifiers'
-- and 'values' to store the value-list identifier and the actual values
-- respectively.
--
-- The 'values' table is partitioned to improve performance and maintenance.
-- Please note that additional maintenance scripts are required in order to
-- keep the setup running -- see the comments below for details.
--
-- The function 'collectd_insert' may be used to actually insert values
-- submitted by collectd into those tables.
--
-- Sample configuration:
-- ---------------------
--
-- <Plugin postgresql>
--     <Writer sqlstore>
--         Statement "SELECT collectd_insert($1, $2, $3, $4, $5, $6, $7, $8, $9);"
--     </Writer>
--     <Database foo>
--         # ...
--         Writer sqlstore
--     </Database>
-- </Plugin>

CREATE TABLE identifiers (
    id integer NOT NULL,
    host character varying(64) NOT NULL,
    plugin character varying(64) NOT NULL,
    plugin_inst character varying(64) DEFAULT NULL::character varying,
    type character varying(64) NOT NULL,
    type_inst character varying(64) DEFAULT NULL::character varying
);
CREATE SEQUENCE identifiers_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
ALTER SEQUENCE identifiers_id_seq OWNED BY identifiers.id;
ALTER TABLE ONLY identifiers
    ALTER COLUMN id SET DEFAULT nextval('identifiers_id_seq'::regclass);
ALTER TABLE ONLY identifiers
    ADD CONSTRAINT identifiers_host_plugin_plugin_inst_type_type_inst_key
        UNIQUE (host, plugin, plugin_inst, type, type_inst);
ALTER TABLE ONLY identifiers
    ADD CONSTRAINT identifiers_pkey PRIMARY KEY (id);

-- optionally, create indexes for the identifier fields
CREATE INDEX identifiers_host ON identifiers USING btree (host);
CREATE INDEX identifiers_plugin ON identifiers USING btree (plugin);
CREATE INDEX identifiers_plugin_inst ON identifiers USING btree (plugin_inst);
CREATE INDEX identifiers_type ON identifiers USING btree (type);
CREATE INDEX identifiers_type_inst ON identifiers USING btree (type_inst);

CREATE TABLE "values" (
    id integer NOT NULL,
    tstamp timestamp with time zone NOT NULL,
    name character varying(64) NOT NULL,
    value double precision NOT NULL
);

CREATE OR REPLACE VIEW collectd
    AS SELECT host, plugin, plugin_inst, type, type_inst,
            host
                || '/' || plugin
                || CASE
                    WHEN plugin_inst IS NOT NULL THEN '-'
                    ELSE ''
                END
                || coalesce(plugin_inst, '')
                || '/' || type
                || CASE
                    WHEN type_inst IS NOT NULL THEN '-'
                    ELSE ''
                END
                || coalesce(type_inst, '') AS identifier,
            tstamp, name, value
        FROM identifiers
            JOIN values
            ON values.id = identifiers.id;

-- partition "values" by day (or week, month, ...)

-- create the child tables for today and the next 'days' days:
-- this may, for example, be used in a daily cron-job (or similar) to create
-- the tables for the next couple of days
CREATE OR REPLACE FUNCTION values_update_childs(
        integer
    ) RETURNS SETOF text
    LANGUAGE plpgsql
    AS $_$
DECLARE
    days alias for $1;
    cur_day date;
    next_day date;
    i integer;
BEGIN
    IF days < 1 THEN
        RAISE EXCEPTION 'Cannot have negative number of days';
    END IF;

    i := 0;
    LOOP
        EXIT WHEN i > days;

        SELECT CAST ('now'::date + i * '1day'::interval AS date) INTO cur_day;
        SELECT CAST ('now'::date + (i + 1) * '1day'::interval AS date) INTO next_day;

        i := i + 1;

        BEGIN
            EXECUTE 'CREATE TABLE "values$' || cur_day || '" (
                CHECK (tstamp >= TIMESTAMP ''' || cur_day || ''' '
                    || 'AND tstamp < TIMESTAMP ''' || next_day || ''')
            ) INHERITS (values)';
        EXCEPTION WHEN duplicate_table THEN
            CONTINUE;
        END;

        RETURN NEXT 'values$' || cur_day::text;

        EXECUTE 'ALTER TABLE ONLY "values$' || cur_day || '"
            ADD CONSTRAINT "values_' || cur_day || '_pkey"
                PRIMARY KEY (id, tstamp, name, value)';
        EXECUTE 'ALTER TABLE ONLY "values$' || cur_day || '"
            ADD CONSTRAINT "values_' || cur_day || '_id_fkey"
                FOREIGN KEY (id) REFERENCES identifiers(id)';
    END LOOP;
    RETURN;
END;
$_$;

-- create initial child tables
SELECT values_update_childs(2);

CREATE OR REPLACE FUNCTION values_insert_trigger()
    RETURNS trigger
    LANGUAGE plpgsql
    AS $_$
DECLARE
    child_tbl character varying;
BEGIN
    SELECT 'values$' || CAST (NEW.tstamp AS DATE) INTO child_tbl;
    -- Rather than using 'EXECUTE', some if-cascade checking the date may also
    -- be used. However, this would require frequent updates of the trigger
    -- function while this example works automatically.
    EXECUTE 'INSERT INTO "' || child_tbl || '" VALUES ($1.*)' USING NEW;
    RETURN NULL;
END;
$_$;

CREATE TRIGGER insert_values_trigger
    BEFORE INSERT ON values
    FOR EACH ROW EXECUTE PROCEDURE values_insert_trigger();

-- when querying values make sure to enable constraint exclusion
-- SET constraint_exclusion = on;

CREATE OR REPLACE FUNCTION collectd_insert(
        timestamp with time zone, character varying,
        character varying, character varying,
        character varying, character varying,
        character varying[], character varying[], double precision[]
    ) RETURNS void
    LANGUAGE plpgsql
    AS $_$
DECLARE
    p_time alias for $1;
    p_host alias for $2;
    p_plugin alias for $3;
    p_plugin_instance alias for $4;
    p_type alias for $5;
    p_type_instance alias for $6;
    p_value_names alias for $7;
    -- don't use the type info; for 'StoreRates true' it's 'gauge' anyway
    -- p_type_names alias for $8;
    p_values alias for $9;
    ds_id integer;
    i integer;
BEGIN
    SELECT id INTO ds_id
        FROM identifiers
        WHERE host = p_host
            AND plugin = p_plugin
            AND COALESCE(plugin_inst, '') = COALESCE(p_plugin_instance, '')
            AND type = p_type
            AND COALESCE(type_inst, '') = COALESCE(p_type_instance, '');
    IF NOT FOUND THEN
        INSERT INTO identifiers (host, plugin, plugin_inst, type, type_inst)
            VALUES (p_host, p_plugin, p_plugin_instance, p_type, p_type_instance)
            RETURNING id INTO ds_id;
    END IF;
    i := 1;
    LOOP
        EXIT WHEN i > array_upper(p_value_names, 1);
        INSERT INTO values (id, tstamp, name, value)
            VALUES (ds_id, p_time, p_value_names[i], p_values[i]);
        i := i + 1;
    END LOOP;
END;
$_$;

-- vim: set expandtab :
