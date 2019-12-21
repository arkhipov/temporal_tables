/* -------------------------------------------------------------------------
 *
 * versioning.c
 *
 * Copyright (c) 2012-2017 Vladislav Arkhipov <vlad@arkhipov.ru>
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>
#include <float.h>

#if PG_VERSION_NUM >= 120000
#include "access/relation.h"
#include "access/table.h"
#endif
#if PG_VERSION_NUM >= 90300
#include "access/htup_details.h"
#endif
#include "access/xact.h"
#include "catalog/namespace.h"
#include "catalog/pg_type.h"
#include "commands/trigger.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rangetypes.h"
#if PG_VERSION_NUM >= 100000
#include "utils/regproc.h"
#endif
#include "utils/rel.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"

#include "temporal_tables.h"

#if defined(_MSC_VER) && (_MSC_VER < 1800)
#define nextafter(x, y) _nextafter(x, y)
#endif

#if PG_VERSION_NUM < 110000
// https://github.com/postgres/postgres/commit/4bd1994650fddf49e717e35f1930d62208845974#diff-350265f4962fd3fb1c5c2d8667d79700
#define DatumGetRangeTypeP DatumGetRangeType
#define RangeTypePGetDatum RangeTypeGetDatum
#endif

PGDLLEXPORT Datum versioning(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum set_system_time(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(versioning);
PG_FUNCTION_INFO_V1(set_system_time);

/* Warning if system period was adjusted. */
#define ERRCODE_WARNING_SYSTEM_PERIOD_ADJUSTED MAKE_SQLSTATE('0', '1', 'X', '0', '1')

/* Cached data for versioning trigger. */
typedef struct VersioningHashEntry
{
	Oid 		 relid;					/* hash key (must be first) */
	Oid 		 history_relid;			/* OID of the history relation */
	TupleDesc	 tupdesc;				/* tuple descriptor of the versioned relation */
	TupleDesc	 history_tupdesc;		/* tuple descriptor of the history relation */

	/*
	 * The number of items in attnums or -1 if this cached data is invalid.
	 * If attnums is not zero then tupdesc, history_tupdesc, attnums and
	 * insert_history_plan contains not null values.
	 */
	int			 natts;

	/*
	 * attnums[N] is a number of attribute in the versioned relation that is
	 * also appears in the history relation.
	 */
	int			*attnums;

	/* Cached plan of INSERT command into the history relation. */
	SPIPlanPtr	 insert_history_plan;
} VersioningHashEntry;

/* true if datetimes are integer based. */
static bool integer_datetimes;

/* true if integer_datetimes value was looked up. */
static bool integer_datetimes_set = false;

/* Contains cached data for OID of versioned relation. */
static HTAB *versioning_cache = NULL;

static bool parse_adjust_argument(const char *arg);

/*
 * Local function prototypes.
 */
static TypeCacheEntry *get_period_typcache(FunctionCallInfo fcinfo,
										   Form_pg_attribute attr,
										   Relation relation);

static void check_attr_type(Form_pg_attribute attr,
							Form_pg_attribute history_attr,
							Relation relation,
							Relation history_relation);

static void fill_versioning_hash_entry(VersioningHashEntry *hash_entry,
									   Relation relation,
									   Relation history_relation,
									   TupleDesc tupdesc,
									   const char *period_attname);

static void insert_history_row(HeapTuple tuple,
							   Relation relation,
							   const char *history_relation_argument,
							   const char *period_attname);

static void deserialize_system_period(HeapTuple tuple,
									  Relation relation,
									  int period_attnum,
									  const char *period_attname,
									  TypeCacheEntry *typcache,
									  RangeBound *lower,
									  RangeBound *upper);

static void lookup_integer_datetimes();

static TimestampTz get_system_time();

static TimestampTz next_timestamp(TimestampTz timestamp);

static void adjust_system_period(TypeCacheEntry *typcache,
								 RangeBound *lower,
								 RangeBound *upper,
								 const char *adjust_argument,
								 Relation relation);

static bool modified_in_current_transaction(HeapTuple tuple);

static HeapTuple modify_tuple(Relation rel, HeapTuple tuple,
	                          int period_attnum, RangeType *range);

static Datum versioning_insert(TriggerData *trigdata,
							   TypeCacheEntry *typcache,
							   int period_attnum);

static Datum versioning_update(TriggerData *trigdata,
							   TypeCacheEntry *typcache,
							   int period_attnum,
							   const char *period_attname,
							   const char *history_relation_argument,
							   const char *adjust_argument);

static Datum versioning_delete(TriggerData *trigdata,
							   TypeCacheEntry *typcache,
							   int period_attnum,
							   const char *period_attname,
							   const char *history_relation_argument,
							   const char *adjust_argument);

static void init_versioning_hash_table();
static void *hash_entry_alloc(Size size);

static VersioningHashEntry *lookup_versioning_hash_entry(Oid relid,
														 bool *found);

/*
 * This trigger maintains the logic of versioned tables.
 *
 * Versioned table contains the current active rows. Archived rows are located
 * into the other table called the history table. When you insert, update or
 * delete rows in the versioned table, the current trigger automatically
 * inserts a copy of the old row into the corresponding history table.
 *
 * Versioned table must contains a special column of type tstzrange called
 * the system period. The begin of this period represents the time when the
 * row data became current. The end of this period represents the time when
 * the row data was no longer current.
 *
 * The common history table columns and versioned table columns must have the
 * same data types.
 *
 * To convert a table to a system versioned table:
 *		1. Create a system period column in the original table.
 *		2. Create a history table.
 *		3. Use CREATE TRIGGER on the original table as shown below.
 *
 * In CREATE TRIGGER you are to specify a system period column name, a history
 * relation name and "adjust" parameter:
 *
 * CREATE TRIGGER <trigger_name>
 * BEFORE INSERT OR UPDATE OR DELETE ON <versioned_table>
 * FOR EACH ROW EXECUTE PROCEDURE
 *   versioning(<system_period_column_name>, <history_relation>, <adjust>).
 */
Datum
versioning(PG_FUNCTION_ARGS)
{
	TriggerData		   *trigdata;
	Trigger			   *trigger;
	char			  **args;
	Relation			relation;
	TupleDesc			tupdesc;
	char			   *period_attname;
	int					period_attnum;
	Form_pg_attribute	period_attr;
	TypeCacheEntry 	   *typcache;

	trigdata = (TriggerData *) fcinfo->context;

	/* Check that the trigger function was called in expected context. */
	if (!CALLED_AS_TRIGGER(fcinfo))
		ereport(ERROR,
				(errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
				 errmsg("function \"versioning\" was not called by trigger manager")));

	/* Check proper event. */
	if (!TRIGGER_FIRED_BEFORE(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		ereport(ERROR,
				(errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
				 errmsg("function \"versioning\" must be fired BEFORE ROW")));

	if (!TRIGGER_FIRED_BY_INSERT(trigdata->tg_event) &&
		!TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event) &&
		!TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
		ereport(ERROR,
				(errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
				 errmsg("function \"versioning\" must be fired for INSERT or UPDATE or DELETE")));

	trigger = trigdata->tg_trigger;

	/* Check number of arguments. */
	if (trigger->tgnargs != 3)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("wrong number of parameters for function \"versioning\""),
				 errdetail("expected 3 parameters but got %d",
						   trigger->tgnargs)));

	args = trigger->tgargs;

	relation = trigdata->tg_relation;

	tupdesc = RelationGetDescr(relation);

	period_attname = args[0];

	/* Check that system period attribute exists in the versioned relation. */
	period_attnum = SPI_fnumber(tupdesc, period_attname);

	if (period_attnum == SPI_ERROR_NOATTRIBUTE)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						period_attname,
						RelationGetRelationName(relation))));

	period_attr = TupleDescAttr(tupdesc, period_attnum - 1);

	/* Check that system period attribute is not dropped. */
	if (period_attr->attisdropped)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						period_attname,
						RelationGetRelationName(relation))));

	/* Check that system period attribute is not an array. */
	if (period_attr->attndims != 0)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("system period column \"%s\" of relation \"%s\" is not a range but an array",
						period_attname,
						RelationGetRelationName(relation))));

	/* Locate the typcache entry for the type of system period attribute. */
	typcache = get_period_typcache(fcinfo, period_attr, relation);

	if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event))
		return versioning_insert(trigdata, typcache, period_attnum);
	else if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		return versioning_update(trigdata, typcache,
								 period_attnum, period_attname,
								 args[1], args[2]);
	else
		/* otherwise this is ON DELETE trigger */
		return versioning_delete(trigdata, typcache,
								 period_attnum, period_attname,
								 args[1], args[2]);
}

/*
 * Set the system time value that is used by versioned triggers to the
 * specific value. Revert to the default behaviour if NULL is passed for the
 * argument.
 */
Datum
set_system_time(PG_FUNCTION_ARGS)
{
	TemporalContext *ctx = get_current_temporal_context(true);

	if (PG_ARGISNULL(0))
	{
		ctx->system_time_mode = CurrentTransactionStartTimestamp;
	}
	else
	{
		ctx->system_time_mode = UserDefined;
		ctx->system_time = PG_GETARG_TIMESTAMPTZ(0);
	}

	PG_RETURN_VOID();
}

/*
 * Get the value that should be used as the system time by versioned
 * triggers.
 */
static TimestampTz
get_system_time()
{
	TemporalContext *ctx = get_current_temporal_context(false);

	switch (ctx->system_time_mode)
	{
		case CurrentTransactionStartTimestamp:
			return GetCurrentTransactionStartTimestamp();
		case UserDefined:
			return ctx->system_time;
	}

	Assert(false);

	/* will never get here */
	return 0;
}

/*
 * Parse argument value as boolean. The valid values are "true" for true,
 * "false" for false.
 *
 * If the string parses okay, return the parsed value, else throw an error.
 */
static bool
parse_adjust_argument(const char *arg)
{
	if (pg_strcasecmp(arg, "true") == 0)
		return true;

	if (pg_strcasecmp(arg, "false") == 0)
		return false;

	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("invalid value \"%s\" for \"adjust\" parameter",
					arg),
			 errdetail("valid values are: \"true\", \"false\"")));

	return false;
}

/*
 * Locate the typcache entry for the system period attribute. If the specified
 * attribute is not a range of timestamp with timezone, an error is thrown.
 */
static TypeCacheEntry *
get_period_typcache(FunctionCallInfo fcinfo,
					Form_pg_attribute attr,
					Relation relation)
{
	Oid 			 typoid;
	HeapTuple 		 type_tuple;
	Form_pg_type 	 type;
	TypeCacheEntry	*typcache;

	typoid = attr->atttypid;

	/* Search syscache for attribute type. */
	type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typoid));

	if (!HeapTupleIsValid(type_tuple))
		elog(ERROR, "cache lookup failed for type %u", typoid);

	type = (Form_pg_type) GETSTRUCT(type_tuple);

	/* Check that type is a range. */
	if (type->typtype != TYPTYPE_RANGE)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("system period column \"%s\" of relation \"%s\" is not a range but type %s",
						NameStr(attr->attname),
						RelationGetRelationName(relation),
						format_type_be(typoid))));

	/* Get cached information about the range type. */
	typcache = range_get_typcache(fcinfo, typoid);

	/* Check that this is a range of timestamp with timezone. */
	if (typcache->rngelemtype->type_id != TIMESTAMPTZOID)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("system period column \"%s\" of relation \"%s\" is not a range of timestamp with timezone but of type %s",
						NameStr(attr->attname),
						RelationGetRelationName(relation),
						format_type_be(typcache->rngelemtype->type_id))));

	ReleaseSysCache(type_tuple);

	return typcache;
}

/*
 * Check that the type of an attribute in the versioned table is the same as in
 * the history table.
 */
static void
check_attr_type(Form_pg_attribute attr,
				Form_pg_attribute history_attr,
				Relation relation,
				Relation history_relation)
{
	if (attr->atttypid != history_attr->atttypid ||
		attr->attndims != history_attr->attndims ||
		attr->atttypmod != history_attr->atttypmod)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("column \"%s\" of relation \"%s\" is of type %s but column \"%s\" of history relation \"%s\" is of type %s",
						NameStr(attr->attname),
						RelationGetRelationName(relation),
						format_type_with_typemod(attr->atttypid,
												 attr->atttypmod),
						NameStr(history_attr->attname),
						RelationGetRelationName(history_relation),
						format_type_with_typemod(history_attr->atttypid,
												 history_attr->atttypmod))));
}

/*
 * Fill a newly created hash entry with the cached data.
 *
 * Note that hash entry must be zeroed and natts value of the entry must be
 * set to -1 prior to the call of this function.
 */
static void
fill_versioning_hash_entry(VersioningHashEntry *hash_entry,
						   Relation relation,
						   Relation history_relation,
						   TupleDesc tupdesc,
						   const char *period_attname)
{
	TupleDesc		 history_tupdesc;
	MemoryContext	 oldcontext;
	int				 attnums_size;
	int				*attnums;
	StringInfoData	 querybuf;
	char			*nspname;
	char			*relname;
	int				*history_attnums;
	int				 natts;
	int				 i;
	int				 ret;

	history_tupdesc = RelationGetDescr(history_relation);

	/* Check that the history relation contains the system period attribute. */
	if (SPI_fnumber(history_tupdesc, period_attname) < 0)
		ereport(ERROR,
				(errmsg("history relation \"%s\" does not contain system period column \"%s\"",
						RelationGetRelationName(history_relation),
						period_attname),
				 errhint("history relation must contain system period column with the same name and data type as the versioned one")));

	/*
	 * Allocate versioned relation attributes array. attnums[N] contains the
	 * number of the attribute in the versioned relation.
	 */
	attnums_size = Min(tupdesc->natts, history_tupdesc->natts);
	attnums = palloc(attnums_size * sizeof(int));

	/*
	 * The query string build is
	 * 		INSERT INTO <history_relation> (<attr1>, <attr2>, ...)
	 * 		VALUES ($1, $2, ...)
	 */
	initStringInfo(&querybuf);

	nspname = get_namespace_name(RelationGetNamespace(history_relation));
	relname = RelationGetRelationName(history_relation);

	appendStringInfo(&querybuf,
					 "INSERT INTO %s.%s (",
					 quote_identifier(nspname),
					 quote_identifier(relname));

	/*
	 * Allocate history relation attributes array. history_attnums[N] is a
	 * number of the attribute in the history relation that has the same name
	 * as the attribute with the number attnums[N] in the versioned relation.
	 */
	history_attnums = palloc(attnums_size * sizeof(int));

	/* Fill in common attributes array. */
	natts = 0;
	for (i = 0; i < tupdesc->natts; ++i)
	{
		Form_pg_attribute	 attr;
		Form_pg_attribute	 history_attr;
		int					 history_attnum;
		char				*attname;

		attr = TupleDescAttr(tupdesc, i);

		if (attr->attisdropped)
			continue;

		attname = NameStr(attr->attname);

		history_attnum = SPI_fnumber(history_tupdesc, attname);

		if (history_attnum < 0)
			continue;

		history_attr = TupleDescAttr(history_tupdesc, history_attnum - 1);

		check_attr_type(attr, history_attr, relation, history_relation);

		attnums[natts] = attr->attnum;
		history_attnums[natts] = history_attnum;

		if (natts != 0)
			appendStringInfo(&querybuf, ", ");

		appendStringInfo(&querybuf, "%s", quote_identifier(attname));

		natts++;
	}

	/*
	 * If there are common attributes, then prepare and set the plan and
	 * related information.
	 */
	if (natts != 0)
	{
		Oid			*argtypes;
		SPIPlanPtr	 plan;

		appendStringInfo(&querybuf, ") VALUES (");

		argtypes = palloc(sizeof(Oid) * natts);
		for (i = 0; i < natts; ++i)
		{
			if (i != 0)
				appendStringInfo(&querybuf, ", ");

			appendStringInfo(&querybuf, "$%d", i + 1);

			argtypes[i] = SPI_gettypeid(history_tupdesc, history_attnums[i]);
		}

		appendStringInfo(&querybuf, ")");

		/* Prepare and save the plan. */
		plan = SPI_prepare(querybuf.data, natts, argtypes);

		if (plan == NULL)
			elog(ERROR, "SPI_prepare returned %d for %s",
				 SPI_result, NameStr(querybuf));

		if ((ret = SPI_keepplan(plan)) != 0)
			elog(ERROR, "SPI_keepplan returned %d", ret);

		hash_entry->insert_history_plan = plan;

		pfree(argtypes);

		/* Copy the constructed attribute list into TopMemoryContext. */
		oldcontext = MemoryContextSwitchTo(TopMemoryContext);

		hash_entry->history_relid = RelationGetRelid(history_relation);
		hash_entry->tupdesc = CreateTupleDescCopyConstr(tupdesc);
		hash_entry->history_tupdesc = CreateTupleDescCopyConstr(history_tupdesc);

		hash_entry->attnums = palloc(natts * sizeof(int));
		memcpy(hash_entry->attnums, attnums, natts * sizeof(int));

		MemoryContextSwitchTo(oldcontext);
	}

	/*
	 * Now when the cached data structure is filled we can set natts to mark
	 * the entry valid.
	 */
	hash_entry->natts = natts;

	pfree(attnums);
	pfree(history_attnums);
}

/*
 * Insert a row into the history relation.
 *
 *		tuple: a row to insert
 *		relation: versioned relation
 *		history_relation_name: qualified name of the history relation
 */
static void
insert_history_row(HeapTuple tuple,
				   Relation relation,
				   const char *history_relation_name,
				   const char *period_attname)
{
	RangeVar			*relrv;
	Relation			 history_relation;
	VersioningHashEntry	*hash_entry;
	bool				 found;
	TupleDesc			 tupdesc;
	int					 ret;
	int					 natts;

	/* Open the history relation and obtain AccessShareLock on it. */
	relrv = makeRangeVarFromNameList(stringToQualifiedNameList(history_relation_name));

	history_relation = heap_openrv(relrv, AccessShareLock);

	/* Look up the cached data for the versioned relation OID. */
	hash_entry = lookup_versioning_hash_entry(RelationGetRelid(relation),
											  &found);

	tupdesc = RelationGetDescr(relation);

	if (found)
	{
		TupleDesc history_tupdesc;

		history_tupdesc = RelationGetDescr(history_relation);

		/*
		 * Check that the cached data is still valid.
		 *
		 * If natts is -1, then fill_versioning_hash_entry failed and the
		 * cached data is invalid.
		 *
		 * If the trigger definition changes, then the cached history relation
		 * OID may differs compared to the current one.
		 *
		 * Then check that the structure of the versioned table and the history
		 * table is intact by comparing the cached TupleDesc with the current
		 * one.
		 */
		if (hash_entry->natts == -1 ||
			RelationGetRelid(history_relation) != hash_entry->history_relid ||
			!equalTupleDescs(tupdesc, hash_entry->tupdesc) ||
			!equalTupleDescs(history_tupdesc, hash_entry->history_tupdesc))
		{
			/* Mark the entry invalid. */
			hash_entry->natts = -1;
		
			/* If the cached data structure is invalid, free it's fields. */
			if (hash_entry->tupdesc != NULL)
			{
				FreeTupleDesc(hash_entry->tupdesc);
				hash_entry->tupdesc = NULL;
			}

			if (hash_entry->history_tupdesc != NULL)
			{
				FreeTupleDesc(hash_entry->history_tupdesc);
				hash_entry->history_tupdesc = NULL;
			}

			if (hash_entry->attnums != NULL)
			{
				pfree(hash_entry->attnums);
				hash_entry->attnums = NULL;
			}
			
			if (hash_entry->insert_history_plan != NULL)
			{
				if ((ret = SPI_freeplan(hash_entry->insert_history_plan)) != 0)
					elog(ERROR, "SPI_freeplan returned %d", ret);

				hash_entry->insert_history_plan = NULL;
			}

			/* Make to refill the cached data entry. */
			found = false;
		}
	}

	if ((ret = SPI_connect()) != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect returned %d", ret);

	/*
	 * If there is no cached data or it is invalid, fill the cached data
	 * structure.
	 */
	if (!found)
	{
		fill_versioning_hash_entry(hash_entry, relation, history_relation,
								   tupdesc, period_attname);
	}

	natts = hash_entry->natts;

	/* Execute the plan. */
	if (natts != 0)
	{
		Datum		*values;
		char		*nulls;
		int			*attnums;
		SPIPlanPtr	 plan;
		int			 i;

		values = palloc(natts * sizeof(Datum));
		nulls = palloc(natts * sizeof(char));

		attnums = hash_entry->attnums;
		plan = hash_entry->insert_history_plan;

		for (i = 0; i < natts; ++i)
		{
			bool isnull;

			values[i] = SPI_getbinval(tuple, tupdesc, attnums[i], &isnull);
			nulls[i] = isnull ? 'n' : ' ';
		}

		if ((ret = SPI_execp(plan, values, nulls, 0)) != SPI_OK_INSERT)
			elog(ERROR, "SPI_execp returned %d", ret);

		pfree(values);
		pfree(nulls);
	}

	/* Close the history relation. */
	relation_close(history_relation, AccessShareLock);

	if ((ret = SPI_finish()) != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish returned %d", ret);
}

/*
 * Deconstruct a range value of the system period attribute.
 *
 * If the value is null or is bounded on it's high side, an error is thrown.
 */
static void
deserialize_system_period(HeapTuple tuple,
						  Relation relation,
						  int period_attnum,
						  const char *period_attname,
						  TypeCacheEntry *typcache,
						  RangeBound *lower,
						  RangeBound *upper)
{
	bool		 isnull;
	Datum		 datum;
	RangeType	*system_period;
	bool		 empty;

	datum = SPI_getbinval(tuple, RelationGetDescr(relation), period_attnum,
						  &isnull);

	if (isnull)
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("system period column \"%s\" of relation \"%s\" must not be null",
						period_attname,
						RelationGetRelationName(relation))));

	system_period = DatumGetRangeTypeP(datum);

	range_deserialize(typcache, system_period, lower, upper, &empty);

	if (empty || !upper->infinite)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("system period column \"%s\" of relation \"%s\" contains invalid value",
						period_attname,
						RelationGetRelationName(relation)),
				 errdetail("valid ranges must be non-empty and unbounded on the high side")));
}

/*
 * Look up "integer_datetimes" configuration option.
 */
static void
lookup_integer_datetimes()
{
	const char	*value;

	value = GetConfigOption("integer_datetimes", false, true);

	integer_datetimes = (strcmp(value, "on") == 0);

	integer_datetimes_set = true;
}

/*
 * Add a minimal time interval to the specified timestamp. The returned value
 * is always not equal to this timestamp.
 */
static TimestampTz
next_timestamp(TimestampTz timestamp)
{
	if (!integer_datetimes_set)
		lookup_integer_datetimes();

	/* If Timestamp is int64, add 1 microsecond. */
	if (integer_datetimes)
		return (TimestampTz) ((int64) timestamp + 1);
	else
	{
		float8	ts;
		float8	next;

		/*
		 * If Timestamp is double, try adding 1 microsecond. If microsecond
		 * precision is not available for this value, return the next
		 * representable floating-point number.
		 */
		ts = (float8) timestamp;
		next = ts + 1E-06;
		if (next != ts)
			return (TimestampTz) next;

		return (TimestampTz) nextafter(ts, DBL_MAX);
	}
}

/*
 * Check that the upper bound is greater than the lower bound. If it is not
 * the case and adjust argument of the trigger is set to false, an exception
 * is thrown. If adjust argument is set to true, then the upper bound is set
 * to the lower bound plus delta.
 */
static void
adjust_system_period(TypeCacheEntry *typcache,
					 RangeBound *lower,
					 RangeBound *upper,
					 const char *adjust_argument,
					 Relation relation)
{
	if (range_cmp_bounds(typcache, lower, upper) >= 0)
	{
		TimestampTz next_ts;

		if (!parse_adjust_argument(adjust_argument))
			ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					 errmsg("system period value of relation \"%s\" cannot be set to a valid period because a row that is attempted to modify was also modified by another transaction",
							RelationGetRelationName(relation)),
					 errdetail("the start time of system period is %s but the start time of the current transaction is %s",
							   lower->infinite ? "-infinity" : timestamptz_to_str(DatumGetTimestampTz(lower->val)),
							   timestamptz_to_str(DatumGetTimestampTz(upper->val))),
					 errhint("retry the statement or set \"adjust\" parameter of \"versioning\" function to true")));

		ereport(WARNING,
				(errcode(ERRCODE_WARNING_SYSTEM_PERIOD_ADJUSTED),
				 errmsg("system period value of relation \"%s\" was adjusted",
						RelationGetRelationName(relation))));

		next_ts = next_timestamp(DatumGetTimestampTz(lower->val));

		upper->val = TimestampTzGetDatum(next_ts);
	}
}

/*
 * Check if the tuple was inserted or updated in the current transaction.
 */
static bool
modified_in_current_transaction(HeapTuple tuple)
{
	TransactionId	 oldxmin;

	oldxmin = HeapTupleHeaderGetXmin(tuple->t_data);
	return TransactionIdIsCurrentTransactionId(oldxmin);
}

/*
 * Tuple modification wrapper around SPI_modifytuple for PG<10
 * and heap_modify_tuple_by_cols for PG 10
 */
static HeapTuple
modify_tuple(Relation rel, HeapTuple tuple, int period_attnum, RangeType *range)
{
	int			 colnum[1] = { period_attnum };
	Datum		 values[1] = { RangeTypePGetDatum(range) };
#if PG_VERSION_NUM >= 100000
	bool		 nulls[1] = { false };
	return heap_modify_tuple_by_cols(tuple, RelationGetDescr(rel), 1, colnum, values, nulls);
#else
	char		 nulls[1] = { ' ' };
	return SPI_modifytuple(rel, tuple, 1, colnum, values, nulls);
#endif
}


/*
 * Set system period attribute value of the current row to
 * "[system_time, )".
 */
static Datum
versioning_insert(TriggerData *trigdata,
				  TypeCacheEntry *typcache,
				  int period_attnum)
{
	RangeBound	 lower;
	RangeBound	 upper;
	RangeType	*range;

	/* Construct a period for the current row. */
	lower.val = TimestampTzGetDatum(get_system_time());
	lower.infinite = false;
	lower.inclusive = true;
	lower.lower = true;

	upper.infinite = true;
	upper.inclusive = false;
	upper.lower = false;

	range = make_range(typcache, &lower, &upper, false);

	return PointerGetDatum(modify_tuple(trigdata->tg_relation, trigdata->tg_trigtuple, period_attnum, range));
}

/*
 * Set system period attribute value of the current row to
 * "[system_time, )", insert the original row into the history table
 * with the system period attribute value "[lower, system_time)".
 *
 * If lower is greater than or equals to system_time, adjust argument
 * determines whether timestamps adjustment are made or transaction should
 * fail.
 *
 * When a transaction makes multiple changes to a row, a history row is
 * generated only once.
 */
static Datum
versioning_update(TriggerData *trigdata,
				  TypeCacheEntry *typcache,
				  int period_attnum,
				  const char *period_attname,
				  const char *history_relation_argument,
				  const char *adjust_argument)
{
	HeapTuple		 tuple;
	Relation		 relation;
	RangeBound		 lower;
	RangeBound		 upper;
	RangeType		*range;
	HeapTuple		 history_tuple;

	tuple = trigdata->tg_trigtuple;

	/* Ignore tuples modified in this transaction. */
	if (modified_in_current_transaction(tuple))
		return PointerGetDatum(trigdata->tg_newtuple);

	relation = trigdata->tg_relation;

	deserialize_system_period(tuple, relation, period_attnum, period_attname,
							  typcache, &lower, &upper);

	/* Construct a period for the history row. */
	upper.val = TimestampTzGetDatum(get_system_time());
	upper.infinite = false;
	upper.inclusive = false;

	/* Adjust if needed. */
	adjust_system_period(typcache, &lower, &upper, adjust_argument, relation);

	range = make_range(typcache, &lower, &upper, false);

	history_tuple = modify_tuple(relation, tuple, period_attnum, range);

	insert_history_row(history_tuple, relation, history_relation_argument,
					   period_attname);

	/* Construct a period for the current row. */
	lower.val = upper.val;
	lower.infinite = false;
	lower.inclusive = true;

	upper.infinite = true;
	upper.inclusive = false;

	range = make_range(typcache, &lower, &upper, false);

	return PointerGetDatum(modify_tuple(relation, trigdata->tg_newtuple, period_attnum, range));
}

/*
 * Insert the original row into the history table with the system period
 * attribute value "[lower, system_time)".
 *
 * If lower is greater than or equals to system_time, adjust argument
 * determines whether timestamps adjustment are made or transaction should
 * fail.
 */
static Datum
versioning_delete(TriggerData *trigdata,
				  TypeCacheEntry *typcache,
				  int period_attnum,
				  const char *period_attname,
				  const char *history_relation_argument,
				  const char *adjust_argument)
{
	HeapTuple	 tuple;
	Relation	 relation;
	RangeBound	 lower;
	RangeBound	 upper;
	RangeType	*range;
	HeapTuple	 history_tuple;

	tuple = trigdata->tg_trigtuple;

	/* Ignore tuples modified in this transaction. */
	if (modified_in_current_transaction(tuple))
		return PointerGetDatum(tuple);

	relation = trigdata->tg_relation;

	deserialize_system_period(tuple, relation, period_attnum, period_attname,
							  typcache, &lower, &upper);

	/* Construct a period for the history row. */
	upper.val = TimestampTzGetDatum(get_system_time());
	upper.infinite = false;
	upper.inclusive = false;

	/* Adjust if needed. */
	adjust_system_period(typcache, &lower, &upper, adjust_argument, relation);

	range = make_range(typcache, &lower, &upper, false);

	history_tuple = modify_tuple(relation, tuple, period_attnum, range);

	insert_history_row(history_tuple, relation, history_relation_argument,
					   period_attname);

	return PointerGetDatum(tuple);
}

static void *
hash_entry_alloc(Size size)
{
	return MemoryContextAllocZero(TopMemoryContext, size);
}

/*
 * Initialize the internal hash table for cached data.
 */
static void
init_versioning_hash_table()
{
	HASHCTL	ctl;

	memset(&ctl, 0, sizeof(ctl));
	ctl.alloc = hash_entry_alloc;
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(VersioningHashEntry);
#if PG_VERSION_NUM < 90500
	ctl.hash = oid_hash;
#endif

	versioning_cache = hash_create("Versioning Hash",
								   128,
								   &ctl,
#if PG_VERSION_NUM < 90500
								   HASH_ALLOC | HASH_ELEM | HASH_FUNCTION
#else
								   HASH_ALLOC | HASH_ELEM | HASH_BLOBS
#endif
								  );
}

/*
 * Lookup for a versioned relation OID in the internal hash table of cached
 * data. If not found, return a new entry with all fields zeroed and
 * natts = -1.
 */
static VersioningHashEntry *
lookup_versioning_hash_entry(Oid relid,
							 bool *found)
{
	VersioningHashEntry	*entry;

	if (!versioning_cache)
		init_versioning_hash_table();

	entry = (VersioningHashEntry *) hash_search(versioning_cache,
												(void *) &relid,
												HASH_ENTER,
												found);

	if (!*found)
	{
		/* Mark a newly created entry invalid. */
		entry->natts = -1;
	}

	return entry;
}
