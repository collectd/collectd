-- Table sizes
SELECT owner,
  TABLE_NAME,
  bytes
FROM collectdu.c_tbl_size;

-- Tablespace sizes
SELECT tablespace_name,
  bytes_free,
  bytes_used
FROM collectdu.c_tbs_usage;

-- IO per Tablespace
SELECT SUM(vf.phyblkrd) *8192 AS
phy_blk_r,
  SUM(vf.phyblkwrt) *8192 AS
phy_blk_w,
  'tablespace' AS
i_prefix,
  dt.tablespace_name
FROM((dba_data_files dd JOIN v$filestat vf ON dd.file_id = vf.file#) JOIN dba_tablespaces dt ON dd.tablespace_name = dt.tablespace_name)
GROUP BY dt.tablespace_name;

-- Buffer Pool Hit Ratio:
SELECT DISTINCT 100 *ROUND(1 -((MAX(decode(name,   'physical reads cache',   VALUE))) /(MAX(decode(name,   'db block gets from cache',   VALUE)) + MAX(decode(name,   'consistent gets from cache',   VALUE)))),   4) AS
VALUE,
  'BUFFER_CACHE_HIT_RATIO' AS
buffer_cache_hit_ratio
FROM v$sysstat;

-- Shared Pool Hit Ratio:
SELECT 
  100.0 * sum(PINHITS) / sum(pins) as VALUE,
  'SHAREDPOOL_HIT_RATIO' AS SHAREDPOOL_HIT_RATIO
FROM V$LIBRARYCACHE;

-- PGA Hit Ratio:
SELECT VALUE,
  'PGA_HIT_RATIO' AS
pga_hit_ratio
FROM v$pgastat
WHERE name = 'cache hit percentage';

-- DB Efficiency
SELECT ROUND(SUM(decode(metric_name,   'Database Wait Time Ratio',   VALUE)),   2) AS
database_wait_time_ratio,
  ROUND(SUM(decode(metric_name,   'Database CPU Time Ratio',   VALUE)),   2) AS
database_cpu_time_ratio,
  'DB_EFFICIENCY' AS
db_efficiency
FROM sys.v_$sysmetric
WHERE metric_name IN('Database CPU Time Ratio',   'Database Wait Time Ratio')
 AND intsize_csec =
  (SELECT MAX(intsize_csec)
   FROM sys.v_$sysmetric);
