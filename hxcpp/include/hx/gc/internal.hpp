#pragma once

namespace hx::gc::detail {

// Internal constructor-publication fast path.  The complete managed payload
// must have null reference slots; all later reference writes use storeBarrier.
void publishEmpty(void* object);

// Complete a two-phase managed construction.  The object was made visible as
// an all-null exact object by publishEmpty, then initialized while rooted and
// pinned.  A concurrent marker must rescan it because C++ member initializers
// can write reference slots directly rather than through storeBarrier.
void finishConstruction(void* object);

} // namespace hx::gc::detail
