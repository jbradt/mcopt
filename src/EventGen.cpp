#include "EventGen.h"

namespace mcopt
{
    arma::mat calibrate(const Track& tr, const arma::vec& vd, const double clock)
    {
        arma::mat pos = tr.getPositionMatrix();
        return calibrate(pos, vd, clock);
    }

    arma::mat calibrate(const arma::mat& pos, const arma::vec& vd, const double clock)
    {
        // Assume pos has units of meters, vd in cm/us, clock in Hz.
        arma::mat result = pos + pos.col(2) * -vd.t() / (clock * 1e-4);
        result.col(2) -= pos.col(2);

        return result;
    }

    arma::mat uncalibrate(const Track& tr, const arma::vec& vd, const double clock, const int offset)
    {
        arma::mat pos = tr.getPositionMatrix();
        return uncalibrate(pos, vd, clock, offset);
    }

    arma::mat uncalibrate(const arma::mat& pos, const arma::vec& vd, const double clock, const int offset)
    {
        // Assume tr has units of meters, vd in cm/us, clock in Hz.
        arma::vec tbs = pos.col(2) * clock * 1e-4 / (-vd(2)) + offset;

        arma::mat result = pos - tbs * -vd.t() / (clock * 1e-4);
        result.col(2) = tbs;

        return result;
    }

    arma::mat unTiltAndRecenter(const arma::mat& pos, const double tilt)
    {
        arma::mat tmat {{1, 0, 0},
                        {0, cos(-tilt), -sin(-tilt)},
                        {0, sin(-tilt), cos(-tilt)}};

        arma::mat res = (tmat * pos.t()).t();
        res.col(1) -= tan(tilt);  // Offset after rotation is tan(tilt) * (1.0 m) b/c we rotated about micromegas
        return res;
    }

    arma::vec squareWave(const arma::uword size, const arma::uword leftEdge,
                         const arma::uword width, const double height)
    {
        arma::vec res (size, arma::fill::zeros);
        for (arma::uword i = leftEdge; i < leftEdge + width && i < size; i++) {
            res(i) = height;
        }
        return res;
    }

    static inline double approxSin(const double t)
    {
        return t - t*t*t / 6 + t*t*t*t*t / 120 - t*t*t*t*t*t*t / 5040;
    }

    arma::vec elecPulse(const double amplitude, const double shape, const double clock, const double offset)
    {
        arma::vec res (512, arma::fill::zeros);

        double s = shape * clock;  // IMPORTANT: shape and clock must have compatible units, e.g. MHz and us.

        arma::uword firstPt = static_cast<arma::uword>(std::ceil(offset));
        for (arma::uword i = firstPt; i < res.n_rows; i++) {
            double t = (i - offset) / s;
            res(i) = amplitude * std::exp(-3*t) * approxSin(t) * t*t*t / 0.044;
        }
        return res;
    }

    arma::vec EventGenerator::numElec(const arma::vec& en) const
    {
        const arma::uword numPts = en.n_elem;
        arma::vec result (numPts, arma::fill::zeros);
        result(arma::span(1, numPts-1)) = arma::floor(-arma::diff(en * 1e6 * massNum) / ioniz);  // diff returns N-1 points
        return result;
    }

    arma::mat EventGenerator::diffuseElectrons(const arma::mat& tr) const
    {
        const double diffSigmaDiag = diffSigma * std::sqrt(2);
        const arma::mat diffPts {{diffSigma, 0},                     // East
                                 {-diffSigma, 0},                    // West
                                 {0, diffSigma},                     // North
                                 {0, -diffSigma},                    // South
                                 {diffSigmaDiag, diffSigmaDiag},     // Northeast
                                 {diffSigmaDiag, -diffSigmaDiag},    // Southeast
                                 {-diffSigmaDiag, diffSigmaDiag},    // Northwest
                                 {-diffSigmaDiag, -diffSigmaDiag}};  // Southwest

        const arma::uword numDiffPts = diffPts.n_rows;  // Number of diffusion points in addition to the original one
        const arma::uword numPts = tr.n_rows;

        const double centerAmpl = 0.4;
        const double diffAmpl = (1 - centerAmpl) / numDiffPts;

        arma::mat result (numPts * (numDiffPts + 1), tr.n_cols);

        result.rows(0, numPts-1) = tr;
        result(arma::span(0, numPts-1), 3) *= centerAmpl;

        for (arma::uword ptIdx = 0; ptIdx < numDiffPts; ptIdx++) {
            const arma::uword firstRow = numPts * (ptIdx+1);

            for (arma::uword i = 0; i < numPts; i++) {
                const arma::uword thisRow = firstRow + i;
                result(thisRow, arma::span(0, 1)) = tr(i, arma::span(0, 1)) + diffPts.row(ptIdx) * std::sqrt(tr(i, 2));
                result(thisRow, 2) = tr(i, 2);
                result(thisRow, 3) = tr(i, 3) * diffAmpl;
            }
        }

        return result;
    }

    arma::mat EventGenerator::prepareTrack(const arma::mat& pos, const arma::vec& en) const
    {
        // This creates a matrix with columns (x, y, z, numElec)

        const arma::uword nrows = pos.n_rows;
        const arma::uword ncols = 4;
        assert(en.n_rows == nrows);

        arma::mat uncalPts (nrows, ncols);

        arma::mat posTilted = unTiltAndRecenter(pos, tilt);
        uncalPts.cols(0, 2) = uncalibrate(posTilted, vd, clock);
        uncalPts.col(3) = numElec(en);

        arma::mat result = diffuseElectrons(uncalPts);

        return result;
    }

    inline double EventGenerator::conversionFactor() const
    {
        /* This factor converts number of primary electrons to signal amplitude in ADC bins.
         * The micromegas gain gets us secondary electrons, Q_e gets charge, and the electronics gain
         * gets voltage after the preamp. The full range of the preamp is 1 V, so assume 1 V corresponds to
         * saturation (or 4096 ADC bins).
         */
        return micromegasGain * Constants::E_CHG / electronicsGain * 4096;
    }

    std::map<pad_t, arma::vec> EventGenerator::makeEvent(const Track& tr) const
    {
        arma::mat trMat = tr.getMatrix();
        arma::mat pos = trMat.cols(0, 2);
        arma::vec en = trMat.col(4);
        return makeEvent(pos, en);
    }

    std::map<pad_t, arma::vec> EventGenerator::makeEvent(const arma::mat& pos, const arma::vec& en) const
    {
        arma::mat uncal = prepareTrack(pos, en);  // has columns (x, y, z, numElec)

        std::map<pad_t, arma::vec> result;

        const double convFactor = conversionFactor();

        for (arma::uword i = 0; i < uncal.n_rows - 1; i++) {
            pad_t pad = pads->getPadNumberFromCoordinates(uncal(i, 0), uncal(i, 1));
            if (pad != 20000) {
                arma::vec& padSignal = result[pad];
                if (padSignal.is_empty()) {
                    // This means that the signal was just default-constructed by std::map::operator[]
                    padSignal.zeros(512);
                }
                double offset = uncal(i, 2);
                // if (offset > 511) throw TBOverflow(std::to_string(offset));
                if (offset > 511) continue;

                arma::vec pulse = elecPulse(convFactor * uncal(i, 3), shape, clock, offset);
                padSignal += pulse;
            }
        }

        return result;
    }

    std::map<pad_t, Peak> EventGenerator::makePeaksFromSimulation(const Track& tr) const
    {
        std::map<pad_t, arma::vec> evt = makeEvent(tr);

        std::map<pad_t, Peak> res;
        for (const auto& pair : evt) {
            arma::uword maxTB;
            unsigned maxVal = static_cast<unsigned>(std::floor(pair.second.max(maxTB)));  // This stores the location of the max in its argument
            res.emplace(pair.first, Peak{static_cast<unsigned>(maxTB), maxVal});
        }
        return res;
    }

    arma::mat EventGenerator::makePeaksTableFromSimulation(const Track& tr) const
    {
        arma::mat pos = tr.getPositionMatrix();
        arma::vec en = tr.getEnergyVector();
        return makePeaksTableFromSimulation(pos, en);
    }

    arma::mat EventGenerator::makePeaksTableFromSimulation(const arma::mat& pos, const arma::vec& en) const
    {
        std::map<pad_t, arma::vec> evt = makeEvent(pos, en);

        std::vector<arma::rowvec> rows;
        for (const auto& pair : evt) {
            const auto& padNum = pair.first;
            const auto& sig = pair.second;

            // Find center of gravity of the peak
            arma::uvec pkPts = arma::find(sig > 0.3*sig.max());
            arma::vec pkVals = sig.elem(pkPts);
            double total = arma::sum(pkVals);
            if (total < 1e-3) {
                continue;
            }
            double pkCtrGrav = arma::dot(pkPts, pkVals) / arma::sum(pkVals);

            double maxVal = sig.max();
            auto xy = pads->getPadCenter(padNum);
            rows.push_back(arma::rowvec{xy.at(0), xy.at(1), pkCtrGrav, maxVal,
                                        static_cast<double>(padNum)});
        }

        arma::mat result (rows.size(), 5);
        for (size_t i = 0; i < rows.size(); i++) {
            result.row(i) = rows.at(i);
        }

        return result;
    }

    arma::vec EventGenerator::makeMeshSignal(const Track& tr) const
    {
        const arma::mat pos = tr.getPositionMatrix();
        const arma::vec en = tr.getEnergyVector();
        return makeMeshSignal(pos, en);
    }

    arma::vec EventGenerator::makeMeshSignal(const arma::mat& pos, const arma::vec& en) const
    {
        std::map<pad_t, arma::vec> evt = makeEvent(pos, en);
        arma::vec res (512, arma::fill::zeros);

        for (const auto& pair : evt) {
            res += pair.second;
        }

        return res;
    }

    arma::vec EventGenerator::makeHitPattern(const arma::mat& pos, const arma::vec& en) const
    {
        arma::mat uncal = prepareTrack(pos, en);

        arma::vec hits (10240, arma::fill::zeros);

        const double convFactor = conversionFactor();

        for (arma::uword i = 0; i < uncal.n_rows - 1; i++) {
            pad_t pad = pads->getPadNumberFromCoordinates(uncal(i, 0), uncal(i, 1));
            if (pad != 20000) {
                hits(pad) += convFactor * uncal(i, 3);
            }
        }

        return hits;
    }
}
