/// @file
/// SPDX-License-Identifier: GPL-3.0-or-later
/// @author Simon Heybrock
/// Copyright &copy; 2018 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
/// National Laboratory, and European Spallation Source ERIC.
#include <gtest/gtest.h>

#include "test_macros.h"

#include "dataset_index.h"
#include "dataset_view.h"

TEST(ExampleInstrument, basics) {
  gsl::index ndet = 10;

  Dataset detectors;
  detectors.insert<Coord::DetectorId>({Dim::Detector, ndet},
                                 {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
  detectors.insert<Data::Position>("", {Dim::Detector, ndet}, ndet,
                                   Eigen::Vector3d{0.0, 0.0, 2.0});
  for (auto &det :
       DatasetView<const Coord::DetectorId, Data::Position>(detectors))
    det.get<Data::Position>().x() = 0.01 * det.get<Coord::DetectorId>();

  Dataset components;
  components.insert<Data::Position>("source", {}, 1,
                                    Eigen::Vector3d{0.0, 0.0, -10.0});
  components.insert<Data::Position>("sample", {}, 1,
                                    Eigen::Vector3d{0.0, 0.0, 0.0});

  Dataset d;
  d.insert<Coord::DetectorInfo>({}, {detectors});
  d.insert<Coord::ComponentInfo>({}, {components});
}
