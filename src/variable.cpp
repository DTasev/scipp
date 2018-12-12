/// @file
/// SPDX-License-Identifier: GPL-3.0-or-later
/// @author Simon Heybrock
/// Copyright &copy; 2018 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
/// National Laboratory, and European Spallation Source ERIC.
#include "variable.h"
#include "dataset.h"
#include "except.h"
#include "variable_view.h"

template <template <class> class Op, class T> struct ArithmeticHelper {
  template <class InputView, class OutputView>
  static void apply(const OutputView &a, const InputView &b) {
    std::transform(a.begin(), a.end(), b.begin(), a.begin(), Op<T>());
  }
};

template <class T1, class T2> bool equal(const T1 &view1, const T2 &view2) {
  return std::equal(view1.begin(), view1.end(), view2.begin(), view2.end());
}

template <class T> class DataModel;
template <class T> class VariableConceptT;
template <class T> struct RebinHelper {
  static void rebin(const Dim dim, const gsl::span<const T> &oldModel,
                    const gsl::span<T> &newModel,
                    const VariableView<const T> &oldCoordView,
                    const gsl::index oldOffset,
                    const VariableView<const T> &newCoordView,
                    const gsl::index newOffset) {

    auto oldCoordIt = oldCoordView.begin();
    auto newCoordIt = newCoordView.begin();
    auto oldIt = oldModel.begin();
    auto newIt = newModel.begin();
    while (newIt != newModel.end() && oldIt != oldModel.end()) {
      if (&*(oldCoordIt + 1) == &(*oldCoordIt) + oldOffset) {
        // Last bin in this 1D subhistogram, go to next.
        ++oldCoordIt;
        ++oldIt;
        continue;
      }
      const auto xo_low = *oldCoordIt;
      const auto xo_high = *(&(*oldCoordIt) + oldOffset);
      if (&*(newCoordIt + 1) == &(*newCoordIt) + newOffset) {
        // Last bin in this 1D subhistogram, go to next.
        ++newCoordIt;
        ++newIt;
        continue;
      }
      const auto xn_low = *newCoordIt;
      const auto xn_high = *(&(*newCoordIt) + newOffset);
      if (xn_high <= xo_low) {
        // No overlap, go to next new bin
        ++newCoordIt;
        ++newIt;
      } else if (xo_high <= xn_low) {
        // No overlap, go to next old bin
        ++oldCoordIt;
        ++oldIt;
      } else {
        auto delta = xo_high < xn_high ? xo_high : xn_high;
        delta -= xo_low > xn_low ? xo_low : xn_low;
        *newIt += *oldIt * delta / (xo_high - xo_low);

        if (xn_high > xo_high) {
          ++oldCoordIt;
          ++oldIt;
        } else {
          ++newCoordIt;
          ++newIt;
        }
      }
    }
  }

  // Special rebin version for rebinning inner dimension to a joint new coord.
  static void rebinInner(const Dim dim, const VariableConceptT<T> &oldT,
                         VariableConceptT<T> &newT,
                         const VariableConceptT<T> &oldCoordT,
                         const VariableConceptT<T> &newCoordT) {
    const auto &oldData = oldT.getSpan();
    auto newData = newT.getSpan();
    const auto oldSize = oldT.dimensions().size(dim);
    const auto newSize = newT.dimensions().size(dim);
    const auto count = oldT.dimensions().volume() / oldSize;
    const auto *xold = &*oldCoordT.getSpan().begin();
    const auto *xnew = &*newCoordT.getSpan().begin();
#pragma omp parallel for
    for (gsl::index c = 0; c < count; ++c) {
      gsl::index iold = 0;
      gsl::index inew = 0;
      const auto oldOffset = c * oldSize;
      const auto newOffset = c * newSize;
      while ((iold < oldSize) && (inew < newSize)) {
        auto xo_low = xold[iold];
        auto xo_high = xold[iold + 1];
        auto xn_low = xnew[inew];
        auto xn_high = xnew[inew + 1];

        if (xn_high <= xo_low)
          inew++; /* old and new bins do not overlap */
        else if (xo_high <= xn_low)
          iold++; /* old and new bins do not overlap */
        else {
          // delta is the overlap of the bins on the x axis
          auto delta = xo_high < xn_high ? xo_high : xn_high;
          delta -= xo_low > xn_low ? xo_low : xn_low;

          auto owidth = xo_high - xo_low;
          newData[newOffset + inew] +=
              oldData[oldOffset + iold] * delta / owidth;

          if (xn_high > xo_high) {
            iold++;
          } else {
            inew++;
          }
        }
      }
    }
  }
};

#define DISABLE_ARITHMETICS_T(...)                                             \
  template <template <class> class Op, class T>                                \
  struct ArithmeticHelper<Op, __VA_ARGS__> {                                   \
    template <class... Args> static void apply(Args &&...) {                   \
      throw std::runtime_error(                                                \
          "Not an arithmetic type. Cannot apply operand.");                    \
    }                                                                          \
  };

DISABLE_ARITHMETICS_T(std::shared_ptr<T>)
DISABLE_ARITHMETICS_T(std::array<T, 4>)
DISABLE_ARITHMETICS_T(std::array<T, 3>)
DISABLE_ARITHMETICS_T(boost::container::small_vector<T, 1>)
DISABLE_ARITHMETICS_T(std::vector<T>)
DISABLE_ARITHMETICS_T(std::pair<T, T>)
DISABLE_ARITHMETICS_T(ValueWithDelta<T>)

template <template <class> class Op> struct ArithmeticHelper<Op, std::string> {
  template <class... Args> static void apply(Args &&...) {
    throw std::runtime_error("Cannot add strings. Use append() instead.");
  }
};

#define DISABLE_REBIN_T(...)                                                   \
  template <class T> struct RebinHelper<__VA_ARGS__> {                         \
    template <class... Args> static void rebin(Args &&...) {                   \
      throw std::runtime_error("Not and arithmetic type. Cannot rebin.");      \
    }                                                                          \
    template <class... Args> static void rebinInner(Args &&...) {              \
      throw std::runtime_error("Not and arithmetic type. Cannot rebin.");      \
    }                                                                          \
  };

#define DISABLE_REBIN(...)                                                     \
  template <> struct RebinHelper<__VA_ARGS__> {                                \
    template <class... Args> static void rebin(Args &&...) {                   \
      throw std::runtime_error("Not and arithmetic type. Cannot rebin.");      \
    }                                                                          \
    template <class... Args> static void rebinInner(Args &&...) {              \
      throw std::runtime_error("Not and arithmetic type. Cannot rebin.");      \
    }                                                                          \
  };

DISABLE_REBIN_T(std::shared_ptr<T>)
DISABLE_REBIN_T(std::array<T, 4>)
DISABLE_REBIN_T(std::array<T, 3>)
DISABLE_REBIN_T(std::vector<T>)
DISABLE_REBIN_T(boost::container::small_vector<T, 1>)
DISABLE_REBIN_T(std::pair<T, T>)
DISABLE_REBIN_T(ValueWithDelta<T>)
DISABLE_REBIN(Dataset)
DISABLE_REBIN(std::string)

VariableConcept::VariableConcept(const Dimensions &dimensions)
    : m_dimensions(dimensions){};

template <class T> class ViewModel;

template <class T> class VariableConceptT : public VariableConcept {
public:
  VariableConceptT(const Dimensions &dimensions)
      : VariableConcept(dimensions) {}

  virtual gsl::span<T> getSpan() = 0;
  virtual gsl::span<T> getSpan(const Dim dim, const gsl::index begin,
                               const gsl::index end) = 0;
  virtual gsl::span<const T> getSpan() const = 0;
  virtual gsl::span<const T> getSpan(const Dim dim, const gsl::index begin,
                                     const gsl::index end) const = 0;
  virtual VariableView<T> getView(const Dimensions &dims) = 0;
  virtual VariableView<T> getView(const Dimensions &dims, const Dim dim,
                                  const gsl::index begin) = 0;
  virtual VariableView<const T> getView(const Dimensions &dims) const = 0;
  virtual VariableView<const T> getView(const Dimensions &dims, const Dim dim,
                                        const gsl::index begin) const = 0;

  std::unique_ptr<VariableConcept> makeView() const override {
    auto &dims = dimensions();
    return std::make_unique<ViewModel<decltype(getView(dims))>>(dims,
                                                                getView(dims));
  }

  std::unique_ptr<VariableConcept> makeView() override {
    if (isConstView())
      return const_cast<const VariableConceptT &>(*this).makeView();
    auto &dims = dimensions();
    return std::make_unique<ViewModel<decltype(getView(dims))>>(dims,
                                                                getView(dims));
  }

  std::unique_ptr<VariableConcept>
  makeView(const Dim dim, const gsl::index begin,
           const gsl::index end) const override {
    auto dims = dimensions();
    if (end == -1)
      dims.erase(dim);
    else
      dims.resize(dim, end - begin);
    return std::make_unique<ViewModel<decltype(getView(dims, dim, begin))>>(
        dims, getView(dims, dim, begin));
  }

  std::unique_ptr<VariableConcept> makeView(const Dim dim,
                                            const gsl::index begin,
                                            const gsl::index end) override {
    if (isConstView())
      return const_cast<const VariableConceptT &>(*this).makeView(dim, begin,
                                                                  end);
    auto dims = dimensions();
    if (end == -1)
      dims.erase(dim);
    else
      dims.resize(dim, end - begin);
    return std::make_unique<ViewModel<decltype(getView(dims, dim, begin))>>(
        dims, getView(dims, dim, begin));
  }

  bool operator==(const VariableConcept &other) const override {
    if (dimensions() != other.dimensions())
      return false;
    const auto &otherT = dynamic_cast<const VariableConceptT &>(other);
    if (isContiguous()) {
      if (other.isContiguous() &&
          dimensions().isContiguousIn(other.dimensions())) {
        return equal(getSpan(), otherT.getSpan());
      } else {
        return equal(getSpan(), otherT.getView(dimensions()));
      }
    } else {
      if (other.isContiguous() &&
          dimensions().isContiguousIn(other.dimensions())) {
        return equal(getView(dimensions()), otherT.getSpan());
      } else {
        return equal(getView(dimensions()), otherT.getView(dimensions()));
      }
    }
  }

  template <template <class> class Op>
  VariableConcept &apply(const VariableConcept &other) {
    try {
      const auto &otherT = dynamic_cast<const VariableConceptT &>(other);
      if (isContiguous() && dimensions().contains(other.dimensions())) {
        if (other.isContiguous() &&
            dimensions().isContiguousIn(other.dimensions())) {
          ArithmeticHelper<Op, T>::apply(getSpan(), otherT.getSpan());
        } else {
          ArithmeticHelper<Op, T>::apply(getSpan(),
                                         otherT.getView(dimensions()));
        }
      } else if (dimensions().contains(other.dimensions())) {
        if (other.isContiguous() &&
            dimensions().isContiguousIn(other.dimensions())) {
          ArithmeticHelper<Op, T>::apply(getView(dimensions()),
                                         otherT.getSpan());
        } else {
          ArithmeticHelper<Op, T>::apply(getView(dimensions()),
                                         otherT.getView(dimensions()));
        }
      } else {
        // LHS has fewer dimensions than RHS, e.g., for computing sum. Use view.
        if (other.isContiguous() &&
            dimensions().isContiguousIn(other.dimensions())) {
          ArithmeticHelper<Op, T>::apply(getView(other.dimensions()),
                                         otherT.getSpan());
        } else {
          ArithmeticHelper<Op, T>::apply(getView(other.dimensions()),
                                         otherT.getView(other.dimensions()));
        }
      }
    } catch (const std::bad_cast &) {
      throw std::runtime_error("Cannot apply arithmetic operation to "
                               "Variables: Underlying data types do not "
                               "match.");
    }
    return *this;
  }

  VariableConcept &operator+=(const VariableConcept &other) override {
    return apply<std::plus>(other);
  }

  VariableConcept &operator-=(const VariableConcept &other) override {
    return apply<std::minus>(other);
  }

  VariableConcept &operator*=(const VariableConcept &other) override {
    return apply<std::multiplies>(other);
  }

  void copy(const VariableConcept &other, const Dim dim,
            const gsl::index offset, const gsl::index otherBegin,
            const gsl::index otherEnd) override {
    auto iterDims = dimensions();
    const gsl::index delta = otherEnd - otherBegin;
    if (iterDims.contains(dim))
      iterDims.resize(dim, delta);

    const auto &otherT = dynamic_cast<const VariableConceptT &>(other);
    auto otherView = otherT.getView(iterDims, dim, otherBegin);
    // Four cases for minimizing use of VariableView --- just copy contiguous
    // range where possible.
    if (isContiguous() && iterDims.isContiguousIn(dimensions())) {
      auto target = getSpan(dim, offset, offset + delta);
      if (other.isContiguous() && iterDims.isContiguousIn(other.dimensions())) {
        auto source = otherT.getSpan(dim, otherBegin, otherEnd);
        std::copy(source.begin(), source.end(), target.begin());
      } else {
        std::copy(otherView.begin(), otherView.end(), target.begin());
      }
    } else {
      auto view = getView(iterDims, dim, offset);
      if (other.isContiguous() && iterDims.isContiguousIn(other.dimensions())) {
        auto source = otherT.getSpan(dim, otherBegin, otherEnd);
        std::copy(source.begin(), source.end(), view.begin());
      } else {
        std::copy(otherView.begin(), otherView.end(), view.begin());
      }
    }
  }

  void rebin(const VariableConcept &old, const Dim dim,
             const VariableConcept &oldCoord,
             const VariableConcept &newCoord) override {
    // Dimensions of *this and old are guaranteed to be the same.
    const auto &oldT = dynamic_cast<const VariableConceptT &>(old);
    const auto &oldCoordT = dynamic_cast<const VariableConceptT &>(oldCoord);
    const auto &newCoordT = dynamic_cast<const VariableConceptT &>(newCoord);
    if (dimensions().label(0) == dim && oldCoord.dimensions().count() == 1 &&
        newCoord.dimensions().count() == 1) {
      RebinHelper<T>::rebinInner(dim, oldT, *this, oldCoordT, newCoordT);
    } else {
      auto oldCoordDims = oldCoord.dimensions();
      oldCoordDims.resize(dim, oldCoordDims.size(dim) - 1);
      auto newCoordDims = newCoord.dimensions();
      newCoordDims.resize(dim, newCoordDims.size(dim) - 1);
      auto oldCoordView = oldCoordT.getView(dimensions());
      auto newCoordView = newCoordT.getView(dimensions());
      const auto oldOffset = oldCoordDims.offset(dim);
      const auto newOffset = newCoordDims.offset(dim);

      RebinHelper<T>::rebin(dim, oldT.getSpan(), getSpan(), oldCoordView,
                            oldOffset, newCoordView, newOffset);
    }
  }
};

template <class T>
auto makeSpan(T &model, const Dimensions &dims, const Dim dim,
              const gsl::index begin, const gsl::index end) {
  if (!dims.contains(dim) && (begin != 0 || end != 1))
    throw std::runtime_error("VariableConcept: Slice index out of range.");
  if (!dims.contains(dim) || dims.size(dim) == end - begin) {
    return gsl::make_span(model.data(), model.data() + model.size());
  }
  const gsl::index beginOffset = begin * dims.offset(dim);
  const gsl::index endOffset = end * dims.offset(dim);
  return gsl::make_span(model.data() + beginOffset, model.data() + endOffset);
}

/// Implementation of VariableConcept that holds data.
template <class T>
class DataModel : public VariableConceptT<typename T::value_type> {
  using value_type = std::remove_const_t<typename T::value_type>;

public:
  DataModel(const Dimensions &dimensions, T model)
      : VariableConceptT<typename T::value_type>(std::move(dimensions)),
        m_model(std::move(model)) {
    if (this->dimensions().volume() != m_model.size())
      throw std::runtime_error("Creating Variable: data size does not match "
                               "volume given by dimension extents");
  }

  gsl::span<value_type> getSpan() override {
    return gsl::make_span(m_model.data(), m_model.data() + size());
  }
  gsl::span<value_type> getSpan(const Dim dim, const gsl::index begin,
                                const gsl::index end) override {
    return makeSpan(m_model, this->dimensions(), dim, begin, end);
  }

  gsl::span<const value_type> getSpan() const override {
    return gsl::make_span(m_model.data(), m_model.data() + size());
  }
  gsl::span<const value_type> getSpan(const Dim dim, const gsl::index begin,
                                      const gsl::index end) const override {
    return makeSpan(m_model, this->dimensions(), dim, begin, end);
  }

  VariableView<value_type> getView(const Dimensions &dims) override {
    return makeVariableView(m_model.data(), 0, dims, this->dimensions());
  }
  VariableView<value_type> getView(const Dimensions &dims, const Dim dim,
                                   const gsl::index begin) override {
    gsl::index beginOffset = this->dimensions().contains(dim)
                                 ? begin * this->dimensions().offset(dim)
                                 : begin * this->dimensions().volume();
    return makeVariableView(m_model.data(), beginOffset, dims,
                            this->dimensions());
  }

  VariableView<const value_type>
  getView(const Dimensions &dims) const override {
    return makeVariableView(m_model.data(), 0, dims, this->dimensions());
  }
  VariableView<const value_type>
  getView(const Dimensions &dims, const Dim dim,
          const gsl::index begin) const override {
    gsl::index beginOffset = this->dimensions().contains(dim)
                                 ? begin * this->dimensions().offset(dim)
                                 : begin * this->dimensions().volume();
    return makeVariableView(m_model.data(), beginOffset, dims,
                            this->dimensions());
  }

  std::shared_ptr<VariableConcept> clone() const override {
    return std::make_shared<DataModel<T>>(this->dimensions(), m_model);
  }

  std::unique_ptr<VariableConcept> cloneUnique() const override {
    return std::make_unique<DataModel<T>>(this->dimensions(), m_model);
  }

  std::unique_ptr<VariableConcept>
  cloneMutable(VariableConcept &mutableData) const override {
    throw std::runtime_error("DataModel::cloneMutable() should not be called.");
  }

  std::shared_ptr<VariableConcept>
  clone(const Dimensions &dims) const override {
    return std::make_shared<DataModel<T>>(dims, T(dims.volume()));
  }

  bool isContiguous() const override { return true; }
  bool isView() const override { return false; }
  bool isConstView() const override { return false; }

  gsl::index size() const override { return m_model.size(); }

  T m_model;
};

/// Implementation of VariableConcept that represents a view onto data.
template <class T>
class ViewModel
    : public VariableConceptT<std::remove_const_t<typename T::value_type>> {
  using value_type = std::remove_const_t<typename T::value_type>;

  void requireMutable() const {
    if (isConstView())
      throw std::runtime_error(
          "View is const, cannot get mutable range of data.");
  }
  void requireContiguous() const {
    if (!isContiguous())
      throw std::runtime_error(
          "View is not contiguous, cannot get contiguous range of data.");
  }

public:
  ViewModel(const Dimensions &dimensions, T model)
      : VariableConceptT<value_type>(std::move(dimensions)),
        m_model(std::move(model)) {
    if (this->dimensions().volume() != m_model.size())
      throw std::runtime_error("Creating Variable: data size does not match "
                               "volume given by dimension extents");
  }

  gsl::span<value_type> getSpan() override {
    requireMutable();
    requireContiguous();
    if constexpr (std::is_const<typename T::value_type>::value)
      return gsl::span<value_type>();
    else
      return gsl::make_span(m_model.data(), m_model.data() + size());
  }
  gsl::span<value_type> getSpan(const Dim dim, const gsl::index begin,
                                const gsl::index end) override {
    requireMutable();
    requireContiguous();
    if constexpr (std::is_const<typename T::value_type>::value) {
      return gsl::span<value_type>();
    } else {
      return makeSpan(m_model, this->dimensions(), dim, begin, end);
    }
  }

  gsl::span<const value_type> getSpan() const override {
    requireContiguous();
    return gsl::make_span(m_model.data(), m_model.data() + size());
  }
  gsl::span<const value_type> getSpan(const Dim dim, const gsl::index begin,
                                      const gsl::index end) const override {
    requireContiguous();
    return makeSpan(m_model, this->dimensions(), dim, begin, end);
  }

  VariableView<value_type> getView(const Dimensions &dims) override {
    requireMutable();
    if constexpr (std::is_const<typename T::value_type>::value)
      return VariableView<value_type>(nullptr, 0, {}, {});
    else
      return {m_model, dims};
  }
  VariableView<value_type> getView(const Dimensions &dims, const Dim dim,
                                   const gsl::index begin) override {
    requireMutable();
    if constexpr (std::is_const<typename T::value_type>::value)
      return VariableView<value_type>(nullptr, 0, {}, {});
    else
      return {m_model, dims, dim, begin};
  }

  VariableView<const value_type>
  getView(const Dimensions &dims) const override {
    return {m_model, dims};
  }
  VariableView<const value_type>
  getView(const Dimensions &dims, const Dim dim,
          const gsl::index begin) const override {
    return {m_model, dims, dim, begin};
  }

  std::shared_ptr<VariableConcept> clone() const override {
    return std::make_shared<ViewModel<T>>(this->dimensions(), m_model);
  }

  std::unique_ptr<VariableConcept> cloneUnique() const override {
    return std::make_unique<ViewModel<T>>(this->dimensions(), m_model);
  }

  std::unique_ptr<VariableConcept>
  cloneMutable(VariableConcept &mutableData) const override {
    auto *data = dynamic_cast<VariableConceptT<value_type> &>(mutableData)
                     .getSpan()
                     .data();
    return std::make_unique<ViewModel<VariableView<value_type>>>(
        this->dimensions(), m_model.createMutable(data));
  }

  std::shared_ptr<VariableConcept>
  clone(const Dimensions &dims) const override {
    throw std::runtime_error("Cannot resize view.");
  }

  bool isContiguous() const override {
    return this->dimensions().isContiguousIn(m_model.parentDimensions());
  }
  bool isView() const override { return true; }
  bool isConstView() const override {
    return std::is_const<typename T::value_type>::value;
  }

  gsl::index size() const override { return m_model.size(); }

  T m_model;
};

Variable::Variable(const ConstVariableSlice &slice)
    : Variable(*slice.m_variable) {
  *this = slice;
}

template <class T>
Variable::Variable(const Tag tag, const Unit::Id unit,
                   const Dimensions &dimensions, T object)
    : m_tag(tag), m_unit{unit},
      m_object(std::make_unique<DataModel<T>>(std::move(dimensions),
                                              std::move(object))) {}

Variable &Variable::operator=(const ConstVariableSlice &slice) {
  m_tag = slice.tag();
  m_name = slice.m_variable->m_name;
  setUnit(slice.unit());
  setDimensions(slice.dimensions());
  data().copy(slice.data(), Dim::Invalid, 0, 0, 1);
  return *this;
}

void Variable::setDimensions(const Dimensions &dimensions) {
  if (dimensions == m_object->dimensions())
    return;
  m_object = m_object->clone(dimensions);
}

template <class T> const Vector<T> &Variable::cast() const {
  return dynamic_cast<const DataModel<Vector<T>> &>(*m_object).m_model;
}

template <class T> Vector<T> &Variable::cast() {
  return dynamic_cast<DataModel<Vector<T>> &>(m_object.access()).m_model;
}

#define INSTANTIATE(...)                                                       \
  template Variable::Variable(const Tag, const Unit::Id, const Dimensions &,   \
                              Vector<__VA_ARGS__>);                            \
  template Vector<__VA_ARGS__> &Variable::cast<__VA_ARGS__>();                 \
  template const Vector<__VA_ARGS__> &Variable::cast<__VA_ARGS__>() const;

INSTANTIATE(std::string)
INSTANTIATE(double)
INSTANTIATE(char)
INSTANTIATE(int32_t)
INSTANTIATE(int64_t)
INSTANTIATE(std::pair<int64_t, int64_t>)
INSTANTIATE(ValueWithDelta<double>)
#if defined(_WIN32) || defined(__clang__) && defined(__APPLE__)
INSTANTIATE(gsl::index)
INSTANTIATE(std::pair<gsl::index, gsl::index>)
#endif
INSTANTIATE(boost::container::small_vector<gsl::index, 1>)
INSTANTIATE(std::vector<std::string>)
INSTANTIATE(std::vector<gsl::index>)
INSTANTIATE(Dataset)
INSTANTIATE(std::array<double, 3>)
INSTANTIATE(std::array<double, 4>)
INSTANTIATE(std::shared_ptr<std::array<double, 100>>)

template <class T1, class T2> bool equals(const T1 &a, const T2 &b) {
  // Compare even before pointer comparison since data may be shared even if
  // names differ.
  if (a.name() != b.name())
    return false;
  if (a.unit() != b.unit())
    return false;
  // Deep comparison
  if (a.tag() != b.tag())
    return false;
  if (!(a.dimensions() == b.dimensions()))
    return false;
  return a.data() == b.data();
}

bool Variable::operator==(const Variable &other) const {
  // Compare even before pointer comparison since data may be shared even if
  // names differ.
  if (name() != other.name())
    return false;
  if (unit() != other.unit())
    return false;
  // Trivial case: Pointers are equal
  if (m_object == other.m_object)
    return true;
  // Deep comparison
  if (tag() != other.tag())
    return false;
  if (!(dimensions() == other.dimensions()))
    return false;
  return data() == other.data();
}

bool Variable::operator==(const ConstVariableSlice &other) const {
  return equals(*this, other);
}

bool Variable::operator!=(const Variable &other) const {
  return !(*this == other);
}

bool Variable::operator!=(const ConstVariableSlice &other) const {
  return !(*this == other);
}

template <class T1, class T2> T1 &plus_equals(T1 &variable, const T2 &other) {
  // Addition with different Variable type is supported, mismatch of underlying
  // element types is handled in DataModel::operator+=.
  // Different name is ok for addition.
  if (variable.unit() != other.unit())
    throw std::runtime_error("Cannot add Variables: Units do not match.");
  if (!variable.template valueTypeIs<Data::Events>() &&
      !variable.template valueTypeIs<Data::Table>()) {
    if (variable.dimensions().contains(other.dimensions())) {
      // Note: This will broadcast/transpose the RHS if required. We do not
      // support changing the dimensions of the LHS though!
      variable.data() += other.data();
    } else {
      throw std::runtime_error(
          "Cannot add Variables: Dimensions do not match.");
    }
  } else {
    if (variable.dimensions() == other.dimensions()) {
      using ConstViewOrRef =
          std::conditional_t<std::is_same<T2, Variable>::value,
                             const Vector<Dataset> &,
                             const VariableView<const Dataset>>;
      ConstViewOrRef otherDatasets = other.template cast<Dataset>();
      if (otherDatasets.size() > 0 &&
          otherDatasets[0].dimensions().count() != 1)
        throw std::runtime_error(
            "Cannot add Variable: Nested Dataset dimension must be 1.");
      auto &datasets = variable.template cast<Dataset>();
      const Dim dim = datasets[0].dimensions().label(0);
#pragma omp parallel for
      for (gsl::index i = 0; i < datasets.size(); ++i)
        datasets[i] = concatenate(datasets[i], otherDatasets[i], dim);
    } else {
      throw std::runtime_error(
          "Cannot add Variables: Dimensions do not match.");
    }
  }
  return variable;
}

Variable &Variable::operator+=(const Variable &other) {
  return plus_equals(*this, other);
}
Variable &Variable::operator+=(const ConstVariableSlice &other) {
  return plus_equals(*this, other);
}

template <class T1, class T2> T1 &minus_equals(T1 &variable, const T2 &other) {
  if (variable.unit() != other.unit())
    throw std::runtime_error("Cannot subtract Variables: Units do not match.");
  if (variable.dimensions().contains(other.dimensions())) {
    if (variable.template valueTypeIs<Data::Events>())
      throw std::runtime_error("Subtraction of events lists not implemented.");
    variable.data() -= other.data();
  } else {
    throw std::runtime_error(
        "Cannot subtract Variables: Dimensions do not match.");
  }
  return variable;
}

Variable &Variable::operator-=(const Variable &other) {
  return minus_equals(*this, other);
}
Variable &Variable::operator-=(const ConstVariableSlice &other) {
  return minus_equals(*this, other);
}

template <class T1, class T2> T1 &times_equals(T1 &variable, const T2 &other) {
  if (!variable.dimensions().contains(other.dimensions()))
    throw std::runtime_error(
        "Cannot multiply Variables: Dimensions do not match.");
  if (variable.template valueTypeIs<Data::Events>())
    throw std::runtime_error("Multiplication of events lists not implemented.");
  // setUnit is catching bad cases of changing units (if `variable` is a slice).
  variable.setUnit(variable.unit() * other.unit());
  variable.data() *= other.data();
  return variable;
}

Variable &Variable::operator*=(const Variable &other) {
  return times_equals(*this, other);
}
Variable &Variable::operator*=(const ConstVariableSlice &other) {
  return times_equals(*this, other);
}

template <class T> VariableSlice &VariableSlice::assign(const T &other) {
  // TODO Should mismatching tags be allowed, as long as the type matches?
  if (tag() != other.tag())
    throw std::runtime_error("Cannot assign to slice: Type mismatch.");
  // Name mismatch ok, but do not assign it.
  if (unit() != other.unit())
    throw std::runtime_error("Cannot assign to slice: Unit mismatch.");
  if (dimensions() != other.dimensions())
    throw dataset::except::DimensionMismatchError(dimensions(),
                                                  other.dimensions());
  data().copy(other.data(), Dim::Invalid, 0, 0, 1);
  return *this;
}

template VariableSlice &VariableSlice::assign(const Variable &);
template VariableSlice &VariableSlice::assign(const ConstVariableSlice &);

VariableSlice &VariableSlice::operator+=(const Variable &other) {
  return plus_equals(*this, other);
}
VariableSlice &VariableSlice::operator+=(const ConstVariableSlice &other) {
  return plus_equals(*this, other);
}

VariableSlice &VariableSlice::operator-=(const Variable &other) {
  return minus_equals(*this, other);
}
VariableSlice &VariableSlice::operator-=(const ConstVariableSlice &other) {
  return minus_equals(*this, other);
}

VariableSlice &VariableSlice::operator*=(const Variable &other) {
  return times_equals(*this, other);
}
VariableSlice &VariableSlice::operator*=(const ConstVariableSlice &other) {
  return times_equals(*this, other);
}

template <class T> bool ConstVariableSlice::operator==(const T &other) const {
  // Always use deep comparison (pointer comparison does not make sense since we
  // may be looking at a different section).
  return equals(*this, other);
}

template bool ConstVariableSlice::operator==(const Variable &) const;
template bool ConstVariableSlice::operator==(const ConstVariableSlice &) const;

template <class T> bool ConstVariableSlice::operator!=(const T &other) const {
  return !(*this == other);
}

template bool ConstVariableSlice::operator!=(const Variable &) const;
template bool ConstVariableSlice::operator!=(const ConstVariableSlice &) const;

void VariableSlice::setUnit(const Unit &unit) {
  // TODO Should we forbid setting the unit altogether? I think it is useful in
  // particular since views onto subsets of dataset do not imply slicing of
  // variables but return slice views.
  if ((this->unit() != unit) &&
      (dimensions() != m_mutableVariable->dimensions()))
    throw std::runtime_error("Partial view on data of variable cannot be used "
                             "to change the unit.\n");
  m_mutableVariable->setUnit(unit);
}

template <class T>
const VariableView<const T> &ConstVariableSlice::cast() const {
  return dynamic_cast<const ViewModel<VariableView<const T>> &>(data()).m_model;
}

template <class T> VariableView<const T> VariableSlice::cast() const {
  if (m_view->isConstView())
    return {
        dynamic_cast<const ViewModel<VariableView<const T>> &>(data()).m_model,
        dimensions()};
  // Make a const view from the mutable one.
  return {dynamic_cast<const ViewModel<VariableView<T>> &>(data()).m_model,
          dimensions()};
}

template <class T> const VariableView<T> &VariableSlice::cast() {
  return dynamic_cast<const ViewModel<VariableView<T>> &>(data()).m_model;
}

#define INSTANTIATE_SLICEVIEW(...)                                             \
  template const VariableView<const __VA_ARGS__>                               \
      &ConstVariableSlice::cast<__VA_ARGS__>() const;                          \
  template VariableView<const __VA_ARGS__> VariableSlice::cast() const;        \
  template const VariableView<__VA_ARGS__> &VariableSlice::cast();

INSTANTIATE_SLICEVIEW(double);
INSTANTIATE_SLICEVIEW(int32_t);
INSTANTIATE_SLICEVIEW(char);
INSTANTIATE_SLICEVIEW(std::string);

void Variable::setSlice(const Variable &slice, const Dimension dim,
                        const gsl::index index) {
  if (m_unit != slice.m_unit)
    throw std::runtime_error("Cannot set slice: Units do not match.");
  if (m_object == slice.m_object)
    return;
  if (!dimensions().contains(slice.dimensions()))
    throw std::runtime_error("Cannot set slice: Dimensions do not match.");
  data().copy(slice.data(), dim, index, 0, 1);
}

ConstVariableSlice Variable::operator()(const Dim dim, const gsl::index begin,
                                        const gsl::index end) const {
  return {*this, dim, begin, end};
}

VariableSlice Variable::operator()(const Dim dim, const gsl::index begin,
                                   const gsl::index end) {
  return {*this, dim, begin, end};
}

Variable operator+(Variable a, const Variable &b) { return a += b; }
Variable operator-(Variable a, const Variable &b) { return a -= b; }
Variable operator*(Variable a, const Variable &b) { return a *= b; }

Variable slice(const Variable &var, const Dimension dim,
               const gsl::index index) {
  auto out(var);
  auto dims = out.dimensions();
  dims.erase(dim);
  out.setDimensions(dims);
  out.data().copy(var.data(), dim, 0, index, index + 1);
  return out;
}

Variable slice(const Variable &var, const Dimension dim, const gsl::index begin,
               const gsl::index end) {
  auto out(var);
  auto dims = out.dimensions();
  dims.resize(dim, end - begin);
  if (dims == out.dimensions())
    return out;
  out.setDimensions(dims);
  out.data().copy(var.data(), dim, 0, begin, end);
  return out;
}

// Example of a "derived" operation: Implementation does not require adding a
// virtual function to VariableConcept.
std::vector<Variable> split(const Variable &var, const Dim dim,
                            const std::vector<gsl::index> &indices) {
  if (indices.empty())
    return {var};
  std::vector<Variable> vars;
  vars.emplace_back(slice(var, dim, 0, indices.front()));
  for (gsl::index i = 0; i < indices.size() - 1; ++i)
    vars.emplace_back(slice(var, dim, indices[i], indices[i + 1]));
  vars.emplace_back(
      slice(var, dim, indices.back(), var.dimensions().size(dim)));
  return vars;
}

Variable concatenate(const Variable &a1, const Variable &a2,
                     const Dimension dim) {
  if (a1.tag() != a2.tag())
    throw std::runtime_error(
        "Cannot concatenate Variables: Data types do not match.");
  if (a1.unit() != a2.unit())
    throw std::runtime_error(
        "Cannot concatenate Variables: Units do not match.");
  if (a1.name() != a2.name())
    throw std::runtime_error(
        "Cannot concatenate Variables: Names do not match.");
  const auto &dims1 = a1.dimensions();
  const auto &dims2 = a2.dimensions();
  // TODO Many things in this function should be refactored and moved in class
  // Dimensions.
  // TODO Special handling for edge variables.
  for (const auto &dim1 : dims1.labels()) {
    if (dim1 != dim) {
      if (!dims2.contains(dim1))
        throw std::runtime_error(
            "Cannot concatenate Variables: Dimensions do not match.");
      if (dims2.size(dim1) != dims1.size(dim1))
        throw std::runtime_error(
            "Cannot concatenate Variables: Dimension extents do not match.");
    }
  }
  auto size1 = dims1.count();
  auto size2 = dims2.count();
  if (dims1.contains(dim))
    size1--;
  if (dims2.contains(dim))
    size2--;
  // This check covers the case of dims2 having extra dimensions not present in
  // dims1.
  // TODO Support broadcast of dimensions?
  if (size1 != size2)
    throw std::runtime_error(
        "Cannot concatenate Variables: Dimensions do not match.");

  auto out(a1);
  auto dims(dims1);
  gsl::index extent1 = 1;
  gsl::index extent2 = 1;
  if (dims1.contains(dim))
    extent1 += dims1.size(dim) - 1;
  if (dims2.contains(dim))
    extent2 += dims2.size(dim) - 1;
  if (dims.contains(dim))
    dims.resize(dim, extent1 + extent2);
  else
    dims.add(dim, extent1 + extent2);
  out.setDimensions(dims);

  out.data().copy(a1.data(), dim, 0, 0, extent1);
  out.data().copy(a2.data(), dim, extent1, 0, extent2);

  return out;
}

Variable rebin(const Variable &var, const Variable &oldCoord,
               const Variable &newCoord) {
  auto rebinned(var);
  auto dims = rebinned.dimensions();
  const Dim dim = coordDimension[newCoord.tag().value()];
  dims.resize(dim, newCoord.dimensions().size(dim) - 1);
  rebinned.setDimensions(dims);
  // TODO take into account unit if values have been divided by bin width.
  rebinned.data().rebin(var.data(), dim, oldCoord.data(), newCoord.data());
  return rebinned;
}

Variable permute(const Variable &var, const Dimension dim,
                 const std::vector<gsl::index> &indices) {
  auto permuted(var);
  for (gsl::index i = 0; i < indices.size(); ++i)
    permuted.data().copy(var.data(), dim, i, indices[i], indices[i] + 1);
  return permuted;
}

Variable filter(const Variable &var, const Variable &filter) {
  if (filter.dimensions().ndim() != 1)
    throw std::runtime_error(
        "Cannot filter variable: The filter must by 1-dimensional.");
  const auto dim = filter.dimensions().labels()[0];
  auto mask = filter.get<const Coord::Mask>();

  const gsl::index removed = std::count(mask.begin(), mask.end(), 0);
  if (removed == 0)
    return var;

  auto out(var);
  auto dims = out.dimensions();
  dims.resize(dim, dims.size(dim) - removed);
  out.setDimensions(dims);

  gsl::index iOut = 0;
  // Note: Could copy larger chunks of applicable for better(?) performance.
  // Note: This implementation is inefficient, since we need to cast to concrete
  // type for *every* slice. Should be combined into a single virtual call.
  for (gsl::index iIn = 0; iIn < mask.size(); ++iIn)
    if (mask[iIn])
      out.data().copy(var.data(), dim, iOut++, iIn, iIn + 1);
  return out;
}

Variable sum(const Variable &var, const Dim dim) {
  auto summed(var);
  auto dims = summed.dimensions();
  dims.erase(dim);
  // setDimensions zeros the data
  summed.setDimensions(dims);
  summed.data() += var.data();
  return summed;
}

Variable mean(const Variable &var, const Dim dim) {
  auto summed = sum(var, dim);
  double scale = 1.0 / var.dimensions().size(dim);
  return summed * makeVariable<Data::Value>({}, {scale});
}
