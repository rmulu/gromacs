/*
This source code file is part of thread_mpi.  
Written by Sander Pronk, Erik Lindahl, and possibly others. 

Copyright (c) 2009, Sander Pronk, Erik Lindahl.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1) Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2) Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3) Neither the name of the copyright holders nor the
   names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY US ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL WE BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

If you want to redistribute modifications, please consider that
scientific software is very special. Version control is crucial -
bugs must be traceable. We will be happy to consider code for
inclusion in the official distribution, but derived work should not
be called official thread_mpi. Details are found in the README & COPYING
files.
*/


/* the profiling functions */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if ! (defined( _WIN32 ) || defined( _WIN64 ) )
#include <sys/time.h>
#endif

#include "thread_mpi/threads.h"
#include "thread_mpi/atomic.h"
#include "thread_mpi/tmpi.h"
#include "tmpi_impl.h"

#ifdef TMPI_TRACE
#include <stdarg.h>
#endif


/* this must match the tmpi_functions enum: */
const char *tmpi_function_names[] = 
{
    "Send",
    "Recv",
    "Sendrecv",
    "Isend",
    "Irecv",
    "Wait",
    "Test",
    "Waitall",

    "Barrier",

    "Bcast",
    "Gather",
    "Gatherv",
    "Scatter",
    "Scatterv",
    "Alltoall",
    "Alltoallv"
};


/* this must match the tmpi_wait_functions enum: */
const char *tmpi_waitfn_names[] = 
{
    "Send",
    "Recv",
    "Waitall",
    "Coll. send",
    "Coll. recv",
    "Barrier",
    "(All)Reduce",
};



/* we intentionally only do the ifdef here; this supresses warnings at the link
   stage about empty object files */
#ifdef TMPI_PROFILE

void tMPI_Profile_init(struct tmpi_profile *prof)
{
    int i;

    /* reset counters */
    for(i=0;i<TMPIFN_Nfunctions;i++)
    {
        prof->mpifn_calls[i]=0;
    }
#ifdef TMPI_CYCLE_COUNT
    for(i=0;i<TMPIWAIT_N;i++)
    {
        prof->wait_cycles[i]=0;
    }
    prof->global_start=tmpi_cycles_read();
    prof->global_stop=0;
    prof->start=0;
    prof->stop=0;
#endif
}

void tMPI_Profile_count(enum tmpi_functions fn)
{
    struct tmpi_thread *th=tMPI_Get_current();

    tMPI_Profile_count_thread(th, fn);
}


void tMPI_Profile_count_thread(struct tmpi_thread *th, enum tmpi_functions fn)
{
    (th->profile.mpifn_calls[fn])++;
    /*printf("counting %d, %ld\n",fn, th->profile.mpifn_calls[fn]);*/
}




#ifdef TMPI_CYCLE_COUNT
void tMPI_Profile_wait_start()
{
    struct tmpi_thread *th=tMPI_Get_current();
    tMPI_Profile_wait_start_thread(th);
}

void tMPI_Profile_wait_start_thread(struct tmpi_thread *th)
{
    th->profile.start=tmpi_cycles_read();
}

void tMPI_Profile_wait_stop(enum tmpi_wait_functions fn)
{
    struct tmpi_thread *th=tMPI_Get_current();
    tMPI_Profile_wait_stop_thread(th, fn);
}

void tMPI_Profile_wait_stop_thread(struct tmpi_thread *th, 
                                   enum tmpi_wait_functions fn)
{
    tmpi_cycles_t stop=tmpi_cycles_read();
    th->profile.wait_cycles[fn] += (stop - th->profile.start);
}
#endif






void tMPI_Profile_stop(struct tmpi_profile *prof)
{
#ifdef TMPI_CYCLE_COUNT
    prof->global_stop=tmpi_cycles_read();
#endif
}

/* output functions */
void tMPI_Profiles_summarize(int Nthreads, struct tmpi_thread *threads)
{
    int i,j,len=0;

    printf("\nTMPI PROFILE:\n");
    printf("%11s", " ");
    len+=11;
    for(j=0;j<Nthreads;j++)
    {
        char thrn[128];
        snprintf(thrn, sizeof(thrn), "Thread %d", j);
        printf(" %10s", thrn);
        len+=11;
    }
    printf(" %10s\n", "Total");
    len+=11;

    /* print line */
    for(i=0;i<len;i++)
        printf("-");
    printf("\n");

    for(i=0;i<TMPIFN_Nfunctions;i++)
    {
        long unsigned int total=0;

        printf("%11s", tmpi_function_names[i]);
        for(j=0;j<Nthreads;j++)
        {
            long unsigned int count=threads[j].profile.mpifn_calls[i];

            total += count;
            printf(" %10ld", count);
        }
        printf(" %10ld\n", total);
    }

#ifdef TMPI_CYCLE_COUNT
    printf("\nWait times as fraction of total run time:\n");
    for(j=0;j<Nthreads;j++)
        threads[j].profile.totals=0.;

    for(i=0;i<TMPIWAIT_N;i++)
    {
        double tot_time=0.;
        double tot_diff=0.;

        printf("%11s", tmpi_waitfn_names[i]);
        for(j=0;j<Nthreads;j++)
        {
            double time=(double)(threads[j].profile.global_stop - 
                                 threads[j].profile.global_start );
            double diff=((double)threads[j].profile.wait_cycles[i]);
            tot_time += time;
            tot_diff += diff;
            threads[j].profile.totals += diff;
            printf(" %10.5f", diff/time);
        }
        printf(" %10.5f\n", tot_diff/tot_time);
    }

    {
        double tot_time=0.;
        double tot_diff=0.;

        printf("%11s", "Total wait");
        for(j=0;j<Nthreads;j++)
        {
            double time=(double)(threads[j].profile.global_stop - 
                                 threads[j].profile.global_start );
            double diff=threads[j].profile.totals;

            tot_time += time;
            tot_diff += diff;
            printf(" %10.5f", diff/time );
        } 
        printf(" %10.5f\n", tot_diff/tot_time);
    }
#endif
    /* print line */
    for(i=0;i<len;i++)
        printf("-");
    printf("\n");

    printf("\n");

}

/* destroy all of them  */
void tMPI_Profiles_destroy(int Nthreads, struct tmpi_thread *threads)
{
    /*
    int i;

    for(i=0;i<Nthreads;i++)
        tMPI_Free(threads[i].profile);*/
}
#endif
