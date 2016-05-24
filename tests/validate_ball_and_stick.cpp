#include <fstream>

#include "gtest.h"
#include "util.hpp"

#include <cell.hpp>
#include <fvm.hpp>

#include <json/src/json.hpp>

// compares results with those generated by nrn/soma.py
TEST(ball_and_stick, neuron_baseline)
{
    using namespace nest::mc;
    using namespace nlohmann;

    nest::mc::cell cell;

    // setup global state for the mechanisms
    nest::mc::mechanisms::setup_mechanism_helpers();

    // Soma with diameter 12.6157 um and HH channel
    auto soma = cell.add_soma(12.6157/2.0);
    soma->add_mechanism(hh_parameters());

    // add dendrite of length 200 um and diameter 1 um with passive channel
    auto dendrite = cell.add_cable(0, segmentKind::dendrite, 0.5, 0.5, 200);
    dendrite->add_mechanism(pas_parameters());

    dendrite->mechanism("membrane").set("r_L", 100); // no effect for single compartment cell

    // add stimulus
    cell.add_stimulus({1,1}, {5., 80., 0.3});

    // load data from file
    auto cell_data = testing::load_spike_data("../nrn/ball_and_stick.json");
    EXPECT_TRUE(cell_data.size()>0);
    if(cell_data.size()==0) return;

    json& nrn =
        *std::max_element(
            cell_data.begin(), cell_data.end(),
            [](json& lhs, json& rhs) {return lhs["nseg"]<rhs["nseg"];}
        );

    auto& measurements = nrn["measurements"];

    double dt = nrn["dt"];
    double tfinal =   100.; // ms
    int nt = tfinal/dt;

    // inline type for storing the results of a simulation along with
    // the information required to compare two simulations for accuracy
    struct result {
        std::vector<std::vector<double>> spikes;
        std::vector<std::vector<double>> baseline_spikes;
        std::vector<testing::spike_comparison> comparisons;
        std::vector<double> thresh;
        int n_comparments;

        result(int nseg, double dt,
          std::vector<std::vector<double>> &v,
          json& measurements
        ) {
            n_comparments = nseg;
            baseline_spikes = {
                measurements["soma"]["spikes"],
                measurements["dend"]["spikes"],
                measurements["clamp"]["spikes"]
            };
            thresh = {
                measurements["soma"]["thresh"],
                measurements["dend"]["thresh"],
                measurements["clamp"]["thresh"]
            };
            for(auto i=0; i<3; ++i) {
                // calculate the NEST MC spike times
                spikes.push_back
                    (testing::find_spikes(v[i], thresh[i], dt));
                // compare NEST MC and baseline NEURON spike times
                comparisons.push_back
                    (testing::compare_spikes(spikes[i], baseline_spikes[i]));
            }
        }
    };

    std::vector<result> results;
    for(auto run_index=0u; run_index<cell_data.size(); ++run_index) {
        auto& run = cell_data[run_index];
        int num_compartments = run["nseg"];
        dendrite->set_compartments(num_compartments);
        std::vector<std::vector<double>> v(3);

        // make the lowered finite volume cell
        fvm::fvm_cell<double, int> model(cell);
        auto graph = cell.model();

        // set initial conditions
        using memory::all;
        model.voltage()(all) = -65.;
        model.initialize(); // have to do this _after_ initial conditions are set

        // run the simulation
        auto soma_comp = nest::mc::find_compartment_index({0,0}, graph);
        auto dend_comp = nest::mc::find_compartment_index({1,0.5}, graph);
        auto clamp_comp = nest::mc::find_compartment_index({1,1.0}, graph);
        v[0].push_back(model.voltage()[soma_comp]);
        v[1].push_back(model.voltage()[dend_comp]);
        v[2].push_back(model.voltage()[clamp_comp]);
        for(auto i=0; i<nt; ++i) {
            model.advance(dt);
            // save voltage at soma
            v[0].push_back(model.voltage()[soma_comp]);
            v[1].push_back(model.voltage()[dend_comp]);
            v[2].push_back(model.voltage()[clamp_comp]);
        }

        results.push_back( {run["nseg"], dt, v, measurements} );
    }

    // print results
    for(auto& r : results){
        // select the location with the largest error for printing
        auto m =
            std::max_element(
                r.comparisons.begin(), r.comparisons.end(),
                [](testing::spike_comparison& l, testing::spike_comparison& r)
                {return l.max_relative_error()<r.max_relative_error();}
            );
        std::cout << std::setw(5) << r.n_comparments
                  << " compartments : " << *m
                  << "\n";
    }

    // sort results in ascending order of compartments
    std::sort(
        results.begin(), results.end(),
        [](const result& l, const result& r)
            {return l.n_comparments<r.n_comparments;}
    );

    // the strategy for testing is the following:
    //  1. test that the solution converges to the finest reference solution as
    //     the number of compartments increases (i.e. spatial resolution is
    //     refined)
    for(auto i=1u; i<results.size(); ++i) {
        for(auto j=0; j<3; ++j) {
            EXPECT_TRUE(
                  results[i].comparisons[j].max_relative_error()
                < results[i-1].comparisons[j].max_relative_error()
            );
        }
    }

    //  2. test that the best solution (i.e. with most compartments) matches the
    //     reference solution closely (less than 0.1% over the course of 100ms
    //     simulation)
    for(auto j=0; j<3; ++j) {
        EXPECT_TRUE(results.back().comparisons[j].max_relative_error()*100<0.1);
    }
}

