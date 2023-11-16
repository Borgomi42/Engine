/*
 Copyright (C) 2023 Quaternion Risk Management Ltd
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

#include <orea/engine/xvaenginecg.hpp>

#include <ored/scripting/engines/scriptedinstrumentpricingenginecg.hpp>
#include <ored/utilities/to_string.hpp>

#include <qle/ad/backwardderivatives.hpp>
#include <qle/ad/forwardevaluation.hpp>
#include <qle/ad/ssaform.hpp>
#include <qle/methods/multipathvariategenerator.hpp>

#include <boost/timer/timer.hpp>

namespace ore {
namespace analytics {

XvaEngineCG::XvaEngineCG(const Size nThreads, const Date& asof, const boost::shared_ptr<ore::data::Loader>& loader,
                         const boost::shared_ptr<ore::data::CurveConfigurations>& curveConfigs,
                         const boost::shared_ptr<ore::data::TodaysMarketParameters>& todaysMarketParams,
                         const boost::shared_ptr<ore::analytics::ScenarioSimMarketParameters>& simMarketData,
                         const boost::shared_ptr<ore::data::EngineData>& engineData,
                         const boost::shared_ptr<ore::analytics::CrossAssetModelData>& crossAssetModelData,
                         const boost::shared_ptr<ore::analytics::ScenarioGeneratorData>& scenarioGeneratorData,
                         const boost::shared_ptr<ore::data::Portfolio>& portfolio, const string& marketConfiguration,
                         const string& marketConfigurationInCcy,
                         const boost::shared_ptr<ore::analytics::SensitivityScenarioData>& sensitivityData,
                         const boost::shared_ptr<ReferenceDataManager>& referenceData,
                         const IborFallbackConfig& iborFallbackConfig, const bool continueOnCalibrationError,
                         const bool continueOnError, const std::string& context)
    : nThreads_(nThreads), asof_(asof), loader_(loader), curveConfigs_(curveConfigs),
      todaysMarketParams_(todaysMarketParams), simMarketData_(simMarketData), engineData_(engineData),
      crossAssetModelData_(crossAssetModelData), scenarioGeneratorData_(scenarioGeneratorData), portfolio_(portfolio),
      marketConfiguration_(marketConfiguration), marketConfigurationInCcy_(marketConfigurationInCcy),
      sensitivityData_(sensitivityData), referenceData_(referenceData), iborFallbackConfig_(iborFallbackConfig),
      continueOnCalibrationError_(continueOnCalibrationError), continueOnError_(continueOnError), context_(context) {

    // for testing, duplicate trade in porfolio for performance testing
    portfolio_ = boost::make_shared<Portfolio>();
    std::string pfxml = portfolio->toXMLString();
    for (Size i = 0; i < atoi(getenv("N")); ++i) {
        auto p = boost::make_shared<Portfolio>();
        p->fromXMLString(pfxml);
        for (auto const& [id, t] : p->trades()) {
            t->id() += "_" + std::to_string(i + 1);
            portfolio_->add(t);
        }
    }
    // end portfolio duplication

    boost::timer::cpu_timer timer;

    LOG("XvaEngineCG started");

    // 1 build T0 market

    DLOG("XvaEngineCG: build init market");

    initMarket_ = boost::make_shared<ore::data::TodaysMarket>(asof_, todaysMarketParams_, loader_, curveConfigs_,
                                                              continueOnError_, true, true, referenceData_, false,
                                                              iborFallbackConfig_, false, true);

    boost::timer::nanosecond_type timing1 = timer.elapsed().wall;

    // 2 build sim market

    DLOG("XvaEngineCG: build sim market");

    // note: take use spreaded term structures from sensitivity config?
    simMarket_ = boost::make_shared<ore::analytics::ScenarioSimMarket>(
        initMarket_, simMarketData_, marketConfiguration_, *curveConfigs_, *todaysMarketParams_, continueOnError_, true,
        false, false, iborFallbackConfig_, true);

    boost::timer::nanosecond_type timing2 = timer.elapsed().wall;

    // 3 set up cam builder against sim market

    DLOG("XvaEngineCG: build cam model builder");

    // note: sim market has one config only, no in-ccy config to calibrate IR components
    camBuilder_ = boost::make_shared<CrossAssetModelBuilder>(
        simMarket_, crossAssetModelData_, marketConfigurationInCcy_, marketConfiguration_, marketConfiguration_,
        marketConfiguration_, marketConfiguration_, marketConfiguration_, false, continueOnCalibrationError_,
        std::string(), SalvagingAlgorithm::Spectral, "xva engine cg - cam builder");

    // 4 set up gaussian cam cg model

    DLOG("XvaEngineCG: build cam cg model");

    QL_REQUIRE(
        crossAssetModelData_->discretization() == CrossAssetModel::Discretization::Euler,
        "XvaEngineCG: cam is required to use discretization 'Euler', please update simulation parameters accordingly.");

    std::vector<std::string> currencies;                                                   // from cam
    std::vector<Handle<YieldTermStructure>> curves;                                        // from cam
    std::vector<Handle<Quote>> fxSpots;                                                    // from cam
    std::vector<std::pair<std::string, boost::shared_ptr<InterestRateIndex>>> irIndices;   // from trade building
    std::vector<std::pair<std::string, boost::shared_ptr<ZeroInflationIndex>>> infIndices; // from trade building
    std::vector<std::string> indices;                                                      // from trade building
    std::vector<std::string> indexCurrencies;                                              // from trade building

    // 4b for the PoC we populate the containers with hardcoded values ... TEMPORARY HACK ...

    currencies.push_back("EUR");
    curves.push_back(camBuilder_->model()->irModel(0)->termStructure());
    irIndices.push_back(std::make_pair("EUR-EURIBOR-6M", *initMarket_->iborIndex("EUR-EURIBOR-6M")));

    // note: these must be fine enough for Euler, e.g. weekly over the whole simulation period
    std::set<Date> simulationDates(scenarioGeneratorData_->getGrid()->dates().begin(),
                                   scenarioGeneratorData_->getGrid()->dates().end());
    // note: this should be added to CrossAssetModelData
    Size timeStepsPerYear = 1;

    // note: projectedStateProcessIndices can be removed from GaussianCamCG constructor most probably
    model_ = boost::make_shared<GaussianCamCG>(camBuilder_->model(), scenarioGeneratorData_->samples(), currencies,
                                               curves, fxSpots, irIndices, infIndices, indices, indexCurrencies,
                                               simulationDates, timeStepsPerYear, iborFallbackConfig,
                                               std::vector<Size>(), std::vector<std::string>(), true);
    model_->calculate();
    auto g = model_->computationGraph();

    DLOG("Built computation graph for model, size is " << g->size());
    TLOGGERSTREAM(ssaForm(*g, getRandomVariableOpLabels()));

    boost::timer::nanosecond_type timing3 = timer.elapsed().wall;

    // 4c build trades with global cg cam model

    DLOG("XvaEngineCG: build trades using global cam cg model");

    auto edCopy = boost::make_shared<EngineData>(*engineData_);
    edCopy->globalParameters()["GenerateAdditionalResults"] = "false";
    edCopy->globalParameters()["RunType"] = "NPV";
    map<MarketContext, string> configurations;
    configurations[MarketContext::irCalibration] = marketConfigurationInCcy_;
    configurations[MarketContext::fxCalibration] = marketConfiguration_;
    configurations[MarketContext::pricing] = marketConfiguration_;
    auto factory =
        boost::make_shared<EngineFactory>(edCopy, simMarket_, configurations, referenceData_, iborFallbackConfig_,
                                          EngineBuilderFactory::instance().generateAmcCgEngineBuilders(
                                              model_, scenarioGeneratorData_->getGrid()->dates()),
                                          true);

    portfolio_->build(factory, "xva engine cg", true);

    boost::timer::nanosecond_type timing4 = timer.elapsed().wall;

    // 5 add to computation graph for all trades and store npv, amc npv nodes, node range for each trade

    DLOG("XvaEngineCG: add to computation graph for all trades");

    std::map<std::string, std::vector<std::size_t>> amcNpvNodes; // including time zero npv
    std::map<std::string, std::pair<std::size_t, std::size_t>> tradeNodeRanges;

    for (auto const& [id, trade] : portfolio_->trades()) {
        auto qlInstr = boost::dynamic_pointer_cast<ScriptedInstrument>(trade->instrument()->qlInstrument());
        QL_REQUIRE(qlInstr, "XvaEngineCG: expeced trade to provide ScriptedInstrument, trade '" << id << "' does not.");
        auto engine = boost::dynamic_pointer_cast<ScriptedInstrumentPricingEngineCG>(qlInstr->pricingEngine());
        QL_REQUIRE(engine, "XvaEngineCG: expected to get ScriptedInstrumentPricingEngineCG, trade '"
                               << id << "' has a different engine.");
        g->setPrefix(id + "_");
        std::size_t firstNode = g->size();
        engine->buildComputationGraph();
        std::size_t lastNode = g->size();
        tradeNodeRanges[id] = std::make_pair(firstNode, lastNode);
        std::vector<std::size_t> tmp;
        tmp.push_back(g->variable(engine->npvName() + "_0"));
        for (std::size_t i = 0; i < simulationDates.size(); ++i) {
            tmp.push_back(g->variable("_AMC_NPV_" + std::to_string(i)));
        }
        amcNpvNodes[id] = tmp;
    }

    DLOG("Extended computation graph for trades, size is " << g->size());
    TLOGGERSTREAM(ssaForm(*g, getRandomVariableOpLabels()));

    boost::timer::nanosecond_type timing5 = timer.elapsed().wall;

    // 6 populate random variates

    DLOG("XvaEngineCG: populate random variates");

    std::vector<RandomVariable> values(g->size(), RandomVariable(model_->size(), 0.0));

    auto const& rv = model_->randomVariates();
    if (!rv.empty()) {
        auto gen = makeMultiPathVariateGenerator(scenarioGeneratorData_->sequenceType(), rv.size(), rv.front().size(),
                                                 scenarioGeneratorData_->seed(), scenarioGeneratorData_->ordering(),
                                                 scenarioGeneratorData_->directionIntegers());
        for (Size path = 0; path < model_->size(); ++path) {
            auto p = gen->next();
            for (Size j = 0; j < rv.front().size(); ++j) {
                for (Size k = 0; k < rv.size(); ++k) {
                    values[rv[k][j]].set(path, p.value[j][k]);
                }
            }
        }
        DLOG("generated rvs for " << rv.size() << " underlyings and " << rv.front().size() << " time steps.");
    }

    boost::timer::nanosecond_type timing6 = timer.elapsed().wall;

    // 7 populate constants and model parameters

    DLOG("XvaEngineCG: populate constants and model parameters");

    for (auto const& c : g->constants()) {
        values[c.second] = RandomVariable(model_->size(), c.first);
    }

    baseModelParams_ = model_->modelParameters();
    for (auto const& p : baseModelParams_) {
        values[p.first] = RandomVariable(model_->size(), p.second);
    }

    DLOG("set " << g->constants().size() << " constants and " << baseModelParams_.size() << " model parameters.");

    boost::timer::nanosecond_type timing7 = timer.elapsed().wall;

    // 8 do forward evaluation for all trades, keep npv and amc npv nodes

    DLOG("XvaEngineCG: run forward evaluation");

    std::vector<bool> keepNodes(g->size(), false);
    for (auto const& [_, amcNpvNode] : amcNpvNodes) {
        for (auto const& n : amcNpvNode)
            keepNodes[n] = true;
    }

    // note. regression order and polynom type should ultimately come from st pe config or xva analytics config (?)
    ops_ = getRandomVariableOps(model_->size(), 4, QuantLib::LsmBasisSystem::Monomial);
    grads_ = getRandomVariableGradients(model_->size(), 4, QuantLib::LsmBasisSystem::Monomial);
    opNodeRequirements_ = getRandomVariableOpNodeRequirements();

    forwardEvaluation(*g, values, ops_, RandomVariable::deleter, false, opNodeRequirements_, keepNodes);

    DLOG("ran forward evaluation.");

    boost::timer::nanosecond_type timing8 = timer.elapsed().wall;

    // 8b dump epe profile out

    // for (auto const& [id, npvs] : amcNpvNodes) {
    //     for (Size i = 0; i < simulationDates.size() + 1; ++i) {
    //         std::cout << id << ","
    //                   << ore::data::to_string(i == 0 ? model_->referenceDate()
    //                                                  : *std::next(simulationDates.begin(), i - 1))
    //                   << "," << expectation(values[npvs[i]]) << ","
    //                   << expectation(max(values[npvs[i]], RandomVariable(model_->size(), 0.0))) << ","
    //                   << expectation(max(-values[npvs[i]], RandomVariable(model_->size(), 0.0))) << "\n";
    //     }
    // }
    // std::cout << std::flush;

    // 9 build the postprocessing computation graph

    // 10 do forward evaluation on postprocessing graph

    // 11 do backward derivatives run on postprocessing graph

    // 12 for all trades, do single forward evaluation runs and roll back derivatives from postprocessing graph

    std::cout << "Computation graph size:   " << g->size() << std::endl;
    std::cout << "Peak mem usage:           " << ore::data::os::getPeakMemoryUsageBytes() / 1024 / 1024 << " MB"
              << std::endl;
    std::cout << "T0 market build:          " << timing1 / 1E6 << " ms" << std::endl;
    std::cout << "Sim market build:         " << (timing2 - timing1) / 1E6 << " ms" << std::endl;
    std::cout << "Model CG build:           " << (timing3 - timing2) / 1E6 << " ms" << std::endl;
    std::cout << "Portfolio build:          " << (timing4 - timing3) / 1E6 << " ms" << std::endl;
    std::cout << "Trade CG build:           " << (timing5 - timing4) / 1E6 << " ms" << std::endl;
    std::cout << "RV gen:                   " << (timing6 - timing5) / 1E6 << " ms" << std::endl;
    std::cout << "model params / const set  " << (timing7 - timing6) / 1E6 << " ms" << std::endl;
    std::cout << "forward eval              " << (timing8 - timing7) / 1E6 << " ms" << std::endl;
    std::cout << "total                     " << timing8 / 1E6 << " ms" << std::endl;
}

} // namespace analytics
} // namespace ore
