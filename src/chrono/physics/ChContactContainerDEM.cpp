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
// Authors: Alessandro Tasora, Radu Serban
// =============================================================================

#include "chrono/physics/ChContactContainerDEM.h"
#include "chrono/physics/ChSystem.h"
#include "chrono/physics/ChSystemDEM.h"

namespace chrono {

using namespace collision;
using namespace geometry;

// Register into the object factory, to enable run-time dynamic creation and persistence
ChClassRegister<ChContactContainerDEM> a_registration_ChContactContainerDEM;

ChContactContainerDEM::ChContactContainerDEM()
    : n_added_6_6(0), n_added_6_3(0), n_added_3_3(0), n_added_333_6(0), n_added_333_3(0), n_added_333_333(0) {}

ChContactContainerDEM::ChContactContainerDEM(const ChContactContainerDEM& other) : ChContactContainerBase(other) {
    n_added_6_6 = 0;
    n_added_6_3 = 0;
    n_added_3_3 = 0;
    n_added_333_6 = 0;
    n_added_333_3 = 0;
    n_added_333_333 = 0;
}

ChContactContainerDEM::~ChContactContainerDEM() {
    RemoveAllContacts();
}

void ChContactContainerDEM::Update(double mytime, bool update_assets) {
    // Inherit time changes of parent class, basically doing nothing :)
    ChContactContainerBase::Update(mytime, update_assets);
}

template <class Tcont, class Titer>
void _RemoveAllContacts(std::list<Tcont*>& contactlist, Titer& lastcontact, int& n_added) {
    typename std::list<Tcont*>::iterator itercontact = contactlist.begin();
    while (itercontact != contactlist.end()) {
        delete (*itercontact);
        (*itercontact) = 0;
        ++itercontact;
    }
    contactlist.clear();
    lastcontact = contactlist.begin();
    n_added = 0;
}

void ChContactContainerDEM::RemoveAllContacts() {
    _RemoveAllContacts(contactlist_6_6, lastcontact_6_6, n_added_6_6);
    _RemoveAllContacts(contactlist_6_3, lastcontact_6_3, n_added_6_3);
    _RemoveAllContacts(contactlist_3_3, lastcontact_3_3, n_added_3_3);
    _RemoveAllContacts(contactlist_333_6, lastcontact_333_6, n_added_333_6);
    _RemoveAllContacts(contactlist_333_3, lastcontact_333_3, n_added_333_3);
    _RemoveAllContacts(contactlist_333_333, lastcontact_333_333, n_added_333_333);
    //**TODO*** cont. roll.
}

void ChContactContainerDEM::BeginAddContact() {
    lastcontact_6_6 = contactlist_6_6.begin();
    n_added_6_6 = 0;

    lastcontact_6_3 = contactlist_6_3.begin();
    n_added_6_3 = 0;

    lastcontact_3_3 = contactlist_3_3.begin();
    n_added_3_3 = 0;

    lastcontact_333_6 = contactlist_333_6.begin();
    n_added_333_6 = 0;

    lastcontact_333_3 = contactlist_333_3.begin();
    n_added_333_3 = 0;

    lastcontact_333_333 = contactlist_333_333.begin();
    n_added_333_333 = 0;

    // lastcontact_roll = contactlist_roll.begin();
    // n_added_roll = 0;
}

void ChContactContainerDEM::EndAddContact() {
    // remove contacts that are beyond last contact
    while (lastcontact_6_6 != contactlist_6_6.end()) {
        delete (*lastcontact_6_6);
        lastcontact_6_6 = contactlist_6_6.erase(lastcontact_6_6);
    }
    while (lastcontact_6_3 != contactlist_6_3.end()) {
        delete (*lastcontact_6_3);
        lastcontact_6_3 = contactlist_6_3.erase(lastcontact_6_3);
    }
    while (lastcontact_3_3 != contactlist_3_3.end()) {
        delete (*lastcontact_3_3);
        lastcontact_3_3 = contactlist_3_3.erase(lastcontact_3_3);
    }
    while (lastcontact_333_6 != contactlist_333_6.end()) {
        delete (*lastcontact_333_6);
        lastcontact_333_6 = contactlist_333_6.erase(lastcontact_333_6);
    }
    while (lastcontact_333_3 != contactlist_333_3.end()) {
        delete (*lastcontact_333_3);
        lastcontact_333_3 = contactlist_333_3.erase(lastcontact_333_3);
    }
    while (lastcontact_333_333 != contactlist_333_333.end()) {
        delete (*lastcontact_333_333);
        lastcontact_333_333 = contactlist_333_333.erase(lastcontact_333_333);
    }

    // while (lastcontact_roll != contactlist_roll.end()) {
    //    delete (*lastcontact_roll);
    //    lastcontact_roll = contactlist_roll.erase(lastcontact_roll);
    //}
}

template <class Tcont, class Titer, class Ta, class Tb>
void _OptimalContactInsert(std::list<Tcont*>& contactlist,
                           Titer& lastcontact,
                           int& n_added,
                           ChContactContainerBase* mcontainer,
                           Ta* objA,  ///< collidable object A
                           Tb* objB,  ///< collidable object B
                           const collision::ChCollisionInfo& cinfo) {
    if (lastcontact != contactlist.end()) {
        // reuse old contacts
        (*lastcontact)->Reset(objA, objB, cinfo);

        lastcontact++;

    } else {
        // add new contact
        Tcont* mc = new Tcont(mcontainer, objA, objB, cinfo);

        contactlist.push_back(mc);
        lastcontact = contactlist.end();
    }
    n_added++;
}

void ChContactContainerDEM::AddContact(const collision::ChCollisionInfo& mcontact) {
    assert(mcontact.modelA->GetContactable());
    assert(mcontact.modelB->GetContactable());

    // Do nothing if the shapes are separated
    if (mcontact.distance >= 0)
        return;

    // See if both collision models use DEM i.e. 'nonsmooth dynamics' material
    // of type ChMaterialSurface, trying to downcast from ChMaterialSurfaceBase.
    // If not DEM vs DEM, just bailout (ex it could be that this was a DVI vs DVI contact)

    auto mmatA =
        std::dynamic_pointer_cast<ChMaterialSurfaceDEM>(mcontact.modelA->GetContactable()->GetMaterialSurfaceBase());
    auto mmatB =
        std::dynamic_pointer_cast<ChMaterialSurfaceDEM>(mcontact.modelB->GetContactable()->GetMaterialSurfaceBase());

    if (!mmatA || !mmatB)
        return;

    // Bail out if any of the two contactable objects is
    // not contact-active:

    bool inactiveA = !mcontact.modelA->GetContactable()->IsContactActive();
    bool inactiveB = !mcontact.modelB->GetContactable()->IsContactActive();

    if ((inactiveA && inactiveB))
        return;

    // CREATE THE CONTACTS
    //
    // Switch among the various cases of contacts: i.e. between a 6-dof variable and another 6-dof variable,
    // or 6 vs 3, etc.
    // These cases are made distinct to exploit the optimization coming from templates and static data sizes
    // in contact types.

    if (ChContactable_1vars<6>* mmboA = dynamic_cast<ChContactable_1vars<6>*>(mcontact.modelA->GetContactable())) {
        // 6_6
        if (ChContactable_1vars<6>* mmboB = dynamic_cast<ChContactable_1vars<6>*>(mcontact.modelB->GetContactable())) {
            _OptimalContactInsert(contactlist_6_6, lastcontact_6_6, n_added_6_6, this, mmboA, mmboB, mcontact);
        }
        // 6_3
        if (ChContactable_1vars<3>* mmboB = dynamic_cast<ChContactable_1vars<3>*>(mcontact.modelB->GetContactable())) {
            _OptimalContactInsert(contactlist_6_3, lastcontact_6_3, n_added_6_3, this, mmboA, mmboB, mcontact);
        }
        // 6_333 -> 333_6
        if (ChContactable_3vars<3, 3, 3>* mmboB =
                dynamic_cast<ChContactable_3vars<3, 3, 3>*>(mcontact.modelB->GetContactable())) {
            collision::ChCollisionInfo swapped_contact(mcontact, true);
            _OptimalContactInsert(contactlist_333_6, lastcontact_333_6, n_added_333_6, this, mmboB, mmboA,
                                  swapped_contact);
        }
    }

    if (ChContactable_1vars<3>* mmboA = dynamic_cast<ChContactable_1vars<3>*>(mcontact.modelA->GetContactable())) {
        // 3_6 -> 6_3
        if (ChContactable_1vars<6>* mmboB = dynamic_cast<ChContactable_1vars<6>*>(mcontact.modelB->GetContactable())) {
            collision::ChCollisionInfo swapped_contact(mcontact, true);
            _OptimalContactInsert(contactlist_6_3, lastcontact_6_3, n_added_6_3, this, mmboB, mmboA, swapped_contact);
        }
        // 3_3
        if (ChContactable_1vars<3>* mmboB = dynamic_cast<ChContactable_1vars<3>*>(mcontact.modelB->GetContactable())) {
            _OptimalContactInsert(contactlist_3_3, lastcontact_3_3, n_added_3_3, this, mmboA, mmboB, mcontact);
        }
        // 3_333 -> 333_3
        if (ChContactable_3vars<3, 3, 3>* mmboB =
                dynamic_cast<ChContactable_3vars<3, 3, 3>*>(mcontact.modelB->GetContactable())) {
            collision::ChCollisionInfo swapped_contact(mcontact, true);
            _OptimalContactInsert(contactlist_333_3, lastcontact_333_3, n_added_333_3, this, mmboB, mmboA,
                                  swapped_contact);
        }
    }

    if (ChContactable_3vars<3, 3, 3>* mmboA =
            dynamic_cast<ChContactable_3vars<3, 3, 3>*>(mcontact.modelA->GetContactable())) {
        // 333_6
        if (ChContactable_1vars<6>* mmboB = dynamic_cast<ChContactable_1vars<6>*>(mcontact.modelB->GetContactable())) {
            _OptimalContactInsert(contactlist_333_6, lastcontact_333_6, n_added_333_6, this, mmboA, mmboB, mcontact);
        }
        // 333_3
        if (ChContactable_1vars<3>* mmboB = dynamic_cast<ChContactable_1vars<3>*>(mcontact.modelB->GetContactable())) {
            _OptimalContactInsert(contactlist_333_3, lastcontact_333_3, n_added_333_3, this, mmboA, mmboB, mcontact);
        }
        // 333_3
        if (ChContactable_3vars<3, 3, 3>* mmboB =
                dynamic_cast<ChContactable_3vars<3, 3, 3>*>(mcontact.modelB->GetContactable())) {
            _OptimalContactInsert(contactlist_333_333, lastcontact_333_333, n_added_333_333, this, mmboA, mmboB,
                                  mcontact);
        }
    }

    // ***TODO*** Fallback to some dynamic-size allocated constraint for cases that were not trapped by the switch
}

void ChContactContainerDEM::ComputeContactForces() {
    contact_forces.clear();
    SumAllContactForces(contactlist_6_6, contact_forces);
    SumAllContactForces(contactlist_6_3, contact_forces);
    SumAllContactForces(contactlist_333_6, contact_forces);
}

template <class Tcont>
void _ReportAllContacts(std::list<Tcont*>& contactlist, ChReportContactCallback* mcallback) {
    typename std::list<Tcont*>::iterator itercontact = contactlist.begin();
    while (itercontact != contactlist.end()) {
        bool proceed = mcallback->ReportContactCallback(
            (*itercontact)->GetContactP1(), (*itercontact)->GetContactP2(), *(*itercontact)->GetContactPlane(),
            (*itercontact)->GetContactDistance(), (*itercontact)->GetContactForce(),
            VNULL,  // no react torques
            (*itercontact)->GetObjA(), (*itercontact)->GetObjB());
        if (!proceed)
            break;
        ++itercontact;
    }
}

void ChContactContainerDEM::ReportAllContacts(ChReportContactCallback* mcallback) {
    _ReportAllContacts(contactlist_6_6, mcallback);
    _ReportAllContacts(contactlist_6_3, mcallback);
    _ReportAllContacts(contactlist_3_3, mcallback);
    _ReportAllContacts(contactlist_333_6, mcallback);
    _ReportAllContacts(contactlist_333_3, mcallback);
    _ReportAllContacts(contactlist_333_333, mcallback);
    //***TODO*** rolling cont.
}

////////// STATE INTERFACE ////

template <class Tcont>
void _IntLoadResidual_F(std::list<Tcont*>& contactlist, ChVectorDynamic<>& R, const double c) {
    typename std::list<Tcont*>::iterator itercontact = contactlist.begin();
    while (itercontact != contactlist.end()) {
        (*itercontact)->ContIntLoadResidual_F(R, c);
        ++itercontact;
    }
}

void ChContactContainerDEM::IntLoadResidual_F(const unsigned int off, ChVectorDynamic<>& R, const double c) {
    _IntLoadResidual_F(contactlist_6_6, R, c);
    _IntLoadResidual_F(contactlist_6_3, R, c);
    _IntLoadResidual_F(contactlist_3_3, R, c);
    _IntLoadResidual_F(contactlist_333_6, R, c);
    _IntLoadResidual_F(contactlist_333_3, R, c);
    _IntLoadResidual_F(contactlist_333_333, R, c);
}

template <class Tcont>
void _KRMmatricesLoad(std::list<Tcont*> contactlist, double Kfactor, double Rfactor) {
    typename std::list<Tcont*>::iterator itercontact = contactlist.begin();
    while (itercontact != contactlist.end()) {
        (*itercontact)->ContKRMmatricesLoad(Kfactor, Rfactor);
        ++itercontact;
    }
}

void ChContactContainerDEM::KRMmatricesLoad(double Kfactor, double Rfactor, double Mfactor) {
    _KRMmatricesLoad(contactlist_6_6, Kfactor, Rfactor);
    _KRMmatricesLoad(contactlist_6_3, Kfactor, Rfactor);
    _KRMmatricesLoad(contactlist_3_3, Kfactor, Rfactor);
    _KRMmatricesLoad(contactlist_333_6, Kfactor, Rfactor);
    _KRMmatricesLoad(contactlist_333_3, Kfactor, Rfactor);
    _KRMmatricesLoad(contactlist_333_333, Kfactor, Rfactor);
}

template <class Tcont>
void _InjectKRMmatrices(std::list<Tcont*> contactlist, ChSystemDescriptor& mdescriptor) {
    typename std::list<Tcont*>::iterator itercontact = contactlist.begin();
    while (itercontact != contactlist.end()) {
        (*itercontact)->ContInjectKRMmatrices(mdescriptor);
        ++itercontact;
    }
}

void ChContactContainerDEM::InjectKRMmatrices(ChSystemDescriptor& mdescriptor) {
    _InjectKRMmatrices(contactlist_6_6, mdescriptor);
    _InjectKRMmatrices(contactlist_6_3, mdescriptor);
    _InjectKRMmatrices(contactlist_3_3, mdescriptor);
    _InjectKRMmatrices(contactlist_333_6, mdescriptor);
    _InjectKRMmatrices(contactlist_333_3, mdescriptor);
    _InjectKRMmatrices(contactlist_333_333, mdescriptor);
}

// OBSOLETE
void ChContactContainerDEM::ConstraintsFbLoadForces(double factor) {
    GetLog() << "ChContactContainerDEM::ConstraintsFbLoadForces OBSOLETE - use new bookkeeping! \n";
}

void ChContactContainerDEM::ArchiveOUT(ChArchiveOut& marchive) {
    // version number
    marchive.VersionWrite(1);
    // serialize parent class
    ChContactContainerBase::ArchiveOUT(marchive);
    // serialize all member data:
    // NO SERIALIZATION of contact list because assume it is volatile and generated when needed
}

/// Method to allow de serialization of transient data from archives.
void ChContactContainerDEM::ArchiveIN(ChArchiveIn& marchive) {
    // version number
    int version = marchive.VersionRead();
    // deserialize parent class
    ChContactContainerBase::ArchiveIN(marchive);
    // stream in all member data:
    RemoveAllContacts();
    // NO SERIALIZATION of contact list because assume it is volatile and generated when needed
}

}  // end namespace chrono
