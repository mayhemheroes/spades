#include "read_cloud_path_extend/scaffold_graph_extractor.hpp"
#include "scaffold_graph_validation.hpp"
#include "transition_extractor.hpp"

namespace path_extend {
namespace validation {

ScaffoldGraphStats ScaffoldGraphValidator::GetScaffoldGraphStats(
        const path_extend::scaffold_graph::ScaffoldGraph& scaffold_graph,
        const vector<vector<EdgeWithMapping>>& reference_paths) {
    GeneralTransitionStorageBuilder reference_transition_builder(g_, 1, false, false);
    ReverseTransitionStorageBuilder reverse_transition_builder;
    ConjugateTransitionStorageBuilder conjugate_transition_builder(g_);
    const size_t distance = 10;
    GeneralTransitionStorageBuilder general_transition_builder(g_, distance, true, true);
    GeneralTransitionStorageBuilder forward_neighbourhood_transition_builder(g_, distance, false, false);
    auto reference_transitions = reference_transition_builder.GetTransitionStorage(reference_paths);
    auto reverse_transitions = reverse_transition_builder.GetTransitionStorage(reference_paths);
    auto conjugate_transitions = conjugate_transition_builder.GetTransitionStorage(reference_paths);
    auto near_in_both_strands_transitions = general_transition_builder.GetTransitionStorage(reference_paths);
    auto forward_neighbouring_transitions =
        forward_neighbourhood_transition_builder.GetTransitionStorage(reference_paths);
//    auto reference_path_index = BuildReferenceIndex(reference_paths);
    auto stats = GetScaffoldGraphStatsFromTransitions(scaffold_graph, reference_transitions,
                                                      reverse_transitions, conjugate_transitions,
                                                      near_in_both_strands_transitions,
                                                      forward_neighbouring_transitions);
    return stats;
}

set<transitions::Transition> ScaffoldGraphValidator::GetFalseNegativeTransitions(
        const ScaffoldGraphValidator::ScaffoldGraph &graph,
        const ContigTransitionStorage &transitions) const {
    std::set<transitions::Transition> graph_transitions;
    std::set<transitions::Transition> result;
    for (const auto& edge: graph.edges()) {
        transitions::Transition t(edge.getStart(), edge.getEnd());
        graph_transitions.insert(t);
    }
    for (const auto& transition: transitions) {
        if (graph_transitions.find(transition) == graph_transitions.end()){
            result.insert(transition);
        }
    }
    return result;
}

ScaffoldGraphStats ScaffoldGraphValidator::GetScaffoldGraphStatsFromTransitions(
        const path_extend::scaffold_graph::ScaffoldGraph &graph,
        const ContigTransitionStorage &reference_transitions,
        const ContigTransitionStorage &reverse_transitions,
        const ContigTransitionStorage &conjugate_transitions,
        const ContigTransitionStorage &near_in_both_strands_transitions,
        const ContigTransitionStorage &forward_neighbouring_transitions) {
    ScaffoldGraphStats stats;
    DEBUG("True positive");
    stats.true_positive_ = CountStatsUsingTransitions(graph, reference_transitions);
    DEBUG("To previous");
    stats.to_prev_ = CountStatsUsingTransitions(graph, reverse_transitions);
    DEBUG("To near rc");
    stats.to_next_rc_ = CountStatsUsingTransitions(graph, conjugate_transitions);
    DEBUG("To close in both");
    stats.to_close_in_both_strands_ = CountStatsUsingTransitions(graph, near_in_both_strands_transitions);
    DEBUG("False negative");
    stats.false_negative_ = reference_transitions.size() - stats.true_positive_;
    DEBUG("False positive");
    stats.false_positive_ = CountFalsePositive(graph, reference_transitions);
    DEBUG("Next with distance");
    stats.to_next_with_distance_ = CountStatsUsingTransitions(graph, forward_neighbouring_transitions);
    DEBUG("Edges");
    stats.edges_ = graph.EdgeCount();

    auto false_negative_transitions = GetFalseNegativeTransitions(graph, reference_transitions);
    DEBUG(false_negative_transitions.size() << " false negative transitions:");
    for (const auto &transition: false_negative_transitions) {
        DEBUG(transition.first_.int_id() << " -> " << transition.second_.int_id());
        size_t outdegree = graph.OutgoingEdgeCount(transition.first_);
        if (outdegree == 1) {
            ++stats.single_false_transition_;
        }
        if (outdegree == 0) {
            ++stats.no_outgoing_;
        }
    }

    path_extend::ScaffoldGraphExtractor extractor;
    auto univocal_edges = extractor.ExtractUnivocalEdges(graph);
    stats.univocal_edges_ = univocal_edges.size();
    for (const auto &edge: univocal_edges) {
        auto start = edge.getStart();
        auto end = edge.getEnd();
        bool start_covered = reference_transitions.IsEdgeCovered(start);
        bool end_covered = reference_transitions.IsEdgeCovered(end);
        if (not reference_transitions.CheckTransition(start, end) and start_covered and end_covered) {
            ++stats.false_univocal_edges_;
        }
    }
    return stats;
}
size_t ScaffoldGraphValidator::CountStatsUsingTransitions(const ScaffoldGraphValidator::ScaffoldGraph& graph,
                                                          const ContigTransitionStorage& transitions) {
    size_t result = 0;
    for (const ScaffoldGraph::ScaffoldEdge& edge: graph.edges()) {
        if (transitions.CheckTransition(edge.getStart(), edge.getEnd())) {
            TRACE(edge.getStart().int_id() << ", " << edge.getEnd().int_id());
            ++result;
        }
    }
    return result;
}

size_t ScaffoldGraphValidator::CountFalsePositive(const ScaffoldGraphValidator::ScaffoldGraph& graph,
                                                  const ContigTransitionStorage& reference_transtions) {
    size_t result = 0;
    for (const ScaffoldGraph::ScaffoldEdge& edge: graph.edges()) {
        auto start = edge.getStart();
        auto end = edge.getEnd();
        bool start_covered = reference_transtions.IsEdgeCovered(start);
        bool end_covered = reference_transtions.IsEdgeCovered(end);
        if (not reference_transtions.CheckTransition(start, end) and start_covered and end_covered) {
            ++result;
        }
    }
    return result;
}
ScaffoldGraphValidator::ScaffoldGraphValidator(const Graph &g_)
    : g_(g_) {}
}
}