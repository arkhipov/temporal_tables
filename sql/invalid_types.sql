CREATE TABLE invalid_types (a bigint, "b b" date, sys_period tstzrange);

CREATE TABLE invalid_types_history (a bigint, "b b" timestamp, sys_period tstzrange);

CREATE TRIGGER versioning_trigger
BEFORE INSERT OR UPDATE OR DELETE ON invalid_types
FOR EACH ROW EXECUTE PROCEDURE versioning('sys_period', 'invalid_types_history', false);

INSERT INTO invalid_types DEFAULT VALUES;

DELETE FROM invalid_types;
