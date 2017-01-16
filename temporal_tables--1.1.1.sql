/* temporal_tables/temporal_tables--1.1.1.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION temporal_tables" to load this file.\quit

CREATE FUNCTION versioning()
RETURNS TRIGGER
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION versioning() FROM PUBLIC;

COMMENT ON FUNCTION versioning() IS 'System-period temporal table trigger';

CREATE FUNCTION set_system_time(timestamptz)
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

COMMENT ON FUNCTION set_system_time(timestamptz) IS 'Set the system time used by versioning triggers to the specific value. NULL reverts back to the default behaviour and uses CURRENT_TIMESTAMP';
