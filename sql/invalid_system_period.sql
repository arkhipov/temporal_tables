CREATE TABLE invalid_system_period (sys_period bigint);

CREATE TRIGGER versioning_trigger
BEFORE INSERT OR UPDATE OR DELETE ON invalid_system_period
FOR EACH ROW EXECUTE PROCEDURE versioning('sys_period', NULL, NULL);

INSERT INTO invalid_system_period DEFAULT VALUES;

CREATE TABLE invalid_system_period2 (sys_period tstzrange);

ALTER TABLE invalid_system_period2 DROP COLUMN sys_period;

CREATE TRIGGER versioning_trigger
BEFORE INSERT OR UPDATE OR DELETE ON invalid_system_period2
FOR EACH ROW EXECUTE PROCEDURE versioning('sys_period', NULL, NULL);

INSERT INTO invalid_system_period2 DEFAULT VALUES;

CREATE TABLE invalid_system_period3 (sys_period tstzrange[]);

CREATE TRIGGER versioning_trigger
BEFORE INSERT OR UPDATE OR DELETE ON invalid_system_period3
FOR EACH ROW EXECUTE PROCEDURE versioning('sys_period', NULL, NULL);

INSERT INTO invalid_system_period3 DEFAULT VALUES;

CREATE TABLE invalid_system_period4 (sys_period tsrange);

CREATE TRIGGER versioning_trigger
BEFORE INSERT OR UPDATE OR DELETE ON invalid_system_period4
FOR EACH ROW EXECUTE PROCEDURE versioning('sys_period', NULL, NULL);

INSERT INTO invalid_system_period4 DEFAULT VALUES;
