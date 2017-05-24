# versioning/Makefile

MODULE_big = temporal_tables
OBJS = temporal_tables.o versioning.o

EXTENSION = temporal_tables
DATA = temporal_tables--1.1.1.sql \
       temporal_tables--1.0.0--1.0.1.sql \
       temporal_tables--1.0.1--1.0.2.sql \
       temporal_tables--1.0.2--1.1.0.sql \
       temporal_tables--1.1.0--1.1.1.sql
DOCS = README.md

REGRESS = install no_system_period invalid_system_period \
          no_history_table no_history_system_period invalid_types \
          invalid_system_period_values \
          versioning versioning_custom_system_time combinations \
          structure uninstall

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
