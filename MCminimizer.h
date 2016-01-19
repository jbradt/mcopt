#ifndef MCOPT_MCMINIMIZER_H
#define MCOPT_MCMINIMIZER_H

#include <armadillo>
#include <vector>
#include <tuple>
#include "Tracker.h"
#include "Track.h"
#include "EventGen.h"

namespace mcopt
{
    arma::vec dropNaNs(const arma::vec& data);

    typedef std::tuple<arma::vec, arma::mat, arma::vec, arma::vec> MCminimizeResult;

    class MCminimizer
    {
    public:
        MCminimizer(const Tracker& tracker)
            : tracker(tracker) {}

        static arma::mat makeParams(const arma::vec& ctr, const arma::vec& sigma, const unsigned numSets,
                                    const arma::vec& mins, const arma::vec& maxes);
        static arma::mat findDeviations(const arma::mat& simtrack, const arma::mat& expdata);
        arma::mat prepareSimulatedTrackMatrix(const arma::mat& simtrack) const;
        double runTrack(const arma::vec& p, const arma::mat& trueValues) const;
        MCminimizeResult minimize(const arma::vec& ctr0, const arma::vec& sigma0, const arma::mat& trueValues,
                                  const unsigned numIters, const unsigned numPts, const double redFactor) const;

    private:
        Tracker tracker;
    };
}

#endif /* end of include guard: MCOPT_MCMINIMIZER_H */
