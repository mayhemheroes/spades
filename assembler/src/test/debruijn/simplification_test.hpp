#pragma once

#include <boost/test/unit_test.hpp>
#include "test_utils.hpp"
#include "graph_simplification.hpp"

namespace debruijn_graph {

BOOST_AUTO_TEST_SUITE(graph_simplification_tests)

static debruijn_config::simplification::bulge_remover standard_br_config_generation() {
	debruijn_config::simplification::bulge_remover br_config;
	br_config.max_length_div_K = 3;
	br_config.max_coverage = 1000.;
	br_config.max_relative_coverage = 1.2;
	br_config.max_delta = 3;
	br_config.max_relative_delta = 0.1;
	return br_config;
}

debruijn_config::simplification::bulge_remover standard_br_config() {
	static debruijn_config::simplification::bulge_remover br_config = standard_br_config_generation();
	return br_config;
}

static debruijn_config::simplification::erroneous_connections_remover standard_ec_config_generation() {
	debruijn_config::simplification::erroneous_connections_remover ec_config;
	ec_config.max_coverage = 30;
	ec_config.max_length_div_K = 2;
	return ec_config;
}

debruijn_config::simplification::erroneous_connections_remover standard_ec_config() {
	static debruijn_config::simplification::erroneous_connections_remover ec_config = standard_ec_config_generation();
	return ec_config;
}

static debruijn_config::simplification::tip_clipper standard_tc_config_generation() {
	debruijn_config::simplification::tip_clipper tc_config;
	tc_config.max_coverage = 1000.;
	tc_config.max_relative_coverage = 1.2;
	tc_config.max_tip_length = 100;
	return tc_config;
}

debruijn_config::simplification::tip_clipper standard_tc_config() {
	static debruijn_config::simplification::tip_clipper tc_config = standard_tc_config_generation();
	return tc_config;
}

BOOST_AUTO_TEST_CASE( SimpleTipClipperTest ) {
	Graph g(55);
	IdTrackHandler<Graph> int_ids(g);
	ScanBasicGraph("./src/test/debruijn/graph_fragments/simpliest_tip/simpliest_tip", g, int_ids);

	ClipTips(g, standard_tc_config());

	BOOST_CHECK_EQUAL(g.size(), 4);
}

BOOST_AUTO_TEST_CASE( SimpleBulgeRemovalTest ) {
	Graph g(55);
	IdTrackHandler<Graph> int_ids(g);
	ScanBasicGraph("./src/test/debruijn/graph_fragments/simpliest_bulge/simpliest_bulge", g, int_ids);

	RemoveBulges(g, standard_br_config());

	BOOST_CHECK_EQUAL(g.size(), 4);
}

BOOST_AUTO_TEST_CASE( TipobulgeTest ) {
	Graph g(55);
	IdTrackHandler<Graph> int_ids(g);
	ScanBasicGraph("./src/test/debruijn/graph_fragments/tipobulge/tipobulge", g, int_ids);

	ClipTips(g, standard_tc_config());

	RemoveBulges(g, standard_br_config());

	BOOST_CHECK_EQUAL(g.size(), 16);
}

BOOST_AUTO_TEST_SUITE_END()}
