-- contrib/collectd.lua
--
-- Auxilliary functions to use in plugins written in Lua. Load this file using
--   require ("collectd.lua");
-- at the beginning of your script.

function collectd_error (msg)
	return (collectd_log (3, msg));
end

function collectd_warning (msg)
	return (collectd_log (4, msg));
end

function collectd_notice (msg)
	return (collectd_log (5, msg));
end

function collectd_info (msg)
	return (collectd_log (6, msg));
end

function collectd_debug (msg)
	return (collectd_log (7, msg));
end

