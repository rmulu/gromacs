/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2013,2014,2015,2016,2017,2018,2019, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
#ifndef GMX_MDLIB_SIM_UTIL_H
#define GMX_MDLIB_SIM_UTIL_H

#include "gromacs/fileio/enxio.h"
#include "gromacs/mdlib/vcm.h"
#include "gromacs/timing/wallcycle.h"
#include "gromacs/timing/walltime_accounting.h"
#include "gromacs/utility/arrayref.h"

struct gmx_ekindata_t;
struct gmx_enerdata_t;
struct gmx_output_env_t;
struct gmx_pme_t;
struct nonbonded_verlet_t;
struct t_forcerec;
struct t_mdatoms;
struct t_nrnb;

namespace gmx
{
class BoxDeformation;
class Constraints;
class EnergyOutput;
class IMDOutputProvider;
class MDLogger;
class Update;
}

typedef struct gmx_global_stat *gmx_global_stat_t;

/*! \brief Parallellizes put_atoms_in_box()
 *
 * This wrapper function around put_atoms_in_box() with the ugly manual
 * workload splitting is needed to avoid silently introducing multithreading
 * in tools.
 * \param[in]    ePBC   The pbc type
 * \param[in]    box    The simulation box
 * \param[inout] x      The coordinates of the atoms
 */
void put_atoms_in_box_omp(int ePBC, const matrix box, gmx::ArrayRef<gmx::RVec> x);



/* ROUTINES from stat.c */
gmx_global_stat_t global_stat_init(const t_inputrec *ir);

void global_stat_destroy(gmx_global_stat_t gs);

void global_stat(const gmx_global_stat *gs,
                 const t_commrec *cr, gmx_enerdata_t *enerd,
                 tensor fvir, tensor svir, rvec mu_tot,
                 const t_inputrec *inputrec,
                 gmx_ekindata_t *ekind,
                 const gmx::Constraints *constr, t_vcm *vcm,
                 int nsig, real *sig,
                 int *totalNumberOfBondedInteractions,
                 gmx_bool bSumEkinhOld, int flags);
/* All-reduce energy-like quantities over cr->mpi_comm_mysim */

bool do_per_step(int64_t step, int64_t nstep);
/* Return TRUE if io should be done */

/* Routine in sim_util.c */

gmx_bool use_GPU(const nonbonded_verlet_t *nbv);

#endif
