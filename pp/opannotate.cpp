/**
 * @file opannotate.cpp
 * Implement opannotate utility
 *
 * @remark Copyright 2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 */

#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <utility>

#include "op_header.h"
#include "profile.h"
#include "op_sample_file.h"
#include "cverb.h"
#include "string_manip.h"
#include "demangle_symbol.h"
#include "child_reader.h"
#include "op_file.h"
#include "file_manip.h"
#include "partition_files.h"
#include "opannotate_options.h"
#include "profile_container.h"
#include "symbol_sort.h"

using namespace std;
using namespace options;

namespace {

scoped_ptr<profile_container> samples;

// needed to display samples file information
scoped_ptr<opd_header> header;

/// how opannotate was invoked
string cmdline;

/// empty annotation fill string
string annotation_fill;

/// string used as start / end comment to annotate source
string const begin_comment("/* ");
string const in_comment(" * ");
string const end_comment(" */");

/// field width for the sample count
unsigned int const count_width = 6;


// FIXME share with opgprof.cpp and opreport.cpp
image_set populate_samples(profile_container & samples,
			   partition_files const & files,
			   bool merge_lib)
{
	image_set images = sort_by_image(files, extra_found_images);

	image_set::const_iterator it;
	for (it = images.begin(); it != images.end(); ) {
		pair<image_set::const_iterator, image_set::const_iterator>
			p_it = images.equal_range(it->first);

		if (p_it.first == p_it.second)
			continue;

		op_bfd abfd(p_it.first->first, symbol_filter);
		profile_t profile;

		string app_name = p_it.first->second.image;
		if (merge_lib) {
			app_name = p_it.first->first;
		}

		for (it = p_it.first;  it != p_it.second; ++it) {
			profile.add_sample_file(it->second.sample_filename,
						abfd.get_start_offset());
		}

		check_mtime(abfd.get_filename(), profile.get_header());
	
		samples.add(profile, abfd, app_name);
	}

	return images;
}

void save_sample_file_header(partition_files const & files)
{
	if (files.nr_set()) {
		partition_files::filename_set const & file_set = files.set(0);
		opd_header temp = read_header(file_set.begin()->sample_filename);
		header.reset(new opd_header(temp));
	}
}


string get_annotation_fill()
{
	string str;

	str += string(count_width, ' ') + ' ';
	str += string(percent_width, ' '); 

	return str;
}


symbol_entry const * find_symbol(string const & image_name,
				 string const & str_vma)
{
	// do not use the bfd equivalent:
	//  - it does not skip space at begin
	//  - we does not need cross architecture compile so the native
	// strtoull must work, assuming unsigned long long can contain a vma
	// and on 32/64 bits box bfd_vma is 64 bits
	bfd_vma vma = strtoull(str_vma.c_str(), NULL, 16);

	return samples->find_symbol(image_name, vma);
}


void output_info(ostream & out)
{
	out << begin_comment << '\n';

	out << in_comment << "Command line: " << cmdline << '\n'
	    << in_comment << '\n';

	out << in_comment << "Interpretation of command line:" << '\n';

	if (!assembly) {
		out << in_comment
		    << "Output annotated source file with samples" << '\n';

		// FIXME: re-add this
#if 0
		if (threshold_percent != 0) {
			if (!do_until_more_than_samples) {
				out << in_comment
				    << "Output files where the selected counter reach "
				    << threshold_percent << "% of the samples"
				    << '\n';
			} else {
				out << in_comment << "output files until "
				    << threshold_percent
				    << "% of the samples is reached on the selected counter"
				    << '\n';
			}
		} else
#endif
		{
			out << in_comment << "Output all files" << '\n';
		}
	} else {
		out << in_comment
		    << "Output annotated assembly listing with samples"
		    << '\n';

		if (!objdump_params.empty()) {
			out << in_comment << "Passing the following "
				"additional arguments to objdump ; \"";
			for (size_t i = 0 ; i < objdump_params.size() ; ++i)
				out << objdump_params[i] << " ";
			out << "\"" << '\n';
		}
	}

	out << in_comment << '\n';

	stringstream stream;

	stream << *header;
	stream.seekp(0);

	string line;
	while (getline(stream, line)) {
		out << in_comment << line << '\n';
	}

	out << end_comment << '\n';
}


string counter_str(size_t counter, size_t total)
{
	ostringstream os;
	os << setw(count_width) << counter << ' ';

	os << format_double(op_ratio(counter, total) * 100.0,
			    percent_int_width, percent_fract_width);
	return os.str();
}


string asm_line_annotation(symbol_entry const * last_symbol,
			   string const & value)
{
	// do not use the bfd equivalent:
	//  - it does not skip space at begin
	//  - we does not need cross architecture compile so the native
	// strtoull must work, assuming unsigned long long can contain a vma
	// and on 32/64 bits box bfd_vma is 64 bits
	bfd_vma vma = strtoull(value.c_str(), NULL, 16);

	string str;

	sample_entry const * sample = samples->find_sample(last_symbol, vma);
	if (sample)
		str += counter_str(sample->count, samples->samples_count());

	if (str.empty())
		str = annotation_fill;

	str += " :";
	return str;
}


string symbol_annotation(symbol_entry const * symbol)
{
	if (!symbol)
		return string();

	string annot = counter_str(symbol->sample.count,
				   samples->samples_count());
	if (annot.empty())
		return  string();

	string const & symname = symbol_names.demangle(symbol->name);

	string str = " ";
	str += begin_comment + symname + " total: ";
	str += counter_str(symbol->sample.count, samples->samples_count());
	str += end_comment;
	return str;
}


namespace {

/// return true if  this line contains a symbol name in objdump formatting
/// symbol are on the form 08030434 <symbol_name>:  we need to be strict
/// here to avoid any interpretation of a source line as a symbol line
bool is_symbol_line(string const & str, string::size_type pos)
{
	if (str[pos] != ' ' || str[pos + 1] != '<')
		return false;

	return str[str.length() - 1] == ':';
}

}


symbol_entry const * output_objdump_asm_line(symbol_entry const * last_symbol,
		string const & app_name, string const & str,
		symbol_collection const & symbols,
		bool & do_output)
{
	// output of objdump is a human readable form and can contain some
	// ambiguity so this code is dirty. It is also optimized a little bit
	// so it is difficult to simplify it without breaking something ...

	// line of interest are: "[:space:]*[:xdigit:]?[ :]", the last char of
	// this regexp dis-ambiguate between a symbol line and an asm line. If
	// source contain line of this form an ambiguity occur and we rely on
	// the robustness of this code.

	size_t pos = 0;
	while (pos < str.length() && isspace(str[pos]))
		++pos;

	if (pos == str.length() || !isxdigit(str[pos])) {
		if (do_output) {
			cout << annotation_fill << " :" << str << '\n';
			return last_symbol;
		}
	}

	while (pos < str.length() && isxdigit(str[pos]))
		++pos;

	if (pos == str.length() || (!isspace(str[pos]) && str[pos] != ':')) {
		if (do_output) {
			cout << annotation_fill << " :" << str << '\n';
			return last_symbol;
		}
	}

	if (is_symbol_line(str, pos)) {
		last_symbol = find_symbol(app_name, str);

		// ! complexity: linear in number of symbol must use sorted
		// by address vector and lower_bound ?
		// Note this use a pointer comparison. It work because symbols
		// pointer are unique
		if (find(symbols.begin(), symbols.end(), last_symbol)
			!= symbols.end()) {
			do_output = true;
		} else {
			do_output = false;
		}

		if (do_output)
			cout << str << symbol_annotation(last_symbol) << '\n';

	} else {
		// not a symbol, probably an asm line.
		if (do_output)
			cout << asm_line_annotation(last_symbol, str)
			     << str << '\n';
	}

	return last_symbol;
}


void do_one_output_objdump(symbol_collection const & symbols,
			   string const & app_name, bfd_vma start, bfd_vma end)
{
	vector<string> args;

	args.push_back("-d");
	args.push_back("--no-show-raw-insn");
	if (source)
		args.push_back("-S");

	if (start || end != ~(bfd_vma)0) {
		ostringstream arg1, arg2;
		arg1 << "--start-address=" << start;
		arg2 << "--stop-address=" << end;
		args.push_back(arg1.str());
		args.push_back(arg2.str());
	}

	if (!objdump_params.empty()) {
		for (size_t i = 0 ; i < objdump_params.size() ; ++i)
			args.push_back(objdump_params[i]);
	}

	args.push_back(app_name);
	child_reader reader("objdump", args);
	if (reader.error()) {
		cerr << "An error occur during the execution of objdump:\n\n";
		cerr << reader.error_str() << endl;
		return;
	}

	// to filter output of symbols (filter based on command line options)
	bool do_output = true;

	symbol_entry const * last_symbol = 0;
	string str;
	while (reader.getline(str)) {
		last_symbol = output_objdump_asm_line(last_symbol, app_name,
					str, symbols, do_output);
	}

	// objdump always returns SUCCESS so we must rely on the stderr state
	// of objdump. If objdump error message is cryptic our own error
	// message will be probably also cryptic
	ostringstream std_err;
	ostringstream std_out;
	reader.get_data(std_out, std_err);
	if (std_err.str().length()) {
		cerr << "An error occur during the execution of objdump:\n\n";
		cerr << std_err.str() << endl;
		return ;
	}

	reader.terminate_process();  // force error code to be acquired

	// required because if objdump stop by signal all above things suceeed
	// (signal error message are not output through stdout/stderr)
	if (reader.error()) {
		cerr << "An error occur during the execution of objdump:\n\n";
		cerr << reader.error_str() << endl;
		return;
	}
}


void output_objdump_asm(symbol_collection const & symbols,
			string const & app_name)
{
	// this is only an optimisation, we can either filter output by
	// directly calling objdump and rely on the symbol filtering or
	// we can call objdump with the right parameter to just disassemble
	// the needed part. This is a real win only when calling objdump
	// a medium number of times, I dunno if the used threshold is optimal
	// but it is a conservative value.
	size_t const max_objdump_exec = 50;
	if (symbols.size() <= max_objdump_exec) {
		symbol_collection::const_iterator cit = symbols.begin();
		symbol_collection::const_iterator end = symbols.end();
		for (; cit != end; ++cit) {
			bfd_vma start = (*cit)->sample.vma;
			bfd_vma end  = start + (*cit)->size;
			do_one_output_objdump(symbols, app_name, start, end);
		}
	} else {
		do_one_output_objdump(symbols, app_name, 0, ~bfd_vma(0));
	}
}


void output_asm(string const & app_name)
{
	profile_container::symbol_choice choice;
	choice.threshold = options::threshold;
	choice.image_name = app_name;
	choice.match_image = true;
	symbol_collection symbols = samples->select_symbols(choice);

	sort_options options;
	options.add_sort_option(sort_options::sample);
	options.sort(symbols, false, false);

	output_info(cout);

	output_objdump_asm(symbols, app_name);
}


string const source_line_annotation(string const & filename, size_t linenr)
{
	string str;

	u32 count = samples->samples_count(filename, linenr);
	if (count)
		str += counter_str(count, samples->samples_count());

	if (str.empty())
		str = annotation_fill;

	str += " :";
	return str;
}


string source_symbol_annotation(string const & filename, size_t linenr)
{
	symbol_entry const * symbol = samples->find_symbol(filename, linenr);

	return symbol_annotation(symbol);
}


void output_per_file_info(ostream & out, string const & filename,
			  u32 total_count_for_file)
{
	out << begin_comment << '\n'
	     << in_comment << "Total samples for file : "
	     << '"' << filename << '"'
	     << '\n';
	out << in_comment << '\n' << in_comment
	    << counter_str(total_count_for_file, samples->samples_count())
	    << '\n';
	out << end_comment << '\n' << '\n';
}


string const line0_info(string const & filename)
{
	string annotation = source_line_annotation(filename, 0);
	if (trim(annotation, " \t:").empty())
		return string();

	string str = "<credited to line zero> ";
	str += annotation;
	return str;
}


void do_output_one_file(ostream & out, istream & in, string const & filename,
			bool header)
{
	u32 count = samples->samples_count(filename);

	if (header) {
		output_per_file_info(out, filename, count);
		out << line0_info(filename) << '\n';
	}


	if (in) {
		string str;

		for (size_t linenr = 1 ; getline(in, str) ; ++linenr) {
			out << source_line_annotation(filename, linenr) << str
			    << source_symbol_annotation(filename, linenr)
			    << '\n';
		}

	} else {
		// FIXME : we have no input file : we just outputfooter
		// so on user can known total nr of samples for this source
		// later we must add code that iterate through symbol in this
		// file to output one annotation for each symbol. To do this we
		// need a select_symbol(filename); in profile_container which
		// fall back to the implementation in symbol_container
		// using a lazilly build symbol_map sorted by filename
		// (necessary functors already exist in symbol_functors.h)
	}

	if (!header) {
		output_per_file_info(out, filename, count);
		out << line0_info(filename) << '\n';
	}
}


void output_one_file(istream & in, string const & filename,
		     bool output_separate_file)
{
	if (!output_separate_file) {
		do_output_one_file(cout, in, filename, true);
		return;
	}

	string out_filename = filename;

	size_t pos = out_filename.find(source_dir);
	if (pos == 0) {
		out_filename.erase(0, source_dir.length());
	} else if (pos == string::npos) {
		// filename is outside the source dir: ignore this file
		cerr << "opannotate: file "
		     << '"' << out_filename << '"' << " ignored" << endl;
		return;
	}

	out_filename = relative_to_absolute_path(output_dir + out_filename);

	if (create_path(out_filename.c_str())) {
		cerr << "unable to create directory: "
		     << '"' << dirname(out_filename) << '"' << endl;
		return;
	}

	// paranoid checking: out_filename and filename must be distinct file.
	if (is_files_identical(filename, out_filename)) {
		cerr << "input and output_filename are identical: "
		     << '"' << filename << '"'
		     << ','
		     << '"' << out_filename << '"'
		     << endl;
		return;
	}

	ofstream out(out_filename.c_str());
	if (!out) {
		cerr << "unable to open output file "
		     << '"' << out_filename << '"' << endl;
	} else {
		do_output_one_file(out, in, filename, false);
		output_info(out);
	}
}


void output_source(path_filter const & filter, bool output_separate_file)
{
	if (!output_separate_file)
		output_info(cout);

	vector<string> filenames =
		samples->select_filename(options::threshold);

	for (size_t i = 0 ; i < filenames.size() ; ++i) {
		if (!filter.match(filenames[i]))
			continue;

		ifstream in(filenames[i].c_str());

		if (!in) {
			// it is common to have empty filename due to the lack
			// of debug info (eg _init function) so warn only
			// if the filename is non empty. The case: no debug
			// info at all has already been checked.
			if (filenames[i].length())
				cerr << "opannotate (warning): unable to "
				     << "open for reading: "
				     << filenames[i] << endl;
		} 

		if (filenames[i].length()) {
			output_one_file(in, filenames[i], output_separate_file);
		}
	}
}


bool annotate_source(image_set const & images)
{
	annotation_fill = get_annotation_fill();

	bool output_separate_file = false;
	if (!source_dir.empty()) {
		output_separate_file = true;

		source_dir = relative_to_absolute_path(source_dir);
		if (source_dir.length() &&
		    source_dir[source_dir.length() - 1] != '/')
			source_dir += '/';
	}

	if (!output_dir.empty() || output_separate_file) {
		output_separate_file = true;

		output_dir = relative_to_absolute_path(output_dir);
		if (output_dir.length() &&
		    output_dir[output_dir.length() - 1] != '/')
			output_dir += '/';


		if (create_path(output_dir.c_str())) {
			cerr << "unable to create " << output_dir
			     << " directory: " << endl;
			return false;
		}
	}

	if (output_separate_file && output_dir == source_dir) {
		cerr << "You cannot specify the same directory for "
		     << "--output-dir and --source-dir" << endl;
		return false;
	}

	if (assembly) {
		image_set::const_iterator it;
		for (it = images.begin(); it != images.end(); ) {
			output_asm(it->first);

			it = images.upper_bound(it->first);
		}
	} else {
		output_source(file_filter, output_separate_file);
	}

	return true;
}


int opannotate(vector<string> const & non_options)
{
	handle_options(non_options);

	samples.reset(new profile_container(false, true, true));

	save_sample_file_header(*sample_file_partition);

	image_set images = populate_samples(*samples, *sample_file_partition,
					    false);
	annotate_source(images);

	return 0;
}

} // anonymous namespace


int main(int argc, char const * argv[])
{
	// set the invocation, for the file headers later
	for (int i = 0 ; i < argc ; ++i)
		cmdline += string(argv[i]) + " ";

	return run_pp_tool(argc, argv, opannotate);
}