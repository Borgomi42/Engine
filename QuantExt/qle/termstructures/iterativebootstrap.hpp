/*
 Copyright (C) 2019 Quaternion Risk Management Ltd
 All rights reserved.

 This file is part of ORE, a free-software/open-source library
 for transparent pricing and risk analysis - http://opensourcerisk.org

 ORE is free software: you can redistribute it and/or modify it
 under the terms of the Modified BSD License.  You should have received a
 copy of the license along with this program.
 The license is also available online at <http://opensourcerisk.org>

 This program is distributed on the basis that it will form a useful
 contribution to risk analytics and model standardisation, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE. See the license for more details.
*/

/*! \file qle/termstructures/iterativebootstrap.hpp
    \brief Straight copy of ql/termstructures/iterativebootstrap.hpp with minor changes
*/

#ifndef quantext_iterative_bootstrap_hpp
#define quantext_iterative_bootstrap_hpp

#include <ql/termstructures/bootstraphelper.hpp>
#include <ql/termstructures/bootstraperror.hpp>
#include <ql/math/interpolations/linearinterpolation.hpp>
#include <ql/math/solvers1d/finitedifferencenewtonsafe.hpp>
#include <ql/math/solvers1d/brent.hpp>
#include <ql/utilities/dataformatters.hpp>

namespace QuantExt {

/*! Straight copy of QuantLib::IterativeBootstrap with the following modifications
    - addition of a \c globalAccuracy parameter to allow the global bootstrap accuracy to be different than the 
      \c accuracy specified in the \c Curve. In particular, allows for the \c globalAccuracy to be greater than the 
      \c accuracy specified in the \c Curve which is useful in some situations e.g. cubic spline and optionlet 
      stripping. If the \c globalAccuracy is set less than the \c accuracy in the \c Curve, the \c accuracy in the 
      \c Curve is used instead.
*/
template <class Curve>
class IterativeBootstrap {
    typedef typename Curve::traits_type Traits;
    typedef typename Curve::interpolator_type Interpolator;
public:
    IterativeBootstrap(QuantLib::Real globalAccuracy = 1e-10);
    void setup(Curve* ts);
    void calculate() const;
private:
    void initialize() const;
    Curve* ts_;
    QuantLib::Size n_;
    QuantLib::Brent firstSolver_;
    QuantLib::FiniteDifferenceNewtonSafe solver_;
    mutable bool initialized_, validCurve_, loopRequired_;
    mutable QuantLib::Size firstAliveHelper_, alive_;
    mutable std::vector<QuantLib::Real> previousData_;
    mutable std::vector<boost::shared_ptr<QuantLib::BootstrapError<Curve> > > errors_;
    QuantLib::Real globalAccuracy_;
};


template <class Curve>
IterativeBootstrap<Curve>::IterativeBootstrap(QuantLib::Real globalAccuracy)
    : ts_(0), initialized_(false), validCurve_(false),
      loopRequired_(Interpolator::global), globalAccuracy_(globalAccuracy) {}

template <class Curve>
void IterativeBootstrap<Curve>::setup(Curve* ts) {
    ts_ = ts;
    n_ = ts_->instruments_.size();
    QL_REQUIRE(n_ > 0, "no bootstrap helpers given")
    for (QuantLib::Size j = 0; j < n_; ++j)
        ts_->registerWith(ts_->instruments_[j]);
}

template <class Curve>
void IterativeBootstrap<Curve>::initialize() const {
    
    // ensure helpers are sorted
    std::sort(ts_->instruments_.begin(), ts_->instruments_.end(), QuantLib::detail::BootstrapHelperSorter());
    
    // skip expired helpers
    QuantLib::Date firstDate = Traits::initialDate(ts_);
    QL_REQUIRE(ts_->instruments_[n_-1]->pillarDate() > firstDate, "all instruments expired");
    firstAliveHelper_ = 0;
    while (ts_->instruments_[firstAliveHelper_]->pillarDate() <= firstDate)
        ++firstAliveHelper_;
    alive_ = n_-firstAliveHelper_;
    QL_REQUIRE(alive_ >= Interpolator::requiredPoints - 1,"not enough alive instruments: " << alive_ <<
        " provided, " << Interpolator::requiredPoints-1 << " required");

    // calculate dates and times, create errors_
    std::vector<QuantLib::Date>& dates = ts_->dates_;
    std::vector<QuantLib::Time>& times = ts_->times_;
    dates.resize(alive_+1);
    times.resize(alive_+1);
    errors_.resize(alive_+1);
    dates[0] = firstDate;
    times[0] = ts_->timeFromReference(dates[0]);

    QuantLib::Date latestRelevantDate, maxDate = firstDate;
    
    // pillar counter: i
    // helper counter: j
    for (QuantLib::Size i = 1, j = firstAliveHelper_; j < n_; ++i, ++j) {
        
        const boost::shared_ptr<typename Traits::helper>& helper = ts_->instruments_[j];
        dates[i] = helper->pillarDate();
        times[i] = ts_->timeFromReference(dates[i]);
        
        // check for duplicated pillars
        QL_REQUIRE(dates[i-1] != dates[i], "more than one instrument with pillar " << dates[i]);

        latestRelevantDate = helper->latestRelevantDate();
        // check that the helper is really extending the curve, i.e. that
        // pillar-sorted helpers are also sorted by latestRelevantDate
        QL_REQUIRE(latestRelevantDate > maxDate,
                    io::ordinal(j+1) << " instrument (pillar: " <<
                    dates[i] << ") has latestRelevantDate (" <<
                    latestRelevantDate << ") before or equal to "
                    "previous instrument's latestRelevantDate (" <<
                    maxDate << ")");
        maxDate = latestRelevantDate;

        // when a pillar date is different from the last relevant date the
        // convergence loop is required even if the Interpolator is local
        if (dates[i] != latestRelevantDate)
            loopRequired_ = true;

        errors_[i] = boost::make_shared<QuantLib::BootstrapError<Curve> >(ts_, helper, i);
    }
    ts_->maxDate_ = maxDate;

    // set initial guess only if the current curve cannot be used as guess
    if (!validCurve_ || ts_->data_.size() != alive_ + 1) {
        // ts_->data_[0] is the only relevant item,
        // but reasonable numbers might be needed for the whole data vector
        // because, e.g., of interpolation's early checks
        ts_->data_ = std::vector<QuantLib::Real>(alive_ + 1, Traits::initialValue(ts_));
        previousData_.resize(alive_+1);
    }
    initialized_ = true;
}

template <class Curve>
void IterativeBootstrap<Curve>::calculate() const {

    // we might have to call initialize even if the curve is initialized
    // and not moving, just because helpers might be date relative and change
    // with evaluation date change.
    // anyway it makes little sense to use date relative helpers with a
    // non-moving curve if the evaluation date changes
    if (!initialized_ || ts_->moving_)
        initialize();

    // setup helpers
    for (QuantLib::Size j = firstAliveHelper_; j < n_; ++j) {
        const boost::shared_ptr<typename Traits::helper>& helper = ts_->instruments_[j];
        
        // check for valid quote
        QL_REQUIRE(helper->quote()->isValid(),
                    io::ordinal(j + 1) << " instrument (maturity: " <<
                    helper->maturityDate() << ", pillar: " <<
                    helper->pillarDate() << ") has an invalid quote");
        
        // don't try this at home!
        // This call creates helpers, and removes "const".
        // There is a significant interaction with observability.
        helper->setTermStructure(const_cast<Curve*>(ts_));
    }

    const std::vector<QuantLib::Time>& times = ts_->times_;
    const std::vector<QuantLib::Real>& data = ts_->data_;
    QuantLib::Real accuracy = ts_->accuracy_;

    QuantLib::Size maxIterations = Traits::maxIterations()-1;

    // there might be a valid curve state to use as guess
    bool validData = validCurve_;

    for (QuantLib::Size iteration = 0; ; ++iteration) {
        previousData_ = ts_->data_;

        for (QuantLib::Size i = 1; i <= alive_; ++i) {

            // bracket root and calculate guess
            QuantLib::Real min = Traits::minValueAfter(i, ts_, validData, firstAliveHelper_);
            QuantLib::Real max = Traits::maxValueAfter(i, ts_, validData, firstAliveHelper_);
            QuantLib::Real guess = Traits::guess(i, ts_, validData, firstAliveHelper_);
            
            // adjust guess if needed
            if (guess>=max)
                guess = max - (max-min)/5.0;
            else if (guess<=min)
                guess = min + (max-min)/5.0;

            // extend interpolation if needed
            if (!validData) {
                try { // extend interpolation a point at a time
                        // including the pillar to be boostrapped
                    ts_->interpolation_ = ts_->interpolator_.interpolate(
                        times.begin(), times.begin() + i + 1, data.begin());
                } catch (...) {
                    if (!Interpolator::global)
                        throw; // no chance to fix it in a later iteration

                    // otherwise use Linear while the target
                    // interpolation is not usable yet
                    ts_->interpolation_ = QuantLib::Linear().interpolate(
                        times.begin(), times.begin() + i + 1, data.begin());
                }
                ts_->interpolation_.update();
            }

            try {
                if (validData)
                    solver_.solve(*errors_[i], accuracy, guess, min, max);
                else
                    firstSolver_.solve(*errors_[i], accuracy,guess,min,max);
            } catch (std::exception &e) {
                if (validCurve_) {
                    // the previous curve state might have been a
                    // bad guess, so we retry without using it.
                    // This would be tricky to do here (we're
                    // inside multiple nested for loops, we need
                    // to re-initialize...), so we invalidate the
                    // curve, make a recursive call and then exit.
                    validCurve_ = initialized_ = false;
                    calculate();
                    return;
                }
                QL_FAIL(QuantLib::io::ordinal(iteration+1) << " iteration: failed "
                        "at " << QuantLib::io::ordinal(i) << " alive instrument, "
                        "pillar " << errors_[i]->helper()->pillarDate() <<
                        ", maturity " << errors_[i]->helper()->maturityDate() <<
                        ", reference date " << ts_->dates_[0] <<
                        ": " << e.what());
            }
        }

        if (!loopRequired_)
                break;

        // exit condition
        QuantLib::Real change = std::fabs(data[1] - previousData_[1]);
        for (QuantLib::Size i = 2; i <= alive_; ++i)
            change = std::max(change, std::fabs(data[i] - previousData_[i]));
        
        if (change <= globalAccuracy_ || change <= accuracy)
            break;

        QL_REQUIRE(iteration < maxIterations,
                    "convergence not reached after " << iteration <<
                    " iterations; last improvement " << change <<
                    ", required accuracy " << accuracy);

        validData = true;
    }

    validCurve_ = true;
}

}

#endif
