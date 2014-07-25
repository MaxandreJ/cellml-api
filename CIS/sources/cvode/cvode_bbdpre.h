/*
 * -----------------------------------------------------------------
 * $Revision: 1.8 $
 * $Date: 2010/12/01 22:10:38 $
 * -----------------------------------------------------------------
 * Programmer(s): Michael Wittman, Alan C. Hindmarsh and
 *                Radu Serban @ LLNL
 * -----------------------------------------------------------------
 * Copyright (c) 2002, The Regents of the University of California.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * -----------------------------------------------------------------
 * This is the header file for the CVBBDPRE module, for a
 * band-block-diagonal preconditioner, i.e. a block-diagonal
 * matrix with banded blocks, for use with CVSPGMR/CVSPBCG/CVSPTFQMR,
 * and the parallel implementation of the NVECTOR module.
 *
 * Summary:
 *
 * These routines provide a preconditioner matrix that is
 * block-diagonal with banded blocks. The blocking corresponds
 * to the distribution of the dependent variable vector y among
 * the processors. Each preconditioner block is generated from
 * the Jacobian of the local part (on the current processor) of a
 * given function g(t,y) approximating f(t,y). The blocks are
 * generated by a difference quotient scheme on each processor
 * independently. This scheme utilizes an assumed banded
 * structure with given half-bandwidths, mudq and mldq.
 * However, the banded Jacobian block kept by the scheme has
 * half-bandwiths mukeep and mlkeep, which may be smaller.
 *
 * The user's calling program should have the following form:
 *
 *   #include <nvector_parallel.h>
 *   #include <cvode/cvode_bbdpre.h>
 *   ...
 *   void *cvode_mem;
 *   ...
 *   Set y0
 *   ...
 *   cvode_mem = CVodeCreate(...);
 *   ier = CVodeMalloc(...);
 *   ...
 *   flag = CVSpgmr(cvode_mem, pretype, maxl);
 *      -or-
 *   flag = CVSpbcg(cvode_mem, pretype, maxl);
 *      -or-
 *   flag = CVSptfqmr(cvode_mem, pretype, maxl);
 *   ...
 *   flag = CVBBDPrecInit(cvode_mem, Nlocal, mudq ,mldq,
 *                        mukeep, mlkeep, dqrely, gloc, cfn);
 *   ...
 *   ier = CVode(...);
 *   ...
 *   CVodeFree(&cvode_mem);
 *
 *   Free y0
 *
 * The user-supplied routines required are:
 *
 *   f    = function defining the ODE right-hand side f(t,y).
 *
 *   gloc = function defining the approximation g(t,y).
 *
 *   cfn  = function to perform communication need for gloc.
 *
 * Notes:
 *
 * 1) This header file is included by the user for the definition
 *    of the CVBBDData type and for needed function prototypes.
 *
 * 2) The CVBBDPrecInit call includes half-bandwiths mudq and mldq
 *    to be used in the difference quotient calculation of the
 *    approximate Jacobian. They need not be the true
 *    half-bandwidths of the Jacobian of the local block of g,
 *    when smaller values may provide a greater efficiency.
 *    Also, the half-bandwidths mukeep and mlkeep of the retained
 *    banded approximate Jacobian block may be even smaller,
 *    to reduce storage and computation costs further.
 *    For all four half-bandwidths, the values need not be the
 *    same on every processor.
 *
 * 3) The actual name of the user's f function is passed to
 *    CVodeInit, and the names of the user's gloc and cfn
 *    functions are passed to CVBBDPrecInit.
 *
 * 4) The pointer to the user-defined data block user_data, which is
 *    set through CVodeSetUserData is also available to the user in
 *    gloc and cfn.
 *
 * 5) Optional outputs specific to this module are available by
 *    way of routines listed below. These include work space sizes
 *    and the cumulative number of gloc calls. The costs
 *    associated with this module also include nsetups banded LU
 *    factorizations, nlinsetups cfn calls, and npsolves banded
 *    backsolve calls, where nlinsetups and npsolves are
 *    integrator/CVSPGMR/CVSPBCG/CVSPTFQMR optional outputs.
 * -----------------------------------------------------------------
 */

#ifndef _CVBBDPRE_H
#define _CVBBDPRE_H

#ifdef __cplusplus  /* wrapper to enable C++ usage */
extern "C" {
#endif

#include <sundials/sundials_nvector.h>

/*
 * -----------------------------------------------------------------
 * Type : CVLocalFn
 * -----------------------------------------------------------------
 * The user must supply a function g(t,y) which approximates the
 * right-hand side function f for the system y'=f(t,y), and which
 * is computed locally (without interprocess communication).
 * (The case where g is mathematically identical to f is allowed.)
 * The implementation of this function must have type CVLocalFn.
 *
 * This function takes as input the local vector size Nlocal, the
 * independent variable value t, the local real dependent
 * variable vector y, and a pointer to the user-defined data
 * block user_data. It is to compute the local part of g(t,y) and
 * store this in the vector g.
 * (Allocation of memory for y and g is handled within the
 * preconditioner module.)
 * The user_data parameter is the same as that specified by the user
 * through the CVodeSetFdata routine.
 *
 * A CVLocalFn should return 0 if successful, a positive value if
 * a recoverable error occurred, and a negative value if an
 * unrecoverable error occurred.
 * -----------------------------------------------------------------
 */

typedef int (*CVLocalFn)(long int Nlocal, realtype t, N_Vector y,
                         N_Vector g, void *user_data);

/*
 * -----------------------------------------------------------------
 * Type : CVCommFn
 * -----------------------------------------------------------------
 * The user may supply a function of type CVCommFn which performs
 * all interprocess communication necessary to evaluate the
 * approximate right-hand side function described above.
 *
 * This function takes as input the local vector size Nlocal,
 * the independent variable value t, the dependent variable
 * vector y, and a pointer to the user-defined data block user_data.
 * The user_data parameter is the same as that specified by the user
 * through the CVodeSetUserData routine. The CVCommFn cfn is
 * expected to save communicated data in space defined within the
 * structure user_data. Note: A CVCommFn cfn does not have a return value.
 *
 * Each call to the CVCommFn cfn is preceded by a call to the
 * CVRhsFn f with the same (t,y) arguments. Thus cfn can omit any
 * communications done by f if relevant to the evaluation of g.
 * If all necessary communication was done by f, the user can
 * pass NULL for cfn in CVBBDPrecInit (see below).
 *
 * A CVCommFn should return 0 if successful, a positive value if
 * a recoverable error occurred, and a negative value if an
 * unrecoverable error occurred.
 * -----------------------------------------------------------------
 */

typedef int (*CVCommFn)(long int Nlocal, realtype t, N_Vector y,
                        void *user_data);

/*
 * -----------------------------------------------------------------
 * Function : CVBBDPrecInit
 * -----------------------------------------------------------------
 * CVBBDPrecInit allocates and initializes the BBD preconditioner.
 *
 * The parameters of CVBBDPrecInit are as follows:
 *
 * cvode_mem is the pointer to the integrator memory.
 *
 * Nlocal is the length of the local block of the vectors y etc.
 *        on the current processor.
 *
 * mudq, mldq are the upper and lower half-bandwidths to be used
 *            in the difference quotient computation of the local
 *            Jacobian block.
 *
 * mukeep, mlkeep are the upper and lower half-bandwidths of the
 *                retained banded approximation to the local Jacobian
 *                block.
 *
 * dqrely is an optional input. It is the relative increment
 *        in components of y used in the difference quotient
 *        approximations. To specify the default, pass 0.
 *        The default is dqrely = sqrt(unit roundoff).
 *
 * gloc is the name of the user-supplied function g(t,y) that
 *      approximates f and whose local Jacobian blocks are
 *      to form the preconditioner.
 *
 * cfn is the name of the user-defined function that performs
 *     necessary interprocess communication for the
 *     execution of gloc.
 *
 * The return value of CVBBDPrecInit is one of:
 *   CVSPILS_SUCCESS if no errors occurred
 *   CVSPILS_MEM_NULL if the integrator memory is NULL
 *   CVSPILS_LMEM_NULL if the linear solver memory is NULL
 *   CVSPILS_ILL_INPUT if an input has an illegal value
 *   CVSPILS_MEM_FAIL if a memory allocation request failed
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int CVBBDPrecInit(void *cvode_mem, long int Nlocal,
                                  long int mudq, long int mldq,
                                  long int mukeep, long int mlkeep,
                                  realtype dqrely,
                                  CVLocalFn gloc, CVCommFn cfn);

/*
 * -----------------------------------------------------------------
 * Function : CVBBDPrecReInit
 * -----------------------------------------------------------------
 * CVBBDPrecReInit re-initializes the BBDPRE module when solving a
 * sequence of problems of the same size with CVSPGMR/CVBBDPRE or
 * CVSPBCG/CVBBDPRE or CVSPTFQMR/CVBBDPRE provided there is no change
 * in Nlocal, mukeep, or mlkeep. After solving one problem, and after
 * calling CVodeReInit to re-initialize the integrator for a subsequent
 * problem, call CVBBDPrecReInit.
 *
 * All arguments have the same names and meanings as those
 * of CVBBDPrecInit.
 *
 * The return value of CVBBDPrecReInit is one of:
 *   CVSPILS_SUCCESS if no errors occurred
 *   CVSPILS_MEM_NULL if the integrator memory is NULL
 *   CVSPILS_LMEM_NULL if the linear solver memory is NULL
 *   CVSPILS_PMEM_NULL if the preconditioner memory is NULL
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int CVBBDPrecReInit(void *cvode_mem, long int mudq, long int mldq,
                    realtype dqrely);

/*
 * -----------------------------------------------------------------
 * BBDPRE optional output extraction routines
 * -----------------------------------------------------------------
 * CVBBDPrecGetWorkSpace returns the BBDPRE real and integer work space
 *                       sizes.
 * CVBBDPrecGetNumGfnEvals returns the number of calls to gfn.
 *
 * The return value of CVBBDPrecGet* is one of:
 *   CVSPILS_SUCCESS if no errors occurred
 *   CVSPILS_MEM_NULL if the integrator memory is NULL
 *   CVSPILS_LMEM_NULL if the linear solver memory is NULL
 *   CVSPILS_PMEM_NULL if the preconditioner memory is NULL
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int CVBBDPrecGetWorkSpace(void *cvode_mem, long int *lenrwLS, long int *leniwLS);
SUNDIALS_EXPORT int CVBBDPrecGetNumGfnEvals(void *cvode_mem, long int *ngevalsBBDP);

#ifdef __cplusplus
}
#endif

#endif
