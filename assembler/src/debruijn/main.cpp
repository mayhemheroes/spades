/*
 * Assembler Main
 */


#include "config_struct.hpp"
#include "io/reader.hpp"
#include "io/rc_reader_wrapper.hpp"
#include "io/cutting_reader_wrapper.hpp"
#include "io/filtering_reader_wrapper.hpp"
#include "launch.hpp"
#include "logging.hpp"
#include "simple_tools.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "omni/distance_estimation.hpp"
//#include <distance_estimation.hpp>

DECL_PROJECT_LOGGER("d")

int make_dir(std::string const& str)
{
    return mkdir(str.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH | S_IWOTH);
}


int main() {

    using namespace debruijn_graph;

    checkFileExistenceFATAL(cfg_filename);
    cfg::create_instance(cfg_filename);

	// check config_struct.hpp parameters
	if (K % 2 == 0)
		FATAL("K in config.hpp must be odd!\n");

	// read configuration file (dataset path etc.)
	string input_dir = cfg::get().input_dir;
	string dataset   = cfg::get().dataset_name;

	make_dir(cfg::get().output_root );
	make_dir(cfg::get().output_dir  );
	make_dir(cfg::get().output_saves);

	string genome_filename = input_dir + cfg::get().reference_genome;
	string reads_filename1 = input_dir + cfg::get().ds.first;
	string reads_filename2 = input_dir + cfg::get().ds.second;

	checkFileExistenceFATAL(genome_filename);
	checkFileExistenceFATAL(reads_filename1);
	checkFileExistenceFATAL(reads_filename2);
	INFO("Assembling " << dataset << " dataset");

	// typedefs :)
	typedef io::Reader<io::SingleRead> ReadStream;
	typedef io::Reader<io::PairedRead> PairedReadStream;
	typedef io::RCReaderWrapper<io::PairedRead> RCStream;
	typedef io::FilteringReaderWrapper<io::PairedRead> FilteringStream;

	// read data ('reads')

	PairedReadStream pairStream(std::make_pair(reads_filename1,reads_filename2), cfg::get().ds.IS);

	string real_reads = cfg::get().uncorrected_reads;
	if (real_reads != "none") {
		reads_filename1 = input_dir + (real_reads + "_1");
		reads_filename2 = input_dir + (real_reads + "_2");
	}
	ReadStream reads_1(reads_filename1);
	ReadStream reads_2(reads_filename2);

	vector<ReadStream*> reads = {&reads_1, &reads_2};

	FilteringStream filter_stream(pairStream);

	RCStream rcStream(filter_stream);

	// read data ('genome')
	std::string genome;
	{
		ReadStream genome_stream(genome_filename);
		io::SingleRead full_genome;
		genome_stream >> full_genome;
		genome = full_genome.GetSequenceString().substr(0, cfg::get().ds.LEN); // cropped
	}
	// assemble it!
	INFO("Assembling " << dataset << " dataset");
	debruijn_graph::assembly_genome(rcStream, Sequence(genome)/*, work_tmp_dir, reads*/);

	string latest_folder = cfg::get().output_root + "latest";
	unlink(latest_folder.c_str());

	if (symlink(cfg::get().output_suffix.c_str(), latest_folder.c_str()) != 0)
	    WARN( "Symlink to latest launch failed");

	INFO("Assembling " << dataset << " dataset finished");
	// OK
	return 0;
}

