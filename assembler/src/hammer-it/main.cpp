#include "logger/log_writers.hpp"

#include "io/reader.hpp"
#include "io/osequencestream.hpp"
#include "io/read_processor.hpp"

#include "adt/concurrent_dsu.hpp"

#include "segfault_handler.hpp"
#include "memory_limit.hpp"

#include "HSeq.hpp"
#include "kmer_data.hpp"
#include "hamcluster.hpp"
#include "subcluster.hpp"
#include "err_helper_table.hpp"
#include "read_corrector.hpp"
#include "config_struct.hpp"

#include "openmp_wrapper.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iomanip>

void create_console_logger() {
  using namespace logging;

  logger *lg = create_logger("");
  lg->add_writer(std::make_shared<console_writer>());
  attach_logger(lg);
}

struct UfCmp {
  bool operator()(const std::vector<unsigned> &lhs, const std::vector<unsigned> &rhs) {
    //return (lhs[0] < rhs[0]);
    return lhs.size() > rhs.size();
  }
};

// This is weird workaround for bug in gcc 4.4.7
static bool stage(hammer_config::HammerStage start, hammer_config::HammerStage current) {
  switch (start) {
    case hammer_config::HammerStage::KMerCounting:
      return true;
    case hammer_config::HammerStage::HammingClustering:
      return current != hammer_config::HammerStage::KMerCounting;
    case hammer_config::HammerStage::SubClustering:
      return (current != hammer_config::HammerStage::KMerCounting &&
              current != hammer_config::HammerStage::HammingClustering);
    case hammer_config::HammerStage::ReadCorrection:
      return current == hammer_config::HammerStage::ReadCorrection;
  }
}

int main(int argc, char** argv) {
  segfault_handler sh;

  srand(42);
  srandom(42);

  try {
    create_console_logger();

    std::string config_file = "hammer-it.cfg";
    if (argc > 1) config_file = argv[1];
    INFO("Loading config from " << config_file.c_str());
    cfg::create_instance(config_file);

    // hard memory limit
    const size_t GB = 1 << 30;
    limit_memory(cfg::get().hard_memory_limit * GB);

    KMerData kmer_data;
    if (stage(cfg::get().start_stage, hammer_config::HammerStage::KMerCounting)) {
      // FIXME: Actually it's num_files here
      KMerDataCounter(32).FillKMerData(kmer_data);
      if (cfg::get().debug_mode) {
        INFO("Debug mode on. Saving K-mer index.");
        std::ofstream ofs(path::append_path(cfg::get().working_dir, "count.kmdata"), std::ios::binary);
        kmer_data.binary_write(ofs);
      }
    } else {
      INFO("Loading K-mer index.");
      std::ifstream ifs(path::append_path(cfg::get().working_dir, "count.kmdata"), std::ios::binary);
      VERIFY(ifs.good());
      kmer_data.binary_read(ifs);
    }

    std::vector<std::vector<size_t> > classes;
    if (stage(cfg::get().start_stage, hammer_config::HammerStage::HammingClustering)) {
      ConcurrentDSU uf(kmer_data.size());
      KMerHamClusterer clusterer(cfg::get().tau);
      INFO("Clustering Hamming graph.");
      clusterer.cluster(path::append_path(cfg::get().working_dir, "kmers.hamcls"), kmer_data, uf);
      uf.get_sets(classes);
      size_t num_classes = classes.size();
      INFO("Clustering done. Total clusters: " << num_classes);

      if (cfg::get().debug_mode) {
        INFO("Debug mode on. Writing down clusters.");
        std::ofstream ofs(path::append_path(cfg::get().working_dir, "hamming.cls"), std::ios::binary);

        ofs.write((char*)&num_classes, sizeof(num_classes));
        for (size_t i=0; i < classes.size(); ++i) {
          size_t sz = classes[i].size();
          ofs.write((char*)&sz, sizeof(sz));
          ofs.write((char*)&classes[i][0], sz * sizeof(classes[i][0]));
        }
      }
    } else {
      INFO("Loading clusters.");
      std::ifstream ifs(path::append_path(cfg::get().working_dir, "hamming.cls"), std::ios::binary);
      VERIFY(ifs.good());

      size_t num_classes = 0;
      ifs.read((char*)&num_classes, sizeof(num_classes));
      classes.resize(num_classes);

      for (size_t i = 0; i < num_classes; ++i) {
        size_t sz = 0;
        ifs.read((char*)&sz, sizeof(sz));
        classes[i].resize(sz);
        ifs.read((char*)&classes[i][0], sz * sizeof(classes[i][0]));
      }
    }

    size_t singletons = 0;
    for (size_t i = 0; i < classes.size(); ++i)
      if (classes[i].size() == 1)
        singletons += 1;
    INFO("Singleton clusters: " << singletons);

    if (stage(cfg::get().start_stage, hammer_config::HammerStage::SubClustering)) {
      size_t nonread = 0;
#if 1
      INFO("Subclustering.");
#     pragma omp parallel for shared(nonread, classes, kmer_data)
      for (size_t i = 0; i < classes.size(); ++i) {
        auto& cluster = classes[i];

#       pragma omp atomic
        nonread += subcluster(kmer_data, cluster);
      }
#else
      INFO("Assigning centers");
#     pragma omp parallel for shared(nonread, classes, kmer_data)
      for (size_t i = 0; i < classes.size(); ++i) {
        const auto& cluster = classes[i];
#       pragma omp atomic
        nonread += assign(kmer_data, cluster);
      }
#endif
      INFO("Total " << nonread << " nonread kmers were generated");

      if (cfg::get().debug_mode) {
        INFO("Debug mode on. Saving K-mer index.");
        std::ofstream ofs(path::append_path(cfg::get().working_dir, "cluster.kmdata"), std::ios::binary);
        kmer_data.binary_write(ofs);
      }
    } else {
      INFO("Loading K-mer index.");
      std::ifstream ifs(path::append_path(cfg::get().working_dir, "cluster.kmdata"), std::ios::binary);
      VERIFY(ifs.good());
      kmer_data.binary_read(ifs);
    }

#if 0
    std::ofstream fasta_ofs("centers.fasta");
    fasta_ofs << std::fixed << std::setprecision(6) << std::setfill('0');
    std::sort(classes.begin(), classes.end(),  UfCmp());
    for (size_t i = 0; i < classes.size(); ++i) {
      auto& cluster = classes[i];
      std::sort(cluster.begin(), cluster.end(), CountCmp(kmer_data));
      hammer::HKMer c = center(kmer_data, cluster);
      size_t idx = kmer_data.seq_idx(c);
      if (kmer_data[idx].kmer == c) {
        fasta_ofs << '>' << std::setw(6) << i
                  << "-cov_" << std::setw(0) << kmer_data[idx].count
                  << "-qual_" << 1.0 - kmer_data[idx].qual;

        if (cluster.size() == 1)
          fasta_ofs << "_singleton";
        fasta_ofs << '\n' << c << '\n';
      }
    }
#endif

    INFO("Correcting reads.");
    using namespace hammer::correction;
    SingleReadCorrector::NoDebug pred;
    const auto& dataset = cfg::get().dataset;
    for (auto it = dataset.reads_begin(), et = dataset.reads_end(); it != et; ++it) {
      INFO("Correcting " << *it);
      io::Reader irs(*it, io::PhredOffset);
      io::osequencestream ors(path::append_path(cfg::get().output_dir, path::basename(*it) + ".fasta")); // FIXME: Proper filename

      SingleReadCorrector read_corrector(kmer_data, pred);
      hammer::ReadProcessor(cfg::get().max_nthreads).Run(irs, read_corrector, ors);
    }

#if 0
    std::sort(classes.begin(), classes.end(),  UfCmp());
    for (size_t i = 0; i < classes.size(); ++i) {
      auto& cluster = classes[i];
      std::sort(cluster.begin(), cluster.end(), CountCmp(kmer_data));
      dump(kmer_data, cluster);
    }
#endif
  } catch (std::bad_alloc const& e) {
    std::cerr << "Not enough memory to run BayesHammer. " << e.what() << std::endl;
    return EINTR;
  } catch (const YAML::Exception &e) {
    std::cerr << "Error reading config file: " << e.what() << std::endl;
    return EINTR;
  } catch (std::exception const& e) {
    std::cerr << "Exception caught " << e.what() << std::endl;
    return EINTR;
  } catch (...) {
    std::cerr << "Unknown exception caught " << std::endl;
    return EINTR;
  }

  return 0;
}
