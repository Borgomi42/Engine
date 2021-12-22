/*
 Copyright (C) 2020 Quaternion Risk Management Ltd
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

/*! \file ored/utilities/marketdata.hpp
    \brief market data related utilties
    \ingroup utilities
*/

#pragma once

#include <ored/marketdata/market.hpp>
#include <string>

namespace ore {
namespace data {

extern const std::string xccyCurveNamePrefix;

/*! For a given currency code, \p ccyCode, return the internal name for the cross currency based yield curve.

    For a given currency code, \p ccyCode, this function returns `__XCCY__-ccyCode`. This curve, if available, is
    currently used in special cases to allow for separate discount curves when discounting the cashflows on cross
    currency interest rate swaps.
*/
std::string xccyCurveName(const std::string& ccyCode);

/*! Attempt to return a yield curve from the market using the name generated by `xccyCurveName(ccyCode)`. If no yield
    curve is available, return the discount curve for the given currency code, \p ccyCode.
*/
QuantLib::Handle<QuantLib::YieldTermStructure>
xccyYieldCurve(const boost::shared_ptr<Market>& market, const std::string& ccyCode,
               const std::string& configuration = Market::defaultConfiguration);

/*! Attempt to return a yield curve from the market using the name generated by `xccyCurveName(ccyCode)`. If no yield
    curve is available, return the discount curve for the given currency code, \p ccyCode. The parameter
    \p outXccyExists is populated with `true` if a yield curve was found under `xccyCurveName(ccyCode)` and it is
    populated with `false` if there was no such yield curve.
*/
QuantLib::Handle<QuantLib::YieldTermStructure>
xccyYieldCurve(const boost::shared_ptr<Market>& market, const std::string& ccyCode, bool& outXccyExists,
               const std::string& configuration = Market::defaultConfiguration);

/*! For a given security id and credit curve id return the internal name for a security specific
    copy of the credit curve. This is used to separate sensitivities on credit curves by securities. */
std::string securitySpecificCreditCurveName(const std::string& securityId, const std::string& creditCurveId);

/*! Return the credit curve id for a name generated with securitySpecificCreditCurveName(). If the name was not
  generated with securitySpecificCreditCurveName(), return the input name unchanged. */
std::string creditCurveNameFromSecuritySpecificCreditCurveName(const std::string& name);

/*! Attempt to return a security specific default curve using the name generated by 'securitySpecificCreditCurveName()'.
  If no such curve is available return the credit curve for the given creditCurveId. */
QuantLib::Handle<QuantLib::DefaultProbabilityTermStructure>
securitySpecificCreditCurve(const boost::shared_ptr<Market>& market, const std::string& securityId,
                            const std::string& creditCurveId,
                            const std::string& configuration = Market::defaultConfiguration);

/*! Pretty print an internal curve name occuring (once or several times) in a string (e.g. in a risk factor name). */
std::string prettyPrintInternalCurveName(std::string name);

/*! Build an Fx Index given a market */
boost::shared_ptr<QuantExt::FxIndex> buildFxIndex(const string& fxIndex, const string& domestic, const string& foreign,
                                                  const boost::shared_ptr<Market>& market, const string& configuration,
                                                  bool useXbsCurves = false);

/*! Get a Xccy curve from  given a market */
Handle<YieldTermStructure> xccyYieldCurve(const boost::shared_ptr<Market>& market, const string& ccyCode,
                                          const string& configuration);

void getFxIndexConventions(const string& index, Natural& fixingDays, Calendar& fixingCalendar);

} // namespace data
} // namespace ore
