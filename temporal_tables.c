/* -------------------------------------------------------------------------
 *
 * temporal_tables.c
 *
 * Copyright (c) 2012-2023 Vladislav Arkhipov <vlad@arkhipov.ru>
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"

#include "nodes/pg_list.h"
#include "utils/memutils.h"

#include "temporal_tables.h"
#include "utils/elog.h"

PG_MODULE_MAGIC;

PGDLLEXPORT void _PG_init(void);

static void temporal_tables_xact_callback(XactEvent event,
										  void *arg);

static void temporal_tables_subxact_callback(SubXactEvent event,
											 SubTransactionId mySubid,
											 SubTransactionId parentSubid,
											 void *arg);

/* TemporalContext stack */
static List *temporal_contexts;

void
_PG_init(void)
{
	MemoryContext		oldcxt;
	TemporalContext	   *ctx;

	// Create the TemporalContexts stack and add the top temporal context
	// into it. We use the special value InvalidSubTransactionId to denote
	// the top TemporalContext. This context is never modified directly
	// since we will set it as the current context in the case of a
	// transaction rolling back. If a transaction commits, we will copy the
	// current TemporalContext content to this context. Note that this
	// context is created in the TopMemoryContext as it must survive
	// transaction boundaries.
	oldcxt = MemoryContextSwitchTo(TopMemoryContext);

	ctx = palloc0(sizeof(TemporalContext));
	ctx->system_time_mode = CurrentTransactionStartTimestamp;
	ctx->subid = InvalidSubTransactionId;

	temporal_contexts = list_make1(ctx);

	MemoryContextSwitchTo(oldcxt);

	// Register some callback functions that manage the TemporalContexts
	// stack.
	RegisterXactCallback(temporal_tables_xact_callback, NULL);
	RegisterSubXactCallback(temporal_tables_subxact_callback, NULL);
}

static void
copy_temporal_context(TemporalContext *dest, TemporalContext *src,
					  SubTransactionId subid)
{
	memcpy(dest, src, sizeof(TemporalContext));
	dest->subid = subid;
}

static TemporalContext *
push_temporal_context(SubTransactionId subid)
{
	MemoryContext oldcxt;
	TemporalContext *ctx;

	// We use the TopTransactionContext because we sometimes propagate the
	// context to the parent subtransaction without copying it.
	oldcxt = MemoryContextSwitchTo(TopTransactionContext);

	ctx = palloc(sizeof(TemporalContext));
	copy_temporal_context(ctx, linitial(temporal_contexts), subid);

	temporal_contexts = lcons(ctx, temporal_contexts);

	MemoryContextSwitchTo(oldcxt);

	return ctx;
}

/*
 * A callback routine on a transaction commit/abort. It pops the top from
 * the TemporalContexts stack and copy its content to the top
 * TemporalContext in the case of the transaction committed.
 */
static void
temporal_tables_xact_callback(XactEvent event, void *arg)
{
	if (event == XACT_EVENT_COMMIT || event == XACT_EVENT_ABORT)
	{
		TemporalContext *ctx = linitial(temporal_contexts);
		if (ctx->subid != InvalidSubTransactionId)
		{
			temporal_contexts = list_delete_first(temporal_contexts);

			if (event == XACT_EVENT_COMMIT)
			{
				TemporalContext *top_ctx = linitial(temporal_contexts);

				Assert(top_ctx->subid == InvalidSubTransactionId);

				copy_temporal_context(top_ctx, ctx,
									  InvalidSubTransactionId);
			}
		}
	}
}

/*
 * A callback routine on a subtransaction commit/abort. It pops the top from
 * the TemporalContexts stack.
 */
static void
temporal_tables_subxact_callback(SubXactEvent event,
								 SubTransactionId mySubid,
								 SubTransactionId parentSubid, void *arg)
{
	if (event == SUBXACT_EVENT_COMMIT_SUB ||
		event == SUBXACT_EVENT_ABORT_SUB)
	{
		TemporalContext *ctx = linitial(temporal_contexts);

		// If subid does not equal to the current subtransaction id, that
		// means that we have not pushed a new temporal context in this
		// subtransaction, so there is nothing to pop here.
		if (ctx->subid == GetCurrentSubTransactionId())
		{
			if (event == SUBXACT_EVENT_ABORT_SUB)
			{
				temporal_contexts = list_delete_first(temporal_contexts);
				pfree(ctx);
			}
			else // SUBXACT_EVENT_COMMIT_SUB
			{
				TemporalContext *parent_ctx = lsecond(temporal_contexts);

				if (parent_ctx->subid != parentSubid)
				{
					ctx->subid = parentSubid;
				}
				else
				{
					copy_temporal_context(parent_ctx, ctx, parentSubid);
					temporal_contexts = list_delete_first(temporal_contexts);
					pfree(ctx);
				}
			}
		}
	}
}

TemporalContext *
get_current_temporal_context(bool will_modify)
{
	TemporalContext	   *ctx = linitial(temporal_contexts);
	SubTransactionId	subid;

	// Since the operation is read-only, we can safely return the top from
	// the TemporalContexts stack as that TemporalContext has not been
	// changed since the transaction it was created in.
	if (!will_modify)
		return ctx;

	subid = GetCurrentSubTransactionId();
	if (ctx->subid == subid)
		return ctx;

	return push_temporal_context(subid);
}
