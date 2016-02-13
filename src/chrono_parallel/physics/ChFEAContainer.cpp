
#include <stdlib.h>
#include <algorithm>
#include <math.h>
#include "chrono_parallel/physics/ChSystemParallel.h"
#include <chrono_parallel/physics/Ch3DOFContainer.h>
#include <thrust/fill.h>
#include "chrono_parallel/constraints/ChConstraintUtils.h"
#include "chrono_parallel/math/svd.h"

#include "chrono_parallel/math/other_types.h"  // for uint, int2, int3
#include "chrono_parallel/math/real.h"         // for real
#include "chrono_parallel/math/real2.h"        // for real2
#include "chrono_parallel/math/real3.h"        // for real3
#include "chrono_parallel/math/real4.h"        // for quaternion, real4
#include "chrono_parallel/math/mat33.h"        // for quaternion, real4

namespace chrono {

using namespace collision;
using namespace geometry;

//////////////////////////////////////
//////////////////////////////////////

/// CLASS FOR TETRAHEDRAL FEA ELEMENTS
uint3 SortedFace(int face, const uint4& tetrahedron) {
    int i = tetrahedron.x;
    int j = tetrahedron.y;
    int k = tetrahedron.z;
    int l = tetrahedron.w;
    switch (face) {
        case 0:
            return Sort(_make_uint3(j, k, l));
            break;
        case 1:
            return Sort(_make_uint3(i, k, l));
            break;
        case 2:
            return Sort(_make_uint3(i, j, l));
            break;
        case 3:
            return Sort(_make_uint3(i, j, k));
            break;
    }
}

ChFEAContainer::ChFEAContainer(ChSystemParallelDVI* system) {
    data_manager = system->data_manager;
    data_manager->AddFEAContainer(this);
    num_rigid_constraints = 0;
    rigid_constraint_recovery_speed = 1;
    family.x = 1;
    family.y = 0x7FFF;
}

ChFEAContainer::~ChFEAContainer() {}

void ChFEAContainer::AddNodes(const std::vector<real3>& positions, const std::vector<real3>& velocities) {
    custom_vector<real3>& pos_node = data_manager->host_data.pos_node_fea;
    custom_vector<real3>& vel_node = data_manager->host_data.vel_node_fea;

    pos_node.insert(pos_node.end(), positions.begin(), positions.end());
    vel_node.insert(vel_node.end(), velocities.begin(), velocities.end());
    // In case the number of velocities provided were not enough, resize to the number of fea nodes
    vel_node.resize(pos_node.size());
    data_manager->num_fea_nodes = pos_node.size();
}
void ChFEAContainer::AddElements(const std::vector<uint4>& indices) {
    custom_vector<uint4>& tet_indices = data_manager->host_data.tet_indices;
    tet_indices.insert(tet_indices.end(), indices.begin(), indices.end());
    data_manager->num_fea_tets = tet_indices.size();
}

void ChFEAContainer::AddConstraint(const uint node, std::shared_ptr<ChBody>& body) {
    // Creates constraints between a 3dof node and a 6dof rigid body
    // point is the nodal point
    int body_a = body->GetId();
    int body_b = node;

    custom_vector<real3>& pos_node_fea = data_manager->host_data.pos_node_fea;

    real3 pos_rigid = real3(body->GetPos().x, body->GetPos().y, body->GetPos().z);
    quaternion rot_rigid = quaternion(body->GetRot().e0, body->GetRot().e1, body->GetRot().e2, body->GetRot().e3);
    //    printf("body ID: %d %d [%f %f %f] [%f %f %f]\n", body_b, body_a, pos_rigid.x, pos_rigid.y, pos_rigid.z,
    //           pos_node_fea[body_b].x, pos_node_fea[body_b].y, pos_node_fea[body_b].z);
    constraint_position.push_back(TransformParentToLocal(pos_rigid, rot_rigid, pos_node_fea[body_b]));
    constraint_bodies.push_back(_make_int2(body_a, body_b));

    num_rigid_constraints++;
}

int ChFEAContainer::GetNumConstraints() {
    // 6 rows in the tetrahedral jacobian 1 row for volume constraint
    int num_constraints = data_manager->num_fea_tets * (6 + 1);
    num_constraints += data_manager->num_rigid_tet_contacts * 3;
    num_constraints += num_rigid_constraints * 3;
    return num_constraints;
}
int ChFEAContainer::GetNumNonZeros() {
    // 12*3 entries in the elastic, 12*3 entries in the shear, 12 entries in volume
    int nnz =
        data_manager->num_fea_tets * 12 * 3 + data_manager->num_fea_tets * 12 * 3 + data_manager->num_fea_tets * 12 * 1;
    // 6 entries for rigid body side, 3 for node
    nnz += (6 + 9) * 3 * data_manager->num_rigid_tet_contacts;  // contacts
    nnz += 9 * 3 * num_rigid_constraints;                       // fixed joints
    return nnz;
}

void ChFEAContainer::Setup(int start_constraint) {
    start_tet = start_constraint;
    num_tet_constraints = data_manager->num_fea_tets * (6 + 1);

    start_boundary = start_constraint + num_tet_constraints;
    start_rigid = start_boundary + data_manager->num_rigid_tet_contacts * 3;
}

void ChFEAContainer::ComputeInvMass(int offset) {
    CompressedMatrix<real>& M_inv = data_manager->host_data.M_inv;
    uint num_nodes = data_manager->num_fea_nodes;
    custom_vector<real>& mass_node = data_manager->host_data.mass_node_fea;

    for (int i = 0; i < num_nodes; i++) {
        real inv_mass = 1.0 / mass_node[i];
        M_inv.append(offset + i * 3 + 0, offset + i * 3 + 0, inv_mass);
        M_inv.finalize(offset + i * 3 + 0);
        M_inv.append(offset + i * 3 + 1, offset + i * 3 + 1, inv_mass);
        M_inv.finalize(offset + i * 3 + 1);
        M_inv.append(offset + i * 3 + 2, offset + i * 3 + 2, inv_mass);
        M_inv.finalize(offset + i * 3 + 2);
    }
}
void ChFEAContainer::ComputeMass(int offset) {
    CompressedMatrix<real>& M = data_manager->host_data.M;
    uint num_nodes = data_manager->num_fea_nodes;
    custom_vector<real>& mass_node = data_manager->host_data.mass_node_fea;

    for (int i = 0; i < num_nodes; i++) {
        real mass = mass_node[i];
        M.append(offset + i * 3 + 0, offset + i * 3 + 0, mass);
        M.finalize(offset + i * 3 + 0);
        M.append(offset + i * 3 + 1, offset + i * 3 + 1, mass);
        M.finalize(offset + i * 3 + 1);
        M.append(offset + i * 3 + 2, offset + i * 3 + 2, mass);
        M.finalize(offset + i * 3 + 2);
    }
}

void ChFEAContainer::Update(double ChTime) {
    uint num_nodes = data_manager->num_fea_nodes;
    uint num_fluid_bodies = data_manager->num_fluid_bodies;
    uint num_rigid_bodies = data_manager->num_rigid_bodies;
    uint num_shafts = data_manager->num_shafts;
    custom_vector<real3>& pos_node = data_manager->host_data.pos_node_fea;
    custom_vector<real3>& vel_node = data_manager->host_data.vel_node_fea;
    real3 g_acc = data_manager->settings.gravity;
    custom_vector<real>& mass_node = data_manager->host_data.mass_node_fea;
    for (int i = 0; i < num_nodes; i++) {
        real3 vel = vel_node[i];
        real3 h_gravity = data_manager->settings.step_size * mass_node[i] * g_acc;
        data_manager->host_data.v[num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3 + i * 3 + 0] = vel.x;
        data_manager->host_data.v[num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3 + i * 3 + 1] = vel.y;
        data_manager->host_data.v[num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3 + i * 3 + 2] = vel.z;

        data_manager->host_data.hf[num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3 + i * 3 + 0] = h_gravity.x;
        data_manager->host_data.hf[num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3 + i * 3 + 1] = h_gravity.y;
        data_manager->host_data.hf[num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3 + i * 3 + 2] = h_gravity.z;
    }
}
void ChFEAContainer::UpdatePosition(double ChTime) {
    uint num_nodes = data_manager->num_fea_nodes;
    uint num_fluid_bodies = data_manager->num_fluid_bodies;
    uint num_rigid_bodies = data_manager->num_rigid_bodies;
    uint num_shafts = data_manager->num_shafts;
    //
    custom_vector<real3>& pos_node = data_manager->host_data.pos_node_fea;
    custom_vector<real3>& vel_node = data_manager->host_data.vel_node_fea;
    //
    for (int i = 0; i < num_nodes; i++) {
        real3 vel;
        vel.x = data_manager->host_data.v[num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3 + i * 3 + 0];
        vel.y = data_manager->host_data.v[num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3 + i * 3 + 1];
        vel.z = data_manager->host_data.v[num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3 + i * 3 + 2];

        real speed = Length(vel);
        if (speed > max_velocity) {
            vel = vel * max_velocity / speed;
        }
        vel_node[i] = vel;
        pos_node[i] += vel * data_manager->settings.step_size;
    }
}
void ChFEAContainer::Initialize() {
    uint num_tets = data_manager->num_fea_tets;
    uint num_nodes = data_manager->num_fea_nodes;

    custom_vector<real3>& pos_node = data_manager->host_data.pos_node_fea;
    custom_vector<uint4>& tet_indices = data_manager->host_data.tet_indices;
    custom_vector<real>& mass_node = data_manager->host_data.mass_node_fea;

    X0.resize(num_tets);
    V.resize(num_tets);
    mass_node.resize(num_nodes);

    // initialize node masses to zero;
    Thrust_Fill(mass_node, 0);

    for (int i = 0; i < num_tets; i++) {
        uint4 tet_ind = tet_indices[i];

        real3 x0 = pos_node[tet_ind.x];
        real3 x1 = pos_node[tet_ind.y];
        real3 x2 = pos_node[tet_ind.z];
        real3 x3 = pos_node[tet_ind.w];

        real3 c1 = x1 - x0;
        real3 c2 = x2 - x0;
        real3 c3 = x3 - x0;
        Mat33 Ds = Mat33(c1, c2, c3);

        X0[i] = Inverse(Ds);
        real det = Determinant(Ds);
        real vol = (det) / 6.0;
        real volSqrt = Sqrt(vol);

        V[i] = vol;  // Abs(Dot(x1-x0, Cross(x2-x0, x3-x0)))/6.0;

        real tet_mass = material_density * vol;
        real node_mass = tet_mass / 4.0;

        mass_node[tet_ind.x] += node_mass;
        mass_node[tet_ind.y] += node_mass;
        mass_node[tet_ind.z] += node_mass;
        mass_node[tet_ind.w] += node_mass;

        // printf("Vol: %f Mass: %f [%d %d %d %d]\n", vol, tet_mass, tet_ind.x,tet_ind.y,tet_ind.z,tet_ind.w);

        //        real3 y[4];
        //        y[1] = X0[i].row(0);
        //        y[2] = X0[i].row(1);
        //        y[3] = X0[i].row(2);
        //        y[0] = -y[1] - y[2] - y[3];
    }

    //    ed = yDamping;
    //    nud = pDamping;
    //    real omnd = 1.0 - nud;
    //    real om2nd = 1.0 - 2 * nud;
    //    real fd = ed / (1.0 + nud) / om2nd;
    //    Mat33 Ed = fd * Mat33(omnd, nud, nud,   //
    //                          nud, omnd, nud,   //
    //                          nud, nud, omnd);  //

    FindSurface();
}

bool Cone_generalized_rnode(real& gamma_n, real& gamma_u, real& gamma_v, const real& mu) {
    real f_tang = sqrt(gamma_u * gamma_u + gamma_v * gamma_v);

    // inside upper cone? keep untouched!
    if (f_tang < (mu * gamma_n)) {
        return false;
    }

    // inside lower cone? reset  normal,u,v to zero!
    if ((f_tang) < -(1.0 / mu) * gamma_n || (fabs(gamma_n) < 10e-15)) {
        gamma_n = 0;
        gamma_u = 0;
        gamma_v = 0;
        return false;
    }

    // remaining case: project orthogonally to generator segment of upper cone

    gamma_n = (f_tang * mu + gamma_n) / (mu * mu + 1);
    real tproj_div_t = (gamma_n * mu) / f_tang;
    gamma_u *= tproj_div_t;
    gamma_v *= tproj_div_t;

    return true;
}

void ChFEAContainer::Project(real* gamma) {
    real mu = data_manager->fea_container->contact_mu;
    real coh = data_manager->fea_container->contact_cohesion;
    if (data_manager->num_rigid_tet_contacts == 0) {
        return;
    }
    uint num_rigid_tet_contacts = data_manager->num_rigid_tet_contacts;
    custom_vector<int>& neighbor_rigid_tet = data_manager->host_data.neighbor_rigid_tet;
    custom_vector<int>& contact_counts = data_manager->host_data.c_counts_rigid_tet;
    int num_boundary_tets = data_manager->host_data.boundary_element_fea.size();
#pragma omp parallel for
    for (int p = 0; p < num_boundary_tets; p++) {
        int start = contact_counts[p];
        int end = contact_counts[p + 1];
        for (int index = start; index < end; index++) {
            int i = index - start;                                        // index that goes from 0
            int rigid = neighbor_rigid_tet[p * max_rigid_neighbors + i];  // rigid is stored in the first index
            real rigid_fric = data_manager->host_data.fric_data[rigid].x;
            real cohesion = Max((data_manager->host_data.cohesion_data[rigid] + coh) * .5, 0.0);
            real friction = (rigid_fric == 0 || mu == 0) ? 0 : (rigid_fric + mu) * .5;

            real3 gam;
            gam.x = gamma[start_boundary + index];
            gam.y = gamma[start_boundary + num_rigid_tet_contacts + index * 2 + 0];
            gam.z = gamma[start_boundary + num_rigid_tet_contacts + index * 2 + 1];

            gam.x += cohesion;

            real mu = friction;
            if (mu == 0) {
                gam.x = gam.x < 0 ? 0 : gam.x - cohesion;
                gam.y = gam.z = 0;

                gamma[start_boundary + index] = gam.x;
                gamma[start_boundary + num_rigid_tet_contacts + index * 2 + 0] = gam.y;
                gamma[start_boundary + num_rigid_tet_contacts + index * 2 + 1] = gam.z;
                continue;
            }

            if (Cone_generalized_rnode(gam.x, gam.y, gam.z, mu)) {
            }

            gamma[start_boundary + index] = gam.x - cohesion;
            gamma[start_boundary + num_rigid_tet_contacts + index * 2 + 0] = gam.y;
            gamma[start_boundary + num_rigid_tet_contacts + index * 2 + 1] = gam.z;
        }
    }
}
void ChFEAContainer::Build_D() {
    uint num_fluid_bodies = data_manager->num_fluid_bodies;
    uint num_rigid_bodies = data_manager->num_rigid_bodies;
    uint num_shafts = data_manager->num_shafts;
    uint num_tets = data_manager->num_fea_tets;

    real e = youngs_modulus;
    real nu = poisson_ratio;

    const real mu = 0.5 * e / (1.0 + nu);  // 0.5?
    const real muInv = 1.0 / mu;

    real omn = 1.0 - nu;
    real om2n = 1.0 - 2 * nu;
    real s = e / (1.0 + nu);
    real f = s / om2n;
    Mat33 E = f * Mat33(omn, nu, nu,   //
                        nu, omn, nu,   //
                        nu, nu, omn);  //

    Mat33 Einv = Inverse(E);
    Mat33 C_upper = Einv;
    Mat33 C_lower(real3(muInv, muInv, muInv));

    custom_vector<real3>& pos_node = data_manager->host_data.pos_node_fea;
    custom_vector<uint4>& tet_indices = data_manager->host_data.tet_indices;
    CompressedMatrix<real>& D_T = data_manager->host_data.D_T;
    uint b_off = num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3;
#pragma omp parallel for
    for (int i = 0; i < num_tets; i++) {
        uint4 tet_ind = tet_indices[i];

        real3 p0 = pos_node[tet_ind.x];
        real3 p1 = pos_node[tet_ind.y];
        real3 p2 = pos_node[tet_ind.z];
        real3 p3 = pos_node[tet_ind.w];

        real3 c1 = p1 - p0;
        real3 c2 = p2 - p0;
        real3 c3 = p3 - p0;
        Mat33 Ds = Mat33(c1, c2, c3);

        real det = Determinant(Ds);
        real vol = Abs((det) / 6.0);
        real volSqrt = Sqrt(vol);
        Mat33 X = X0[i];
        Mat33 F = Ds * X;  // 4.27
        Mat33 Ftr = Transpose(F);

        Mat33 U, V;
        real3 SV;

        // chrono::SVD(F, U, SV, V);
        // Mat33 R = MultTranspose(U, V);
        // Mat33 S = V * Mat33(SV) * Transpose(V);

        Mat33 strain = 0.5 * (Ftr * F - Mat33(1));  // Green strain
        // Mat33 strain = S - Mat33(1);

        // Print(Ds, "Ds");
        // Print(X, "X");
        // Print(strain, "strain");
        // std::cin.get();

        real3 y[4];
        y[1] = X.row(0);
        y[2] = X.row(1);
        y[3] = X.row(2);
        y[0] = -y[1] - y[2] - y[3];

        real3 r1 = 1. / 6. * Cross(c2, c3);
        real3 r2 = 1. / 6. * Cross(c3, c1);
        real3 r3 = 1. / 6. * Cross(c1, c2);
        real3 r0 = -r1 - r2 - r3;

        real vf = (0.5 / (volSqrt));
        // This helps to scale jacboian so boundaries aren't ignored
        real cf = 2 * volSqrt;  // 2 * volSqrt;
        // diagonal elements of strain matrix
        Mat33 A1 = cf * Mat33(y[0]) * Ftr;  //+ vf * OuterProduct(real3(strain[0], strain[5], strain[10]), r0);
        Mat33 A2 = cf * Mat33(y[1]) * Ftr;  //+ vf * OuterProduct(real3(strain[0], strain[5], strain[10]), r1);
        Mat33 A3 = cf * Mat33(y[2]) * Ftr;  //+ vf * OuterProduct(real3(strain[0], strain[5], strain[10]), r2);
        Mat33 A4 = cf * Mat33(y[3]) * Ftr;  //+ vf * OuterProduct(real3(strain[0], strain[5], strain[10]), r3);
                                            //        A1 = A1 * (1.0 / Determinant(A1));
                                            //        A2 = A2 * (1.0 / Determinant(A2));
                                            //        A3 = A3 * (1.0 / Determinant(A3));
                                            //        A4 = A4 * (1.0 / Determinant(A4));

        SetRow3Check(D_T, start_tet + i * 7 + 0, b_off + tet_ind.x * 3, A1.row(0));
        SetRow3Check(D_T, start_tet + i * 7 + 0, b_off + tet_ind.y * 3, A2.row(0));
        SetRow3Check(D_T, start_tet + i * 7 + 0, b_off + tet_ind.z * 3, A3.row(0));
        SetRow3Check(D_T, start_tet + i * 7 + 0, b_off + tet_ind.w * 3, A4.row(0));
        /////==================================================================================================================================

        SetRow3Check(D_T, start_tet + i * 7 + 1, b_off + tet_ind.x * 3, A1.row(1));
        SetRow3Check(D_T, start_tet + i * 7 + 1, b_off + tet_ind.y * 3, A2.row(1));
        SetRow3Check(D_T, start_tet + i * 7 + 1, b_off + tet_ind.z * 3, A3.row(1));
        SetRow3Check(D_T, start_tet + i * 7 + 1, b_off + tet_ind.w * 3, A4.row(1));
        /////==================================================================================================================================

        SetRow3Check(D_T, start_tet + i * 7 + 2, b_off + tet_ind.x * 3, A1.row(2));
        SetRow3Check(D_T, start_tet + i * 7 + 2, b_off + tet_ind.y * 3, A2.row(2));
        SetRow3Check(D_T, start_tet + i * 7 + 2, b_off + tet_ind.z * 3, A3.row(2));
        SetRow3Check(D_T, start_tet + i * 7 + 2, b_off + tet_ind.w * 3, A4.row(2));

        /////==================================================================================================================================
        // Off diagonal strain elements
        Mat33 B1 =
            0.5 * cf * SkewSymmetricAlt(y[0]) * Ftr;  //+ vf * OuterProduct(real3(strain[9], strain[8], strain[4]), r0);
        Mat33 B2 =
            0.5 * cf * SkewSymmetricAlt(y[1]) * Ftr;  //+ vf * OuterProduct(real3(strain[9], strain[8], strain[4]), r1);
        Mat33 B3 =
            0.5 * cf * SkewSymmetricAlt(y[2]) * Ftr;  //+ vf * OuterProduct(real3(strain[9], strain[8], strain[4]), r2);
        Mat33 B4 =
            0.5 * cf * SkewSymmetricAlt(y[3]) * Ftr;  //+ vf * OuterProduct(real3(strain[9], strain[8], strain[4]), r3);

        //        B1 = B1 * (1.0 / Determinant(B1));
        //        B2 = B2 * (1.0 / Determinant(B2));
        //        B3 = B3 * (1.0 / Determinant(B3));
        //        B4 = B4 * (1.0 / Determinant(B4));

        SetRow3Check(D_T, start_tet + i * 7 + 3, b_off + tet_ind.x * 3, B1.row(0));
        SetRow3Check(D_T, start_tet + i * 7 + 3, b_off + tet_ind.y * 3, B2.row(0));
        SetRow3Check(D_T, start_tet + i * 7 + 3, b_off + tet_ind.z * 3, B3.row(0));
        SetRow3Check(D_T, start_tet + i * 7 + 3, b_off + tet_ind.w * 3, B4.row(0));

        /////==================================================================================================================================

        SetRow3Check(D_T, start_tet + i * 7 + 4, b_off + tet_ind.x * 3, B1.row(1));
        SetRow3Check(D_T, start_tet + i * 7 + 4, b_off + tet_ind.y * 3, B2.row(1));
        SetRow3Check(D_T, start_tet + i * 7 + 4, b_off + tet_ind.z * 3, B3.row(1));
        SetRow3Check(D_T, start_tet + i * 7 + 4, b_off + tet_ind.w * 3, B4.row(1));

        /////==================================================================================================================================

        SetRow3Check(D_T, start_tet + i * 7 + 5, b_off + tet_ind.x * 3, B1.row(2));
        SetRow3Check(D_T, start_tet + i * 7 + 5, b_off + tet_ind.y * 3, B2.row(2));
        SetRow3Check(D_T, start_tet + i * 7 + 5, b_off + tet_ind.z * 3, B3.row(2));
        SetRow3Check(D_T, start_tet + i * 7 + 5, b_off + tet_ind.w * 3, B4.row(2));

        /////==================================================================================================================================
        // Volume

        SetRow3Check(D_T, start_tet + i * 7 + 6, b_off + tet_ind.x * 3, r0);
        SetRow3Check(D_T, start_tet + i * 7 + 6, b_off + tet_ind.y * 3, r1);
        SetRow3Check(D_T, start_tet + i * 7 + 6, b_off + tet_ind.z * 3, r2);
        SetRow3Check(D_T, start_tet + i * 7 + 6, b_off + tet_ind.w * 3, r3);
    }

    custom_vector<real3>& pos_rigid = data_manager->host_data.pos_rigid;
    custom_vector<quaternion>& rot_rigid = data_manager->host_data.rot_rigid;

    real h = data_manager->fea_container->kernel_radius;
    custom_vector<real3>& cpta = data_manager->host_data.cpta_rigid_tet;
    custom_vector<real3>& cptb = data_manager->host_data.cptb_rigid_tet;
    custom_vector<real3>& norm = data_manager->host_data.norm_rigid_tet;
    custom_vector<real4>& face_rigid_tet = data_manager->host_data.face_rigid_tet;
    custom_vector<int>& neighbor_rigid_tet = data_manager->host_data.neighbor_rigid_tet;
    custom_vector<int>& contact_counts = data_manager->host_data.c_counts_rigid_tet;
    custom_vector<uint>& boundary_element_fea = data_manager->host_data.boundary_element_fea;
    uint num_rigid_tet_contacts = data_manager->num_rigid_tet_contacts;
    int num_boundary_tets = data_manager->host_data.boundary_element_fea.size();
    if (num_rigid_tet_contacts > 0) {
#pragma omp parallel for
        for (int p = 0; p < num_boundary_tets; p++) {
            int start = contact_counts[p];
            int end = contact_counts[p + 1];
            uint4 tetind = tet_indices[boundary_element_fea[p]];
            for (int index = start; index < end; index++) {
                int i = index - start;  // index that goes from 0
                int rigid = neighbor_rigid_tet[p * max_rigid_neighbors + i];
                int node = p;  // node body is in second index
                real3 U = norm[p * max_rigid_neighbors + i], V, W;
                Orthogonalize(U, V, W);
                real3 T1, T2, T3;
                Compute_Jacobian(rot_rigid[rigid], U, V, W, cpta[p * max_rigid_neighbors + i] - pos_rigid[rigid], T1,
                                 T2, T3);

                SetRow6Check(D_T, start_boundary + index + 0, rigid * 6, -U, T1);
                SetRow6Check(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 0, rigid * 6, -V, T2);
                SetRow6Check(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 1, rigid * 6, -W, T3);

                real4 face = face_rigid_tet[p * max_rigid_neighbors + i];
                uint3 sortedf = SortedFace(face.w, tetind);

                SetRow3Check(D_T, start_boundary + index + 0, b_off + sortedf.x * 3, U * face.x);
                SetRow3Check(D_T, start_boundary + index + 0, b_off + sortedf.y * 3, U * face.y);
                SetRow3Check(D_T, start_boundary + index + 0, b_off + sortedf.z * 3, U * face.z);

                SetRow3Check(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 0, b_off + sortedf.x * 3,
                             V * face.x);
                SetRow3Check(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 0, b_off + sortedf.y * 3,
                             V * face.y);
                SetRow3Check(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 0, b_off + sortedf.z * 3,
                             V * face.z);

                SetRow3Check(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 1, b_off + sortedf.x * 3,
                             W * face.x);
                SetRow3Check(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 1, b_off + sortedf.y * 3,
                             W * face.y);
                SetRow3Check(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 1, b_off + sortedf.z * 3,
                             W * face.z);
            }
        }
    }

    if (num_rigid_constraints > 0) {
        for (int index = 0; index < num_rigid_constraints; index++) {
            int2 body_id = constraint_bodies[index];

            int body_a = body_id.x;
            int node_b = body_id.y;

            Mat33 Arw = Transpose(Mat33(rot_rigid[body_a]));
            real3 temp = Arw * Normalize((pos_node[node_b] - pos_rigid[body_a]));
            Mat33 atilde = SkewSymmetric(temp);
            Mat33 Jrb = Arw * atilde;  // Jacobian for body

            //======= Experimental jacobian weighting
            //            real dJxn = Abs(Determinant(Jxn));
            //            real dJxb = Abs(Determinant(Jxb));
            //            real dJrb = Abs(Determinant(Jrb));

            //            if (dJxn > 1) {
            //                Jxn = Jxn * (1.0 / dJxn);
            //            }
            //            if (dJxb > 1) {
            //                Jxb = Jxb * (1.0 / dJxb);
            //            }
            //            if (dJrb > 1) {
            //                Jrb = Jrb * (1.0 / dJrb);
            //            }
            //
            Arw = Arw * .2;
            Jrb = Jrb * .2;
            //            //=======

            SetRow6Check(D_T, start_rigid + index * 3 + 0, body_a * 6, -Arw.row(0), Jrb.row(0));
            SetRow6Check(D_T, start_rigid + index * 3 + 1, body_a * 6, -Arw.row(1), Jrb.row(1));
            SetRow6Check(D_T, start_rigid + index * 3 + 2, body_a * 6, -Arw.row(2), Jrb.row(2));

            SetRow3Check(D_T, start_rigid + index * 3 + 0, b_off + node_b * 3, Arw.row(0));
            SetRow3Check(D_T, start_rigid + index * 3 + 1, b_off + node_b * 3, Arw.row(1));
            SetRow3Check(D_T, start_rigid + index * 3 + 2, b_off + node_b * 3, Arw.row(2));
        }
    }
}
void ChFEAContainer::Build_b() {
    LOG(INFO) << "ChConstraintTet::Build_b";
    uint num_tets = data_manager->num_fea_tets;
    SubVectorType b_sub = blaze::subvector(data_manager->host_data.b, start_tet, num_tet_constraints);
    custom_vector<real3>& pos_node = data_manager->host_data.pos_node_fea;
    custom_vector<uint4>& tet_indices = data_manager->host_data.tet_indices;
    custom_vector<real3>& pos_rigid = data_manager->host_data.pos_rigid;
    custom_vector<quaternion>& rot_rigid = data_manager->host_data.rot_rigid;
    real step_size = data_manager->settings.step_size;
#pragma omp parallel for
    for (int i = 0; i < num_tets; i++) {
        uint4 tet_ind = tet_indices[i];

        real3 x0 = pos_node[tet_ind.x];
        real3 x1 = pos_node[tet_ind.y];
        real3 x2 = pos_node[tet_ind.z];
        real3 x3 = pos_node[tet_ind.w];

        real3 c1 = x1 - x0;
        real3 c2 = x2 - x0;
        real3 c3 = x3 - x0;
        Mat33 Ds = Mat33(c1, c2, c3);

        real det = Determinant(Ds);
        real vol = Abs((det) / 6.0);

        Mat33 X = X0[i];
        Mat33 F = Ds * X;
        Mat33 Ftr = Transpose(F);

        //        real3 y[4];
        //        y[1] = X.row(0);
        //        y[2] = X.row(1);
        //        y[3] = X.row(2);
        //        y[0] = -y[1] - y[2] - y[3];

        Mat33 strain = 0.5 * (Ftr * F - Mat33(1));  // Green strain
                                                    // Mat33 strain = S - Mat33(1);
        real volSqrt = Sqrt(vol);
        real cf = 1;  // volSqrt;

        b_sub[i * 7 + 0] = cf * strain[0];
        b_sub[i * 7 + 1] = cf * strain[5];
        b_sub[i * 7 + 2] = cf * strain[10];

        b_sub[i * 7 + 3] = cf * strain[9];
        b_sub[i * 7 + 4] = cf * strain[8];
        b_sub[i * 7 + 5] = cf * strain[4];

        // Volume

        b_sub[i * 7 + 6] = (Determinant(F) - 1);  // 100 * (vol - V[i]);
        // real vv = vol;

        // printf("vol: %f %f %f\n", (Determinant(F) - 1), (Determinant(Ds) - Determinant(Inverse(X))), 100 * vol -
        // V[i]);
    }

    LOG(INFO) << "ChConstraintRigidNode::Build_b";
    uint num_rigid_tet_contacts = data_manager->num_rigid_tet_contacts;
    uint num_unilaterals = data_manager->num_unilaterals;
    uint num_bilaterals = data_manager->num_bilaterals;

    //    if (num_rigid_tet_contacts > 0) {
    custom_vector<real4>& face_rigid_tet = data_manager->host_data.face_rigid_tet;
    custom_vector<int>& neighbor_rigid_node = data_manager->host_data.neighbor_rigid_tet;
    custom_vector<int>& contact_counts = data_manager->host_data.c_counts_rigid_tet;
    int num_boundary_tets = data_manager->host_data.boundary_element_fea.size();
    if (num_rigid_tet_contacts > 0) {
#pragma omp parallel for
        for (int p = 0; p < num_boundary_tets; p++) {
            int start = contact_counts[p];
            int end = contact_counts[p + 1];
            for (int index = start; index < end; index++) {
                int i = index - start;  // index that goes from 0
                real bi = 0;
                real depth = data_manager->host_data.dpth_rigid_tet[p * max_rigid_neighbors + i];

                bi = std::max(real(1.0) / step_size * depth, -contact_recovery_speed);
                //
                data_manager->host_data.b[start_boundary + index + 0] = bi;
                data_manager->host_data.b[start_boundary + num_rigid_tet_contacts + index * 2 + 0] = 0;
                data_manager->host_data.b[start_boundary + num_rigid_tet_contacts + index * 2 + 1] = 0;
            }
        }
    }

    if (num_rigid_constraints > 0) {
        for (int index = 0; index < num_rigid_constraints; index++) {
            int2 body_id = constraint_bodies[index];

            int body_a = body_id.x;
            int node_b = body_id.y;

            Mat33 Arw(rot_rigid[body_a]);
            real3 res =
                Transpose(Arw) * (pos_node[node_b] - TransformLocalToParent(pos_rigid[body_a], rot_rigid[body_a],
                                                                            constraint_position[index]));

            // res =
            //    Clamp(res / step_size, real3(-rigid_constraint_recovery_speed),
            //    real3(rigid_constraint_recovery_speed));

            // printf("res: [%f %f %f] \n", res.x, res.y, res.z);

            data_manager->host_data.b[start_rigid + index * 3 + 0] = res.x;
            data_manager->host_data.b[start_rigid + index * 3 + 1] = res.y;
            data_manager->host_data.b[start_rigid + index * 3 + 2] = res.z;
        }
    }
}
void ChFEAContainer::Build_E() {
    uint num_tets = data_manager->num_fea_tets;
    SubVectorType E_sub = blaze::subvector(data_manager->host_data.E, start_tet, num_tet_constraints);
    custom_vector<real3>& pos_node = data_manager->host_data.pos_node_fea;
    custom_vector<uint4>& tet_indices = data_manager->host_data.tet_indices;

    const real mu = 0.5 * youngs_modulus / (1. + poisson_ratio);  // 0.5?
    const real muInv = 1. / mu;

    real omn = 1. - poisson_ratio;
    real om2n = 1. - 2 * poisson_ratio;
    real s = youngs_modulus / (1. + poisson_ratio);
    real f = s / om2n;
    Mat33 E = f * Mat33(omn, poisson_ratio, poisson_ratio, poisson_ratio, omn, poisson_ratio, poisson_ratio,
                        poisson_ratio, omn);

    E = Inverse(E);

// real diag_stretch = youngs_modulus * (1. - poisson_ratio) / ((1. + poisson_ratio) * (1. - 2 * poisson_ratio));
// real diag_strain = youngs_modulus * (1. - 2 * poisson_ratio) / ((1. + poisson_ratio) * (1. - 2 * poisson_ratio));

#pragma omp parallel for
    for (int i = 0; i < num_tets; i++) {
        E_sub[i * 7 + 0] = E[0];   // diag_stretch;
        E_sub[i * 7 + 1] = E[5];   // diag_stretch;
        E_sub[i * 7 + 2] = E[10];  // diag_stretch;

        E_sub[i * 7 + 3] = muInv;  // diag_strain;
        E_sub[i * 7 + 4] = muInv;  // diag_strain;
        E_sub[i * 7 + 5] = muInv;  // diag_strain;
        // Volume
        E_sub[i * 7 + 6] = 0;  // 1.0 / youngs_modulus;
        //        printf("E [%f,%f,%f,%f,%f,%f] \n", E_sub[i * 7 + 0], E_sub[i * 7 + 1], E_sub[i * 7 + 2], E_sub[i * 7 +
        //        3],
        //               E_sub[i * 7 + 4], E_sub[i * 7 + 5], E_sub[i * 7 + 6]);
    }

    SubVectorType E_rigid = blaze::subvector(data_manager->host_data.E, start_rigid, num_rigid_constraints);
    E_rigid = 0;
}

template <typename T>
static void inline AppendRow12(T& D, const int row, const int offset, const uint4 col, const real init) {
    D.append(row, offset + col.x * 3 + 0, init);
    D.append(row, offset + col.x * 3 + 1, init);
    D.append(row, offset + col.x * 3 + 2, init);

    D.append(row, offset + col.y * 3 + 0, init);
    D.append(row, offset + col.y * 3 + 1, init);
    D.append(row, offset + col.y * 3 + 2, init);

    D.append(row, offset + col.z * 3 + 0, init);
    D.append(row, offset + col.z * 3 + 1, init);
    D.append(row, offset + col.z * 3 + 2, init);

    D.append(row, offset + col.w * 3 + 0, init);
    D.append(row, offset + col.w * 3 + 1, init);
    D.append(row, offset + col.w * 3 + 2, init);
}
void ChFEAContainer::GenerateSparsity() {
    uint num_tets = data_manager->num_fea_tets;
    uint num_rigid_bodies = data_manager->num_rigid_bodies;
    uint num_shafts = data_manager->num_shafts;
    uint num_fluid_bodies = data_manager->num_fluid_bodies;
    uint num_rigid_tet_contacts = data_manager->num_rigid_tet_contacts;

    uint body_offset = num_rigid_bodies * 6 + num_shafts + num_fluid_bodies * 3;

    CompressedMatrix<real>& D_T = data_manager->host_data.D_T;
    custom_vector<uint4>& tet_indices = data_manager->host_data.tet_indices;
    // std::cout << "start_tet " << start_tet << std::endl;
    for (int i = 0; i < num_tets; i++) {
        uint4 tet_ind = tet_indices[i];

        tet_ind = Sort(tet_ind);

        AppendRow12(D_T, start_tet + i * 7 + 0, body_offset, tet_ind, 0);
        D_T.finalize(start_tet + i * 7 + 0);

        AppendRow12(D_T, start_tet + i * 7 + 1, body_offset, tet_ind, 0);
        D_T.finalize(start_tet + i * 7 + 1);

        AppendRow12(D_T, start_tet + i * 7 + 2, body_offset, tet_ind, 0);
        D_T.finalize(start_tet + i * 7 + 2);
        ///==================================================================================================================================

        AppendRow12(D_T, start_tet + i * 7 + 3, body_offset, tet_ind, 0);
        D_T.finalize(start_tet + i * 7 + 3);

        AppendRow12(D_T, start_tet + i * 7 + 4, body_offset, tet_ind, 0);
        D_T.finalize(start_tet + i * 7 + 4);

        AppendRow12(D_T, start_tet + i * 7 + 5, body_offset, tet_ind, 0);
        D_T.finalize(start_tet + i * 7 + 5);

        // Volume constraint
        AppendRow12(D_T, start_tet + i * 7 + 6, body_offset, tet_ind, 0);
        D_T.finalize(start_tet + i * 7 + 6);
    }
    if (data_manager->num_rigid_tet_contacts) {
        int num_boundary_tets = data_manager->host_data.boundary_element_fea.size();
        custom_vector<int>& neighbor_rigid_tet = data_manager->host_data.neighbor_rigid_tet;
        custom_vector<int>& contact_counts = data_manager->host_data.c_counts_rigid_tet;
        custom_vector<real4>& face_rigid_tet = data_manager->host_data.face_rigid_tet;
        custom_vector<uint>& boundary_element_fea = data_manager->host_data.boundary_element_fea;
        custom_vector<uint4>& tet_indices = data_manager->host_data.tet_indices;
        for (int p = 0; p < num_boundary_tets; p++) {
            int start = contact_counts[p];
            int end = contact_counts[p + 1];
            uint4 tetind = tet_indices[boundary_element_fea[p]];
            for (int index = start; index < end; index++) {
                int i = index - start;  // index that goes from 0
                int rigid = neighbor_rigid_tet[p * max_rigid_neighbors + i];
                real4 face = face_rigid_tet[p * max_rigid_neighbors + i];
                uint3 sortedf = SortedFace(face.w, tetind);
                AppendRow6(D_T, start_boundary + index + 0, rigid * 6, 0);
                AppendRow3(D_T, start_boundary + index + 0, body_offset + sortedf.x * 3, 0);
                AppendRow3(D_T, start_boundary + index + 0, body_offset + sortedf.y * 3, 0);
                AppendRow3(D_T, start_boundary + index + 0, body_offset + sortedf.z * 3, 0);
                D_T.finalize(start_boundary + index + 0);
            }
        }
        for (int p = 0; p < num_boundary_tets; p++) {
            int start = contact_counts[p];
            int end = contact_counts[p + 1];
            uint4 tetind = tet_indices[boundary_element_fea[p]];
            for (int index = start; index < end; index++) {
                int i = index - start;  // index that goes from 0
                int rigid = neighbor_rigid_tet[p * max_rigid_neighbors + i];
                real4 face = face_rigid_tet[p * max_rigid_neighbors + i];
                uint3 sf = SortedFace(face.w, tetind);

                AppendRow6(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 0, rigid * 6, 0);
                AppendRow3(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 0, body_offset + sf.x * 3, 0);
                AppendRow3(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 0, body_offset + sf.y * 3, 0);
                AppendRow3(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 0, body_offset + sf.z * 3, 0);
                D_T.finalize(start_boundary + num_rigid_tet_contacts + index * 2 + 0);

                AppendRow6(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 1, rigid * 6, 0);
                AppendRow3(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 1, body_offset + sf.x * 3, 0);
                AppendRow3(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 1, body_offset + sf.y * 3, 0);
                AppendRow3(D_T, start_boundary + num_rigid_tet_contacts + index * 2 + 1, body_offset + sf.z * 3, 0);
                D_T.finalize(start_boundary + num_rigid_tet_contacts + index * 2 + 1);
            }
        }
    }

    if (num_rigid_constraints) {
        for (int index = 0; index < num_rigid_constraints; index++) {
            int2 body_id = constraint_bodies[index];
            int body_a = body_id.x;
            int node_b = body_id.y;
            // printf("Rigid fea: %d %d %d\n", start_rigid + index * 3 + 0, body_a * 6, body_offset + node_b * 3);

            AppendRow6(D_T, start_rigid + index * 3 + 0, body_a * 6, 0);
            AppendRow3(D_T, start_rigid + index * 3 + 0, body_offset + node_b * 3, 0);
            D_T.finalize(start_rigid + index * 3 + 0);
            // printf("Rigid fea: %d %d %d\n", start_rigid + index * 3 + 1, body_a * 6, body_offset + node_b * 3);
            AppendRow6(D_T, start_rigid + index * 3 + 1, body_a * 6, 0);
            AppendRow3(D_T, start_rigid + index * 3 + 1, body_offset + node_b * 3, 0);
            D_T.finalize(start_rigid + index * 3 + 1);
            // printf("Rigid fea: %d %d %d\n", start_rigid + index * 3 + 2, body_a * 6, body_offset + node_b * 3);
            AppendRow6(D_T, start_rigid + index * 3 + 2, body_a * 6, 0);
            AppendRow3(D_T, start_rigid + index * 3 + 2, body_offset + node_b * 3, 0);
            D_T.finalize(start_rigid + index * 3 + 2);
        }
    }
}

void ChFEAContainer::PreSolve() {
    if (gamma_old.size() > 0 && gamma_old.size() == data_manager->num_fea_tets * (6 + 1)) {
        blaze::subvector(data_manager->host_data.gamma, start_tet, data_manager->num_fea_tets * (6 + 1)) =
            gamma_old * 0.9;
    }

    if (gamma_old_rigid.size() > 0 && gamma_old_rigid.size() == num_rigid_constraints * 3) {
        blaze::subvector(data_manager->host_data.gamma, start_rigid, num_rigid_constraints * 3) = gamma_old_rigid * 0.9;
    }
}
void ChFEAContainer::PostSolve() {
    if (data_manager->num_fea_tets * (6 + 1) > 0) {
        gamma_old.resize(data_manager->num_fea_tets * (6 + 1));
        gamma_old = blaze::subvector(data_manager->host_data.gamma, start_tet, data_manager->num_fea_tets * (6 + 1));
    }
    if (num_rigid_constraints > 0) {
        gamma_old_rigid.resize(num_rigid_constraints * 3);
        gamma_old_rigid = blaze::subvector(data_manager->host_data.gamma, start_rigid, num_rigid_constraints * 3);
    }
}

struct FaceData {
    uint3 tri;
    int f;
    uint element;
    FaceData(){};
    FaceData(const uint3& t, int face, uint e) : tri(t), f(face), element(e) {}
};

struct {
    bool operator()(const FaceData& face1, const FaceData& face2) {
        uint3 a = face1.tri;
        uint3 b = face2.tri;

        if (a.x < b.x) {
            return true;
        }
        if (b.x < a.x) {
            return false;
        }
        if (a.y < b.y) {
            return true;
        }
        if (b.y < a.y) {
            return false;
        }
        if (a.z < b.z) {
            return true;
        }
        if (b.z < a.z) {
            return false;
        }
        return false;
    }
} customSort;

bool customCompare(const FaceData& face1, const FaceData& face2) {
    uint3 a = face1.tri;
    uint3 b = face2.tri;
    return (a.x == b.x && a.y == b.y && a.z == b.z);
}

void ChFEAContainer::FindSurface() {
    // Mark nodes that are on the boundary
    // Mark triangles that are on the boundary
    // Mark elements that are on the boundary
    uint num_nodes = data_manager->num_fea_nodes;
    uint num_tets = data_manager->num_fea_tets;

    custom_vector<uint4>& tet_indices = data_manager->host_data.tet_indices;
    custom_vector<uint4>& boundary_triangles_fea = data_manager->host_data.boundary_triangles_fea;
    custom_vector<uint>& boundary_node_fea = data_manager->host_data.boundary_node_fea;
    custom_vector<uint>& boundary_element_fea = data_manager->host_data.boundary_element_fea;

    custom_vector<uint> boundary_node_mask_fea;
    custom_vector<uint> boundary_element_mask_fea;

    std::vector<FaceData> faces(4 * num_tets);
    boundary_node_mask_fea.resize(num_nodes);
    boundary_element_mask_fea.resize(num_tets);

    std::fill(boundary_node_mask_fea.begin(), boundary_node_mask_fea.end(), 0);
    std::fill(boundary_element_mask_fea.begin(), boundary_element_mask_fea.end(), 0);

    for (int e = 0; e < num_tets; e++) {
        int i = tet_indices[e].x;
        int j = tet_indices[e].y;
        int k = tet_indices[e].z;
        int l = tet_indices[e].w;

        faces[4 * e + 0] = FaceData(Sort(_make_uint3(j, k, l)), 0, e);
        faces[4 * e + 1] = FaceData(Sort(_make_uint3(i, k, l)), 1, e);
        faces[4 * e + 2] = FaceData(Sort(_make_uint3(i, j, l)), 2, e);
        faces[4 * e + 3] = FaceData(Sort(_make_uint3(i, j, k)), 3, e);
    }

    std::sort(faces.begin(), faces.end(), customSort);

    uint face = 0;

    while (face < 4 * num_tets) {
        if (face < 4 * num_tets - 1) {
            uint3 tri1 = faces[face].tri;
            uint3 tri2 = faces[face + 1].tri;

            if (tri1.x == tri2.x && tri1.y == tri2.y && tri1.z == tri2.z) {
                face += 2;
                continue;
            }
        }

        uint3 tri = faces[face].tri;
        boundary_node_mask_fea[tri.x] = 1;
        boundary_node_mask_fea[tri.y] = 1;
        boundary_node_mask_fea[tri.z] = 1;

        int f = faces[face].f;
        uint e = faces[face].element;

        boundary_element_mask_fea[e] = 1;

        if (f == 0) {
            boundary_triangles_fea.push_back(_make_uint4(tet_indices[e].y, tet_indices[e].z, tet_indices[e].w, e));
        } else if (f == 1) {
            boundary_triangles_fea.push_back(_make_uint4(tet_indices[e].x, tet_indices[e].z, tet_indices[e].w, e));
        } else if (f == 2) {
            boundary_triangles_fea.push_back(_make_uint4(tet_indices[e].x, tet_indices[e].y, tet_indices[e].w, e));
        } else if (f == 3) {
            boundary_triangles_fea.push_back(_make_uint4(tet_indices[e].x, tet_indices[e].y, tet_indices[e].z, e));
        }

        face++;
    }
    num_boundary_triangles = boundary_triangles_fea.size();
    // num_boundary_nodes = std::accumulate(boundary_node_mask_fea.begin(), boundary_node_mask_fea.end(), 0);
    // num_boundary_elements = std::accumulate(boundary_element_mask_fea.begin(), boundary_element_mask_fea.end(), 0);

    // get the list of boundary tetrahedra and nodes
    boundary_node_fea.clear();

    for (int i = 0; i < num_nodes; i++) {
        if (boundary_node_mask_fea[i]) {
            boundary_node_fea.push_back(i);
        }
    }
    num_boundary_nodes = boundary_node_fea.size();

    boundary_element_fea.clear();

    for (int i = 0; i < num_tets; i++) {
        if (boundary_element_mask_fea[i]) {
            boundary_element_fea.push_back(i);
        }
    }
    num_boundary_elements = boundary_element_fea.size();
}

}  // END_OF_NAMESPACE____

/////////////////////