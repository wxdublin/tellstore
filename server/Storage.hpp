/*
 * (C) Copyright 2015 ETH Zurich Systems Group (http://www.systems.ethz.ch/) and others.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Contributors:
 *     Markus Pilman <mpilman@inf.ethz.ch>
 *     Simon Loesing <sloesing@inf.ethz.ch>
 *     Thomas Etter <etterth@gmail.com>
 *     Kevin Bocksrocker <kevin.bocksrocker@gmail.com>
 *     Lucas Braun <braunl@inf.ethz.ch>
 */
#pragma once

#include <config.h>

#if defined USE_DELTA_MAIN_REWRITE
#include <deltamain/DeltaMainRewriteStore.hpp>
#elif defined USE_LOGSTRUCTURED_MEMORY
#include <logstructured/LogstructuredMemoryStore.hpp>
#else
#error "Unknown implementation"
#endif

namespace tell {
namespace store {

#if defined USE_DELTA_MAIN_REWRITE
#if defined USE_ROW_STORE
using Storage = DeltaMainRewriteRowStore;
#elif defined USE_COLUMN_MAP
using Storage = DeltaMainRewriteColumnStore;
#else
#error "Unknown implementation"
#endif

#elif defined USE_LOGSTRUCTURED_MEMORY
using Storage = LogstructuredMemoryStore;

#else
#error "Unknown implementation"
#endif

} // namespace store
} // namespace tell
