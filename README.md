Temporal Tables Extension
=========================

[![PGXN version](https://badge.fury.io/pg/temporal_tables.svg)](http://badge.fury.io/pg/temporal_tables) [![Build Status](https://travis-ci.org/arkhipov/temporal_tables.svg?branch=master)](https://travis-ci.org/arkhipov/temporal_tables)

Introduction
===============

A temporal table is a table that records the period of time when a row is valid.
There are two types of periods: the application period (also known as valid-time
or business-time) and the system period (also known as transaction-time).

The system period is a column (or a pair of columns) with a system-maintained
value that contains the period of time when a row is valid from a database
perspective.  When you insert a row into such table, the system automatically
generates the values for the start and end of the period.  When you update or
delete a row from a system-period temporal table, the old row is archived into
another table, which is called the history table.

The application period is a column (or a pair of columns) with an
application-maintained value that contains the period of time when a row is
valid from an application perspective.  This column is populated by an
application.

Note that these two time periods do not have to be the same for a single fact.
For example, you may have a temporal table storing data about historical or even
future facts. The application period of these facts differs from the system
period which is set when we add or modify the facts into the table.

Currently, Temporal Tables Extension supports the system-period temporal tables
only.

Additional information on temporal databases can be found at the following
sites:

1. [Wikipedia: Temporal Database](http://en.wikipedia.org/wiki/Temporal_database)
2. [Developing Time-Oriented Database Applications in SQL, Richard T. Snodgrass, Morgan Kaufmann Publishers, Inc., San Francisco, July, 1999, 504+xxiii pages, ISBN 1-55860-436-7.](http://www.cs.arizona.edu/~rts/tdbbook.pdf)
3. [WG2 N1536. WG3: KOA-046. Temporal Features in SQL standard. Krishna Kulkarni,. IBM Corporation](http://metadata-standards.org/Document-library/Documents-by-number/WG2-N1501-N1550/WG2_N1536_koa046-Temporal-features-in-SQL-standard.pdf)

There is [a fantastic tutorial](http://clarkdave.net/2015/02/historical-records-with-postgresql-and-temporal-tables-and-sql-2011/)
on using and querying temporal tables in PostgreSQL with the Temporal Tables
Extension written by [Clark Dave](https://github.com/clarkdave).

Requirements
===============

Temporal Tables Extension requires PostgreSQL 9.2 or higher.

Installation
===============

If you are running Linux, the easiest way to install the extension is to to use
the [PGXN client](http://pgxnclient.projects.pgfoundry.org/).

    $ pgxn install temporal_tables

Or if you prefer to stick with the good old Make, you can set up the extension
like this:

    $ make
    $ make install
    $ make installcheck

If you encounter an error such as:

    "Makefile", line 8: Need an operator

You need to use GNU make, which may well be installed on your system as gmake:

    $ gmake
    $ gmake install
    $ gmake installcheck

If you encounter an error such as:

    make: pg_config: Command not found

Be sure that you have pg_config installed and in your path.  If you used a
package management system such as RPM to install PostgreSQL, be sure that the
-devel package is also installed.  If necessary tell the build process where to
find it:

    $ env PG_CONFIG=/path/to/pg_config make && make install && make installcheck

If you encounter an error such as:

    ERROR: must be owner of database regression

You need to run the test suite using a super user, such as the default
"postgres" super user:

    $ make installcheck PGUSER=postgres

If you are running Windows, you need to run the [MSBuild](https://www.microsoft.com/en-us/download/details.aspx?id=48159)
command in the [Visual Studio command prompt](https://msdn.microsoft.com/en-us/library/f35ctcxw.aspx).

    > msbuild /p:configuration=9.4 /p:platform=x64

The platforms available are x64 and x86 and the configuration are 9.2, 9.3
and 9.4.

Or you can download the latest released zip [here](https://github.com/arkhipov/temporal_tables/releases/latest).

Then you must copy the DLL from the project into the PostgreSQL's `lib`
directory and the `.sql` and `.control` files into the directory
`share\extension`.

    > copy x64\9.4\temporal_tables.dll "C:\Program Files\PostgreSQL\9.4\lib"
    > copy *.control "C:\Program Files\PostgreSQL\9.4\share\extension"
    > copy *.sql "C:\Program Files\PostgreSQL\9.4\share\extension"

Once the extension is installed, you can add it to a database.  Connect to a
database as a super user and do this:

    $ CREATE EXTENSION temporal_tables;

Usage
========

Creating a system-period temporal table
---------------------------------------

Temporal Tables Extension uses a general trigger function to maintain
system-period temporal tables behaviour:

    versioning(<system_period_column_name>, <history_table_name>, <adjust>)

The function must be fired before INSERT or UPDATE or DELETE on a
system-period temporal table.  You are to specify a system period column name, a
history table name and "adjust" parameter (see Updating data section for
details).

Let's have a look at a simple example.

First, create a table:

```SQL
CREATE TABLE employees
(
  name text NOT NULL PRIMARY KEY,
  department text,
  salary numeric(20, 2)
);
```

In order to make this table system-period temporal table we should first add a
system period column:

```SQL
ALTER TABLE employees ADD COLUMN sys_period tstzrange NOT NULL;
```

Then we need a history table that contains archived rows of our table.  The
easiest way to create it is by using LIKE statement:

```SQL
CREATE TABLE employees_history (LIKE employees);
```

Note that a history table does not have to have the same structure as the
original one.  For example, you may want to archive some columns of an original
row but ignore others, or a history table may contain some useful information
that is not necessary in the original table.  The only two requirements for a
history table are:

  1. A history table must contain system period column with the same name and
     data type as in the original one.
  2. If the history table and the original one both contain the column then the
     data type of this column must be the same in these two tables.

Finally we create a trigger on our table to link it with the history table:

```SQL
CREATE TRIGGER versioning_trigger
BEFORE INSERT OR UPDATE OR DELETE ON employees
FOR EACH ROW EXECUTE PROCEDURE versioning('sys_period',
                                          'employees_history',
                                          true);
```

Inserting data
--------------

For a user inserting data into a system-period temporal table is similar to
inserting data into a regular table.  For example, the following data was
inserted on August 8, 2006 to the table employees:

```SQL
INSERT INTO employees (name, department, salary)
VALUES ('Bernard Marx', 'Hatchery and Conditioning Centre', 10000);

INSERT INTO employees (name, department, salary)
VALUES ('Lenina Crowne', 'Hatchery and Conditioning Centre', 7000);

INSERT INTO employees (name, department, salary)
VALUES ('Helmholtz Watson', 'College of Emotional Engineering', 18500);
```

The employees table now contains the following data:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------
  Bernard Marx     | Hatchery and Conditioning Centre |   10000 | [2006-08-08, )
  Lenina Crowne    | Hatchery and Conditioning Centre |    7000 | [2006-08-08, )
  Helmholtz Watson | College of Emotional Engineering |   18500 | [2006-08-08, )

The history table employees_history is empty:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------

The start of sys_period column represents the time when the row became current.
The trigger generates this value by using a CURRENT_TIMESTAMP value which
denotes the time when the first data change statement was executed in the
current transaction.

Updating data
-------------

When a user updates the values of columns in rows of system-period temporal
table, the trigger inserts a copy of the old row into the associated history
table.  If a single transaction makes multiple updates to the same row, only
one history row is generated.  For example, the following data was updated on
February 27, 2007 in the table employees:

```SQL
UPDATE employees SET salary = 11200 WHERE name = 'Bernard Marx';
```

The employees table now contains the following data:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------
  Bernard Marx     | Hatchery and Conditioning Centre |  11200  | [2007-02-27, )
  Lenina Crowne    | Hatchery and Conditioning Centre |   7000  | [2006-08-08, )
  Helmholtz Watson | College of Emotional Engineering |  18500  | [2006-08-08, )

The history table employees_history now contains the following data:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------
  Bernard Marx     | Hatchery and Conditioning Centre |   10000 | [2006-08-08, 2007-02-27)

Update conflicts and time adjustment
------------------------------------

Update conflicts can occur when multiple transactions are updating the same row.
For example, two transactions A and B are executing statements against the
employees table at the same time:

  Time | Transaction A                                                            |  Transaction B
  ---- | ------------------------------------------------------------------------ | -------------------------------------
    T1 | INSERT INTO employees (name, salary) VALUES ('Bernard Marx', 10000);     |
    T2 |                                                                          | INSERT INTO employees (name, salary) VALUES ('Lenina Crowne', 7000);
    T3 |                                                                          | COMMIT;
    T4 | UPDATE employees SET salary = 6800 WHERE name = 'Lenina Crowne';         |
    T5 | INSERT INTO employees (name, salary) VALUES ('Helmholtz Watson', 18500); |
    T6 | COMMIT;                                                                  |

After the inserts at T1 and T2, the employees history contains the following
data:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------
  Bernard Marx     | Hatchery and Conditioning Centre |  10000  | [T1, )
  Lenina Crowne    | Hatchery and Conditioning Centre |   7000  | [T2, )

The history table employee_history is empty.

At time T4 the trigger must set the start of sys_period column of the row to T1
and insert the following row into the history table:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------
  Lenina Crowne    | Hatchery and Conditioning Centre |    7000 | [T2, T1)

However, T2 > T1 and the row cannot be inserted.  In this situation, the update
at time T4 would fail with SQLSTATE 22000.  To avoid such failures, you can
specify "adjust" parameter of the trigger and set it to "true".  Then the start
of sys_period column at time T4 is set to time T2 plus delta (a small interval
of time, typically equals to 1 microsecond).  After this adjustment and the
completion of transaction A, the employees table looks like this:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------
  Bernard Marx     | Hatchery and Conditioning Centre |   10000 | [T1, )
  Lenina Crowne    | Hatchery and Conditioning Centre |    6800 | [T2 + delta, )
  Helmholtz Watson | College of Emotional Engineering |   18500 | [T1, )

The history table employees_history contains the following data:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------
  Lenina Crowne    | Hatchery and Conditioning Centre |    7000 | [T2, T2 + delta)

Deleting data
-------------

When a user deletes data from a system-period temporal table, the trigger adds
rows to the associated history table.  For example, the following data was
deleted on 24 December, 2012 from the table employees:

```SQL
DELETE FROM employees WHERE name = 'Helmholtz Watson';
```

The employees table now contains the following data:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------
  Bernard Marx     | Hatchery and Conditioning Centre |   10000 | [2007-02-27, )
  Lenina Crowne    | Hatchery and Conditioning Centre |    7000 | [2006-08-08, )

The history table employees_history now looks like this:

  name             | department                       | salary  | sys_period
  ---------------- | -------------------------------- | ------- | --------------
  Bernard Marx     | Hatchery and Conditioning Centre |   10000 | [2006-08-08, 2007-02-27)
  Helmholtz Watson | College of Emotional Engineering |   18500 | [2006-08-08, 2012-12-24)

Advanced usage
==============

Instead of using CURRENT_TIMESTAMP, you may want to set a custom system time for
versioning triggers.  It is useful for creating a data warehouse from a system
that recorded a system time and you want to use that time instead.

```SQL
SELECT set_system_time('1985-08-08 06:42:00+08');
```

To revert it back to the default behaviour, call the function with `NULL` as its
argument.

```SQL
SELECT set_system_time(NULL);
```

If the `set_system_time` function is issued within a transaction that is later
aborted, all the changes are undone.  If the transaction is committed, the
changes will persist until the end of the session.

Examples and hints
=====================

Using inheritance when creating history tables
----------------------------------------------

In the example above we used LIKE statement to create the history table,
sometimes it is better to use inheritance for this task.  For example:

```SQL
CREATE TABLE employees_history
(
  name text NOT NULL PRIMARY KEY,
  department text,
  salary numeric(20, 2),
  sys_period tstzrange NOT NULL
);
```

Then create the employees table:

```SQL
CREATE TABLE employees () INHERITS (employees_history);
```

Pruning history tables
----------------------

History tables are always growing and so are consuming an increasing amount of
storage.  There are several ways you can prune old data from a history table:

  1. Periodically delete old data from a history table.
  2. Use partitioning and detach old partitions from a history table (for more
     information on table parititioning see PostgreSQL documentation).

There are many possible rules for pruning old rows:

  1. Prune rows older than a certain age.
  2. Retain only the latest N versions of a row.
  3. Prune rows when a corresponding row is deleted from the system-period
     temporal table.
  4. Prune rows that satisfy the specified business rules.

You can also set another tablespace for a history table to move it on a cheaper
storage.

Using system-period temporal tables for data audit
--------------------------------------------------

It is possible to use system-period temporal tables for data audit.  For
example, you can add the following triggers to save user that modified or
deleted the current row:

```SQL
CREATE FUNCTION employees_modify()
RETURNS TRIGGER AS $$
BEGIN
  NEW.user_modified = SESSION_USER;
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER employees_modify
BEFORE INSERT OR UPDATE ON employees
FOR EACH ROW EXECUTE PROCEDURE employees_modify();

CREATE FUNCTION employees_delete()
RETURNS TRIGGER AS $$
BEGIN
  NEW.user_deleted = SESSION_USER;
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER employees_delete
BEFORE INSERT ON employees_history
FOR EACH ROW EXECUTE PROCEDURE employees_delete();
```

Notes
========

Temporal Tables Extension is distributed under the terms of BSD 2-clause
license. See LICENSE or http://www.opensource.org/licenses/bsd-license.php for
more details.
