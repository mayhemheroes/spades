#include "path_cluster_helper.hpp"
#include "common/barcode_index/cluster_storage/graph_cluster_storage_builder.hpp"
#include "common/barcode_index/cluster_storage/cluster_storage_helper.hpp"
#include "common/modules/path_extend/scaffolder2015/scaffold_graph.hpp"

namespace path_extend {
transitions::ClusterTransitionStorage PathClusterTransitionStorageHelper::GetPathClusterTransitionStorage(
        const SimpleTransitionGraph &graph) const {
    auto path_clusters = cluster_extractor_helper_.GetPathClusters(graph);
    contracted_graph::ContractedGraphFactoryHelper contracted_helper(g_);
    cluster_storage::ClusterGraphAnalyzer cluster_graph_analyzer(contracted_helper);

    auto path_cluster_extractor =
        make_shared<path_extend::transitions::PathClusterTransitionExtractor>(cluster_graph_analyzer);
    path_extend::transitions::ClusterTransitionStorageBuilder transition_storage_builder;
    DEBUG("Building transition storage");
    transition_storage_builder.BuildFromClusters(path_clusters, path_cluster_extractor);
    path_extend::transitions::ClusterTransitionStorage transition_storage = *(transition_storage_builder.GetStorage());
    size_t transition_storage_size = 0;
    for (const auto& entry: transition_storage) {
        transition_storage_size += entry.second;
    }
    DEBUG("Transition storage size: " << transition_storage_size);
    return transition_storage;
}
vector<cluster_storage::Cluster> PathClusterExtractorHelper::GetPathClusters(
        const PathClusterExtractorHelper::SimpleTransitionGraph &graph) const {
    cluster_storage::GraphClusterStorageBuilder cluster_storage_builder(g_, barcode_extractor_, linkage_distance_);
    DEBUG("Constructing cluster storage");
    auto cluster_storage = cluster_storage_builder.ConstructClusterStorage(*initial_cluster_storage_, graph);
    contracted_graph::ContractedGraphFactoryHelper contracted_helper(g_);
    cluster_storage::ClusterGraphAnalyzer cluster_graph_analyzer(contracted_helper);
    auto path_cluster_filter_ptr = make_shared<cluster_storage::PathClusterFilter>(cluster_graph_analyzer);
//    const size_t max_span = 7000;
//    auto max_span_filter = make_shared<cluster_storage::MaxSpanClusterFilter>(max_span);
//    vector<shared_ptr<cluster_storage::ClusterFilter>> cluster_filters({path_cluster_filter_ptr, max_span_filter});
//    auto composite_filter = make_shared<cluster_storage::CompositeClusterFilter>(cluster_filters);
    cluster_storage::ClusterStorageExtractor cluster_extractor;
    DEBUG("Processing");
    auto path_clusters = cluster_extractor.FilterClusterStorage(cluster_storage, path_cluster_filter_ptr);

    return path_clusters;
}
PathClusterExtractorHelper::PathClusterExtractorHelper(
        const Graph &g,
        shared_ptr<cluster_storage::InitialClusterStorage> initial_cluster_storage,
        shared_ptr<barcode_index::FrameBarcodeIndexInfoExtractor> barcode_extractor,
        size_t linkage_distance)
    : g_(g),
      initial_cluster_storage_(initial_cluster_storage),
      barcode_extractor_(barcode_extractor),
      linkage_distance_(linkage_distance) {}

PathClusterStorage GraphBasedPathClusterNormalizer::GetNormalizedStorage(
        const vector<cluster_storage::Cluster> &path_clusters) const {
    typedef std::set<scaffold_graph::ScaffoldVertex> VertexSet;
    std::map<VertexSet, double> cluster_to_weight;

    for (const auto& cluster: path_clusters) {
        const auto vertex_set = cluster.GetVertexSet();
        if (cluster_to_weight.find(vertex_set) == cluster_to_weight.end()) {
            cluster_to_weight[vertex_set] = 1;
        } else {
            cluster_to_weight[vertex_set]++;
        }
    }

    size_t local_tail_threshold = 10000;
    for (auto &entry: cluster_to_weight) {
        if (entry.first.size() == 2) {
            double length_normalizer = 0;
            for (const auto &vertex: entry.first) {
                length_normalizer += static_cast<double>(std::max(local_tail_threshold, vertex.getLengthFromGraph(g_)));
            }
            entry.second = entry.second / length_normalizer * 100000;
            for (const auto &vertex: entry.first) {
                entry.second /= vertex.getCoverageFromGraph(g_);
            }
            entry.second *= 10000;
        }
    }
    PathClusterStorage storage(cluster_to_weight);
    return storage;
}
GraphBasedPathClusterNormalizer::GraphBasedPathClusterNormalizer(const Graph &g_) : g_(g_) {}

PathClusterStorage::PathClusterStorage(const map<PathClusterStorage::VertexSet, double> &cluster_to_weight)
    : cluster_to_weight_(cluster_to_weight) {}
PathClusterStorage::ClusterToWeightT::const_iterator PathClusterStorage::begin() const {
    return cluster_to_weight_.begin();
}
PathClusterStorage::ClusterToWeightT::const_iterator PathClusterStorage::end() const {
    return cluster_to_weight_.end();
}

PathClusterConflictResolver::ConflictIndex PathClusterConflictResolver::GetConflicts(
        const PathClusterConflictResolver::SimpleTransitionGraph &graph) const {
    ConflictIndex conflicts;
    for (const auto& vertex: graph) {
        const auto& outcoming_vertices = graph.GetOutcoming(vertex);
        const auto& incoming_vertices = graph.GetIncoming(vertex);
        for (const auto &first: outcoming_vertices) {
            for (const auto &second: outcoming_vertices) {
                if (first.int_id() < second.int_id()) {
                    conflicts.AddConflict(first, second, vertex);
                }
            }
        }
        for (const auto &first: incoming_vertices) {
            for (const auto &second: incoming_vertices) {
                if (first.int_id() < second.int_id()) {
                    conflicts.AddConflict(first, second, vertex);
                }
            }
        }
    }
    return conflicts;
}
bool PathClusterConflictResolver::AreClustersConflicted(
        const PathClusterConflictResolver::VertexSet &first,
        const PathClusterConflictResolver::VertexSet &second,
        const PathClusterConflictResolver::ConflictIndex &conflicts) const {
    if (first.size() != second.size()) {
        return false;
    }
    VertexSet first_minus_second;
    std::set_difference(first.begin(), first.end(), second.begin(), second.end(),
                        std::inserter(first_minus_second, first_minus_second.begin()));
    if (first_minus_second.size() != 1) {
        return false;
    }
    VertexSet second_minus_first;
    std::set_difference(second.begin(), second.end(), first.begin(), first.end(),
                        std::inserter(second_minus_first, second_minus_first.begin()));
    VERIFY_DEV(second_minus_first.size() == 1);
    ScaffoldVertex first_vertex = *(first_minus_second.begin());
    ScaffoldVertex second_vertex = *(second_minus_first.begin());
    if (not conflicts.HasConflict(first_vertex, second_vertex)) {
        return false;
    }

    const auto& conflict_bases = conflicts.GetShared(first_vertex, second_vertex);
    VertexSet intersection;
    std::set_intersection(conflict_bases.begin(), conflict_bases.end(), first.begin(), first.end(),
                          std::inserter(intersection, intersection.begin()));
    return not intersection.empty();

}
vector<PathClusterConflictResolver::VertexSet> PathClusterConflictResolver::GetClusterSets(
        const PathClusterConflictResolver::SimpleTransitionGraph &graph, const PathClusterStorage &storage) const {
    std::set<VertexSet> result_set;
    for (const auto &entry: storage) {
        result_set.insert(entry.first);
    }
    DEBUG("Getting conflicts");
    const auto conflict_index = GetConflicts(graph);
    DEBUG("Got conflicts");
    for (const auto &entry: storage) {
        const auto &first_set = entry.first;
        double first_weight = entry.second;
        for (const auto &other: storage) {
            const auto &second_set = other.first;
            const auto &second_weight = other.second;
            DEBUG("Checking conflicts");
            if (AreClustersConflicted(first_set, second_set, conflict_index)) {
                if (first_weight > second_weight * relative_threshold_) {
                    result_set.erase(second_set);
                } else if (second_weight > first_weight * relative_threshold_) {
                    result_set.erase(first_set);
                } else {
                    result_set.erase(first_set);
                    result_set.erase(second_set);
                }
            }
        }
    }
    DEBUG("Returning");
    vector<VertexSet> result;
    std::move(result_set.begin(), result_set.end(), std::back_inserter(result));
    return result;
}
PathClusterConflictResolver::PathClusterConflictResolver(double relative_threshold) :
    relative_threshold_(relative_threshold) {}
void PathClusterConflictResolver::ConflictIndex::AddConflict(const PathClusterConflictResolver::ScaffoldVertex &first,
                                                             const PathClusterConflictResolver::ScaffoldVertex &second,
                                                             const PathClusterConflictResolver::ScaffoldVertex &shared) {
    std::set<ScaffoldVertex> conflict_set;
    conflict_set.insert(first);
    conflict_set.insert(second);
    conflict_to_shared_[conflict_set].insert(shared);
}
bool PathClusterConflictResolver::ConflictIndex::HasConflict(
        const PathClusterConflictResolver::ScaffoldVertex &first,
        const PathClusterConflictResolver::ScaffoldVertex &second) const {
    std::set<ScaffoldVertex> conflict_set;
    conflict_set.insert(first);
    conflict_set.insert(second);
    return conflict_to_shared_.find(conflict_set) != conflict_to_shared_.end();
}
set<PathClusterConflictResolver::ScaffoldVertex> PathClusterConflictResolver::ConflictIndex::GetShared(
        const PathClusterConflictResolver::ScaffoldVertex &first,
        const PathClusterConflictResolver::ScaffoldVertex &second) const {
    VERIFY_DEV(HasConflict(first, second));
    std::set<ScaffoldVertex> conflict_set;
    conflict_set.insert(first);
    conflict_set.insert(second);
    return conflict_to_shared_.at(conflict_set);
}
vector<CloudPathExtractor::InternalPathWithSet> CloudPathExtractor::ExtractAllPaths(
        const CloudPathExtractor::SimpleTransitionGraph &graph,
        const ScaffoldVertex &source, const ScaffoldVertex &sink) const {
    DEBUG("Extracting all paths");
    TRACE(source.int_id() << " -> " << sink.int_id());
    vector<InternalPathWithSet> result;
    std::queue<InternalPathWithSet> current_paths;
    InternalPathWithSet start;
    start.AddVertex(source);
    current_paths.push(start);
    while (not current_paths.empty()) {
        auto last_path = current_paths.front();
        string last_path_string;
        TRACE("Last path size: " << last_path.path_.size());
        for (const auto &vertex: last_path.path_) {
            last_path_string += std::to_string(vertex.int_id()) += " -> ";
            TRACE("Last path:");
            TRACE(last_path_string);
        }
        current_paths.pop();
        ScaffoldVertex last_vertex = last_path.path_.back();
        TRACE("Last vertex in graph: " << graph.ContainsVertex(last_vertex));
        if (last_vertex == sink) {
            result.push_back(last_path);
            continue;
        }
        for (auto it = graph.outcoming_begin(last_vertex); it != graph.outcoming_end(last_vertex); ++it) {
            ScaffoldVertex new_vertex = *it;
            TRACE("New vertex: " << new_vertex.int_id());
            if (not last_path.HasVertex(new_vertex)) {
                auto last_path_copy = last_path;
                last_path_copy.AddVertex(new_vertex);
                TRACE("Adding new path");
                current_paths.push(last_path_copy);
            }
        }
    }
    DEBUG("Extracted " << result.size() << " paths");
    return result;
}
bool CloudPathExtractor::IsPathCorrect(const InternalPathWithSet &path,
                                       const vector<CloudPathExtractor::VertexSet> &clouds) const {
    DEBUG("Checking path");
    const auto vertex_path = path.path_;
    const auto &vertex_set = path.path_vertices_;
    vector<vector<ScaffoldVertex>> supporting_clouds;
    unordered_map<ScaffoldVertex, size_t> vertex_to_index;
    for (size_t i = 0; i < vertex_path.size(); ++i) {
        vertex_to_index[vertex_path[i]] = i;
    }

    auto path_compare_strong = [&vertex_to_index](const ScaffoldVertex &first, const ScaffoldVertex &second) {
      return vertex_to_index.at(first) < vertex_to_index.at(second);
    };

    auto path_compare_weak = [&vertex_to_index](const ScaffoldVertex &first, const ScaffoldVertex &second) {
      return vertex_to_index.at(first) <= vertex_to_index.at(second);
    };

    for (const auto &cloud: clouds) {
        bool cloud_in_path = true;
        for (const auto &vertex: cloud) {
            if (vertex_set.find(vertex) == vertex_set.end()) {
                cloud_in_path = false;
                break;
            }

        }
        if (not cloud_in_path) {
            continue;
        }
        vector<ScaffoldVertex> cloud_vertices;
        std::copy(cloud.begin(), cloud.end(), std::back_inserter(cloud_vertices));
        std::sort(cloud_vertices.begin(), cloud_vertices.end(), path_compare_strong);
        for (size_t i = 1; i < cloud_vertices.size(); ++i) {
            if (vertex_to_index.at(cloud_vertices[i]) != vertex_to_index.at(cloud_vertices[i - 1]) + 1) {
                break;
            }
        }
        supporting_clouds.push_back(cloud_vertices);
    }
    DEBUG("Found " << supporting_clouds.size() << " supporting clouds");

    for (size_t i = 0; i < vertex_path.size(); ++i) {
        for (size_t j = i + 1; j < vertex_path.size(); ++j) {
            if (i != 0 or j != vertex_path.size() - 1) {
                bool found_intersection = false;
                DEBUG("Checking intersections");
                for (const auto& cloud: supporting_clouds) {
                    ScaffoldVertex cloud_first = cloud[0];
                    ScaffoldVertex cloud_last = cloud.back();
                    ScaffoldVertex path_first = vertex_path[i];
                    ScaffoldVertex path_last = vertex_path[j];
                    bool right_nontrival = path_compare_weak(cloud_first, path_last) and
                        path_compare_strong(path_first, cloud_first) and path_compare_strong(path_last, cloud_last);
                    bool left_nontrivial = path_compare_weak(path_first, cloud_last) and
                        path_compare_strong(cloud_last, path_last) and path_compare_strong(cloud_first, path_first);
                    if (left_nontrivial or right_nontrival) {
                        found_intersection = true;
                    }
                }
                DEBUG("Checked intersections");
                if (not (found_intersection)) {
                    return false;
                }
            } else if (j - i <= 2) {
                bool found_containing = false;
                for (const auto& cloud: supporting_clouds) {
                    if (cloud[0] == vertex_path[i] and cloud.back() == vertex_path[j]) {
                        found_containing = true;
                    }
                }
                if (not (found_containing)) {
                    return false;
                }
            }
        }
    }
    return true;
}
vector<vector<CloudPathExtractor::ScaffoldVertex>> CloudPathExtractor::ExtractCorrectPaths(
        const CloudPathExtractor::SimpleTransitionGraph &graph,
        const CloudPathExtractor::ScaffoldVertex &source,
        const CloudPathExtractor::ScaffoldVertex &sink,
        const vector<CloudPathExtractor::VertexSet> &clouds) const {
    DEBUG("Extracting correct paths");
    const auto paths = ExtractAllPaths(graph, source, sink);
    vector<vector<ScaffoldVertex>> result;
    if (paths.size() == 1) {
        DEBUG("Single path");
        result.push_back(paths[0].path_);
        return result;
    }
    for (const auto &path: paths) {
        if (IsPathCorrect(path, clouds)) {
            result.push_back(path.path_);
        }
    }
    return result;
}
void CloudPathExtractor::InternalPathWithSet::AddVertex(const CloudPathExtractor::ScaffoldVertex &vertex) {
    VERIFY_DEV(not HasVertex(vertex));
    path_.push_back(vertex);
    path_vertices_.insert(vertex);
}
bool CloudPathExtractor::InternalPathWithSet::HasVertex(const CloudPathExtractor::ScaffoldVertex &vertex) const {
    return path_vertices_.find(vertex) != path_vertices_.end();
}
vector<vector<CloudBasedPathExtractor::ScaffoldVertex>> CloudBasedPathExtractor::GetCorrectPaths(
        const CloudBasedPathExtractor::SimpleTransitionGraph &graph,
        const CloudBasedPathExtractor::ScaffoldVertex &source,
        const CloudBasedPathExtractor::ScaffoldVertex &sink) const {
    PathClusterExtractorHelper path_cluster_extractor(g_, initial_cluster_storage_,
                                                      barcode_extractor_, linkage_distance_);
    auto path_clusters = path_cluster_extractor.GetPathClusters(graph);
    GraphBasedPathClusterNormalizer path_cluster_normalizer(g_);
    auto cluster_to_weight = path_cluster_normalizer.GetNormalizedStorage(path_clusters);
    PathClusterConflictResolver conflict_resolver(relative_cluster_threshold_);
    auto final_clusters = conflict_resolver.GetClusterSets(graph, cluster_to_weight);
    CloudPathExtractor cloud_path_extractor;
    DEBUG("Extracting paths using final clusters");
    auto resulting_paths = cloud_path_extractor.ExtractCorrectPaths(graph, source, sink, final_clusters);
    DEBUG("Extracted paths");
    return resulting_paths;
}
CloudBasedPathExtractor::CloudBasedPathExtractor(
        const Graph &g,
        shared_ptr<cluster_storage::InitialClusterStorage> initial_cluster_storage,
        shared_ptr<barcode_index::FrameBarcodeIndexInfoExtractor> barcode_extractor,
        size_t linkage_distance, double relative_cluster_threshold)
    : g_(g),
      initial_cluster_storage_(initial_cluster_storage),
      barcode_extractor_(barcode_extractor),
      linkage_distance_(linkage_distance),
      relative_cluster_threshold_(relative_cluster_threshold) {}

vector<cluster_storage::Cluster> ScaffoldGraphPathClusterHelper::GetPathClusters(
        const scaffold_graph::ScaffoldGraph &graph) const {
    const size_t linkage_distance = 10;
    TransitionGraph simple_graph;
    for (const auto &vertex: graph.vertices()) {
        simple_graph.AddVertex(vertex);
    }
    for (const auto &edge: graph.edges()) {
        simple_graph.AddEdge(edge.getStart(), edge.getEnd());
    }
    PathClusterExtractorHelper cluster_extractor_helper(g_, initial_cluster_storage_,
                                                        barcode_extractor_, linkage_distance);
    return cluster_extractor_helper.GetPathClusters(simple_graph);
}
ScaffoldGraphPathClusterHelper::ScaffoldGraphPathClusterHelper(
        const Graph &g,
        shared_ptr<barcode_index::FrameBarcodeIndexInfoExtractor> barcode_extractor,
        shared_ptr<cluster_storage::InitialClusterStorage> initial_cluster_storage,
        size_t max_threads)
    : g_(g), barcode_extractor_(barcode_extractor),
      initial_cluster_storage_(initial_cluster_storage), max_threads_(max_threads) {}

vector<cluster_storage::Cluster> ScaffoldGraphPathClusterHelper::GetAllClusters(
        const scaffold_graph::ScaffoldGraph &graph) const {
    const size_t linkage_distance = 10;
    cluster_storage::GraphClusterStorageBuilder cluster_storage_builder(g_, barcode_extractor_, linkage_distance);
    DEBUG("Constructing cluster storage");
    TransitionGraph simple_graph;
    for (const auto &vertex: graph.vertices()) {
        simple_graph.AddVertex(vertex);
    }
    for (const auto &edge: graph.edges()) {
        simple_graph.AddEdge(edge.getStart(), edge.getEnd());
    }
    auto cluster_storage = cluster_storage_builder.ConstructClusterStorage(*initial_cluster_storage_, simple_graph);
    vector<cluster_storage::Cluster> result;
    for (const auto &entry: cluster_storage) {
        if (entry.second.Size() >= 2) {
            result.push_back(entry.second);
        }
    }
    return result;
}
vector<ScaffoldGraphPathClusterHelper::Cluster> ScaffoldGraphPathClusterHelper::GetPathClusters(
        const vector<ScaffoldGraphPathClusterHelper::Cluster> &clusters) const {
    contracted_graph::ContractedGraphFactoryHelper contracted_helper(g_);
    cluster_storage::ClusterGraphAnalyzer cluster_graph_analyzer(contracted_helper);
    auto path_cluster_filter_ptr = make_shared<cluster_storage::PathClusterFilter>(cluster_graph_analyzer);
    cluster_storage::ClusterStorageExtractor cluster_extractor;
    return cluster_extractor.FilterClusters(clusters, path_cluster_filter_ptr);
}
vector<set<ScaffoldGraphPathClusterHelper::ScaffoldVertex>> ScaffoldGraphPathClusterHelper::GetCorrectedClusters(
        const vector<ScaffoldGraphPathClusterHelper::Cluster> &path_clusters,
        const scaffold_graph::ScaffoldGraph &graph) const {
    auto transition_graph = ScaffoldToTransition(graph);
    return GetCorrectedClusters(path_clusters, transition_graph);
}
vector<set<ScaffoldGraphPathClusterHelper::ScaffoldVertex>> ScaffoldGraphPathClusterHelper::GetFinalClusters(
        const scaffold_graph::ScaffoldGraph &graph) const {
    auto transition_graph = ScaffoldToTransition(graph);
    return GetFinalClusters(transition_graph);
}
ScaffoldGraphPathClusterHelper::TransitionGraph ScaffoldGraphPathClusterHelper::ScaffoldToTransition(
        const scaffold_graph::ScaffoldGraph &graph) const {
    TransitionGraph simple_graph;
    for (const auto &vertex: graph.vertices()) {
        simple_graph.AddVertex(vertex);
    }
    for (const auto &edge: graph.edges()) {
        simple_graph.AddEdge(edge.getStart(), edge.getEnd());
    }
    return simple_graph;
}
vector<set<ScaffoldGraphPathClusterHelper::ScaffoldVertex>> ScaffoldGraphPathClusterHelper::GetFinalClusters(
        const ScaffoldGraphPathClusterHelper::TransitionGraph &graph) const {
    const size_t linkage_distance = 10;
    PathClusterExtractorHelper cluster_extractor_helper(g_, initial_cluster_storage_,
                                                        barcode_extractor_, linkage_distance);
    auto path_clusters = cluster_extractor_helper.GetPathClusters(graph);
    return GetCorrectedClusters(path_clusters, graph);
}
vector<set<ScaffoldGraphPathClusterHelper::ScaffoldVertex>> ScaffoldGraphPathClusterHelper::GetCorrectedClusters(
        const vector<ScaffoldGraphPathClusterHelper::Cluster> &path_clusters,
        const ScaffoldGraphPathClusterHelper::TransitionGraph &graph) const {
    GraphBasedPathClusterNormalizer path_cluster_normalizer(g_);
    auto cluster_to_weight = path_cluster_normalizer.GetNormalizedStorage(path_clusters);
    const double relative_threshold = 2;
    PathClusterConflictResolver conflict_resolver(relative_threshold);
    auto final_clusters = conflict_resolver.GetClusterSets(graph, cluster_to_weight);
    return final_clusters;
}
}