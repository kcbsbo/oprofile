/**
 * @file daemon/opd_mangling.c
 * Mangling and opening of sample files
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 */

#include <sys/types.h>
 
#include "opd_mangling.h"
#include "opd_kernel.h"
#include "opd_cookie.h"
#include "opd_sfile.h"
#include "opd_printf.h"
#include "opd_util.h"

#include "op_file.h"
#include "op_sample_file.h"
#include "op_config.h"
#include "op_cpu_type.h"
#include "op_mangle.h"
#include "op_events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


static char const * get_dep_name(struct sfile const * sf)
{
	/* don't add a useless depname */
	if (sf->cookie == sf->app_cookie)
		return NULL;

	if (!separate_kernel && !(separate_lib && !sf->kernel))
		return NULL;

	/* this will fail if e.g. kernel thread */
	if (sf->app_cookie == 0)
		return NULL;

	return find_cookie(sf->app_cookie);
}


static struct opd_event * find_event(unsigned long counter)
{
	size_t i;

	for (i = 0; opd_events[i].name; ++i) {
		if (counter == opd_events[i].counter)
			return &opd_events[i];
	}

	fprintf(stderr, "Unknown event for counter %lu\n", counter);
	abort();
	return NULL;
}


static char * mangle_filename(struct sfile const * sf, int counter)
{
	char * mangled;
	struct mangle_values values;
	struct opd_event * event = find_event(counter);

	values.flags = 0;
	if (sf->kernel) {
		values.image_name = sf->kernel->name;
		values.flags |= MANGLE_KERNEL;
	} else {
		values.image_name = find_cookie(sf->cookie);
	}

	/* FIXME: log */
	if (!values.image_name)
		return NULL;

	values.dep_name = get_dep_name(sf);
	if (values.dep_name)
		values.flags |= MANGLE_DEP_NAME;

	if (separate_thread) {
		values.flags |= MANGLE_TGID | MANGLE_TID;
		values.tid = sf->tid;
		values.tgid = sf->tgid;
	}
 
	if (separate_cpu) {
		values.flags |= MANGLE_CPU;
		values.cpu = sf->cpu;
	}

	values.event_name = event->name;
	values.count = event->count;
	values.unit_mask = event->um;

	mangled = op_mangle_filename(&values);

	return mangled;
}


int opd_open_sample_file(struct sfile * sf, int counter)
{
	char * mangled;
	samples_odb_t * file;
	struct opd_header * header;
	struct opd_event * event = find_event(counter);
	char const * binary;
	int err;

	file = &sf->files[counter];

	mangled = mangle_filename(sf, counter);

	if (!mangled)
		return EINVAL;

	verbprintf("Opening \"%s\"\n", mangled);

	create_path(mangled);

	sfile_get(sf);

retry:
	err = odb_open(file, mangled, ODB_RDWR, sizeof(struct opd_header));

	/* This can naturally happen when racing against opcontrol --reset. */
	if (err) {
		if (err == EMFILE) {
			if (sfile_lru_clear()) {
				printf("LRU cleared but odb_open() fails for %s.\n", mangled);
				abort();
			}
			goto retry;
		}

		fprintf(stderr, "oprofiled: open of %s failed: %s\n",
		        mangled, strerror(err));
		goto out;
	}

	header = file->base_memory;

	if (!sf->kernel)
		binary = find_cookie(sf->cookie);
	else
		binary = sf->kernel->name;

	memset(header, '\0', sizeof(struct opd_header));
	header->version = OPD_VERSION;
	memcpy(header->magic, OPD_MAGIC, sizeof(header->magic));
	header->is_kernel = !!sf->kernel;
	header->ctr_event = event->value;
	header->ctr_count = event->count;
	header->ctr_um = event->um;
	header->ctr = counter;
	header->cpu_type = cpu_type;
	header->cpu_speed = cpu_speed;
	header->mtime = binary ? op_get_mtime(binary) : 0;
	header->separate_lib = separate_lib;
	header->separate_kernel = separate_kernel;
	/* FIXME: separate_thread/cpu ? */

out:
	sfile_put(sf);
	free(mangled);
	return err;
}