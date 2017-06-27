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

/*! \file scenario/sensitivityscenariodata.hpp
    \brief A class to hold the parametrisation for building sensitivity scenarios
    \ingroup scenario
*/

#pragma once

#include <ored/utilities/parsers.hpp>
#include <ored/utilities/xmlutils.hpp>
#include <qle/termstructures/dynamicstype.hpp>

using QuantLib::Period;
using QuantLib::Rate;
using std::vector;
using std::string;
using std::pair;
using ore::data::XMLSerializable;
using ore::data::XMLDocument;
using ore::data::XMLNode;
using ore::data::XMLUtils;

namespace ore {
namespace analytics {

//! Description of sensitivity shift scenarios
/*! \ingroup scenario
 */
class SensitivityScenarioData : public XMLSerializable {
public:
    enum class ShiftType { Absolute, Relative };

    struct CurveShiftData {
        string shiftType;
        Real shiftSize;
        vector<Period> shiftTenors;
        vector<string> parInstruments;
        bool parInstrumentSingleCurve;
        map<string, string> parInstrumentConventions;
    };

    struct CapFloorVolShiftData {
        string shiftType;
        Real shiftSize;
        vector<Period> shiftExpiries;
        vector<Real> shiftStrikes; // absolute
        string indexName;
    };

    struct SwaptionVolShiftData {
        string shiftType;
        Real shiftSize;
        vector<Period> shiftExpiries;
        vector<Period> shiftTerms;
        vector<Real> shiftStrikes; // FIXME: absolute or relative to ATM ?
        string indexName;
    };

    struct VolShiftData {
        string shiftType;
        Real shiftSize;
        vector<Period> shiftExpiries;
        vector<Real> shiftStrikes; // FIXME: absolute or relative to ATM ?
    };

    struct SpotShiftData {
        string shiftType;
        Real shiftSize;
    };

    struct CdsVolShiftData {
        string ccy;
        string shiftType;
        Real shiftSize;
        vector<Period> shiftExpiries;
    };

    struct BaseCorrelationShiftData {
        string indexName;
        string shiftType;
        Real shiftSize;
        vector<Period> shiftTerms;
        vector<Real> shiftLossLevels;
    };

    //! Default constructor
    SensitivityScenarioData() : parConversion_(false){};

    //! \name Inspectors
    //@{
    bool parConversion() const { return parConversion_; }

    const vector<string>& discountCurrencies() const { return discountCurrencies_; }
    const map<string, CurveShiftData>& discountCurveShiftData() const { return discountCurveShiftData_; }

    const vector<string>& indexNames() const { return indexNames_; }
    const map<string, CurveShiftData>& indexCurveShiftData() const { return indexCurveShiftData_; }

    const vector<string>& yieldCurveNames() const { return yieldCurveNames_; }
    const map<string, CurveShiftData>& yieldCurveShiftData() const { return yieldCurveShiftData_; }

    const vector<string>& fxCcyPairs() const { return fxCcyPairs_; }
    const map<string, SpotShiftData>& fxShiftData() const { return fxShiftData_; }

    const vector<string>& swaptionVolCurrencies() const { return swaptionVolCurrencies_; }
    const map<string, SwaptionVolShiftData>& swaptionVolShiftData() const { return swaptionVolShiftData_; }

    const vector<string>& capFloorVolCurrencies() const { return capFloorVolCurrencies_; }
    const map<string, CapFloorVolShiftData>& capFloorVolShiftData() const { return capFloorVolShiftData_; }

    const vector<string>& fxVolCcyPairs() const { return fxVolCcyPairs_; }
    const map<string, VolShiftData>& fxVolShiftData() const { return fxVolShiftData_; }

    const vector<string>& cdsVolNames() const { return cdsVolNames_; }
    const map<string, CdsVolShiftData>& cdsVolShiftData() const { return cdsVolShiftData_; }

    const vector<string>& baseCorrelationNames() const { return baseCorrelationNames_; }
    const map<string, BaseCorrelationShiftData>& baseCorrelationShiftData() const { return baseCorrelationShiftData_; }

    // todo
    // const vector<string>& infIndices() const { return inflationIndices_; }
    // const map<string, InflationCurveShiftData> inflationCurveShiftData() const { return inflationCurveShiftData_; }

    // todo
    const vector<string>& creditNames() const { return creditNames_; }
    const map<string, string>& creditCcys() const { return creditCcys_; }
    const map<string, CurveShiftData>& creditCurveShiftData() const { return creditCurveShiftData_; }

    const vector<string>& equityNames() const { return equityNames_; }
    const map<string, SpotShiftData>& equityShiftData() const { return equityShiftData_; }

    const vector<string>& dividendYieldNames() const { return dividendYieldNames_; }
    const map<string, CurveShiftData>& dividendYieldShiftData() const { return dividendYieldShiftData_; }

    const vector<string>& equityVolNames() const { return equityVolNames_; }
    const map<string, VolShiftData>& equityVolShiftData() const { return equityVolShiftData_; }

    const vector<pair<string, string>>& crossGammaFilter() const { return crossGammaFilter_; }

    //@}

    //! \name Setters
    //@{
    bool& parConversion() { return parConversion_; }

    vector<string>& discountCurrencies() { return discountCurrencies_; }
    map<string, CurveShiftData>& discountCurveShiftData() { return discountCurveShiftData_; }

    vector<string>& indexNames() { return indexNames_; }
    map<string, CurveShiftData>& indexCurveShiftData() { return indexCurveShiftData_; }

    vector<string>& yieldCurveNames() { return yieldCurveNames_; }
    map<string, CurveShiftData>& yieldCurveShiftData() { return yieldCurveShiftData_; }

    vector<string>& fxCcyPairs() { return fxCcyPairs_; }
    map<string, SpotShiftData>& fxShiftData() { return fxShiftData_; }

    vector<string>& swaptionVolCurrencies() { return swaptionVolCurrencies_; }
    map<string, SwaptionVolShiftData>& swaptionVolShiftData() { return swaptionVolShiftData_; }

    vector<string>& capFloorVolCurrencies() { return capFloorVolCurrencies_; }
    map<string, CapFloorVolShiftData>& capFloorVolShiftData() { return capFloorVolShiftData_; }

    vector<string>& fxVolCcyPairs() { return fxVolCcyPairs_; }
    map<string, VolShiftData>& fxVolShiftData() { return fxVolShiftData_; }

    vector<string>& cdsVolNames() { return cdsVolNames_; }
    map<string, CdsVolShiftData>& cdsVolShiftData() { return cdsVolShiftData_; }

    vector<string>& baseCorrelationNames() { return baseCorrelationNames_; }
    map<string, BaseCorrelationShiftData>& baseCorrelationShiftData() { return baseCorrelationShiftData_; }

    // todo
    // vector<string>& infIndices() { return inflationIndices_; }
    // map<string, InflationCurveShiftData> inflationCurveShiftData() { return inflationCurveShiftData_; }

    // todo
    vector<string>& creditNames() { return creditNames_; }
    map<string, string>& creditCcys() { return creditCcys_; }
    map<string, CurveShiftData>& creditCurveShiftData() { return creditCurveShiftData_; }

    vector<string>& equityNames() { return equityNames_; }
    map<string, SpotShiftData>& equityShiftData() { return equityShiftData_; }

    vector<string>& dividendYieldNames() { return dividendYieldNames_; }
    map<string, CurveShiftData>& dividendYieldShiftData() { return dividendYieldShiftData_; }

    vector<string>& equityVolNames() { return equityVolNames_; }
    map<string, VolShiftData>& equityVolShiftData() { return equityVolShiftData_; }

    vector<pair<string, string>>& crossGammaFilter() { return crossGammaFilter_; }

    //@}

    //! \name Serialisation
    //@{
    virtual void fromXML(XMLNode* node);
    virtual XMLNode* toXML(XMLDocument& doc);
    //@}

    //! \name Equality Operators
    //@{
    // bool operator==(const SensitivityScenarioData& rhs);
    // bool operator!=(const SensitivityScenarioData& rhs);
    //@}

    //! Utilities
    //@{
    string getIndexCurrency(string indexName);
    //@}

private:
    bool parConversion_;

    vector<string> discountCurrencies_;
    map<string, CurveShiftData> discountCurveShiftData_; // key: ccy

    vector<string> indexNames_;
    map<string, CurveShiftData> indexCurveShiftData_; // key: indexName

    vector<string> yieldCurveNames_;
    map<string, CurveShiftData> yieldCurveShiftData_; // key: yieldCurveName

    vector<string> capFloorVolCurrencies_;
    map<string, CapFloorVolShiftData> capFloorVolShiftData_; // key: ccy

    vector<string> swaptionVolCurrencies_;
    map<string, SwaptionVolShiftData> swaptionVolShiftData_; // key: ccy

    vector<string> fxVolCcyPairs_;
    map<string, VolShiftData> fxVolShiftData_; // key: ccy pair

    vector<string> fxCcyPairs_;
    map<string, SpotShiftData> fxShiftData_; // key: ccy pair

    vector<string> cdsVolNames_;
    map<string, CdsVolShiftData> cdsVolShiftData_; // key: ccy pair

    // todo
    vector<string> inflationIndices_;
    map<string, CurveShiftData> inflationCurveShiftData_; // key: inflation index name

    vector<string> creditNames_;
    map<string, string> creditCcys_;
    map<string, CurveShiftData> creditCurveShiftData_; // key: credit name

    vector<string> equityVolNames_;
    map<string, VolShiftData> equityVolShiftData_;

    vector<string> equityNames_;
    map<string, SpotShiftData> equityShiftData_;

    vector<string> dividendYieldNames_;
    map<string, CurveShiftData> dividendYieldShiftData_; // key: equity name

    vector<string> baseCorrelationNames_;
    map<string, BaseCorrelationShiftData> baseCorrelationShiftData_;

    vector<pair<string, string>> crossGammaFilter_;
};
} // namespace analytics
} // namespace ore
