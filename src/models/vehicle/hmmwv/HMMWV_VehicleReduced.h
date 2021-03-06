// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All right reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Radu Serban, Justin Madsen
// =============================================================================
//
// HMMWV 9-body vehicle model...
//
// =============================================================================

#ifndef HMMWV_VEHICLE_REDUCED_H
#define HMMWV_VEHICLE_REDUCED_H

#include "chrono/core/ChCoordsys.h"
#include "chrono/physics/ChMaterialSurfaceBase.h"
#include "chrono/physics/ChSystem.h"

#include "chrono_vehicle/wheeled_vehicle/ChWheeledVehicle.h"

#include "models/ChApiModels.h"
#include "models/vehicle/hmmwv/HMMWV_BrakeSimple.h"
#include "models/vehicle/hmmwv/HMMWV_DoubleWishboneReduced.h"
#include "models/vehicle/hmmwv/HMMWV_Driveline2WD.h"
#include "models/vehicle/hmmwv/HMMWV_Driveline4WD.h"
#include "models/vehicle/hmmwv/HMMWV_RackPinion.h"
#include "models/vehicle/hmmwv/HMMWV_Wheel.h"

namespace chrono {
namespace vehicle {
namespace hmmwv {

class CH_MODELS_API HMMWV_VehicleReduced : public ChWheeledVehicle {
  public:
    HMMWV_VehicleReduced(
        const bool fixed = false,
        DrivelineType driveType = AWD,
        VisualizationType chassisVis = NONE,
        VisualizationType wheelVis = PRIMITIVES,
        ChMaterialSurfaceBase::ContactMethod contactMethod = ChMaterialSurfaceBase::DVI);

    HMMWV_VehicleReduced(ChSystem* system,
                         const bool fixed = false,
                         DrivelineType driveType = AWD,
                         VisualizationType chassisVis = NONE,
                         VisualizationType wheelVis = PRIMITIVES);

    ~HMMWV_VehicleReduced();

    virtual int GetNumberAxles() const override { return 2; }

    virtual ChCoordsys<> GetLocalDriverCoordsys() const override { return m_driverCsys; }

    virtual void Initialize(const ChCoordsys<>& chassisPos) override;

    void ExportMeshPovray(const std::string& out_dir);

  private:
    void Create(bool fixed, VisualizationType chassisVis, VisualizationType wheelVis);

    DrivelineType m_driveType;

    // Chassis visualization mesh
    static const std::string m_chassisMeshName;
    static const std::string m_chassisMeshFile;

    // Chassis mass properties
    static const double m_chassisMass;
    static const ChVector<> m_chassisCOM;
    static const ChVector<> m_chassisInertia;

    // Driver local coordinate system
    static const ChCoordsys<> m_driverCsys;
};

}  // end namespace hmmwv
}  // end namespace vehicle
}  // end namespace chrono

#endif
