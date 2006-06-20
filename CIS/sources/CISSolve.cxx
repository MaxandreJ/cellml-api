#include <exception>
#include <inttypes.h>
#include "Utilities.hxx"
#include "CISImplementation.hxx"
#include <gsl/gsl_odeiv.h>
#include <gsl/gsl_errno.h>

struct EvaluationInformation
{
  double* constants;
  double* variables;
  uint32_t rateSizeBytes, rateSize;
  void (*ComputeRates)(double* BOUND, double* RATES, double* CONSTANTS,
                       double* VARIABLES);
  void (*ComputeVariables)(double* BOUND, double* RATES, double* CONSTANTS,
                           double* VARIABLES);
};

int
EvaluateRates(double bound, const double vars[],
              double rates[], void* params)
{
  EvaluationInformation* ei = reinterpret_cast<EvaluationInformation*>(params);
  
  // XXX This is very unfortunate. Change CCGS to split arrays?
  if (ei->variables != vars)
    memcpy(ei->variables, vars, ei->rateSizeBytes);

  // Update variables that change based on bound/other vars...
  ei->ComputeVariables(&bound, rates, ei->constants, ei->variables);

  // Compute the rates...
  ei->ComputeRates(&bound, rates, ei->constants, ei->variables);

  // printf("Compute r0=%g at bound %g\n", rates[0], bound);

  return GSL_SUCCESS;
}

int
EvaluateJacobian
(
 double bound, const double vars[],
 double jac[], double rates[], void* params
)
{
  EvaluationInformation* ei = reinterpret_cast<EvaluationInformation*>(params);
  
  // XXX This is very unfortunate. Change CCGS to split arrays?
  memcpy(ei->variables, vars, ei->rateSizeBytes);

  double rate0[ei->rateSize], rate1[ei->rateSize];
  ei->ComputeVariables(&bound, rate0, ei->constants, ei->variables);
  ei->ComputeRates(&bound, rate0, ei->constants, ei->variables);

  uint32_t i, j;
  for (i = 0; i < ei->rateSize; i++)
  {
    double perturb = vars[i] * 1E-10;
    if (perturb == 0)
      perturb = 1E-90;

    ei->variables[i] = vars[i] + perturb;
    ei->ComputeVariables(&bound, rate1, ei->constants, ei->variables);
    ei->ComputeRates(&bound, rate1, ei->constants, ei->variables);

    for (j = 0; j < ei->rateSize; j++)
    {
      jac[i * ei->rateSize + j] = (rate1[j] - rate0[j]) / perturb;
    }

    ei->variables[i] = vars[i];
  }

  // Now perturb the bound and see what happens...
  double perturb = bound * 1E-13;
  if (perturb == 0)
    perturb = 1E-90;
  double newbound = bound + perturb;
  ei->ComputeVariables(&newbound, rates, ei->constants, ei->variables);
  ei->ComputeRates(&newbound, rate1, ei->constants, ei->variables);
  for (i = 0; i < ei->rateSize; i++)
  {
    rates[i] = (rate1[i] - rate0[i]) / perturb;
  }

  return GSL_SUCCESS;
}

// Don't cache more than 1MB of variables...
#define VARIABLE_STORAGE_LIMIT 1048576
// Don't cache for more than 1 second...
#define VARIABLE_TIME_LIMIT 1

void
CDA_CellMLIntegrationRun::SolveODEProblem
(
 CompiledModelFunctions* f, uint32_t constSize, double* constants,
 uint32_t varSize, double* variables, uint32_t rateSize, double* rates
)
{
  gsl_odeiv_system sys;
  EvaluationInformation ei;

  sys.dimension = rateSize;
  sys.params = reinterpret_cast<void*>(&ei);

  sys.function = EvaluateRates;
  sys.jacobian = EvaluateJacobian;
  ei.constants = constants;
  ei.variables = variables;
  ei.rateSizeBytes = rateSize * sizeof(double);
  ei.ComputeRates = f->ComputeRates;
  ei.ComputeVariables = f->ComputeVariables;

  gsl_odeiv_step* s;
  gsl_odeiv_evolve* e = gsl_odeiv_evolve_alloc(rateSize);
  gsl_odeiv_control* c = gsl_odeiv_control_standard_new(mEpsAbs, mEpsRel,
                                                        mScalVar, mScalRate);

  // Allocate a control function...
  const gsl_odeiv_step_type* T = NULL;
  switch (mStepType)
  {
  case iface::cellml_services::RUNGE_KUTTA_2_3:
    T = gsl_odeiv_step_rk2;
    break;
  case iface::cellml_services::RUNGE_KUTTA_4:
    T = gsl_odeiv_step_rk4;
    break;
  case iface::cellml_services::RUNGE_KUTTA_FEHLBERG_4_5:
    T = gsl_odeiv_step_rkf45;
    break;
  case iface::cellml_services::RUNGE_KUTTA_CASH_KARP_4_5:
    T = gsl_odeiv_step_rkck;
    break;
  case iface::cellml_services::RUNGE_KUTTA_PRINCE_DORMAND_8_9:
    T = gsl_odeiv_step_rk8pd;
    break;
  case iface::cellml_services::RUNGE_KUTTA_IMPLICIT_2:
    T = gsl_odeiv_step_rk2imp;
    break;
  case iface::cellml_services::RUNGE_KUTTA_IMPLICIT_2_SOLVE:
    T = gsl_odeiv_step_rk2simp;
    break;
  case iface::cellml_services::RUNGE_KUTTA_IMPLICIT_4:
    T = gsl_odeiv_step_rk4imp;
    break;
  case iface::cellml_services::BULIRSCH_STOER_IMPLICIT_BD:
    T = gsl_odeiv_step_bsimp;
    break;
  case iface::cellml_services::GEAR_1:
    T = gsl_odeiv_step_gear1;
    break;
  case iface::cellml_services::GEAR_2:
    T = gsl_odeiv_step_gear2;
    break;
  }
  s = gsl_odeiv_step_alloc(T, rateSize);
  
  // Start the main loop...
  double boundhigh, bound;
  boundhigh = mStartBvar;
  double stepSize = 1E-6;
  
  uint32_t storageCapacity = VARIABLE_STORAGE_LIMIT / (varSize * sizeof(double));
  double* storage = new double[storageCapacity * varSize * sizeof(double)];
  uint32_t storageExpiry = time(0) + VARIABLE_TIME_LIMIT;
  uint32_t storageSize = 0;

  while (boundhigh < mStopBvar)
  {
    bound = boundhigh;
    boundhigh += mIncrementBvar;
    if (boundhigh >= mStopBvar)
      boundhigh = mStopBvar;

    do
    {
      double bhl = boundhigh;
      if (bhl - bound > mStepSizeMax)
        bhl = bound + mStepSizeMax;
      //printf("Entering step at X=%g, h=%g, v0=%g\n", bound,
      //       stepSize, variables[0]);
      gsl_odeiv_evolve_apply(e, c, s, &sys, &bound, bhl,
                             &stepSize, variables);
      //printf("Evolve applied with X=%g, h=%g, v0=%g\n", bound,
      //       stepSize, variables[0]);
      if (mCancelIntegration)
        break;
    }
    while (bound < boundhigh);

    if (mCancelIntegration)
      break;

    // Add to storage...
    memcpy(storage + storageSize, variables, varSize * sizeof(double));
    f->ComputeVariables(&bound, rates, constants, storage + storageSize);
    storageSize += varSize;
    // Are we ready to send?
    uint32_t timeNow = time(0);
    if (timeNow >= storageExpiry || storageSize == storageCapacity)
    {
      if (mObserver != NULL)
        mObserver->results(storageSize, storage);
      storageExpiry = timeNow + VARIABLE_TIME_LIMIT;
      storageSize = 0;
    }
  }
  if (storageSize != 0 && mObserver != NULL)
  {
    mObserver->results(storageSize, storage);
  }
  if (mObserver != NULL)
    mObserver->done();

  delete [] storage;

  // Free gsl structures...
  gsl_odeiv_evolve_free(e);
  gsl_odeiv_control_free(c);
  gsl_odeiv_step_free(s);
}