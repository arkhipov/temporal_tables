/* -------------------------------------------------------------------------
 *
 * temporal_tables.h
 *
 * Copyright (c) 2012-2017 Vladislav Arkhipov <vlad@arkhipov.ru>
 *
 * -------------------------------------------------------------------------
 */
#ifndef __TEMPORAL_TABLES_H__
#define __TEMPORAL_TABLES_H__

#include "postgres.h"
#include "fmgr.h"

#include "access/xact.h"

typedef enum SystemTimeMode
{
	/* use CURRENT_TIMESTAMP */
	CurrentTransactionStartTimestamp,

	/* use the value stored in the system_time field of TemporalContext */
	UserDefined
} SystemTimeMode;

typedef struct TemporalContext
{
	/* the subtransaction that this context was created in */
	SubTransactionId	subid;

	/* the current system time mode */
	SystemTimeMode		system_time_mode;

	/* the system time that is used by triggers in the UserDefined mode */
	TimestampTz			system_time;
} TemporalContext;

/* Get the TemporalContext that is associated with the current transaction. If
 * will_modify is true, the caller is permitted to modify the returned value.
 * Otherwise, the returned value must not be changed.
 */
TemporalContext *get_current_temporal_context(bool will_modify);

#endif
