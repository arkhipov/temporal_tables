/* temporal_tables/temporal_tables--1.0.2.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION temporal_tables" to load this file.\quit

CREATE FUNCTION versioning()
RETURNS TRIGGER
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION versioning() FROM PUBLIC;

COMMENT ON FUNCTION versioning() IS 'System-period temporal table trigger';
