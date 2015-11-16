/* temporal_tables/temporal_tables--1.0.2--1.1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION temporal_tables UPDATE TO '1.1.0'" to load this file.\quit

CREATE FUNCTION set_system_time(timestamptz)
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

COMMENT ON FUNCTION set_system_time(timestamptz) IS 'Set the system time used by versioning triggers to the specific value. NULL reverts back to the default behaviour and uses CURRENT_TIMESTAMP';
