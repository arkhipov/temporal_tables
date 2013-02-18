CREATE TABLE invalid_system_period_values (a bigint, "b b" date, sys_period tstzrange);

INSERT INTO invalid_system_period_values (a, sys_period) VALUES (1, NULL);
INSERT INTO invalid_system_period_values (a, sys_period) VALUES (2, 'empty');
INSERT INTO invalid_system_period_values (a, sys_period) VALUES (3, tstzrange(NULL, CURRENT_TIMESTAMP));

CREATE TABLE invalid_system_period_values_history (a bigint, "b b" timestamp, sys_period tstzrange);

CREATE TRIGGER versioning_trigger
BEFORE INSERT OR UPDATE OR DELETE ON invalid_system_period_values
FOR EACH ROW EXECUTE PROCEDURE versioning('sys_period', 'invalid_system_period_values_history', false);

DELETE FROM invalid_system_period_values WHERE a = 1;
DELETE FROM invalid_system_period_values WHERE a = 2;
DELETE FROM invalid_system_period_values WHERE a = 3;
