/*
 Copyright (C) 2016 Quaternion Risk Management Ltd
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

#include <ored/model/crossassetmodeldata.hpp>
#include <ored/model/inflation/infdkdata.hpp>
#include <ored/model/inflation/infjydata.hpp>
#include <ored/utilities/correlationmatrix.hpp>
#include <ored/utilities/log.hpp>
#include <ored/utilities/parsers.hpp>
#include <ored/utilities/to_string.hpp>

#include <qle/models/fxbsconstantparametrization.hpp>
#include <qle/models/fxbspiecewiseconstantparametrization.hpp>
#include <qle/models/irlgm1fconstantparametrization.hpp>
#include <qle/models/irlgm1fpiecewiseconstanthullwhiteadaptor.hpp>
#include <qle/models/irlgm1fpiecewiseconstantparametrization.hpp>
#include <qle/models/irlgm1fpiecewiselinearparametrization.hpp>

#include <ql/math/optimization/levenbergmarquardt.hpp>
#include <ql/models/shortrate/calibrationhelpers/swaptionhelper.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/utilities/dataformatters.hpp>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/lexical_cast.hpp>


namespace {

using ore::data::CorrelationFactor;
using ore::data::parseCorrelationFactor;
using ore::data::parseInteger;
using ore::data::XMLNode;
using ore::data::XMLUtils;
using std::string;

CorrelationFactor fromNode(ore::data::XMLNode* node, bool firstFactor) {

    string factorTag = firstFactor ? "factor1" : "factor2";
    string idxTag = firstFactor ? "index1" : "index2";

    CorrelationFactor factor = parseCorrelationFactor(XMLUtils::getAttribute(node, factorTag));
    string strIdx = XMLUtils::getAttribute(node, idxTag);
    if (strIdx != "") {
        factor.index = parseInteger(strIdx);
    }

    return factor;
}

}

namespace ore {
namespace data {

void InstantaneousCorrelations::fromXML(XMLNode* node) {
    // Configure correlation structure
    LOG("CrossAssetModelData: adding correlations.");
    XMLNode* correlationNode = XMLUtils::locateNode(node, "InstantaneousCorrelations");
    CorrelationMatrixBuilder cmb;
    if (correlationNode) {
        vector<XMLNode*> nodes = XMLUtils::getChildrenNodes(correlationNode, "Correlation");
        for (Size i = 0; i < nodes.size(); ++i) {
            CorrelationFactor factor_1 = fromNode(nodes[i], true);
            CorrelationFactor factor_2 = fromNode(nodes[i], false);
            Real corr = parseReal(XMLUtils::getNodeValue(nodes[i]));
            cmb.addCorrelation(factor_1, factor_2, corr);
        }
    }
    else {
        QL_FAIL("No InstantaneousCorrelations found in model configuration XML");
    }

    correlations_ = cmb.correlations();
}

XMLNode* InstantaneousCorrelations::toXML(XMLDocument& doc) {
    
    XMLNode* instantaneousCorrelationsNode = doc.allocNode("InstantaneousCorrelations");

    for (auto it = correlations_.begin(); it != correlations_.end(); it++) {

        XMLNode* node = doc.allocNode("Correlation", to_string(it->second->value()));
        XMLUtils::appendNode(instantaneousCorrelationsNode, node);

        CorrelationFactor f_1 = it->first.first;
        XMLUtils::addAttribute(doc, node, "factor1", to_string(f_1.type) + ":" + f_1.name);
        if(f_1.index != Null<Size>())
            XMLUtils::addAttribute(doc, node, "index1", to_string(f_1.index));

        CorrelationFactor f_2 = it->first.second;
        XMLUtils::addAttribute(doc, node, "factor2", to_string(f_2.type) + ":" + f_2.name);
        if (f_2.index != Null<Size>())
            XMLUtils::addAttribute(doc, node, "index2", to_string(f_2.index));
    }

    return instantaneousCorrelationsNode;
}

bool InstantaneousCorrelations::operator==(const InstantaneousCorrelations& rhs) {
    // compare correlations by value (not the handle links)
    map<CorrelationKey, Handle<Quote>>::const_iterator corr1, corr2;
    for (corr1 = correlations_.begin(), corr2 = rhs.correlations_.begin();
        corr1 != correlations_.end() && corr2 != rhs.correlations_.end(); ++corr1, ++corr2) {
        if (corr1->first != corr2->first || !close_enough(corr1->second->value(), corr2->second->value()))
            return false;
    }
    if (corr1 != correlations_.end() || corr2 != rhs.correlations_.end())
        return false;

    return true;
}

bool InstantaneousCorrelations::operator!=(const InstantaneousCorrelations& rhs) {
    return !(*this == rhs);
}

bool CrossAssetModelData::operator==(const CrossAssetModelData& rhs) {

    if (*correlations_ != *rhs.correlations_)
        return false;

    if (domesticCurrency_ != rhs.domesticCurrency_ || currencies_ != rhs.currencies_ || equities_ != rhs.equities_ ||
        infindices_ != rhs.infindices_ || bootstrapTolerance_ != rhs.bootstrapTolerance_ ||
        irConfigs_.size() != rhs.irConfigs_.size() || fxConfigs_.size() != rhs.fxConfigs_.size() ||
        eqConfigs_.size() != rhs.eqConfigs_.size() || infConfigs_.size() != rhs.infConfigs_.size() ||
        crLgmConfigs_.size() != rhs.crLgmConfigs_.size() || crCirConfigs_.size() != rhs.crCirConfigs_.size()) {
        return false;
    }

    for (Size i = 0; i < irConfigs_.size(); i++) {
        if (*irConfigs_[i] != *(rhs.irConfigs_[i])) {
            return false;
        }
    }

    for (Size i = 0; i < fxConfigs_.size(); i++) {
        if (*fxConfigs_[i] != *(rhs.fxConfigs_[i])) {
            return false;
        }
    }

    for (Size i = 0; i < eqConfigs_.size(); i++) {
        if (*eqConfigs_[i] != *(rhs.eqConfigs_[i])) {
            return false;
        }
    }

    // Not checking inflation model data for equality. The equality operators were only written to support 
    // unit testing toXML and fromXML. Questionable if it should be done this way.

    for (Size i = 0; i < crLgmConfigs_.size(); i++) {
        if (*crLgmConfigs_[i] != *(rhs.crLgmConfigs_[i])) {
            return false;
        }
    }

    for (Size i = 0; i < crCirConfigs_.size(); i++) {
        if (*crCirConfigs_[i] != *(rhs.crCirConfigs_[i])) {
            return false;
        }
    }

    return true;
}

bool CrossAssetModelData::operator!=(const CrossAssetModelData& rhs) { return !(*this == rhs); }

void CrossAssetModelData::clear() {
    currencies_.clear();
    equities_.clear();
    irConfigs_.clear();
    fxConfigs_.clear();
    eqConfigs_.clear();
    infConfigs_.clear();
    crLgmConfigs_.clear();
    crCirConfigs_.clear();
    correlations_->clear();
}

void CrossAssetModelData::validate() {
    QL_REQUIRE(irConfigs_.size() > 0, "no IR data provided");
    QL_REQUIRE(fxConfigs_.size() == irConfigs_.size() - 1, "inconsistent number of FX data provided");
    for (Size i = 0; i < fxConfigs_.size(); ++i)
        QL_REQUIRE(fxConfigs_[i]->foreignCcy() == irConfigs_[i + 1]->ccy(),
                   "currency mismatch betwee IR and FX config vectors");
}

std::vector<std::string> pairToStrings(std::pair<std::string, std::string> p) {
    std::vector<std::string> pair = {p.first, p.second};
    return pair;
}

void CrossAssetModelData::fromXML(XMLNode* root) {
    clear();

    XMLNode* sim = XMLUtils::locateNode(root, "Simulation");
    XMLNode* modelNode = XMLUtils::getChildNode(sim, "CrossAssetModel");
    XMLUtils::checkNode(modelNode, "CrossAssetModel");

    domesticCurrency_ = XMLUtils::getChildValue(modelNode, "DomesticCcy", true); // mandatory
    LOG("CrossAssetModelData: domesticCcy " << domesticCurrency_);

    currencies_ = XMLUtils::getChildrenValues(modelNode, "Currencies", "Currency", true);
    for (auto ccy : currencies_) {
        LOG("CrossAssetModelData: ccy " << ccy);
    }

    equities_ = XMLUtils::getChildrenValues(modelNode, "Equities", "Equity");
    for (auto eq : equities_) {
        LOG("CrossAssetModelData equity " << eq);
    }

    infindices_ = XMLUtils::getChildrenValues(modelNode, "InflationIndices", "InflationIndex");
    for (auto inf : infindices_) {
        LOG("CrossAssetModelData inflation index " << inf);
    }

    creditNames_ = XMLUtils::getChildrenValues(modelNode, "CreditNames", "CreditName");
    for (auto cr : creditNames_) {
        LOG("CrossAssetModelData credit name " << cr);
    }

    bootstrapTolerance_ = XMLUtils::getChildValueAsDouble(modelNode, "BootstrapTolerance", true);
    LOG("CrossAssetModelData: bootstrap tolerance = " << bootstrapTolerance_);

    // Configure IR model components

    std::map<std::string, boost::shared_ptr<IrLgmData>> irDataMap;
    XMLNode* irNode = XMLUtils::getChildNode(modelNode, "InterestRateModels");
    if (irNode) {
        for (XMLNode* child = XMLUtils::getChildNode(irNode, "LGM"); child;
             child = XMLUtils::getNextSibling(child, "LGM")) {

            boost::shared_ptr<IrLgmData> config(new IrLgmData());
            config->fromXML(child);

            for (Size i = 0; i < config->optionExpiries().size(); i++) {
                LOG("LGM calibration swaption " << config->optionExpiries()[i] << " x " << config->optionTerms()[i]
                                                << " " << config->optionStrikes()[i]);
            }

            irDataMap[config->ccy()] = config;

            LOG("CrossAssetModelData: IR config built for key " << config->ccy());

        } // end of  for (XMLNode* child = XMLUtils::getChildNode(irNode, "LGM"); child;
    }     // end of if (irNode)
    else {
        LOG("No IR model section found");
    }

    buildIrConfigs(irDataMap);

    for (Size i = 0; i < irConfigs_.size(); i++)
        LOG("CrossAssetModelData: IR config currency " << i << " = " << irConfigs_[i]->ccy());

    // Configure FX model components

    std::map<std::string, boost::shared_ptr<FxBsData>> fxDataMap;
    XMLNode* fxNode = XMLUtils::getChildNode(modelNode, "ForeignExchangeModels");
    if (fxNode) {
        for (XMLNode* child = XMLUtils::getChildNode(fxNode, "CrossCcyLGM"); child;
             child = XMLUtils::getNextSibling(child, "CrossCcyLGM")) {

            boost::shared_ptr<FxBsData> config(new FxBsData());
            config->fromXML(child);

            for (Size i = 0; i < config->optionExpiries().size(); i++) {
                LOG("CC-LGM calibration option " << config->optionExpiries()[i] << " " << config->optionStrikes()[i]);
            }

            fxDataMap[config->foreignCcy()] = config;

            LOG("CrossAssetModelData: FX config built with key (foreign ccy) " << config->foreignCcy());
        }
    } else {
        LOG("No FX Models section found");
    }

    buildFxConfigs(fxDataMap);

    for (Size i = 0; i < fxConfigs_.size(); i++)
        LOG("CrossAssetModelData: FX config currency " << i << " = " << fxConfigs_[i]->foreignCcy());

    // Configure EQ model components

    std::map<std::string, boost::shared_ptr<EqBsData>> eqDataMap;
    XMLNode* eqNode = XMLUtils::getChildNode(modelNode, "EquityModels");
    if (eqNode) {
        for (XMLNode* child = XMLUtils::getChildNode(eqNode, "CrossAssetLGM"); child;
             child = XMLUtils::getNextSibling(child, "CrossAssetLGM")) {

            boost::shared_ptr<EqBsData> config(new EqBsData());
            config->fromXML(child);

            for (Size i = 0; i < config->optionExpiries().size(); i++) {
                LOG("Cross-Asset Equity calibration option " << config->optionExpiries()[i] << " "
                                                             << config->optionStrikes()[i]);
            }

            eqDataMap[config->eqName()] = config;

            LOG("CrossAssetModelData: Equity config built with key " << config->eqName());
        }
    } else {
        LOG("No Equity Models section found");
    }

    buildEqConfigs(eqDataMap);

    for (Size i = 0; i < eqConfigs_.size(); i++)
        LOG("CrossAssetModelData: EQ config name " << i << " = " << eqConfigs_[i]->eqName());

    // Read the inflation model data.
    if (XMLNode* n = XMLUtils::getChildNode(modelNode, "InflationIndexModels")) {
        
        map<string, boost::shared_ptr<InflationModelData>> mp;
        
        // Loop over nodes and pick out any with name: LGM, DodgsonKainth or JarrowYildirim.
        for (XMLNode* cn = XMLUtils::getChildNode(n); cn; cn = XMLUtils::getNextSibling(cn)) {

            boost::shared_ptr<InflationModelData> imData;

            string nodeName = XMLUtils::getNodeName(cn);
            if (nodeName == "LGM" || nodeName == "DodgsonKainth") {
                imData = boost::make_shared<InfDkData>();
            } else if (nodeName == "JarrowYildirim") {
                imData = boost::make_shared<InfJyData>();
            } else {
                WLOG("Did not recognise InflationIndexModels node with name " << nodeName <<
                    " as a valid inflation index model so skipping it.");
                continue;
            }

            const string& indexName = imData->index();
            imData->fromXML(cn);
            mp[indexName] = imData;

            LOG("CrossAssetModelData: inflation index model data built with key " << indexName);
        }
        
        // Align the inflation model data with the infindices_ read in above and handle defaults
        buildInfConfigs(mp);

        // Log the index number and inflation index name for each inflation index model.
        for (Size i = 0; i < infConfigs_.size(); i++)
            LOG("CrossAssetModelData: INF config name " << i << " = " << infConfigs_[i]->index());

    } else {
        LOG("No InflationIndexModels node found so no inflation models configured.");
    }

    // Configure CR model components

    std::map<std::string, boost::shared_ptr<CrLgmData>> crLgmDataMap;
    std::map<std::string, boost::shared_ptr<CrCirData>> crCirDataMap;
    XMLNode* crNode = XMLUtils::getChildNode(modelNode, "CreditModels");
    if (crNode) {
        for (XMLNode* child = XMLUtils::getChildNode(crNode, "LGM"); child;
             child = XMLUtils::getNextSibling(child, "LGM")) {

            boost::shared_ptr<CrLgmData> config(new CrLgmData());
            config->fromXML(child);

            for (Size i = 0; i < config->optionExpiries().size(); i++) {
                LOG("LGM calibration cds option " << config->optionExpiries()[i] << " x " << config->optionTerms()[i]
                                                  << " " << config->optionStrikes()[i]);
            }

            crLgmDataMap[config->name()] = config;

            LOG("CrossAssetModelData: CR LGM config built for key " << config->name());

        } // end of  for (XMLNode* child = XMLUtils::getChildNode(irNode, "LGM"); child;
        for (XMLNode* child = XMLUtils::getChildNode(crNode, "CIR"); child;
             child = XMLUtils::getNextSibling(child, "CIR")) {

            boost::shared_ptr<CrCirData> config(new CrCirData());
            config->fromXML(child);

            for (Size i = 0; i < config->optionExpiries().size(); i++) {
                LOG("CIR calibration cds option " << config->optionExpiries()[i] << " x " << config->optionTerms()[i]
                                                  << " " << config->optionStrikes()[i]);
            }

            crCirDataMap[config->name()] = config;

            LOG("CrossAssetModelData: CR CIR config built for key " << config->name());

        } // end of  for (XMLNode* child = XMLUtils::getChildNode(irNode, "CIR"); child;
    }     // end of if (irNode)
    else {
        LOG("No CR model section found");
    }

    buildCrConfigs(crLgmDataMap, crCirDataMap);

    for (Size i = 0; i < crLgmConfigs_.size(); i++)
        LOG("CrossAssetModelData: CR LGM config name " << i << " = " << crLgmConfigs_[i]->name());
    for (Size i = 0; i < crCirConfigs_.size(); i++)
        LOG("CrossAssetModelData: CR CIR config name " << i << " = " << crCirConfigs_[i]->name());

    // Configure correlation structure
    LOG("CrossAssetModelData: adding correlations.");
    correlations_ = boost::make_shared<InstantaneousCorrelations>();
    correlations_->fromXML(modelNode);

    validate();

    LOG("CrossAssetModelData loading from XML done");
}

void CrossAssetModelData::buildIrConfigs(std::map<std::string, boost::shared_ptr<IrLgmData>>& irDataMap) {
    // Append IR configurations into the irConfigs vector in the order of the currencies
    // in the currencies vector.
    // If there is an IR configuration for any of the currencies missing, then we will
    // look up the configuration with key "default" and use this instead. If this is
    // not provided either we will throw an exception.
    irConfigs_.resize(currencies_.size());
    for (Size i = 0; i < currencies_.size(); i++) {
        string ccy = currencies_[i];
        if (irDataMap.find(ccy) != irDataMap.end())
            irConfigs_[i] = irDataMap[ccy];
        else { // copy from default
            LOG("IR configuration missing for currency " << ccy << ", using default");
            if (irDataMap.find("default") == irDataMap.end()) {
                ALOG("Both default IR and " << ccy << " IR configuration missing");
                QL_FAIL("Both default IR and " << ccy << " IR configuration missing");
            }
            boost::shared_ptr<IrLgmData> def = irDataMap["default"];
            irConfigs_[i] = boost::make_shared<IrLgmData>(
                ccy, // overwrite this and keep the others
                def->calibrationType(), def->reversionType(), def->volatilityType(), def->calibrateH(),
                def->hParamType(), def->hTimes(), def->hValues(), def->calibrateA(), def->aParamType(), def->aTimes(),
                def->aValues(), def->shiftHorizon(), def->scaling(), def->optionExpiries(), def->optionTerms(),
                def->optionStrikes());
        }
        LOG("CrossAssetModelData: IR config added for ccy " << ccy << " " << irConfigs_[i]->ccy());
    }
}

void CrossAssetModelData::buildFxConfigs(std::map<std::string, boost::shared_ptr<FxBsData>>& fxDataMap) {
    // Append FX configurations into the fxConfigs vector in the order of the foreign
    // currencies in the currencies vector.
    // If there is an FX configuration for any of the foreign currencies missing,
    // then we will look up the configuration with key "default" and use this instead.
    // If this is not provided either we will throw an exception.
    for (Size i = 0; i < currencies_.size(); i++) {
        string ccy = currencies_[i];
        if (ccy == domesticCurrency_)
            continue;
        if (fxDataMap.find(ccy) != fxDataMap.end())
            fxConfigs_.push_back(fxDataMap[ccy]);
        else { // copy from default
            LOG("FX configuration missing for foreign currency " << ccy << ", using default");
            if (fxDataMap.find("default") == fxDataMap.end()) {
                ALOG("Both default FX and " << ccy << " FX configuration missing");
                QL_FAIL("Both default FX and " << ccy << " FX configuration missing");
            }
            boost::shared_ptr<FxBsData> def = fxDataMap["default"];
            boost::shared_ptr<FxBsData> fxData = boost::make_shared<FxBsData>(
                ccy, def->domesticCcy(), def->calibrationType(), def->calibrateSigma(), def->sigmaParamType(),
                def->sigmaTimes(), def->sigmaValues(), def->optionExpiries(), def->optionStrikes());

            fxConfigs_.push_back(fxData);
        }
        LOG("CrossAssetModelData: FX config added for foreign ccy " << ccy);
    }
}

void CrossAssetModelData::buildEqConfigs(std::map<std::string, boost::shared_ptr<EqBsData>>& eqDataMap) {
    // Append Eq configurations into the eqConfigs vector in the order of the equity
    // names in the equities) vector.
    // If there is an Eq configuration for any of the names missing,
    // then we will look up the configuration with key "default" and use this instead.
    // If this is not provided either we will throw an exception.
    for (Size i = 0; i < equities_.size(); i++) {
        string name = equities_[i];
        if (eqDataMap.find(name) != eqDataMap.end())
            eqConfigs_.push_back(eqDataMap[name]);
        else { // copy from default
            LOG("Equity configuration missing for name " << name << ", using default");
            if (eqDataMap.find("default") == eqDataMap.end()) {
                ALOG("Both default EQ and " << name << " EQ configuration missing");
                QL_FAIL("Both default EQ and " << name << " EQ configuration missing");
            }
            boost::shared_ptr<EqBsData> def = eqDataMap["default"];
            boost::shared_ptr<EqBsData> eqData = boost::make_shared<EqBsData>(
                name, def->currency(), def->calibrationType(), def->calibrateSigma(), def->sigmaParamType(),
                def->sigmaTimes(), def->sigmaValues(), def->optionExpiries(), def->optionStrikes());

            eqConfigs_.push_back(eqData);
        }
        LOG("CrossAssetModelData: EQ config added for name " << name);
    }
}

void CrossAssetModelData::buildInfConfigs(const map<string, boost::shared_ptr<InflationModelData>>& mp) {
    
    // Append inflation model data to the infConfigs_ vector in the order of the inflation indices in the infindices_ 
    // vector.

    // If for any of the inflation indices in the infindices_ vector, there is no inflation model data in mp then the 
    // default inflation model data is used. The default inflation model data should be in mp under the key name 
    // "default". If it is not provided either, an exception is thrown.

    for (const string& indexName : infindices_) {
        
        auto it = mp.find(indexName);
        if (it != mp.end()) {
            infConfigs_.push_back(it->second);
        } else {
            
            LOG("Inflation index model data missing for index " << indexName << " so attempt to use default");

            auto itDefault = mp.find("default");
            QL_REQUIRE(itDefault != mp.end(), "Inflation index model data missing for index " <<
                indexName << " and for default.");

            // Make a copy of the model data and add to vector.
            boost::shared_ptr<InflationModelData> imData = itDefault->second;
            if (auto dk = boost::dynamic_pointer_cast<InfDkData>(imData)) {
                infConfigs_.push_back(boost::make_shared<InfDkData>(*dk));
            } else if (auto jy = boost::dynamic_pointer_cast<InfJyData>(imData)) {
                infConfigs_.push_back(boost::make_shared<InfJyData>(*jy));
            } else {
                QL_FAIL("Expected inflation model data to be DK or JY.");
            }
        }

        LOG("CrossAssetModelData: INF config added for name " << indexName);
    }
}

void CrossAssetModelData::buildCrConfigs(std::map<std::string, boost::shared_ptr<CrLgmData>>& crLgmDataMap,
                                         std::map<std::string, boost::shared_ptr<CrCirData>>& crCirDataMap) {
    // Append IR configurations into the irConfigs vector in the order of the currencies
    // in the currencies vector.
    // If there is an IR configuration for any of the currencies missing, then we will
    // look up the configuration with key "default" and use this instead. If this is
    // not provided either we will throw an exception.
    crLgmConfigs_.clear();
    crCirConfigs_.clear();

    for (Size i = 0; i < creditNames_.size(); i++) {
        string name = creditNames_[i];
        if (crLgmDataMap.find(name) != crLgmDataMap.end()) {
            QL_REQUIRE(crCirDataMap.find(name) == crCirDataMap.end(), "");
            crLgmConfigs_.push_back(crLgmDataMap[name]);
        }
        else if (crCirDataMap.find(name) != crCirDataMap.end()) {
            crCirConfigs_.push_back(crCirDataMap[name]);
        }
        else { // copy from LGM default, CIR default is not used
            LOG("CR configuration missing for name " << name << ", using default");
            if (crLgmDataMap.find("default") == crLgmDataMap.end()) {
                ALOG("Both default CR LGM and " << name << " CR configuration missing");
                QL_FAIL("Both default CR and " << name << " CR configuration missing");
            }
            boost::shared_ptr<CrLgmData> def = crLgmDataMap["default"];
            crLgmConfigs_.push_back(boost::make_shared<CrLgmData>(
                name, // overwrite this and keep the others
                def->calibrationType(), def->reversionType(), def->volatilityType(), def->calibrateH(),
                def->hParamType(), def->hTimes(), def->hValues(), def->calibrateA(), def->aParamType(), def->aTimes(),
                def->aValues(), def->shiftHorizon(), def->scaling(), def->optionExpiries(), def->optionTerms(),
                def->optionStrikes()));
        }
        LOG("CrossAssetModelData: CR config added for name " << name << " " << name);
    }
}

XMLNode* CrossAssetModelData::toXML(XMLDocument& doc) {

    XMLNode* crossAssetModelNode = doc.allocNode("CrossAssetModel");

    XMLUtils::addChild(doc, crossAssetModelNode, "DomesticCcy", domesticCurrency_);
    XMLUtils::addChildren(doc, crossAssetModelNode, "Currencies", "Currency", currencies_);
    XMLUtils::addChildren(doc, crossAssetModelNode, "Equities", "Equity", equities_);
    XMLUtils::addChildren(doc, crossAssetModelNode, "InflationIndices", "InflationIndex", infindices_);
    XMLUtils::addChildren(doc, crossAssetModelNode, "CreditNames", "CreditName", creditNames_);
    XMLUtils::addChild(doc, crossAssetModelNode, "BootstrapTolerance", bootstrapTolerance_);

    XMLNode* interestRateModelsNode = XMLUtils::addChild(doc, crossAssetModelNode, "InterestRateModels");
    for (Size irConfigs_Iterator = 0; irConfigs_Iterator < irConfigs_.size(); irConfigs_Iterator++) {
        XMLNode* lgmNode = irConfigs_[irConfigs_Iterator]->toXML(doc);
        XMLUtils::appendNode(interestRateModelsNode, lgmNode);
    }

    XMLNode* foreignExchangeModelsNode = XMLUtils::addChild(doc, crossAssetModelNode, "ForeignExchangeModels");
    for (Size fxConfigs_Iterator = 0; fxConfigs_Iterator < fxConfigs_.size(); fxConfigs_Iterator++) {
        XMLNode* crossCcyLgmNode = fxConfigs_[fxConfigs_Iterator]->toXML(doc);
        XMLUtils::appendNode(foreignExchangeModelsNode, crossCcyLgmNode);
    }

    XMLNode* eqModelsNode = XMLUtils::addChild(doc, crossAssetModelNode, "EquityModels");
    for (Size eqConfigs_Iterator = 0; eqConfigs_Iterator < eqConfigs_.size(); eqConfigs_Iterator++) {
        XMLNode* crossAssetEqNode = eqConfigs_[eqConfigs_Iterator]->toXML(doc);
        XMLUtils::appendNode(eqModelsNode, crossAssetEqNode);
    }

    XMLNode* infModelsNode = XMLUtils::addChild(doc, crossAssetModelNode, "InflationIndexModels");
    for (Size infConfigs_Iterator = 0; infConfigs_Iterator < infConfigs_.size(); infConfigs_Iterator++) {
        XMLNode* crossAssetInfNode = infConfigs_[infConfigs_Iterator]->toXML(doc);
        XMLUtils::appendNode(infModelsNode, crossAssetInfNode);
    }

    XMLNode* crModelsNode = XMLUtils::addChild(doc, crossAssetModelNode, "CreditModels");
    for (Size crLgmConfigs_Iterator = 0; crLgmConfigs_Iterator < crLgmConfigs_.size(); crLgmConfigs_Iterator++) {
        XMLNode* crossAssetCrLgmNode = crLgmConfigs_[crLgmConfigs_Iterator]->toXML(doc);
        XMLUtils::appendNode(crModelsNode, crossAssetCrLgmNode);
    }
    for (Size crCirConfigs_Iterator = 0; crCirConfigs_Iterator < crCirConfigs_.size(); crCirConfigs_Iterator++) {
        XMLNode* crossAssetCrCirNode = crCirConfigs_[crCirConfigs_Iterator]->toXML(doc);
        XMLUtils::appendNode(crModelsNode, crossAssetCrCirNode);
    }

    XMLNode* instantaneousCorrelationsNode = correlations_->toXML(doc);
    XMLUtils::appendNode(crossAssetModelNode, instantaneousCorrelationsNode);

    return crossAssetModelNode;
}
} // namespace data
} // namespace ore
