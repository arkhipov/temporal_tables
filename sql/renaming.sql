CREATE TABLE versioning (a bigint, "b b" date, sys_period tstzrange);

-- Insert some data before versioning is enabled.
INSERT INTO versioning (a, sys_period) VALUES (1, tstzrange('-infinity', NULL));
INSERT INTO versioning (a, sys_period) VALUES (2, tstzrange('2000-01-01', NULL));

CREATE TABLE versioning_history (a bigint, c date, sys_period tstzrange);

CREATE OR REPLACE FUNCTION setup_versioning(
    main regclass,
    sys_period_column text,
    history regclass,
    adjust boolean)
  RETURNS void AS
$BODY$
begin
  execute format('
CREATE TRIGGER versioning_trigger
BEFORE INSERT OR UPDATE OR DELETE ON %I
FOR EACH ROW EXECUTE PROCEDURE versioning2(''%s'', ''%s'', %s);
', main, sys_period_column, history::oid, adjust::text);
end;
$BODY$
  LANGUAGE plpgsql;

SELECT setup_versioning('versioning', 'sys_period', 'versioning_history', false);

-- Insert.
BEGIN;

INSERT INTO versioning (a) VALUES (3);

SELECT a, "b b", lower(sys_period) = CURRENT_TIMESTAMP FROM versioning ORDER BY a, sys_period;

SELECT * FROM versioning_history ORDER BY a, sys_period;

COMMIT;

-- Make sure that the next transaction's CURRENT_TIMESTAMP is different.
SELECT pg_sleep(0.1);

-- Update.
BEGIN;

UPDATE versioning SET a = 4 WHERE a = 3;

SELECT a, "b b", lower(sys_period) = CURRENT_TIMESTAMP FROM versioning ORDER BY a, sys_period;

SELECT a, c, upper(sys_period) = CURRENT_TIMESTAMP FROM versioning_history ORDER BY a, sys_period;

SELECT a, "b b" FROM versioning WHERE lower(sys_period) = CURRENT_TIMESTAMP ORDER BY a, sys_period;

COMMIT;

-- Make sure that the next transaction's CURRENT_TIMESTAMP is different.
SELECT pg_sleep(0.1);

-- Multiple updates.
BEGIN;

UPDATE versioning SET a = 5 WHERE a = 4;
UPDATE versioning SET "b b" = '2012-01-01' WHERE a = 5;

SELECT a, "b b", lower(sys_period) = CURRENT_TIMESTAMP FROM versioning ORDER BY a, sys_period;

SELECT a, c, upper(sys_period) = CURRENT_TIMESTAMP FROM versioning_history ORDER BY a, sys_period;

SELECT a, "b b" FROM versioning WHERE lower(sys_period) = CURRENT_TIMESTAMP ORDER BY a, sys_period;

COMMIT;

-- Make sure that the next transaction's CURRENT_TIMESTAMP is different.
SELECT pg_sleep(0.1);

-- Rename both tables
ALTER TABLE versioning RENAME TO versioning_new;
ALTER TABLE versioning_history RENAME TO versioning_new_history;

-- Delete.
BEGIN;

DELETE FROM versioning_new WHERE a = 4;

SELECT a, "b b", lower(sys_period) = CURRENT_TIMESTAMP FROM versioning_new ORDER BY a, sys_period;

SELECT a, c, upper(sys_period) = CURRENT_TIMESTAMP FROM versioning_new_history ORDER BY a, sys_period;

SELECT a, "b b" FROM versioning_new WHERE lower(sys_period) = CURRENT_TIMESTAMP ORDER BY a, sys_period;

END;

-- Make sure that the next transaction's CURRENT_TIMESTAMP is different.
SELECT pg_sleep(0.1);

CREATE SCHEMA new;
ALTER TABLE versioning_new_history SET SCHEMA new;

-- Delete.
BEGIN;

DELETE FROM versioning_new;

SELECT * FROM versioning_new;

SELECT a, c, upper(sys_period) = CURRENT_TIMESTAMP FROM new.versioning_new_history ORDER BY a, sys_period;

SELECT a, "b b" FROM versioning_new WHERE lower(sys_period) = CURRENT_TIMESTAMP ORDER BY a, sys_period;

END;

DROP TABLE versioning_new;
DROP TABLE new.versioning_new_history;
DROP SCHEMA new;
