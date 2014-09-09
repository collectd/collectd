-- collectd - contrib/oracle/create_schema.ddl
-- Copyright (C) 2008,2009  Roman Klesel
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of the GNU General Public License as published by the
-- Free Software Foundation; only version 2 of the License is applicable.
--
-- This program is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
-- General Public License for more details.
--
-- You should have received a copy of the GNU General Public License along
-- with this program; if not, write to the Free Software Foundation, Inc.,
-- 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
--
-- Authors:
--   Roman Klesel <roman.klesel at noris.de>

-- Description
--------------
-- This will create a schema to provide collectd with the required permissions
-- and space for statistic data.
-- The idea is to store the output of some expensive queries in static tables
-- and fill these tables with dbms_scheduler jobs as often as necessary.
-- collectd will then just read from the static tables. This will reduces the
-- chance that your system will be killed by excessive monitoring queries and
-- gives the dba control on the interval the information provided to collectd
-- will be refreshed. You have to create a dbms_scheduler job for each of the
-- schemas you what to monitor for object-space-usage. See the example below.
--
-- Requirements
---------------
-- make sure you have: 
-- 		write permission in $PWD
--		you have GID of oracle software owner
-- 		set $ORACLE_HOME 
--		set $ORACLE_SID
--		DB is up an running in RW mode
-- execute like this:
-- sqlplus /nolog @ create_collectd-schema.dll

spool create_collectd-schema.log
connect / as sysdba

-- Create user, tablespace and permissions
 
CREATE TABLESPACE "COLLECTD-TBS" 
	DATAFILE SIZE 30M 
	AUTOEXTEND ON 
	NEXT 10M 
	MAXSIZE 300M
	LOGGING 
	EXTENT MANAGEMENT LOCAL 
	SEGMENT SPACE MANAGEMENT AUTO 
	DEFAULT NOCOMPRESS;

CREATE ROLE "CREATE_COLLECTD_SCHEMA" NOT IDENTIFIED;
GRANT CREATE JOB TO "CREATE_COLLECTD_SCHEMA";
GRANT CREATE SEQUENCE TO "CREATE_COLLECTD_SCHEMA";
GRANT CREATE SYNONYM TO "CREATE_COLLECTD_SCHEMA";
GRANT CREATE TABLE TO "CREATE_COLLECTD_SCHEMA";
GRANT CREATE VIEW TO "CREATE_COLLECTD_SCHEMA";
GRANT CREATE PROCEDURE TO "CREATE_COLLECTD_SCHEMA";

CREATE USER "COLLECTDU" 
	PROFILE "DEFAULT" 
	IDENTIFIED BY "Change_me-1st" 
	PASSWORD EXPIRE 
	DEFAULT TABLESPACE "COLLECTD-TBS"
	TEMPORARY TABLESPACE "TEMP"
	QUOTA UNLIMITED ON "COLLECTD-TBS"
	ACCOUNT UNLOCK;

GRANT "CONNECT" TO "COLLECTDU";
GRANT "SELECT_CATALOG_ROLE" TO "COLLECTDU";
GRANT "CREATE_COLLECTD_SCHEMA" TO "COLLECTDU";
GRANT analyze any TO "COLLECTDU";
GRANT select on dba_tables TO "COLLECTDU";
GRANT select on dba_lobs TO "COLLECTDU";
GRANT select on dba_indexes TO "COLLECTDU";
GRANT select on dba_segments TO "COLLECTDU";
GRANT select on dba_tab_columns TO "COLLECTDU";
GRANT select on dba_free_space TO "COLLECTDU";
GRANT select on dba_data_files TO "COLLECTDU";
-- Create tables and indexes

alter session set current_schema=collectdu;

create table c_tbs_usage (
	tablespace_name varchar2(30),
	bytes_free number,
    bytes_used  number,
        CONSTRAINT "C_TBS_USAGE_UK1" UNIQUE ("TABLESPACE_NAME") USING INDEX
        TABLESPACE "COLLECTD-TBS"  ENABLE)
        TABLESPACE "COLLECTD-TBS";

CREATE TABLE "COLLECTDU"."C_TBL_SIZE" (
    "OWNER" VARCHAR2(30 BYTE), 
	"TABLE_NAME" VARCHAR2(30 BYTE), 
	"BYTES" NUMBER, 
	 CONSTRAINT "C_TBL_SIZE_UK1" UNIQUE ("OWNER", "TABLE_NAME")
         USING INDEX TABLESPACE "COLLECTD-TBS"  ENABLE)
         TABLESPACE "COLLECTD-TBS" ;
 

create or replace PROCEDURE get_object_size(owner IN VARCHAR2) AS

v_owner VARCHAR2(30) := owner;

l_free_blks NUMBER;
l_total_blocks NUMBER;
l_total_bytes NUMBER;
l_unused_blocks NUMBER;
l_unused_bytes NUMBER;
l_lastusedextfileid NUMBER;
l_lastusedextblockid NUMBER;
l_last_used_block NUMBER;

CURSOR cur_tbl IS
SELECT owner,
  TABLE_NAME
FROM dba_tables
WHERE owner = v_owner;

CURSOR cur_idx IS
SELECT owner,
  index_name,
  TABLE_NAME
FROM dba_indexes
WHERE owner = v_owner;

CURSOR cur_lob IS
SELECT owner,
  segment_name,
  TABLE_NAME
FROM dba_lobs
WHERE owner = v_owner;

BEGIN

  DELETE FROM c_tbl_size
  WHERE owner = v_owner;
  COMMIT;

  FOR r_tbl IN cur_tbl
  LOOP
    BEGIN
      dbms_space.unused_space(segment_owner => r_tbl.owner,   segment_name => r_tbl.TABLE_NAME,   segment_type => 'TABLE',   total_blocks => l_total_blocks,   total_bytes => l_total_bytes,   unused_blocks => l_unused_blocks,   unused_bytes => l_unused_bytes,   last_used_extent_file_id => l_lastusedextfileid,   last_used_extent_block_id => l_lastusedextblockid,   last_used_block => l_last_used_block);

    EXCEPTION
    WHEN others THEN
      DBMS_OUTPUT.PUT_LINE('tbl_name: ' || r_tbl.TABLE_NAME);
    END;
    INSERT
    INTO c_tbl_size
    VALUES(r_tbl.owner,   r_tbl.TABLE_NAME,   l_total_bytes -l_unused_bytes);
  END LOOP;

  COMMIT;

  FOR r_idx IN cur_idx
  LOOP
    BEGIN
      dbms_space.unused_space(segment_owner => r_idx.owner,   segment_name => r_idx.index_name,   segment_type => 'INDEX',   total_blocks => l_total_blocks,   total_bytes => l_total_bytes,   unused_blocks => l_unused_blocks,   unused_bytes => l_unused_bytes,   last_used_extent_file_id => l_lastusedextfileid,   last_used_extent_block_id => l_lastusedextblockid,   last_used_block => l_last_used_block);

    EXCEPTION
    WHEN others THEN
      DBMS_OUTPUT.PUT_LINE('idx_name: ' || r_idx.index_name);
    END;

    UPDATE c_tbl_size
    SET bytes = bytes + l_total_bytes -l_unused_bytes
    WHERE owner = r_idx.owner
     AND TABLE_NAME = r_idx.TABLE_NAME;
  END LOOP;

  COMMIT;

  FOR r_lob IN cur_lob
  LOOP
    BEGIN
      dbms_space.unused_space(segment_owner => r_lob.owner,   segment_name => r_lob.segment_name,   segment_type => 'LOB',   total_blocks => l_total_blocks,   total_bytes => l_total_bytes,   unused_blocks => l_unused_blocks,   unused_bytes => l_unused_bytes,   last_used_extent_file_id => l_lastusedextfileid,   last_used_extent_block_id => l_lastusedextblockid,   last_used_block => l_last_used_block);

    EXCEPTION
    WHEN others THEN
      DBMS_OUTPUT.PUT_LINE('lob_name: ' || r_lob.segment_name);
    END;

    UPDATE c_tbl_size
    SET bytes = bytes + l_total_bytes -l_unused_bytes
    WHERE owner = r_lob.owner
     AND TABLE_NAME = r_lob.TABLE_NAME;
  END LOOP;

  COMMIT;

END get_object_size;
/

create or replace PROCEDURE get_tbs_size AS
BEGIN

execute immediate 'truncate table c_tbs_usage';

insert into c_tbs_usage (
select df.tablespace_name as tablespace_name, 
       decode(df.maxbytes,
               0,
               sum(fs.bytes),
               (df.maxbytes-(df.bytes-sum(fs.bytes)))) as bytes_free,
       decode(df.maxbytes,
               0,
               round((df.bytes-sum(fs.bytes))),
               round(df.maxbytes-(df.maxbytes-(df.bytes-sum(fs.bytes))))) as bytes_used
from dba_free_space fs inner join 
       (select 
               tablespace_name, 
               sum(bytes) bytes, 
               sum(decode(maxbytes,0,bytes,maxbytes))  maxbytes
        from dba_data_files
        group by tablespace_name ) df          
on fs.tablespace_name = df.tablespace_name
group by df.tablespace_name,df.maxbytes,df.bytes);

COMMIT;

END get_tbs_size;
/

BEGIN
sys.dbms_scheduler.create_job(
job_name => '"COLLECTDU"."C_TBSSIZE_JOB"',
job_type => 'PLSQL_BLOCK',
job_action => 'begin
   get_tbs_size();
end;',
repeat_interval => 'FREQ=MINUTELY;INTERVAL=5',
start_date => systimestamp at time zone 'Europe/Berlin',
job_class => '"DEFAULT_JOB_CLASS"',
auto_drop => FALSE,
enabled => TRUE);
END;
/

BEGIN
sys.dbms_scheduler.create_job(
job_name => '"COLLECTDU"."C_TBLSIZE_COLLECTDU_JOB"',
job_type => 'PLSQL_BLOCK',
job_action => 'begin
   get_object_size( owner => ''COLLECTDU'' );
end;',
repeat_interval => 'FREQ=HOURLY;INTERVAL=12',
start_date => systimestamp at time zone 'Europe/Berlin',
job_class => '"DEFAULT_JOB_CLASS"',
auto_drop => FALSE,
enabled => TRUE);
END;
/

spool off
quit

-- vim: set syntax=sql :
