/* -*- mode: c; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; c-file-style: "stroustrup"; -*-
 *
 *                This source code is part of
 *
 *                 G   R   O   M   A   C   S
 *
 *          GROningen MAchine for Chemical Simulations
 *
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 *
 * For more info, check our website at http://www.gromacs.org
 *
 * And Hey:
 * GROningen Mixture of Alchemy and Childrens' Stories
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "gmx_fatal.h"
#include "main.h"
#include "smalloc.h"
#include "network.h"
#include "copyrite.h"
#include "statutil.h"
#include <ctype.h>
#include "macros.h"

#include "gromacs/utility/gmxmpi.h"


/* The source code in this file should be thread-safe.
      Please keep it that way. */

gmx_bool gmx_mpi_initialized(void)
{
    int n;
#ifndef GMX_MPI
    return 0;
#else
    MPI_Initialized(&n);

    return n;
#endif
}

void gmx_do_mpi_init(int gmx_unused *argc, char gmx_unused ***argv)
{
#ifndef GMX_MPI
    gmx_call("gmx_do_mpi_init");
#else
    if (!gmx_mpi_initialized())
    {
#ifdef GMX_LIB_MPI
#ifdef GMX_FAHCORE
        (void) fah_MPI_Init(argc, argv);
#else
        (void) MPI_Init(argc, argv);
#endif
#endif
    }
#endif
}

void gmx_fill_commrec_from_mpi(t_commrec *cr)
{
#ifndef GMX_MPI
    gmx_call("gmx_fill_commrec_from_mpi");
#else
    char   buf[256];
    int    resultlen;             /* actual length of node name      */
    int    i, flag;
    int    mpi_num_nodes;
    int    mpi_my_rank;
    char   mpi_hostname[MPI_MAX_PROCESSOR_NAME];

    mpi_num_nodes = gmx_node_num();
    mpi_my_rank   = gmx_node_rank();
    (void) MPI_Get_processor_name( mpi_hostname, &resultlen );

#ifdef GMX_LIB_MPI
    if (debug)
    {
        fprintf(debug, "NNODES=%d, MYRANK=%d, HOSTNAME=%s\n",
                mpi_num_nodes, mpi_my_rank, mpi_hostname);
    }
#endif

    cr->nnodes           = mpi_num_nodes;
    cr->nodeid           = mpi_my_rank;
    cr->sim_nodeid       = mpi_my_rank;
    cr->mpi_comm_mysim   = MPI_COMM_WORLD;
    cr->mpi_comm_mygroup = MPI_COMM_WORLD;
#endif
}

int  gmx_node_num(void)
{
#ifndef GMX_MPI
    return 1;
#else
    int i;
    (void) MPI_Comm_size(MPI_COMM_WORLD, &i);
    return i;
#endif
}

int gmx_node_rank(void)
{
#ifndef GMX_MPI
    return 0;
#else
    int i;
    (void) MPI_Comm_rank(MPI_COMM_WORLD, &i);
    return i;
#endif
}


int gmx_hostname_num()
{
#ifndef GMX_MPI
    return 0;
#else
#ifdef GMX_THREAD_MPI
    /* thread-MPI currently puts the thread number in the process name,
     * we might want to change this, as this is inconsistent with what
     * most MPI implementations would do when running on a single node.
     */
    return 0;
#else
    int  resultlen, hostnum, i, j;
    char mpi_hostname[MPI_MAX_PROCESSOR_NAME], hostnum_str[MPI_MAX_PROCESSOR_NAME];

    MPI_Get_processor_name(mpi_hostname, &resultlen);
    /* This procedure can only differentiate nodes with host names
     * that end on unique numbers.
     */
    i = 0;
    j = 0;
    /* Only parse the host name up to the first dot */
    while (i < resultlen && mpi_hostname[i] != '.')
    {
        if (isdigit(mpi_hostname[i]))
        {
            hostnum_str[j++] = mpi_hostname[i];
        }
        i++;
    }
    hostnum_str[j] = '\0';
    if (j == 0)
    {
        hostnum = 0;
    }
    else
    {
        /* Use only the last 9 decimals, so we don't overflow an int */
        hostnum = strtol(hostnum_str + max(0, j-9), NULL, 10);
    }

    if (debug)
    {
        fprintf(debug, "In gmx_setup_nodecomm: hostname '%s', hostnum %d\n",
                mpi_hostname, hostnum);
    }
    return hostnum;
#endif
#endif
}

void gmx_setup_nodecomm(FILE gmx_unused *fplog, t_commrec *cr)
{
    gmx_nodecomm_t *nc;
    int             n, rank, hostnum, ng, ni;

    /* Many MPI implementations do not optimize MPI_Allreduce
     * (and probably also other global communication calls)
     * for multi-core nodes connected by a network.
     * We can optimize such communication by using one MPI call
     * within each node and one between the nodes.
     * For MVAPICH2 and Intel MPI this reduces the time for
     * the global_stat communication by 25%
     * for 2x2-core 3 GHz Woodcrest connected by mixed DDR/SDR Infiniband.
     * B. Hess, November 2007
     */

    nc = &cr->nc;

    nc->bUse = FALSE;
#ifndef GMX_THREAD_MPI
#ifdef GMX_MPI
    MPI_Comm_size(cr->mpi_comm_mygroup, &n);
    MPI_Comm_rank(cr->mpi_comm_mygroup, &rank);

    hostnum = gmx_hostname_num();

    if (debug)
    {
        fprintf(debug, "In gmx_setup_nodecomm: splitting communicator of size %d\n", n);
    }


    /* The intra-node communicator, split on node number */
    MPI_Comm_split(cr->mpi_comm_mygroup, hostnum, rank, &nc->comm_intra);
    MPI_Comm_rank(nc->comm_intra, &nc->rank_intra);
    if (debug)
    {
        fprintf(debug, "In gmx_setup_nodecomm: node rank %d rank_intra %d\n",
                rank, nc->rank_intra);
    }
    /* The inter-node communicator, split on rank_intra.
     * We actually only need the one for rank=0,
     * but it is easier to create them all.
     */
    MPI_Comm_split(cr->mpi_comm_mygroup, nc->rank_intra, rank, &nc->comm_inter);
    /* Check if this really created two step communication */
    MPI_Comm_size(nc->comm_inter, &ng);
    MPI_Comm_size(nc->comm_intra, &ni);
    if (debug)
    {
        fprintf(debug, "In gmx_setup_nodecomm: groups %d, my group size %d\n",
                ng, ni);
    }

    if (getenv("GMX_NO_NODECOMM") == NULL &&
        ((ng > 1 && ng < n) || (ni > 1 && ni < n)))
    {
        nc->bUse = TRUE;
        if (fplog)
        {
            fprintf(fplog, "Using two step summing over %d groups of on average %.1f processes\n\n",
                    ng, (real)n/(real)ng);
        }
        if (nc->rank_intra > 0)
        {
            MPI_Comm_free(&nc->comm_inter);
        }
    }
    else
    {
        /* One group or all processes in a separate group, use normal summing */
        MPI_Comm_free(&nc->comm_inter);
        MPI_Comm_free(&nc->comm_intra);
        if (debug)
        {
            fprintf(debug, "In gmx_setup_nodecomm: not unsing separate inter- and intra-node communicators.\n");
        }
    }
#endif
#else
    /* tMPI runs only on a single node so just use the nodeid */
    nc->rank_intra = cr->nodeid;
#endif
}

void gmx_init_intranode_counters(t_commrec *cr)
{
    /* counters for PP+PME and PP-only processes on my physical node */
    int nrank_intranode, rank_intranode;
    int nrank_pp_intranode, rank_pp_intranode;
    /* thread-MPI is not initialized when not running in parallel */
#if defined GMX_MPI && !defined GMX_THREAD_MPI
    int nrank_world, rank_world;
    int i, mynum, *num, *num_s, *num_pp, *num_pp_s;

    MPI_Comm_size(MPI_COMM_WORLD, &nrank_world);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank_world);

    /* Get the node number from the hostname to identify the nodes */
    mynum = gmx_hostname_num();

    /* We can't rely on MPI_IN_PLACE, so we need send and receive buffers */
    snew(num,   nrank_world);
    snew(num_s, nrank_world);
    snew(num_pp,   nrank_world);
    snew(num_pp_s, nrank_world);

    num_s[rank_world]    = mynum;
    num_pp_s[rank_world] = (cr->duty & DUTY_PP) ? mynum : -1;

    MPI_Allreduce(num_s,    num,    nrank_world, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(num_pp_s, num_pp, nrank_world, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    nrank_intranode    = 0;
    rank_intranode     = 0;
    nrank_pp_intranode = 0;
    rank_pp_intranode  = 0;
    for (i = 0; i < nrank_world; i++)
    {
        if (num[i] == mynum)
        {
            nrank_intranode++;
            if (i < rank_world)
            {
                rank_intranode++;
            }
        }
        if ((cr->duty & DUTY_PP) && num_pp[i] == mynum)
        {
            nrank_pp_intranode++;
            if (i < rank_world)
            {
                rank_pp_intranode++;
            }
        }
    }
    sfree(num);
    sfree(num_s);
    sfree(num_pp);
    sfree(num_pp_s);
#else
    /* Serial or thread-MPI code: we run within a single physical node */
    nrank_intranode    = cr->nnodes;
    rank_intranode     = cr->sim_nodeid;
    nrank_pp_intranode = cr->nnodes - cr->npmenodes;
    rank_pp_intranode  = cr->nodeid;
#endif

    if (debug)
    {
        char sbuf[STRLEN];
        if (cr->duty & DUTY_PP && cr->duty & DUTY_PME)
        {
            sprintf(sbuf, "PP+PME");
        }
        else
        {
            sprintf(sbuf, "%s", cr->duty & DUTY_PP ? "PP" : "PME");
        }
        fprintf(debug, "On %3s node %d: nrank_intranode=%d, rank_intranode=%d, "
                "nrank_pp_intranode=%d, rank_pp_intranode=%d\n",
                sbuf, cr->sim_nodeid,
                nrank_intranode, rank_intranode,
                nrank_pp_intranode, rank_pp_intranode);
    }

    cr->nrank_intranode    = nrank_intranode;
    cr->rank_intranode     = rank_intranode;
    cr->nrank_pp_intranode = nrank_pp_intranode;
    cr->rank_pp_intranode  = rank_pp_intranode;
}


void gmx_barrier(const t_commrec *cr)
{
#ifndef GMX_MPI
    gmx_call("gmx_barrier");
#else
    MPI_Barrier(cr->mpi_comm_mygroup);
#endif
}

void gmx_abort(int gmx_unused noderank, int gmx_unused nnodes, int gmx_unused errorno)
{
#ifndef GMX_MPI
    gmx_call("gmx_abort");
#else
#ifdef GMX_THREAD_MPI
    fprintf(stderr, "Halting program %s\n", ShortProgram());
    gmx_thanx(stderr);
    exit(1);
#else
    if (nnodes > 1)
    {
        fprintf(stderr, "Halting parallel program %s on CPU %d out of %d\n",
                ShortProgram(), noderank, nnodes);
    }
    else
    {
        fprintf(stderr, "Halting program %s\n", ShortProgram());
    }

    gmx_thanx(stderr);
    MPI_Abort(MPI_COMM_WORLD, errorno);
    exit(1);
#endif
#endif
}

void gmx_bcast(int nbytes, void *b, const t_commrec *cr)
{
#ifndef GMX_MPI
    gmx_call("gmx_bast");
#else
    MPI_Bcast(b, nbytes, MPI_BYTE, MASTERRANK(cr), cr->mpi_comm_mygroup);
#endif
}

void gmx_bcast_sim(int nbytes, void *b, const t_commrec *cr)
{
#ifndef GMX_MPI
    gmx_call("gmx_bast");
#else
    MPI_Bcast(b, nbytes, MPI_BYTE, MASTERRANK(cr), cr->mpi_comm_mysim);
#endif
}

void gmx_sumd(int nr, double r[], const t_commrec *cr)
{
#ifndef GMX_MPI
    gmx_call("gmx_sumd");
#else
#if defined(MPI_IN_PLACE_EXISTS) || defined(GMX_THREAD_MPI)
    if (cr->nc.bUse)
    {
        if (cr->nc.rank_intra == 0)
        {
            /* Use two step summing. */
            MPI_Reduce(MPI_IN_PLACE, r, nr, MPI_DOUBLE, MPI_SUM, 0,
                       cr->nc.comm_intra);
            /* Sum the roots of the internal (intra) buffers. */
            MPI_Allreduce(MPI_IN_PLACE, r, nr, MPI_DOUBLE, MPI_SUM,
                          cr->nc.comm_inter);
        }
        else
        {
            /* This is here because of the silly MPI specification
                that MPI_IN_PLACE should be put in sendbuf instead of recvbuf */
            MPI_Reduce(r, NULL, nr, MPI_DOUBLE, MPI_SUM, 0, cr->nc.comm_intra);
        }
        MPI_Bcast(r, nr, MPI_DOUBLE, 0, cr->nc.comm_intra);
    }
    else
    {
        MPI_Allreduce(MPI_IN_PLACE, r, nr, MPI_DOUBLE, MPI_SUM,
                      cr->mpi_comm_mygroup);
    }
#else
    int i;

    if (nr > cr->mpb->dbuf_alloc)
    {
        cr->mpb->dbuf_alloc = nr;
        srenew(cr->mpb->dbuf, cr->mpb->dbuf_alloc);
    }
    if (cr->nc.bUse)
    {
        /* Use two step summing */
        MPI_Allreduce(r, cr->mpb->dbuf, nr, MPI_DOUBLE, MPI_SUM, cr->nc.comm_intra);
        if (cr->nc.rank_intra == 0)
        {
            /* Sum with the buffers reversed */
            MPI_Allreduce(cr->mpb->dbuf, r, nr, MPI_DOUBLE, MPI_SUM,
                          cr->nc.comm_inter);
        }
        MPI_Bcast(r, nr, MPI_DOUBLE, 0, cr->nc.comm_intra);
    }
    else
    {
        MPI_Allreduce(r, cr->mpb->dbuf, nr, MPI_DOUBLE, MPI_SUM,
                      cr->mpi_comm_mygroup);
        for (i = 0; i < nr; i++)
        {
            r[i] = cr->mpb->dbuf[i];
        }
    }
#endif
#endif
}

void gmx_sumf(int nr, float r[], const t_commrec *cr)
{
#ifndef GMX_MPI
    gmx_call("gmx_sumf");
#else
#if defined(MPI_IN_PLACE_EXISTS) || defined(GMX_THREAD_MPI)
    if (cr->nc.bUse)
    {
        /* Use two step summing.  */
        if (cr->nc.rank_intra == 0)
        {
            MPI_Reduce(MPI_IN_PLACE, r, nr, MPI_FLOAT, MPI_SUM, 0,
                       cr->nc.comm_intra);
            /* Sum the roots of the internal (intra) buffers */
            MPI_Allreduce(MPI_IN_PLACE, r, nr, MPI_FLOAT, MPI_SUM,
                          cr->nc.comm_inter);
        }
        else
        {
            /* This is here because of the silly MPI specification
                that MPI_IN_PLACE should be put in sendbuf instead of recvbuf */
            MPI_Reduce(r, NULL, nr, MPI_FLOAT, MPI_SUM, 0, cr->nc.comm_intra);
        }
        MPI_Bcast(r, nr, MPI_FLOAT, 0, cr->nc.comm_intra);
    }
    else
    {
        MPI_Allreduce(MPI_IN_PLACE, r, nr, MPI_FLOAT, MPI_SUM, cr->mpi_comm_mygroup);
    }
#else
    int i;

    if (nr > cr->mpb->fbuf_alloc)
    {
        cr->mpb->fbuf_alloc = nr;
        srenew(cr->mpb->fbuf, cr->mpb->fbuf_alloc);
    }
    if (cr->nc.bUse)
    {
        /* Use two step summing */
        MPI_Allreduce(r, cr->mpb->fbuf, nr, MPI_FLOAT, MPI_SUM, cr->nc.comm_intra);
        if (cr->nc.rank_intra == 0)
        {
            /* Sum with the buffers reversed */
            MPI_Allreduce(cr->mpb->fbuf, r, nr, MPI_FLOAT, MPI_SUM,
                          cr->nc.comm_inter);
        }
        MPI_Bcast(r, nr, MPI_FLOAT, 0, cr->nc.comm_intra);
    }
    else
    {
        MPI_Allreduce(r, cr->mpb->fbuf, nr, MPI_FLOAT, MPI_SUM,
                      cr->mpi_comm_mygroup);
        for (i = 0; i < nr; i++)
        {
            r[i] = cr->mpb->fbuf[i];
        }
    }
#endif
#endif
}

void gmx_sumi(int nr, int r[], const t_commrec *cr)
{
#ifndef GMX_MPI
    gmx_call("gmx_sumi");
#else
#if defined(MPI_IN_PLACE_EXISTS) || defined(GMX_THREAD_MPI)
    if (cr->nc.bUse)
    {
        /* Use two step summing */
        if (cr->nc.rank_intra == 0)
        {
            MPI_Reduce(MPI_IN_PLACE, r, nr, MPI_INT, MPI_SUM, 0, cr->nc.comm_intra);
            /* Sum with the buffers reversed */
            MPI_Allreduce(MPI_IN_PLACE, r, nr, MPI_INT, MPI_SUM, cr->nc.comm_inter);
        }
        else
        {
            /* This is here because of the silly MPI specification
                that MPI_IN_PLACE should be put in sendbuf instead of recvbuf */
            MPI_Reduce(r, NULL, nr, MPI_INT, MPI_SUM, 0, cr->nc.comm_intra);
        }
        MPI_Bcast(r, nr, MPI_INT, 0, cr->nc.comm_intra);
    }
    else
    {
        MPI_Allreduce(MPI_IN_PLACE, r, nr, MPI_INT, MPI_SUM, cr->mpi_comm_mygroup);
    }
#else
    int i;

    if (nr > cr->mpb->ibuf_alloc)
    {
        cr->mpb->ibuf_alloc = nr;
        srenew(cr->mpb->ibuf, cr->mpb->ibuf_alloc);
    }
    if (cr->nc.bUse)
    {
        /* Use two step summing */
        MPI_Allreduce(r, cr->mpb->ibuf, nr, MPI_INT, MPI_SUM, cr->nc.comm_intra);
        if (cr->nc.rank_intra == 0)
        {
            /* Sum with the buffers reversed */
            MPI_Allreduce(cr->mpb->ibuf, r, nr, MPI_INT, MPI_SUM, cr->nc.comm_inter);
        }
        MPI_Bcast(r, nr, MPI_INT, 0, cr->nc.comm_intra);
    }
    else
    {
        MPI_Allreduce(r, cr->mpb->ibuf, nr, MPI_INT, MPI_SUM, cr->mpi_comm_mygroup);
        for (i = 0; i < nr; i++)
        {
            r[i] = cr->mpb->ibuf[i];
        }
    }
#endif
#endif
}

void gmx_sumli(int nr, gmx_large_int_t r[], const t_commrec *cr)
{
#ifndef GMX_MPI
    gmx_call("gmx_sumli");
#else
#if defined(MPI_IN_PLACE_EXISTS) || defined(GMX_THREAD_MPI)
    if (cr->nc.bUse)
    {
        /* Use two step summing */
        if (cr->nc.rank_intra == 0)
        {
            MPI_Reduce(MPI_IN_PLACE, r, nr, GMX_MPI_LARGE_INT, MPI_SUM, 0,
                       cr->nc.comm_intra);
            /* Sum with the buffers reversed */
            MPI_Allreduce(MPI_IN_PLACE, r, nr, GMX_MPI_LARGE_INT, MPI_SUM,
                          cr->nc.comm_inter);
        }
        else
        {
            /* This is here because of the silly MPI specification
                that MPI_IN_PLACE should be put in sendbuf instead of recvbuf */
            MPI_Reduce(r, NULL, nr, GMX_MPI_LARGE_INT, MPI_SUM, 0, cr->nc.comm_intra);
        }
        MPI_Bcast(r, nr, GMX_MPI_LARGE_INT, 0, cr->nc.comm_intra);
    }
    else
    {
        MPI_Allreduce(MPI_IN_PLACE, r, nr, GMX_MPI_LARGE_INT, MPI_SUM, cr->mpi_comm_mygroup);
    }
#else
    int i;

    if (nr > cr->mpb->libuf_alloc)
    {
        cr->mpb->libuf_alloc = nr;
        srenew(cr->mpb->libuf, cr->mpb->libuf_alloc);
    }
    if (cr->nc.bUse)
    {
        /* Use two step summing */
        MPI_Allreduce(r, cr->mpb->libuf, nr, GMX_MPI_LARGE_INT, MPI_SUM,
                      cr->nc.comm_intra);
        if (cr->nc.rank_intra == 0)
        {
            /* Sum with the buffers reversed */
            MPI_Allreduce(cr->mpb->libuf, r, nr, GMX_MPI_LARGE_INT, MPI_SUM,
                          cr->nc.comm_inter);
        }
        MPI_Bcast(r, nr, GMX_MPI_LARGE_INT, 0, cr->nc.comm_intra);
    }
    else
    {
        MPI_Allreduce(r, cr->mpb->libuf, nr, GMX_MPI_LARGE_INT, MPI_SUM,
                      cr->mpi_comm_mygroup);
        for (i = 0; i < nr; i++)
        {
            r[i] = cr->mpb->libuf[i];
        }
    }
#endif
#endif
}



#ifdef GMX_MPI
void gmx_sumd_comm(int nr, double r[], MPI_Comm mpi_comm)
{
#if defined(MPI_IN_PLACE_EXISTS) || defined(GMX_THREAD_MPI)
    MPI_Allreduce(MPI_IN_PLACE, r, nr, MPI_DOUBLE, MPI_SUM, mpi_comm);
#else
    /* this function is only used in code that is not performance critical,
       (during setup, when comm_rec is not the appropriate communication
       structure), so this isn't as bad as it looks. */
    double *buf;
    int     i;

    snew(buf, nr);
    MPI_Allreduce(r, buf, nr, MPI_DOUBLE, MPI_SUM, mpi_comm);
    for (i = 0; i < nr; i++)
    {
        r[i] = buf[i];
    }
    sfree(buf);
#endif
}
#endif

#ifdef GMX_MPI
void gmx_sumf_comm(int nr, float r[], MPI_Comm mpi_comm)
{
#if defined(MPI_IN_PLACE_EXISTS) || defined(GMX_THREAD_MPI)
    MPI_Allreduce(MPI_IN_PLACE, r, nr, MPI_FLOAT, MPI_SUM, mpi_comm);
#else
    /* this function is only used in code that is not performance critical,
       (during setup, when comm_rec is not the appropriate communication
       structure), so this isn't as bad as it looks. */
    float *buf;
    int    i;

    snew(buf, nr);
    MPI_Allreduce(r, buf, nr, MPI_FLOAT, MPI_SUM, mpi_comm);
    for (i = 0; i < nr; i++)
    {
        r[i] = buf[i];
    }
    sfree(buf);
#endif
}
#endif

void gmx_sumd_sim(int nr, double r[], const gmx_multisim_t *ms)
{
#ifndef GMX_MPI
    gmx_call("gmx_sumd_sim");
#else
    gmx_sumd_comm(nr, r, ms->mpi_comm_masters);
#endif
}

void gmx_sumf_sim(int nr, float r[], const gmx_multisim_t *ms)
{
#ifndef GMX_MPI
    gmx_call("gmx_sumf_sim");
#else
    gmx_sumf_comm(nr, r, ms->mpi_comm_masters);
#endif
}

void gmx_sumi_sim(int nr, int r[], const gmx_multisim_t *ms)
{
#ifndef GMX_MPI
    gmx_call("gmx_sumi_sim");
#else
#if defined(MPI_IN_PLACE_EXISTS) || defined(GMX_THREAD_MPI)
    MPI_Allreduce(MPI_IN_PLACE, r, nr, MPI_INT, MPI_SUM, ms->mpi_comm_masters);
#else
    /* this is thread-unsafe, but it will do for now: */
    int i;

    if (nr > ms->mpb->ibuf_alloc)
    {
        ms->mpb->ibuf_alloc = nr;
        srenew(ms->mpb->ibuf, ms->mpb->ibuf_alloc);
    }
    MPI_Allreduce(r, ms->mpb->ibuf, nr, MPI_INT, MPI_SUM, ms->mpi_comm_masters);
    for (i = 0; i < nr; i++)
    {
        r[i] = ms->mpb->ibuf[i];
    }
#endif
#endif
}

void gmx_sumli_sim(int nr, gmx_large_int_t r[], const gmx_multisim_t *ms)
{
#ifndef GMX_MPI
    gmx_call("gmx_sumli_sim");
#else
#if defined(MPI_IN_PLACE_EXISTS) || defined(GMX_THREAD_MPI)
    MPI_Allreduce(MPI_IN_PLACE, r, nr, GMX_MPI_LARGE_INT, MPI_SUM,
                  ms->mpi_comm_masters);
#else
    /* this is thread-unsafe, but it will do for now: */
    int i;

    if (nr > ms->mpb->libuf_alloc)
    {
        ms->mpb->libuf_alloc = nr;
        srenew(ms->mpb->libuf, ms->mpb->libuf_alloc);
    }
    MPI_Allreduce(r, ms->mpb->libuf, nr, GMX_MPI_LARGE_INT, MPI_SUM,
                  ms->mpi_comm_masters);
    for (i = 0; i < nr; i++)
    {
        r[i] = ms->mpb->libuf[i];
    }
#endif
#endif
}


void gmx_finalize_mpi(void)
{
#ifndef GMX_MPI
    /* Compiled without MPI, no MPI finalizing needed */
    return;
#else
    int finalized;
    int ret;

    if (!gmx_mpi_initialized())
    {
        return;
    }
    /* just as a check; we don't want to finalize twice */
    MPI_Finalized(&finalized);
    if (finalized)
    {
        return;
    }

    /* We sync the processes here to try to avoid problems
     * with buggy MPI implementations that could cause
     * unfinished processes to terminate.
     */
    MPI_Barrier(MPI_COMM_WORLD);

    /*
       if (DOMAINDECOMP(cr)) {
       if (cr->npmenodes > 0 || cr->dd->bCartesian)
        MPI_Comm_free(&cr->mpi_comm_mygroup);
       if (cr->dd->bCartesian)
        MPI_Comm_free(&cr->mpi_comm_mysim);
       }
     */

    /* Apparently certain mpich implementations cause problems
     * with MPI_Finalize. In that case comment out MPI_Finalize.
     */
    if (debug)
    {
        fprintf(debug, "Will call MPI_Finalize now\n");
    }

    ret = MPI_Finalize();
    if (debug)
    {
        fprintf(debug, "Return code from MPI_Finalize = %d\n", ret);
    }
#endif
}
