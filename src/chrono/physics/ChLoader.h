//
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2010 Alessandro Tasora
// Copyright (c) 2013 Project Chrono
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the top level of the distribution
// and at http://projectchrono.org/license-chrono.txt.
//

#ifndef CHLOADER_H
#define CHLOADER_H


#include "core/ChSmartpointers.h"
#include "core/ChVectorDynamic.h"
#include "physics/ChLoadable.h"
#include "chrono/core/ChQuadrature.h"

namespace chrono {



/// Class for loads applied to a single ChLoadable object.
/// Loads can be forces, torques, pressures, thermal loads, etc. depending
/// on the loaded ChLoadable object. For example if the load references
/// a ChBody, the load is a wrench (force+torque), for a tetrahedron FE it is a force, etc.
/// Objects of this class must be capable of computing the generalized load Q from 
/// the load F.

class ChLoader  {
public:
    ChVectorDynamic<> Q;

    virtual void ComputeQ( ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate Q
                           ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate Q
                          ) = 0;

    virtual ChSharedPtr<ChLoadable> GetLoadable() =0;

    virtual bool IsStiff() {return false;}
};


////////////////////////////////////////////////////////////////////////////////


/// Class of loaders for ChLoadableUVW objects (which support 
/// volume loads).

class ChLoaderUVW : public ChLoader {
public:
    typedef ChLoadableUVW type_loadable;

    ChSharedPtr<ChLoadableUVW> loadable;
    
    ChLoaderUVW(ChSharedPtr<ChLoadableUVW> mloadable) : 
          loadable(mloadable) {};

            /// Children classes must provide this function that evaluates F = F(u,v,w)
            /// This will be evaluated during ComputeQ() to perform integration over the domain.
    virtual void ComputeF(const double U,             ///< parametric coordinate in volume
                          const double V,             ///< parametric coordinate in volume
                          const double W,             ///< parametric coordinate in volume 
                          ChVectorDynamic<>& F,        ///< Result F vector here, size must be = n.field coords.of loadable
                          ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate F
                          ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate F
                          ) = 0;

    void SetLoadable(ChSharedPtr<ChLoadableUVW>mloadable) {loadable = mloadable;}
    virtual ChSharedPtr<ChLoadable> GetLoadable() {return loadable;}
    ChSharedPtr<ChLoadableUVW> GetLoadableUVW() {return loadable;}

};


/// Class of loaders for ChLoadableUVW objects (which support 
/// volume loads), for loads of distributed type, so these loads
/// will undergo Gauss quadrature to integrate them in the volume.

class ChLoaderUVWdistributed : public ChLoaderUVW {
public:

    ChLoaderUVWdistributed(ChSharedPtr<ChLoadableUVW> mloadable) : 
          ChLoaderUVW(mloadable) {};

    virtual int GetIntegrationPointsU() = 0;
    virtual int GetIntegrationPointsV() = 0;
    virtual int GetIntegrationPointsW() = 0;

            /// Computes Q = integral (N'*F*detJ dudvdz) 
    virtual void ComputeQ( ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate Q
                           ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate Q
                          ) {
        assert(GetIntegrationPointsU() <= ChQuadrature::GetStaticTables()->Lroots.size());
        assert(GetIntegrationPointsV() <= ChQuadrature::GetStaticTables()->Lroots.size());
        assert(GetIntegrationPointsW() <= ChQuadrature::GetStaticTables()->Lroots.size());

        Q.Reset(loadable->LoadableGet_ndof_w());
        ChVectorDynamic<> mF(loadable->Get_field_ncoords());
 
        std::vector<double>* Ulroots = &ChQuadrature::GetStaticTables()->Lroots[GetIntegrationPointsU()-1];
        std::vector<double>* Uweight = &ChQuadrature::GetStaticTables()->Weight[GetIntegrationPointsU()-1];
        std::vector<double>* Vlroots = &ChQuadrature::GetStaticTables()->Lroots[GetIntegrationPointsV()-1];
        std::vector<double>* Vweight = &ChQuadrature::GetStaticTables()->Weight[GetIntegrationPointsV()-1];
        std::vector<double>* Wlroots = &ChQuadrature::GetStaticTables()->Lroots[GetIntegrationPointsW()-1];
        std::vector<double>* Wweight = &ChQuadrature::GetStaticTables()->Weight[GetIntegrationPointsW()-1];

        ChVectorDynamic<> mNF (Q.GetRows());        // temporary value for loop
        
        // Gauss quadrature :  Q = sum (N'*F*detJ * wi*wj*wk)
        for (unsigned int iu = 0; iu < Ulroots->size(); iu++) {
            for (unsigned int iv = 0; iv < Vlroots->size(); iv++) {
                for (unsigned int iw = 0; iw < Wlroots->size(); iw++) {
                    double detJ;
                    // Compute F= F(u,v,w)
                    this->ComputeF(Ulroots->at(iu),Vlroots->at(iv),Wlroots->at(iw), 
                                    mF, state_x, state_w);
                    // Compute mNF= N(u,v,w)'*F
                    loadable->ComputeNF(Ulroots->at(iu),Vlroots->at(iv),Wlroots->at(iw),
                                        mNF, detJ, mF, state_x, state_w);
                    // Compute Q+= mNF detJ * wi*wj*wk
                    mNF *= (detJ * Uweight->at(iu) * Vweight->at(iv) * Wweight->at(iw));
                    Q += mNF;
                }
            }
        }
    }

};

/// Class of loaders for ChLoadableUVW objects (which support 
/// volume loads) of atomic type, that is, with a concentrated load in a point Pu,Pv,Pz

class ChLoaderUVWatomic : public ChLoaderUVW {
public:
    double Pu;
    double Pv;
    double Pw;

    ChLoaderUVWatomic(ChSharedPtr<ChLoadableUVW> mloadable, const double mU, const double mV, const double mW) : 
          ChLoaderUVW(mloadable)
        {};

            /// Computes Q = N'*F
    virtual void ComputeQ( ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate Q
                           ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate Q
                          ) {
        Q.Reset(loadable->LoadableGet_ndof_w());
        ChVectorDynamic<> mF(loadable->Get_field_ncoords());
 
        double detJ; // not used btw
        
        // Compute F=F(u,v,w)
        this->ComputeF(Pu,Pv,Pw, mF, state_x, state_w);

        // Compute N(u,v,w)'*F
        loadable->ComputeNF(Pu,Pv,Pw, Q, detJ, mF, state_x, state_w);
    }

        /// Set the position, in the volume, where the atomic load is applied
    void SetApplication(double mu, double mv, double mw) {Pu=mu; Pv=mv; Pw=mw;}
};



/// A very usual type of volume loader: the constant gravitational load on Y

class ChLoaderGravity : public ChLoaderUVWdistributed {
private:

public:    
    ChLoaderGravity(ChSharedPtr<ChLoadableUVW> mloadable) :
            ChLoaderUVWdistributed(mloadable)
        {};

    virtual void ComputeF(const double U,             ///< parametric coordinate in volume
                          const double V,             ///< parametric coordinate in volume
                          const double W,             ///< parametric coordinate in volume 
                          ChVectorDynamic<>& F,       ///< Result F vector here, size must be = n.field coords.of loadable
                          ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate F
                          ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate F
                          ) {
        assert((F.GetRows() == 3) | (F.GetRows() == 6)); //only for force or wrench fields
        //F(0)=0;
        F(1)=-9.8* loadable->GetDensity();
        //F(2)=0;
    }

    virtual int GetIntegrationPointsU() {return 1;}
    virtual int GetIntegrationPointsV() {return 1;}
    virtual int GetIntegrationPointsW() {return 1;}
};



////////////////////////////////////////////////////////////////////////////////


/// Class of loaders for ChLoadableUV objects (which support 
/// surface loads).

class ChLoaderUV : public ChLoader {
public:
    typedef ChLoadableUV type_loadable;

    ChSharedPtr<ChLoadableUV> loadable;
      
    ChLoaderUV(ChSharedPtr<ChLoadableUV> mloadable) : 
          loadable(mloadable) {};

            /// Children classes must provide this function that evaluates F = F(u,v)
            /// This will be evaluated during ComputeQ() to perform integration over the domain.
    virtual void ComputeF(const double U,             ///< parametric coordinate in surface
                          const double V,             ///< parametric coordinate in surface
                          ChVectorDynamic<>& F,        ///< Result F vector here, size must be = n.field coords.of loadable
                          ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate F
                          ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate F
                          ) = 0;

    void SetLoadable(ChSharedPtr<ChLoadableUV>mloadable) {loadable = mloadable;}
    virtual ChSharedPtr<ChLoadable> GetLoadable() {return loadable;}
    ChSharedPtr<ChLoadableUV> GetLoadableUV() {return loadable;}

};


/// Class of loaders for ChLoadableUV objects (which support 
/// surface loads), for loads of distributed type, so these loads
/// will undergo Gauss quadrature to integrate them in the surface.

class ChLoaderUVdistributed : public ChLoaderUV {
public:
    
    ChLoaderUVdistributed(ChSharedPtr<ChLoadableUV> mloadable) : 
          ChLoaderUV(mloadable) {};

    virtual int GetIntegrationPointsU() = 0;
    virtual int GetIntegrationPointsV() = 0;

            /// Computes Q = integral (N'*F*detJ dudvdz) 
    virtual void ComputeQ( ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate Q
                           ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate Q
                          ) {
        assert(GetIntegrationPointsU() <= ChQuadrature::GetStaticTables()->Lroots.size());
        assert(GetIntegrationPointsV() <= ChQuadrature::GetStaticTables()->Lroots.size());

        Q.Reset(loadable->LoadableGet_ndof_w());
        ChVectorDynamic<> mF(loadable->Get_field_ncoords());
 
        std::vector<double>* Ulroots = &ChQuadrature::GetStaticTables()->Lroots[GetIntegrationPointsU()-1];
        std::vector<double>* Uweight = &ChQuadrature::GetStaticTables()->Weight[GetIntegrationPointsU()-1];
        std::vector<double>* Vlroots = &ChQuadrature::GetStaticTables()->Lroots[GetIntegrationPointsV()-1];
        std::vector<double>* Vweight = &ChQuadrature::GetStaticTables()->Weight[GetIntegrationPointsV()-1];

        ChVectorDynamic<> mNF (Q.GetRows());        // temporary value for loop
        
        // Gauss quadrature :  Q = sum (N'*F*detJ * wi*wj*wk)
        for (unsigned int iu = 0; iu < Ulroots->size(); iu++) {
            for (unsigned int iv = 0; iv < Vlroots->size(); iv++) {
                    double detJ;
                    // Compute F= F(u,v,w)
                    this->ComputeF(Ulroots->at(iu),Vlroots->at(iv), 
                                    mF, state_x, state_w);
                    // Compute mNF= N(u,v,w)'*F
                    loadable->ComputeNF(Ulroots->at(iu),Vlroots->at(iv),
                                        mNF, detJ, mF, state_x, state_w);
                    // Compute Q+= mNF detJ * wi*wj*wk
                    mNF *= (detJ * Uweight->at(iu) * Vweight->at(iv) );
                    Q += mNF;
            }
        }
    }

};

/// Class of loaders for ChLoadableUV objects (which support 
/// surface loads) of atomic type, that is, with a concentrated load in a point Pu,Pv

class ChLoaderUVatomic : public ChLoaderUV {
public:
    double Pu;
    double Pv;

    ChLoaderUVatomic(ChSharedPtr<ChLoadableUV> mloadable) : 
          ChLoaderUV(mloadable)
         {};

            /// Computes Q = N'*F
    virtual void ComputeQ( ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate Q
                           ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate Q
                          ) {
        Q.Reset(loadable->LoadableGet_ndof_w());
        ChVectorDynamic<> mF(loadable->Get_field_ncoords());
 
        double detJ; // not used btw
        
        // Compute F=F(u,v)
        this->ComputeF(Pu,Pv, mF, state_x, state_w);

        // Compute N(u,v)'*F
        loadable->ComputeNF(Pu,Pv, Q, detJ, mF, state_x, state_w);
    }

        /// Set the position, on the surface where the atomic load is applied
    void SetApplication(double mu, double mv) {Pu=mu; Pv=mv;}
};



/// A very usual type of surface loader: the constant pressure load, 
/// a 3D per-area force that is aligned to the surface normal.

class ChLoaderPressure : public ChLoaderUVdistributed {
private:
    double pressure;
public:    
    ChLoaderPressure(ChSharedPtr<ChLoadableUV> mloadable) :
            ChLoaderUVdistributed(mloadable)
        {};

    virtual void ComputeF(const double U,             ///< parametric coordinate in surface
                          const double V,             ///< parametric coordinate in surface
                          ChVectorDynamic<>& F,       ///< Result F vector here, size must be = n.field coords.of loadable
                          ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate F
                          ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate F
                          ) {
        
        ChVector<> mnorm = this->loadable->ComputeNormal(U,V);
        F.PasteVector(mnorm * (-pressure), 0,0);
    }

    void SetPressure(double mpressure) {pressure = mpressure;}
    double GetPressure() {return pressure;}

    virtual int GetIntegrationPointsU() {return 1;}
    virtual int GetIntegrationPointsV() {return 1;}
};



/////////////////////////////////////////////////////////////////////////////////


/// Class of loaders for ChLoadableU objects (which support 
/// line loads).

class ChLoaderU : public ChLoader {
public:
    typedef ChLoadableU type_loadable;

    ChSharedPtr<ChLoadableU> loadable;
      
    ChLoaderU(ChSharedPtr<ChLoadableU> mloadable) : 
          loadable(mloadable) {};

            /// Children classes must provide this function that evaluates F = F(u)
            /// This will be evaluated during ComputeQ() to perform integration over the domain.
    virtual void ComputeF(const double U,             ///< parametric coordinate in line
                          ChVectorDynamic<>& F,       ///< Result F vector here, size must be = n.field coords.of loadable
                          ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate F
                          ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate F
                          ) = 0;

    void SetLoadable(ChSharedPtr<ChLoadableU>mloadable) {loadable = mloadable;}
    virtual ChSharedPtr<ChLoadable> GetLoadable() {return loadable;}
    ChSharedPtr<ChLoadableU> GetLoadableU() {return loadable;}

};


/// Class of loaders for ChLoadableU objects (which support 
/// line loads), for loads of distributed type, so these loads
/// will undergo Gauss quadrature to integrate them in the surface.

class ChLoaderUdistributed : public ChLoaderU {
public:
    
    ChLoaderUdistributed(ChSharedPtr<ChLoadableU> mloadable) : 
          ChLoaderU(mloadable) {};

    virtual int GetIntegrationPointsU() = 0;

            /// Computes Q = integral (N'*F*detJ du) 
    virtual void ComputeQ( ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate Q
                           ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate Q
                          ) {
        assert(GetIntegrationPointsU() <= ChQuadrature::GetStaticTables()->Lroots.size());

        Q.Reset(loadable->LoadableGet_ndof_w());
        ChVectorDynamic<> mF(loadable->Get_field_ncoords());
 
        std::vector<double>* Ulroots = &ChQuadrature::GetStaticTables()->Lroots[GetIntegrationPointsU()-1];
        std::vector<double>* Uweight = &ChQuadrature::GetStaticTables()->Weight[GetIntegrationPointsU()-1];

        ChVectorDynamic<> mNF (Q.GetRows());        // temporary value for loop
        
        // Gauss quadrature :  Q = sum (N'*F*detJ * wi*wj*wk)
        for (unsigned int iu = 0; iu < Ulroots->size(); iu++) {
                    double detJ;
                    // Compute F= F(u)
                    this->ComputeF(Ulroots->at(iu),
                                    mF, state_x, state_w);
                    // Compute mNF= N(u)'*F
                    loadable->ComputeNF(Ulroots->at(iu),
                                        mNF, detJ, mF, state_x, state_w);
                    // Compute Q+= mNF detJ * wi*wj*wk
                    mNF *= (detJ * Uweight->at(iu) );
                    Q += mNF;
        }
    }

};

/// Class of loaders for ChLoadableU objects (which support 
/// line loads) of atomic type, that is, with a concentrated load in a point Pu

class ChLoaderUatomic : public ChLoaderU {
public:
    double Pu;

    ChLoaderUatomic(ChSharedPtr<ChLoadableU> mloadable) : 
          ChLoaderU(mloadable)
         {};

            /// Computes Q = N'*F
    virtual void ComputeQ( ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate Q
                           ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate Q
                          ) {
        Q.Reset(loadable->LoadableGet_ndof_w());
        ChVectorDynamic<> mF(loadable->Get_field_ncoords());
 
        double detJ; // not used btw
        
        // Compute F=F(u)
        this->ComputeF(Pu, mF, state_x, state_w);
        
        // Compute N(u)'*F
        loadable->ComputeNF(Pu, Q, detJ, mF, state_x, state_w);
    }

        /// Set the position, on the surface where the atomic load is applied
    void SetApplication(double mu) {Pu=mu;}
};







}  // END_OF_NAMESPACE____

#endif  