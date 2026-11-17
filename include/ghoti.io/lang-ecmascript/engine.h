/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Lang-ecmascript.
 *
 * Ghoti.io Lang-ecmascript is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License version 3
 * as published by the Free Software Foundation.
 *
 * Ghoti.io Lang-ecmascript is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file engine.h
 * @stability free
 *
 * The ECMAScript engine as runtime-core sees it.
 *
 * runtime-core runs guest code of any language on one execution context: the
 * context owns the guest stack, the budgets (fuel, memory, native stack),
 * pause and resume, and the requests other threads post. A language joins it
 * as an *engine*: it registers one static descriptor with each context it
 * runs in, and every frame it pushes names that descriptor, so a debugger, a
 * collector or a profiler reads an ECMAScript frame without knowing which
 * language pushed it.
 *
 * This header is that registration. The descriptor names the engine and, for
 * now, nothing else - each callback is NULL, which runtime-core reads as "the
 * engine has nothing to say". The interpreter fills them in as its frames
 * acquire slots, scopes and locations. A host does not need to call these
 * functions before running script; they are public so that a host which
 * shares one context among several languages can see which engines it holds,
 * and so that a tool walking frames can recognize this one.
 */

#ifndef GHOTI_IO_GLECMA_ENGINE_H
#define GHOTI_IO_GLECMA_ENGINE_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/core.h>
#include <ghoti.io/runtime-core/a/engine.h>
#include <ghoti.io/runtime-core/b/context.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The name the engine registers under, as its descriptor's `name`.
 *
 * The family name followed by this build's branch token ("ecmascript-0"), so
 * two majors of the library in one process register under distinct names
 * and a context never confuses one for the other.
 */
#define GLECMA_ENGINE_NAME "ecmascript" GHOTIIO_LANG_ECMASCRIPT_BRANCH

/**
 * @brief The engine's descriptor.
 *
 * One static object for the life of the process, so its address identifies
 * this engine in every context: a frame whose descriptor is this pointer was
 * pushed by this library.
 *
 * @return The descriptor; never NULL. Borrowed: do not free or modify it.
 */
GLECMA_API const GRCORE_EngineDescriptor * glecma_engine_descriptor(void);

/**
 * @brief Registers the engine with a context, once.
 *
 * Registration is idempotent, unlike ::grcore_engine_register, which refuses
 * a descriptor the context already holds: a second call on the same context
 * returns the id the first one was given. Several parts of a host (a realm,
 * a module loader, a debugger) can each make sure the engine is present
 * without agreeing on which of them goes first.
 *
 * @param context The context. The caller must own it, and it must not be
 *   running.
 * @param out_id Receives the engine's id in this context, never zero. Written
 *   only on success.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL argument, a context the
 *   caller does not own or one that is running; ::GLECMA_ERR_LIMIT if the
 *   context's memory budget refuses the engine table; ::GLECMA_ERR_OOM for any
 *   other allocation failure. A refusal leaves the context's engines as they
 *   were.
 */
GLECMA_API GLECMA_Result glecma_engine_register(GRCORE_Context * context,
    GRCORE_EngineId * out_id);

/**
 * @brief Finds the engine's id in a context, without registering it.
 *
 * Reads the context's engine table, so it is for the owner, or the holder of
 * an at-poll or paused context; not for a concurrent thread.
 *
 * @param context The context.
 * @param out_id Receives the engine's id. Written only on success.
 * @return ::GLECMA_OK when the engine is registered; ::GLECMA_ERR_INVALID for
 *   a NULL argument or a context that does not hold the engine.
 */
GLECMA_API GLECMA_Result glecma_engine_find(const GRCORE_Context * context,
    GRCORE_EngineId * out_id);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_ENGINE_H
