/**
 * @file opreport.cpp
 * Implement opreport utility
 *
 * @remark Copyright 2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <sstream>
#include <numeric>

#include "op_exception.h"
#include "string_manip.h"
#include "file_manip.h"
#include "opreport_options.h"
#include "op_header.h"
#include "profile.h"
#include "arrange_profiles.h"
#include "profile_container.h"
#include "symbol_sort.h"
#include "format_output.h"

using namespace std;

namespace {

static size_t nr_groups;

/// storage for a merged file summary
struct summary {
	count_array_t counts;
	string lib_image;

	bool operator<(summary const & rhs) const {
		return options::reverse_sort
		    ? counts[0] < rhs.counts[0] : rhs.counts[0] < counts[0];
	}

	/// add a set of files to a summary
	size_t add_files(list<string> const & files, size_t count_group);
};


size_t summary::add_files(list<string> const & files, size_t count_group)
{
	size_t subtotal = 0;

	list<string>::const_iterator it = files.begin();
	list<string>::const_iterator const end = files.end();

	for (; it != end; ++it) {
		size_t count = profile_t::sample_count(*it);
		counts[count_group] += count;
		subtotal += count;
	}

	return subtotal;
}


/**
 * Summary of an application: a set of image summaries
 * for one application, i.e. an application image and all
 * dependent images such as libraries.
 */
struct app_summary {
	/// total count of us and all dependents
	count_array_t counts;
	/// the main image
	string image;
	/// our dependent images
	vector<summary> deps;

	/// construct and fill in the data
	size_t add_profile(profile_set const & profile, size_t count_group);

	bool operator<(app_summary const & rhs) const {
		return options::reverse_sort 
		    ? counts[0] < rhs.counts[0] : rhs.counts[0] < counts[0];
	}

private:
	/// find a matching summary (including main app summary)
	summary & find_summary(string const & image);
};


summary & app_summary::find_summary(string const & image)
{
	vector<summary>::iterator sit = deps.begin();
	vector<summary>::iterator const send = deps.end();
	for (; sit != send; ++sit) {
		if (sit->lib_image == image)
			return *sit;
	}

	summary summ;
	summ.lib_image = image;
	deps.push_back(summ);
	return deps.back();
}


size_t app_summary::add_profile(profile_set const & profile,
                                size_t count_group)
{
	size_t group_total = 0;

	// first the main image
	summary & summ = find_summary(profile.image);
	size_t app_count = summ.add_files(profile.files, count_group);
	counts[count_group] += app_count;
	group_total += app_count;

	// now all dependent images if any
	list<profile_dep_set>::const_iterator it = profile.deps.begin();
	list<profile_dep_set>::const_iterator const end = profile.deps.end();

	for (; it != end; ++it) {
		summary & summ = find_summary(it->lib_image);
		size_t lib_count = summ.add_files(it->files, count_group);
		counts[count_group] += lib_count;
		group_total += lib_count;
	}

	return group_total;
}


/// all the summaries
struct summary_container {
	summary_container(vector<profile_class> const & pclasses);

	/// all map summaries
	vector<app_summary> apps;
	/// total count of samples for all summaries
	count_array_t total_counts;
};


summary_container::
summary_container(vector<profile_class> const & pclasses)
{
	typedef map<string, app_summary> app_map_t;
	app_map_t app_map;

	for (size_t i = 0; i < pclasses.size(); ++i) {
		list<profile_set>::const_iterator it
			= pclasses[i].profiles.begin();
		list<profile_set>::const_iterator const end
			= pclasses[i].profiles.end();

		for (; it != end; ++it) {
			app_map_t::iterator ait = app_map.find(it->image);
			if (ait == app_map.end()) {
				app_summary app;
				app.image = it->image;
				total_counts[i] += app.add_profile(*it, i);
				app_map[app.image] = app;
			} else {
				total_counts[i]
					+= ait->second.add_profile(*it, i);
			}
		}
	}

	app_map_t::const_iterator it = app_map.begin();
	app_map_t::const_iterator const end = app_map.end();

	for (; it != end; ++it) {
		apps.push_back(it->second);
	}

	// sort by count
	stable_sort(apps.begin(), apps.end());
	vector<app_summary>::iterator ait = apps.begin();
	vector<app_summary>::iterator const aend = apps.end();
	for (; ait != aend; ++ait) {
		stable_sort(ait->deps.begin(), ait->deps.end());
	}
}


void output_header()
{
	if (!options::show_header)
		return;

	cout << classes.cpuinfo << endl;
	if (!classes.event.empty())
		cout << classes.event << endl;

	for (vector<profile_class>::size_type i = 0;
	     i < classes.v.size(); ++i) {
		cout << classes.v[i].longname << endl;
	}
}


string get_filename(string const & filename)
{
	return options::long_filenames ? filename : basename(filename);
}


/// Output a count and a percentage
void output_count(double total_count, size_t count)
{
	cout << setw(9) << count << " ";
	double ratio = op_ratio(count, total_count);
	cout << format_double(ratio * 100, percent_int_width,
			      percent_fract_width) << " ";
}


void output_col_headers(bool indent)
{
	if (!options::show_header)
		return;

	if (indent)
		cout << '\t';

	size_t colwidth = 9 + 1 + percent_width;

	for (size_t i = 0; i < classes.v.size(); ++i) {
		string name = classes.v[i].name;
		if (name.length() > colwidth)
			name = name.substr(0, colwidth - 3)
				+ "...";
		cout << right << setw(colwidth) << name;
		cout << '|';
	}
	cout << '\n';

	if (indent)
		cout << '\t';

	for (size_t i = 0; i < classes.v.size(); ++i) {
		cout << "  samples| ";
		cout << setw(percent_width) << "%|";
	}

	cout << '\n';

	if (indent)
		cout << '\t';

	for (size_t i = 0; i < classes.v.size(); ++i) {
		cout << "-----------";
		string str(percent_width, '-');
		cout << str;
	}

	cout << '\n';

}


void
output_deps(summary_container const & summaries,
	    app_summary const & app)
{
	// the app summary itself is *always* present
	// (perhaps with zero counts) so this test
	// is correct
	if (app.deps.size() == 1)
		return;

	output_col_headers(true);

	for (size_t j = 0 ; j < app.deps.size(); ++j) {
		summary const & summ = app.deps[j];

		if (summ.counts.zero())
			continue;

		cout << '\t';

		for (size_t i = 0; i < nr_groups; ++i) {
			double tot_count = options::global_percent
				? summaries.total_counts[i] : app.counts[i];

			output_count(tot_count, summ.counts[i]);
		}

		cout << get_filename(summ.lib_image);
		cout << '\n';
	}
}


/**
 * Display all the given summary information
 */
void output_summaries(summary_container const & summaries)
{
	output_col_headers(false);

	for (size_t i = 0; i < summaries.apps.size(); ++i) {
		app_summary const & app = summaries.apps[i];

		if ((app.counts[0] * 100.0) / summaries.total_counts[0]
		    < options::threshold) {
			continue;
		}

		for (size_t j = 0; j < nr_groups; ++j) {
			output_count(summaries.total_counts[j],
			             app.counts[j]);
		}

		cout << get_filename(app.image) << '\n';

		output_deps(summaries, app);
	}
}


/// load merged files for one profile
void populate_from_files(profile_container & samples, size_t count_group,
                         string const & image, string const & lib_image,
                         list<string> const & files)
{
	if (!files.size())
		return;

	try {
		op_bfd abfd(lib_image, options::symbol_filter);
		profile_t results;

		list<string>::const_iterator it = files.begin();
		list<string>::const_iterator const end = files.end();

		for (; it != end; ++it) {
			results.add_sample_file(*it, abfd.get_start_offset());
		}

		check_mtime(abfd.get_filename(), results.get_header());

		samples.add(results, abfd, image, count_group);
	}
	catch (op_runtime_error const & e) {
		static bool first_error = true;
		if (first_error) {
			cerr << "warning: some binary images could not be "
			     << "read, and will be ignored in the results"
			     << endl;
		}
		cerr << "op_bfd: " << e.what() << endl;
	}
}


/// load all samples for one given binary
void populate_profile(profile_container & samples,
                      profile_set const & profile, size_t count_group)
{
	populate_from_files(samples, count_group, profile.image,
	                    profile.image, profile.files);

	list<profile_dep_set>::const_iterator it = profile.deps.begin();
	list<profile_dep_set>::const_iterator const end = profile.deps.end();

	for (; it != end; ++it) {
		populate_from_files(samples, count_group, profile.image,
		                    it->lib_image, it->files);
	}
}


/**
 * Load the given samples container with the profile data from
 * the files container, merging as appropriate.
 */
void populate_profiles(profile_class const & pclass,
                       profile_container & samples, size_t count_group)
{
	list<profile_set>::const_iterator it = pclass.profiles.begin();
	list<profile_set>::const_iterator const end = pclass.profiles.end();

	// FIXME: this opens binaries multiple times, need image_set
	// replacement
	for (; it != end; ++it) {
		populate_profile(samples, *it, count_group);
	}
}


format_flags const get_format_flags(column_flags const & cf)
{
	format_flags flags(ff_none);
	flags = format_flags(flags | ff_vma | ff_nr_samples);
	flags = format_flags(flags | ff_percent | ff_symb_name);

	if (options::debug_info)
		flags = format_flags(flags | ff_linenr_info);

	if (options::accumulated) {
		flags = format_flags(flags | ff_nr_samples_cumulated);
		flags = format_flags(flags | ff_percent_cumulated);
	}

	if (cf & cf_image_name)
		flags = format_flags(flags | ff_image_name);

	return flags;
}


void output_symbols(profile_container const & samples, bool multiple_apps)
{
	profile_container::symbol_choice choice;
	choice.threshold = options::threshold;
	symbol_collection symbols = samples.select_symbols(choice);
	options::sort_by.sort(symbols, options::reverse_sort,
	                      options::long_filenames);

	format_output::formatter out(samples);

	out.set_nr_groups(nr_groups);

	if (options::details)
		out.show_details();
	if (options::long_filenames)
		out.show_long_filenames();
	if (!options::show_header)
		out.hide_header();
	if (choice.hints & cf_64bit_vma)
		out.vma_format_64bit();

	format_flags flags = get_format_flags(choice.hints);
	if (multiple_apps)
		flags = format_flags(flags | ff_app_name);

	out.add_format(flags);
	out.output(cout, symbols);
}


int opreport(vector<string> const & non_options)
{
	handle_options(non_options);

	output_header();

	nr_groups = classes.v.size();

	if (!options::symbols) {
		summary_container summaries(classes.v);
		output_summaries(summaries);
		return 0;
	}

	profile_container samples(options::debug_info, options::details);

	bool multiple_apps = false;

	for (size_t i = 0; i < classes.v.size(); ++i) {
		populate_profiles(classes.v[i], samples, i);
		if (classes.v[i].profiles.size() > 1)
			multiple_apps = true;
	}

	output_symbols(samples, multiple_apps);
	return 0;
}

}  // anonymous namespace


int main(int argc, char const * argv[])
{
	cout.tie(0);
	return run_pp_tool(argc, argv, opreport);
}
