# versioning/Makefile

MODULE_big = temporal_tables
OBJS = versioning.o

EXTENSION = temporal_tables
DATA = temporal_tables--1.0.1.sql temporal_tables--1.0.0--1.0.1.sql
DOCS = README.temporal_tables

REGRESS = install no_system_period invalid_system_period \
          no_history_table no_history_system_period invalid_types \
          invalid_system_period_values versioning structure uninstall

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
