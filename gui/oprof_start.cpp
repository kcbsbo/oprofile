/**
 * @file oprof_start.cpp
 * The GUI start main class
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Philippe Elie
 * @author John Levon
 */

#include <sys/stat.h>
#include <unistd.h>

#include <ctime>
#include <cstdio>
#include <cmath>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>

#include <qlineedit.h>
#include <qlistview.h>
#include <qcombobox.h>
#include <qlistbox.h>
#include <qfiledialog.h>
#include <qbuttongroup.h>
#include <qcheckbox.h>
#include <qtabwidget.h>
#include <qmessagebox.h>
#include <qvalidator.h>
#include <qlabel.h>
#include <qpushbutton.h>

#include "oprof_start.h"
#include "op_config.h"
#include "op_config_24.h"
#include "string_manip.h"
#include "version.h"

using namespace std;

op_event_descr::op_event_descr()
	:
	counter_mask(0),
	val(0),
	unit(0),
	min_count(0)
{
}

oprof_start::oprof_start()
	:
	oprof_start_base(0, 0, false, 0),
	event_count_validator(new QIntValidator(event_count_edit)),
	current_ctr(0),
	cpu_speed(get_cpu_speed()),
	total_nr_interrupts(0)
{
	for (uint i = 0; i < OP_MAX_COUNTERS; ++i) {
		current_event[i] = 0;
		ctr_enabled[i] = 0;
	}

	vector<string> args;
	args.push_back("--init");

	if (do_exec_command(OP_BINDIR "/opcontrol", args))
		exit(EXIT_FAILURE);

	cpu_type = op_get_cpu_type();
	op_nr_counters = op_get_nr_counters(cpu_type);

	if (cpu_type == CPU_RTC) {
		current_ctr = 0;
		enabled_toggled(1);
		enabled->hide();
		counter_combo->hide();
		unit_mask_group->hide();
	}

	// we need to build the event descr stuff before loading the
	// configuration because we use locate_events to get an event descr
	// from its name.
	struct list_head * pos;
	struct list_head * events = op_events(cpu_type);

	list_for_each(pos, events) {
		struct op_event * event = list_entry(pos, struct op_event, event_next);

		op_event_descr descr;

		descr.counter_mask = event->counter_mask;
		descr.val = event->val;
		if (event->unit->num) {
			descr.unit = event->unit;
		} else {
			descr.unit = 0;
		}

		descr.name = event->name;
		descr.help_str = event->desc;
		descr.min_count = event->min_count;

		for (uint ctr = 0; ctr < op_nr_counters; ++ctr) {
			uint count;

			if (!(descr.counter_mask & (1 << ctr)))
				continue;

			if (cpu_type == CPU_RTC) {
				count = 1024;
			} else {
				/* setting to cpu Hz / 2000 gives a safe value for
				 * all events, and a good one for most.
				 */
				if (cpu_speed)
					count = cpu_speed * 500;
				else
					count = descr.min_count * 100;
			}

			event_cfgs[ctr][descr.name].count = count;
			event_cfgs[ctr][descr.name].umask = 0;
			if (descr.unit)
				event_cfgs[ctr][descr.name].umask = descr.unit->default_mask;
			event_cfgs[ctr][descr.name].os_ring_count = 1;
			event_cfgs[ctr][descr.name].user_ring_count = 1;
		}

		v_events.push_back(descr);
	}

	load_config_file();

	bool is_25 = op_get_interface() == OP_INTERFACE_25;

	if (is_25) {
		pid_filter_label->hide();
		pid_filter_edit->hide();
		pgrp_filter_label->hide();
		pgrp_filter_edit->hide();
		note_table_size_edit->hide();
		note_table_size_label->hide();
		kernel_only_cb->hide();
		// FIXME: can adapt to 2.5 ...
		buffer_size_edit->hide();
		buffer_size_label->hide();
	} else {
		no_vmlinux->hide();
	}

	// setup the configuration page.
	kernel_filename_edit->setText(config.kernel_filename.c_str());

	no_vmlinux->setChecked(config.no_kernel);

	buffer_size_edit->setText(QString().setNum(config.buffer_size));
	note_table_size_edit->setText(QString().setNum(config.note_table_size));
	if (config.pid_filter)
		pid_filter_edit->setText(QString().setNum(config.pid_filter));
	else
		pid_filter_edit->setText("");
	if (config.pgrp_filter)
		pgrp_filter_edit->setText(QString().setNum(config.pgrp_filter));
	else
		pgrp_filter_edit->setText("");
	verbose->setChecked(config.verbose);
	kernel_only_cb->setChecked(config.kernel_only);
	separate_lib_samples_cb->setChecked(config.separate_lib_samples);
	separate_kernel_samples_cb->setChecked(config.separate_kernel_samples);

	// the unit mask check boxes
	hide_masks();

	event_count_edit->setValidator(event_count_validator);
	QIntValidator * iv;
	iv = new QIntValidator(OP_MIN_BUF_SIZE, OP_MAX_BUF_SIZE, buffer_size_edit);
	buffer_size_edit->setValidator(iv);
	iv = new QIntValidator(OP_MIN_NOTE_TABLE_SIZE, OP_MAX_NOTE_TABLE_SIZE, note_table_size_edit);
	note_table_size_edit->setValidator(iv);
	iv = new QIntValidator(pid_filter_edit);
	pid_filter_edit->setValidator(iv);
	iv = new QIntValidator(pgrp_filter_edit);
	pgrp_filter_edit->setValidator(iv);

	events_list->setSorting(-1);

	for (uint ctr = 0 ; ctr < op_nr_counters ; ++ctr) {
		load_event_config_file(ctr);
	}

	read_set_events();

	// re-init event stuff
	enabled_toggled(ctr_enabled[current_ctr]);
	display_event(current_event[current_ctr]);

	for (uint ctr = 0 ; ctr < op_nr_counters ; ++ctr) {
		counter_combo->insertItem("");
		set_counter_combo(ctr);
	}

	counter_selected(0);

	if (cpu_type == CPU_RTC)
		events_list->setCurrentItem(events_list->firstChild());

	// daemon status timer
	startTimer(5000);
	timerEvent(0);

	resize(minimumSizeHint());
}


void oprof_start::read_set_events()
{
	string name = get_user_filename(".oprofile/daemonrc");

	ifstream in(name.c_str());

	if (!in)
		return;

	string str;
	unsigned int ctr = 0;
	string evname;

	while (getline(in, str)) {
		string val = split(str, '=');
		if (str == "RTC_VALUE" && cpu_type == CPU_RTC) {
			current_event[0] = &locate_event("RTC_INTERRUPTS");
			event_cfgs[ctr]["RTC_INTERRUPTS"].count = touint(val);
			return;
		}
		
		if (!is_prefix(str, "CTR_"))
	       		continue;
	
		// CTR_EVENT[0]
		str = split(str, '_');
		// EVENT[0]
		string num = split(str, '[');
		string const type = str;
		// num == "0]", type == "EVENT"

		num = rtrim(num, "]");
		ctr = touint(num);

		if (type == "EVENT") {
			evname = val;
			ctr_enabled[ctr] = evname.length();
			if (evname.length()) {
				current_event[ctr] = &locate_event(evname);
			} else {
				current_event[ctr] = 0;
			}
			continue;

		}

		if (!ctr_enabled[ctr])
			continue;

		if (type == "COUNT") {
			event_cfgs[ctr][evname].count = touint(val);
		} else if  (type == "KERNEL") {
			event_cfgs[ctr][evname].os_ring_count = tobool(val);
		} else if  (type == "USER") {
			event_cfgs[ctr][evname].user_ring_count = tobool(val);
		} else if (type == "UM") {
			event_cfgs[ctr][evname].umask = touint(val);
		}
	}
}


void oprof_start::load_config_file()
{
	string name = get_user_filename(".oprofile/daemonrc");

	ifstream in(name.c_str());
	if (!in) {
		if (!check_and_create_config_dir())
			return;

		ofstream out(name.c_str());
		if (!out) {
			QMessageBox::warning(this, 0, "Unable to open configuration "
				"file ~/.oprofile/daemonrc");
			return;
		}
		return;
	}

	in >> config;
}


void oprof_start::load_event_config_file(uint ctr)
{
	ostringstream ss;

	ss << get_user_filename(".oprofile/oprof_start_event");
	ss << "#" << ctr;

	string const name = ss.str();

	ifstream in(name.c_str());
	if (!in) {
		if (!check_and_create_config_dir())
			return;

		ofstream out(name.c_str());
		if (!out) {
			string msg = "Unable to open configuration file ";
			msg += name;
			QMessageBox::warning(this, 0, msg.c_str());
			return;
		}
		return;
	}

	// need checking on the key validity :(
	in >> event_cfgs[ctr];
}


bool oprof_start::save_event_config_file(uint ctr)
{
	if (!check_and_create_config_dir())
		return false;

	ostringstream ss;

	ss << get_user_filename(".oprofile/oprof_start_event");
	ss << "#" << ctr;

	string const name = ss.str();

	ofstream out(name.c_str());
	if (!out) {
		string msg = "Unable to save configuration file ";
		msg += name;
		QMessageBox::warning(this, 0, msg.c_str());
		return false;
	}

	out << event_cfgs[ctr];

	return true;
}


// user request a "normal" exit so save the config file.
void oprof_start::accept()
{
	// record the previous settings
	record_selected_event_config();

	for (uint ctr = 0 ; ctr < op_nr_counters ; ++ctr)
		save_event_config_file(ctr);

	save_config();

	QDialog::accept();
}


void oprof_start::closeEvent(QCloseEvent *)
{
	accept();
}


void oprof_start::timerEvent(QTimerEvent *)
{
	static time_t last = time(0);

	daemon_status dstat;

	flush_profiler_data_btn->setEnabled(dstat.running);
	stop_profiler_btn->setEnabled(dstat.running);
	start_profiler_btn->setEnabled(!dstat.running);

	if (!dstat.running) {
		daemon_label->setText("Profiler is not running.");
		return;
	}

	ostringstream ss;
	ss << "Profiler running ";
	ss << dstat.runtime;

	time_t curr = time(0);
	total_nr_interrupts += dstat.nr_interrupts;

	if (curr - last)
		ss << " (" << dstat.nr_interrupts / (curr - last) << " interrupts / second, total " << total_nr_interrupts << ")";

	daemon_label->setText(ss.str().c_str());

	last = curr;
}


void oprof_start::set_counter_combo(uint ctr)
{
	string ctrstr("Counter ");
	char c = '0' + ctr;
	ctrstr += c;
	ctrstr += string(": ");
	if (current_event[ctr])
		ctrstr += current_event[ctr]->name;
	else
		ctrstr += "not used";
	counter_combo->changeItem(ctrstr.c_str(), ctr);
	counter_combo->setMinimumSize(counter_combo->sizeHint());
}


void oprof_start::counter_selected(int ctr)
{
	setUpdatesEnabled(false);
	events_list->clear();

	record_selected_event_config();

	current_ctr = ctr;

	display_event(current_event[current_ctr]);

	QListViewItem * theitem = 0;

	for (vector<op_event_descr>::reverse_iterator cit = v_events.rbegin();
		cit != v_events.rend(); ++cit) {
		if (cit->counter_mask & (1 << ctr)) {
			QListViewItem * item = new QListViewItem(events_list, cit->name.c_str());
			if (current_event[ctr] != 0 && cit->name == current_event[ctr]->name)
				theitem = item;
		}
	}

	if (theitem) {
		events_list->setCurrentItem(theitem);
		events_list->ensureItemVisible(theitem);
	}

	enabled->setChecked(ctr_enabled[ctr]);

	setUpdatesEnabled(true);
	update();
}

void oprof_start::display_event(op_event_descr const * descrp)
{
	setUpdatesEnabled(false);

	if (!descrp) {
		os_ring_count_cb->setChecked(false);
		os_ring_count_cb->setEnabled(false);
		user_ring_count_cb->setChecked(false);
		user_ring_count_cb->setEnabled(false);
		event_count_edit->setText("");
		event_count_edit->setEnabled(false);
		hide_masks();
		return;
	}
	setup_unit_masks(*descrp);
	os_ring_count_cb->setEnabled(true);
	user_ring_count_cb->setEnabled(true);
	event_count_edit->setEnabled(true);
	persistent_config_t<event_setting> const & cfg = event_cfgs[current_ctr];

	os_ring_count_cb->setChecked(cfg[descrp->name].os_ring_count);
	user_ring_count_cb->setChecked(cfg[descrp->name].user_ring_count);
	QString count_text;
	count_text.setNum(cfg[descrp->name].count);
	event_count_edit->setText(count_text);
	event_count_validator->setRange(descrp->min_count, max_perf_count());

	event_help_label->setText(descrp->help_str.c_str());

	setUpdatesEnabled(true);
	update();
}


void oprof_start::event_selected(QListViewItem * item)
{
	op_event_descr const & descr = locate_event(item->text(0).latin1());

	record_selected_event_config();

	display_event(&descr);
	current_event[current_ctr] = &descr;

	set_counter_combo(current_ctr);
}


void oprof_start::event_over(QListViewItem * item)
{
	op_event_descr const & descr = locate_event(item->text(0).latin1());
	event_help_label->setText(descr.help_str.c_str());
}


void oprof_start::enabled_toggled(bool en)
{
	ctr_enabled[current_ctr] = en;
	if (!en) {
		events_list->clearSelection();
		current_event[current_ctr] = 0;
		set_counter_combo(current_ctr);
	}
	events_list->setEnabled(en);

	display_event(current_event[current_ctr]);
}


/// select the kernel image filename
void oprof_start::choose_kernel_filename()
{
	string name = kernel_filename_edit->text().latin1();
	string result = do_open_file_or_dir(name, false);

	if (!result.empty())
		kernel_filename_edit->setText(result.c_str());
}


// this record the current selected event setting in the event_cfg[] stuff.
// FIXME: need validation?
void oprof_start::record_selected_event_config()
{
	op_event_descr const * curr = current_event[current_ctr];

	if (!curr)
		return;

	persistent_config_t<event_setting> & cfg = event_cfgs[current_ctr];
	string name(curr->name);

	cfg[name].count = event_count_edit->text().toUInt();
	cfg[name].os_ring_count = os_ring_count_cb->isChecked();
	cfg[name].user_ring_count = user_ring_count_cb->isChecked();
	cfg[name].umask = get_unit_mask(*curr);
}


// validate and save the configuration (The qt validator installed
// are not sufficient to do the validation)
bool oprof_start::record_config()
{
	config.kernel_filename = kernel_filename_edit->text().latin1();
	config.no_kernel = no_vmlinux->isChecked();

	QString const t = buffer_size_edit->text();
	uint temp = t.toUInt();
	if (temp < OP_MIN_BUF_SIZE || temp > OP_MAX_BUF_SIZE) {
		ostringstream error;

		error << "buffer size out of range: " << temp
		      << " valid range is [" << OP_MIN_BUF_SIZE << ", "
		      << OP_MAX_BUF_SIZE << "]";

		QMessageBox::warning(this, 0, error.str().c_str());

		return false;
	}
	config.buffer_size = temp;

	temp = note_table_size_edit->text().toUInt();
	if (temp < OP_MIN_NOTE_TABLE_SIZE || temp > OP_MAX_NOTE_TABLE_SIZE) {
		ostringstream error;

		error << "note table size out of range: " << temp
		      << " valid range is [" << OP_MIN_NOTE_TABLE_SIZE << ", "
		      << OP_MAX_NOTE_TABLE_SIZE << "]";

		QMessageBox::warning(this, 0, error.str().c_str());

		return false;
	}
	config.note_table_size = temp;

	config.pid_filter = pid_filter_edit->text().toUInt();
	config.pgrp_filter = pgrp_filter_edit->text().toUInt();
	config.kernel_only = kernel_only_cb->isChecked();
	config.verbose = verbose->isChecked();
	config.separate_lib_samples = separate_lib_samples_cb->isChecked();
	config.separate_kernel_samples = separate_kernel_samples_cb->isChecked();

	return true;
}

void oprof_start::get_unit_mask_part(op_event_descr const & descr, uint num, bool selected, uint & mask)
{
	if (!selected)
		return;
	if  (num >= descr.unit->num)
		return;

	if (descr.unit->unit_type_mask == utm_bitmask)
		mask |= descr.unit->um[num].value;
	else
		mask = descr.unit->um[num].value;
}

// return the unit mask selected through the unit mask check box
uint oprof_start::get_unit_mask(op_event_descr const & descr)
{
	uint mask = 0;

	if (!descr.unit)
		return 0;

	// mandatory mask is transparent for user.
	if (descr.unit->unit_type_mask == utm_mandatory) {
		mask = descr.unit->default_mask;
		return mask;
	}

	get_unit_mask_part(descr, 0, check0->isChecked(), mask);
	get_unit_mask_part(descr, 1, check1->isChecked(), mask);
	get_unit_mask_part(descr, 2, check2->isChecked(), mask);
	get_unit_mask_part(descr, 3, check3->isChecked(), mask);
	get_unit_mask_part(descr, 4, check4->isChecked(), mask);
	get_unit_mask_part(descr, 5, check5->isChecked(), mask);
	get_unit_mask_part(descr, 6, check6->isChecked(), mask);
	get_unit_mask_part(descr, 7, check7->isChecked(), mask);
	get_unit_mask_part(descr, 8, check8->isChecked(), mask);
	get_unit_mask_part(descr, 9, check9->isChecked(), mask);
	get_unit_mask_part(descr, 10, check10->isChecked(), mask);
	get_unit_mask_part(descr, 11, check11->isChecked(), mask);
	get_unit_mask_part(descr, 12, check12->isChecked(), mask);
	get_unit_mask_part(descr, 13, check13->isChecked(), mask);
	get_unit_mask_part(descr, 14, check14->isChecked(), mask);
	get_unit_mask_part(descr, 15, check15->isChecked(), mask);
	return mask;
}

void oprof_start::hide_masks()
{
	check0->hide();
	check1->hide();
	check2->hide();
	check3->hide();
	check4->hide();
	check5->hide();
	check6->hide();
	check7->hide();
	check8->hide();
	check9->hide();
	check10->hide();
	check11->hide();
	check12->hide();
	check13->hide();
	check14->hide();
	check15->hide();
}

void oprof_start::setup_unit_masks(op_event_descr const & descr)
{
	op_unit_mask const * um = descr.unit;

	hide_masks();

	if (!um || um->unit_type_mask == utm_mandatory)
		return;

	persistent_config_t<event_setting> const & cfg = event_cfgs[current_ctr];

	unit_mask_group->setExclusive(um->unit_type_mask == utm_exclusive);

	for (size_t i = 0; i < um->num ; ++i) {
		QCheckBox * check = 0;
		switch (i) {
			case 0: check = check0; break;
			case 1: check = check1; break;
			case 2: check = check2; break;
			case 3: check = check3; break;
			case 4: check = check4; break;
			case 5: check = check5; break;
			case 6: check = check6; break;
			case 7: check = check7; break;
			case 8: check = check8; break;
			case 9: check = check9; break;
			case 10: check = check10; break;
			case 11: check = check11; break;
			case 12: check = check12; break;
			case 13: check = check13; break;
			case 14: check = check14; break;
			case 15: check = check15; break;
		}
		check->setText(um->um[i].desc);
		if (um->unit_type_mask == utm_exclusive) {
			check->setChecked(cfg[descr.name].umask == um->um[i].value);
		} else {
			// The last descriptor contains a mask that enable all
			// value so we must enable the last check box only if
			// all bits are on.
			if (i == um->num - 1) {
				check->setChecked(cfg[descr.name].umask == um->um[i].value);
			} else {
				check->setChecked(cfg[descr.name].umask & um->um[i].value);
			}
		}
		check->show();
	}
	unit_mask_group->setMinimumSize(unit_mask_group->sizeHint());
	setup_config_tab->setMinimumSize(setup_config_tab->sizeHint());
}

uint oprof_start::max_perf_count() const
{
	return cpu_type == CPU_RTC ? OP_MAX_RTC_COUNT : OP_MAX_PERF_COUNT;
}

void oprof_start::on_flush_profiler_data()
{
	vector<string> args;
	args.push_back("--dump");

	if (daemon_status().running)
		do_exec_command(OP_BINDIR "/opcontrol", args);
	else
		QMessageBox::warning(this, 0, "The profiler is not started.");
}

// user is happy of its setting.
void oprof_start::on_start_profiler()
{
	// save the current settings
	record_selected_event_config();

	uint c;
	for (c = 0; c < op_nr_counters; ++c) {
		if (ctr_enabled[c] && current_event[c])
			break;
	}
	if (c == op_nr_counters) {
		QMessageBox::warning(this, 0, "No counters enabled.\n");
		return;
	}

	for (uint ctr = 0; ctr < op_nr_counters; ++ctr) {
		if (!current_event[ctr])
			continue;
		if (!ctr_enabled[ctr])
			continue;

		persistent_config_t<event_setting> const & cfg = event_cfgs[ctr];

		op_event_descr const * descr = current_event[ctr];

		if (!cfg[descr->name].os_ring_count &&
		    !cfg[descr->name].user_ring_count) {
			QMessageBox::warning(this, 0, "You must select to "
					 "profile at least one of user binaries/kernel");
			return;
		}

		if (cfg[descr->name].count < descr->min_count ||
		    cfg[descr->name].count > max_perf_count()) {
			ostringstream out;

			out << "event " << descr->name << " count of range: "
			    << cfg[descr->name].count << " must be in [ "
			    << descr->min_count << ", "
			    << max_perf_count()
			    << "]";

			QMessageBox::warning(this, 0, out.str().c_str());
			return;
		}

		if (descr->unit &&
		    descr->unit->unit_type_mask == utm_bitmask &&
		    cfg[descr->name].umask == 0) {
			ostringstream out;

			out << "event " << descr->name<< " invalid unit mask: "
			    << cfg[descr->name].umask << endl;

			QMessageBox::warning(this, 0, out.str().c_str());
			return;
		}
	}

	if (daemon_status().running) {
		int user_choice =
			QMessageBox::warning(this, 0,
					     "Profiler already started:\n\n"
					     "stop and restart it?",
					     "&Restart", "&Cancel", 0, 0, 1);

		if (user_choice == 1)
			return;

		// this flush profiler data also.
		on_stop_profiler();
	}

	vector<string> args;

	// save_config validate the config
	if (!save_config())
		goto out;

	// now actually start
	args.push_back("--start");
	if (config.verbose)
		args.push_back("--verbose");
	do_exec_command(OP_BINDIR "/opcontrol", args);

out:
	total_nr_interrupts = 0;
	timerEvent(0);
}


bool oprof_start::save_config()
{
	if (!record_config())
		return false;

	vector<string> args;

	args.push_back("--setup");

	if (cpu_type == CPU_RTC) {
		persistent_config_t<event_setting> const & cfg = event_cfgs[0];
		op_event_descr const * descr = current_event[0];
		args.push_back("--rtc-value=" + tostr(cfg[descr->name].count));
	} else {
		bool one_enabled = false;

		vector<string> tmpargs;
		tmpargs.push_back("--setup");

		for (uint ctr = 0; ctr < op_nr_counters; ++ctr) {
			if (!current_event[ctr] || !ctr_enabled[ctr]) {
				tmpargs.push_back("--ctr" + tostr(ctr) + "-event=none");
				continue;
			}

			one_enabled = true;

			persistent_config_t<event_setting> const & cfg = event_cfgs[ctr];

			op_event_descr const * descr = current_event[ctr];

			tmpargs.push_back("--ctr" + tostr(ctr) + "-event=" + descr->name);
			tmpargs.push_back("--ctr" + tostr(ctr) + "-count=" + tostr(cfg[descr->name].count));
			tmpargs.push_back("--ctr" + tostr(ctr) + "-kernel=" + tostr(cfg[descr->name].os_ring_count));
			tmpargs.push_back("--ctr" + tostr(ctr) + "-user=" + tostr(cfg[descr->name].user_ring_count));

			if (descr->unit)
				tmpargs.push_back("--ctr" + tostr(ctr) + "-unit-mask=" + tostr(cfg[descr->name].umask));
		}

		// only set counters if at leat one is enabled
		if (one_enabled)
			args = tmpargs;
	}

	if (config.no_kernel) {
		args.push_back("--no-vmlinux");
	} else {
		args.push_back("--vmlinux=" + config.kernel_filename);
	}

	if (op_get_interface() == OP_INTERFACE_24) {
		args.push_back("--kernel-only=" + tostr(config.kernel_only));
		args.push_back("--pid-filter=" + tostr(config.pid_filter));
		args.push_back("--pgrp-filter=" + tostr(config.pgrp_filter));
		args.push_back("--buffer-size=" + tostr(config.buffer_size));
		args.push_back("--note-table-size=" +
			       tostr(config.note_table_size));
	}
	// opcontrol allow multiple setting of --separate option
	if (config.separate_lib_samples)
		args.push_back("--separate=library");
	else if (config.separate_kernel_samples)
		args.push_back("--separate=kernel");
	else
		args.push_back("--separate=none");

	// 2.95 work-around, it didn't like return !do_exec_command() 
	bool ret = !do_exec_command(OP_BINDIR "/opcontrol", args);
	return ret;
}


// flush and stop the profiler if it was started.
void oprof_start::on_stop_profiler()
{
	vector<string> args;
	args.push_back("--shutdown");

	if (daemon_status().running)
		do_exec_command(OP_BINDIR "/opcontrol", args);
	else
		QMessageBox::warning(this, 0, "The profiler is already stopped.");

	timerEvent(0);
}


/// function object for matching against name
class event_name_eq : public unary_function<op_event_descr, bool> {
	string name_;
public:
	explicit event_name_eq(string const & s) : name_(s) {}
	bool operator()(op_event_descr & d) const {
		return d.name == name_;
	}
};

// helper to retrieve an event descr through its name.
op_event_descr const & oprof_start::locate_event(string const & name)
{
	return *(find_if(v_events.begin(), v_events.end(), event_name_eq(name)));
}
