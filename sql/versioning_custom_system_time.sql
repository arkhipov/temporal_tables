SET client_min_messages TO NOTICE;
SET TIME ZONE 'UTC';

CREATE TABLE versioning (a bigint, "b b" date, sys_period tstzrange);

-- Insert some data before versioning is enabled.
INSERT INTO versioning (a, sys_period) VALUES (1, tstzrange('-infinity', NULL));
INSERT INTO versioning (a, sys_period) VALUES (2, tstzrange('2000-01-01', NULL));

CREATE TABLE versioning_history (a bigint, c date, sys_period tstzrange);

CREATE TRIGGER versioning_trigger
BEFORE INSERT OR UPDATE OR DELETE ON versioning
FOR EACH ROW EXECUTE PROCEDURE versioning('sys_period', 'versioning_history', false);

-- Insert.
BEGIN;

SELECT set_system_time('2001-01-01'::timestamptz);

INSERT INTO versioning (a) VALUES (3);

SELECT a, "b b", sys_period FROM versioning ORDER BY a, sys_period;

SELECT * FROM versioning_history ORDER BY a, sys_period;

COMMIT;

-- Update.
BEGIN;

SELECT set_system_time('2001-02-01');

UPDATE versioning SET a = 4 WHERE a = 3;

SELECT a, "b b", sys_period FROM versioning ORDER BY a, sys_period;

SELECT a, c, sys_period FROM versioning_history ORDER BY a, sys_period;

COMMIT;

-- Multiple updates.
BEGIN;

SELECT set_system_time('2001-03-01');

UPDATE versioning SET a = 5 WHERE a = 4;
UPDATE versioning SET "b b" = '2012-01-01' WHERE a = 5;

SELECT a, "b b", sys_period FROM versioning ORDER BY a, sys_period;

SELECT a, c, sys_period FROM versioning_history ORDER BY a, sys_period;

COMMIT;

-- Delete.
BEGIN;

SELECT set_system_time('2001-04-01');

DELETE FROM versioning WHERE a = 4;

SELECT a, "b b", sys_period FROM versioning ORDER BY a, sys_period;

SELECT a, c, sys_period FROM versioning_history ORDER BY a, sys_period;

COMMIT;

-- Persist on commit.
BEGIN;

DELETE FROM versioning WHERE a = 4;

SELECT * FROM versioning WHERE a = 4;

SELECT * FROM versioning_history WHERE a = 4;

ROLLBACK;

-- Reset on rollback.
BEGIN;

SELECT set_system_time(NULL);

ROLLBACK;

BEGIN;

DELETE FROM versioning WHERE a = 4;

SELECT * FROM versioning WHERE a = 4;

SELECT * FROM versioning_history WHERE a = 4;

COMMIT;

-- Savepoints.
BEGIN;

SELECT set_system_time(NULL);

SAVEPOINT p0;

SELECT set_system_time('2002-01-01');

SAVEPOINT p1;

SELECT set_system_time('2002-02-01');

INSERT INTO versioning (a) VALUES (6);

SELECT * FROM versioning WHERE a = 6;

SAVEPOINT p2;

SELECT set_system_time('2002-03-01');

INSERT INTO versioning (a) VALUES (7);

SELECT * FROM versioning WHERE a = 7;

ROLLBACK TO SAVEPOINT p2;

INSERT INTO versioning (a) VALUES (7);

SELECT * FROM versioning WHERE a = 7;

ROLLBACK TO SAVEPOINT p0;

INSERT INTO versioning (a) VALUES (6);

SELECT upper(sys_period) FROM versioning WHERE a = 6 AND lower(sys_period) = CURRENT_TIMESTAMP;

END;

DROP TABLE versioning;
DROP TABLE versioning_history;
