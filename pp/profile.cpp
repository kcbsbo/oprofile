/**
 * @file profile.cpp
 * Encapsulation for samples files over all counter belonging to the
 * same binary image
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Philippe Elie
 * @author John Levon
 */

#include <unistd.h>

#include <iostream>
#include <string>

#include <cerrno>

#include "op_file.h"
#include "op_cpu_type.h"
#include "counter_array.h"
#include "op_sample_file.h"
#include "string_manip.h"
#include "op_print_event.h"

#include "profile.h"

using namespace std;

profile_t::profile_t(string const & sample_file, int counter_)
	:
	nr_counters(2),
	sample_filename(sample_file),
	counter_mask(counter_),
	first_file(-1)
{
	uint i, j;
	time_t mtime = 0;

	for (i = 0; i < OP_MAX_COUNTERS ; ++i) {
		if ((counter_mask &  (1 << i)) != 0) {
			/* if only the i th bit is set in counter spec we do
			 * not allow opening failure to get a more precise
			 * error message */
			open_samples_file(i, (counter_mask & ~(1 << i)) != 0);
		}
	}

	/* find first open file */
	for (first_file = 0; first_file < OP_MAX_COUNTERS ; ++first_file) {
		if (samples[first_file].get() != 0)
			break;
	}

	if (first_file == OP_MAX_COUNTERS) {
		cerr << "Can not open any samples files for " << sample_filename
			<< ". Last error " << strerror(errno) << endl;
		exit(EXIT_FAILURE);
	}

	opd_header const & header = samples[first_file]->header();
	mtime = header.mtime;

	/* determine how many counters are possible via the sample file */
	op_cpu cpu = static_cast<op_cpu>(header.cpu_type);
	nr_counters = op_get_nr_counters(cpu);

	/* check sample files match */
	for (j = first_file + 1; j < OP_MAX_COUNTERS; ++j) {
		if (samples[j].get() == 0)
			continue;
		samples[first_file]->check_headers(*samples[j]);
	}
}

profile_t::~profile_t()
{
}

void profile_t::check_mtime(string const & file) const
{
	time_t const newmtime = op_get_mtime(file.c_str());
	if (newmtime != first_header().mtime) {
		cerr << "oprofpp: WARNING: the last modified time of the binary file "
			<< file << " does not match\n"
			<< "that of the sample file. Either this is the wrong binary or the binary\n"
			<< "has been modified since the sample file was created.\n";
	}
}


void profile_t::open_samples_file(u32 counter, bool can_fail)
{
	string filename = ::sample_filename(string(), sample_filename, counter);

	if (access(filename.c_str(), R_OK) == 0) {
		samples[counter].reset(new counter_profile_t(filename));
	} else {
		if (!can_fail) {
			cerr << "oprofpp: Opening " << filename <<  "failed."
			     << strerror(errno) << endl;
			exit(EXIT_FAILURE);
		}
	}
}

bool profile_t::accumulate_samples(counter_array_t & counter, uint index) const
{
	bool found_samples = false;

	for (uint k = 0; k < nr_counters; ++k) {
		u32 count = samples_count(k, index);
		if (count) {
			counter[k] += count;
			found_samples = true;
		}
	}

	return found_samples;
}

bool profile_t::accumulate_samples(counter_array_t & counter,
	uint start, uint end) const
{
	bool found_samples = false;

	for (uint k = 0; k < nr_counters; ++k) {
		if (is_open(k)) {
			counter[k] += samples[k]->count(start, end);
			if (counter[k])
				found_samples = true;
		}
	}

	return found_samples;
}

void profile_t::set_start_offset(u32 start_offset)
{
	if (!first_header().is_kernel)
		return;

	for (uint k = 0; k < nr_counters; ++k) {
		if (is_open(k))
			samples[k]->set_start_offset(start_offset);
	}
}

/**
 * output_header() - output counter setup
 *
 * output to stdout the cpu type, cpu speed
 * and all counter description available
 */
void profile_t::output_header() const
{
	opd_header const & header = first_header();

	op_cpu cpu = static_cast<op_cpu>(header.cpu_type);

	cout << "Cpu type: " << op_get_cpu_type_str(cpu) << endl;

	cout << "Cpu speed was (MHz estimation) : " << header.cpu_speed << endl;

	for (uint i = 0 ; i < OP_MAX_COUNTERS; ++i) {
		if (samples[i].get() != 0) {
			op_print_event(cout, i, cpu, header.ctr_event,
				       header.ctr_um, header.ctr_count);
		}
	}
}
