/// @file
/// SPDX-License-Identifier: GPL-3.0-or-later
/// @author Simon Heybrock
/// Copyright &copy; 2018 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
/// National Laboratory, and European Spallation Source ERIC.
#include <gsl/span>

#define EXPECT_THROW_MSG(TRY_BLOCK, EXCEPTION_TYPE, MESSAGE)                   \
  EXPECT_THROW({                                                               \
    try {                                                                      \
      TRY_BLOCK;                                                               \
    } catch (const EXCEPTION_TYPE &e) {                                        \
      EXPECT_STREQ(MESSAGE, e.what());                                         \
      throw;                                                                   \
    }                                                                          \
  }, EXCEPTION_TYPE);

template <class T1, class T2>
bool equals(const T1 &a, const std::initializer_list<T2> &b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end());
}
