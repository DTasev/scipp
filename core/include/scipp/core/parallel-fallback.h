// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2020 Scipp contributors (https://github.com/scipp)
/// @file
/// @author Simon Heybrock
#ifndef SCIPP_CORE_PARALLEL_H
#define SCIPP_CORE_PARALLEL_H

#include "scipp/common/index.h"

/// Fallback wrappers without actual threading, in case TBB is not available.
namespace scipp::core::parallel {

class blocked_range {
public:
  constexpr blocked_range(const scipp::index begin, const scipp::index end,
                          const scipp::index grainsize = 1) noexcept
      : m_begin(begin), m_end(end) {
    static_cast<void>(grainsize);
  }
  constexpr scipp::index begin() const noexcept { return m_begin; }
  constexpr scipp::index end() const noexcept { return m_end; }

private:
  scipp::index m_begin;
  scipp::index m_end;
};

template <class Op> void parallel_for(const blocked_range &range, Op &&op) {
  op(range);
}

} // namespace scipp::core::parallel

#endif // SCIPP_CORE_PARALLEL_H