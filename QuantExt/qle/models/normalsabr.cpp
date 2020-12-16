/*
 Copyright (C) 2017 Quaternion Risk Management Ltd
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

#include <qle/models/normalsabr.hpp>

#include <ql/errors.hpp>
#include <ql/math/comparison.hpp>

namespace QuantExt {

Real normalSabrVolatility(Rate strike, Rate forward, Time expiryTime, Real alpha, Real nu, Real rho) {

    // update extreme parameters

    alpha = std::max(alpha, 1E-5);
    if (rho < -1 + 1E-5)
        rho = -1 + 1E-5;
    else if (rho > 1 - 1E-5)
        rho = 1 - 1E-5;

    // calculate result

    Real zeta = nu / alpha * (forward - strike);
    Real x = std::log((std::sqrt(1.0 - 2.0 * rho * zeta + zeta * zeta) - rho + zeta) / (1.0 - rho));
    Real f = close_enough(x, 0.0) ? 1.0 : zeta / x;
    Real vol = alpha * f * (1.0 + expiryTime * (2.0 - 3.0 * rho * rho) * nu * nu / 24.0);
    QL_REQUIRE(std::isfinite(vol), "normalSabrVolatility: computed invalid vol for strike="
                                       << strike << ", forward=" << forward << ", expiryTime=" << expiryTime
                                       << ", alpha=" << alpha << ", nu=" << nu << ", rho=" << rho);
    return std::max(vol, 0.00001);
}

Real normalSabrAlphaFromAtmVol(Rate forward, Time expiryTime, Real atmVol, Real nu, Real rho) {
    return std::max(atmVol / (1.0 + expiryTime * (2.0 - 3.0 * rho * rho) * nu * nu / 24.0), 0.00001);
}

namespace {

Real deltaR(const Real t, const Real s) { return std::exp(t / 8.0) - (3072.0 + t * (384.0 * t * (24.0 + t))) / 3072.0; }

Real gfct(const Real s) { return s / std::tanh(s) - 1.0; }

Real R(const Real t, const Real s) {
    Real s2 = s * s;
    Real s4 = s2 * s2;
    Real s6 = s2 * s4;
    Real t2 = t * t;
    Real t3 = t2 * t;
    Real g = gfct(s);
    Real g2 = g * g;
    Real g3 = g2 * g;
    return 1.0 + 3.0 * t * g / (8.0 * s2) - (5.0 * t2 * (-8.0 * s2 + 3.0 * g2 + 24.0 * g)) / (128.0 * s4) +
           (35.0 * t3 * (-40.0 * s2 + 3.0 * g3 + 24.0 * g2 + 120.0 * g)) / (1024.0 * s6);
}

Real G(const Real t, const Real s) {
    std::sqrt(std::sinh(s) / s) * std::exp(-s * s / (2.0 * t) - t / 8.0) * (R(t, s) + deltaR(t, s));
}

} // namespace

Real normalFreeBoundarySabrVolatility(Rate strike, Rate forward, Time expiryTime, Real alpha, Real nu, Real rho);

} // namespace QuantExt
