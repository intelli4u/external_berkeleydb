/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1996-2001
 *	Sleepycat Software.  All rights reserved.
 */

#include "db_config.h"

#ifndef lint
static const char copyright[] =
    "Copyright (c) 1996-2001\nSleepycat Software Inc.  All rights reserved.\n";
static const char revid[] =
    "$Id: db_recover.c,v 11.26 2001/09/07 13:31:18 bostic Exp $";
#endif

#ifndef NO_SYSTEM_INCLUDES
#include <sys/types.h>

#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#endif

#include "db_int.h"
#include "txn.h"
#include "common_ext.h"
#include "clib_ext.h"

int db_recover_main __P((int, char *[]));
int db_recover_read_timestamp __P((const char *, char *, time_t *));
int db_recover_usage __P((void));
int db_recover_version_check __P((const char *));

int
db_recover(args)
	char *args;
{
	int argc;
	char **argv;

	__db_util_arg("db_recover", args, &argc, &argv);
	return (db_recover_main(argc, argv) ? EXIT_FAILURE : EXIT_SUCCESS);
}

#include <stdio.h>
#define	ERROR_RETURN	ERROR

int
db_recover_main(argc, argv)
	int argc;
	char *argv[];
{
	extern char *optarg;
	extern int optind, __db_getopt_reset;
	const char *progname = "db_recover";
	DB_ENV	*dbenv;
	DB_TXNREGION *region;
	time_t now, timestamp;
	u_int32_t flags;
	int ch, exitval, fatal_recover, ret, retain_env, verbose;
	char *home;

	if ((ret = db_recover_version_check(progname)) != 0)
		return (ret);

	home = NULL;
	timestamp = 0;
	exitval = fatal_recover = retain_env = verbose = 0;
	__db_getopt_reset = 1;
	while ((ch = getopt(argc, argv, "ceh:t:Vv")) != EOF)
		switch (ch) {
		case 'c':
			fatal_recover = 1;
			break;
		case 'e':
			retain_env = 1;
			break;
		case 'h':
			home = optarg;
			break;
		case 't':
			if ((ret =
			    db_recover_read_timestamp(progname, optarg, &timestamp)) != 0)
				return (ret);
			break;
		case 'V':
			printf("%s\n", db_version(NULL, NULL, NULL));
			return (EXIT_SUCCESS);
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			return (db_recover_usage());
		}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		return (db_recover_usage());

	/* Handle possible interruptions. */
	__db_util_siginit();

	/*
	 * Create an environment object and initialize it for error
	 * reporting.
	 */
	if ((ret = db_env_create(&dbenv, 0)) != 0) {
		fprintf(stderr,
		    "%s: db_env_create: %s\n", progname, db_strerror(ret));
		return (EXIT_FAILURE);
	}
	dbenv->set_errfile(dbenv, stderr);
	dbenv->set_errpfx(dbenv, progname);
	if (verbose) {
		(void)dbenv->set_verbose(dbenv, DB_VERB_RECOVERY, 1);
		(void)dbenv->set_verbose(dbenv, DB_VERB_CHKPOINT, 1);
	}
	if (timestamp &&
	    (ret = dbenv->set_tx_timestamp(dbenv, &timestamp)) != 0) {
		dbenv->err(dbenv, ret, "DB_ENV->set_timestamp");
		goto shutdown;
	}

	/*
	 * Initialize the environment -- we don't actually do anything
	 * else, that all that's needed to run recovery.
	 *
	 * Note that unless the caller specified the -e option, we use a
	 * private environment, as we're about to create a region, and we
	 * don't want to to leave it around.  If we leave the region around,
	 * the application that should create it will simply join it instead,
	 * and will then be running with incorrectly sized (and probably
	 * terribly small) caches.  Applications that use -e should almost
	 * certainly use DB_CONFIG files in the directory.
	 */
	flags = 0;
	LF_SET(DB_CREATE | DB_INIT_LOCK | DB_INIT_LOG |
	    DB_INIT_MPOOL | DB_INIT_TXN | DB_USE_ENVIRON);
	LF_SET(fatal_recover ? DB_RECOVER_FATAL : DB_RECOVER);
	LF_SET(retain_env ? 0 : DB_PRIVATE);
	if ((ret = dbenv->open(dbenv, home, flags, 0)) != 0) {
		dbenv->err(dbenv, ret, "DB_ENV->open");
		goto shutdown;
	}

	if (verbose) {
		(void)time(&now);
		region = ((DB_TXNMGR *)dbenv->tx_handle)->reginfo.primary;
		dbenv->errx(dbenv, "Recovery complete at %.24s", ctime(&now));
		dbenv->errx(dbenv, "%s %lx %s [%lu][%lu]",
		    "Maximum transaction id", (u_long)region->last_txnid,
		    "Recovery checkpoint", (u_long)region->last_ckp.file,
		    (u_long)region->last_ckp.offset);
	}

	if (0) {
shutdown:	exitval = 1;
	}

	/* Clean up the environment. */
	if ((ret = dbenv->close(dbenv, 0)) != 0) {
		exitval = 1;
		fprintf(stderr,
		    "%s: dbenv->close: %s\n", progname, db_strerror(ret));
	}

	/* Resend any caught signal. */
	__db_util_sigresend();

	return (exitval == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

#define	ATOI2(ar)	((ar)[0] - '0') * 10 + ((ar)[1] - '0'); (ar) += 2;

/*
 * read_timestamp --
 *	Convert a time argument to Epoch seconds.
 *
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
int
db_recover_read_timestamp(progname, arg, timep)
	const char *progname;
	char *arg;
	time_t *timep;
{
	struct tm *t;
	time_t now;
	int yearset;
	char *p;
					/* Start with the current time. */
	(void)time(&now);
	if ((t = localtime(&now)) == NULL) {
		fprintf(stderr,
		    "%s: localtime: %s\n", progname, strerror(errno));
		return (EXIT_FAILURE);
	}
					/* [[CC]YY]MMDDhhmm[.SS] */
	if ((p = strchr(arg, '.')) == NULL)
		t->tm_sec = 0;		/* Seconds defaults to 0. */
	else {
		if (strlen(p + 1) != 2)
			goto terr;
		*p++ = '\0';
		t->tm_sec = ATOI2(p);
	}

	yearset = 0;
	switch(strlen(arg)) {
	case 12:			/* CCYYMMDDhhmm */
		t->tm_year = ATOI2(arg);
		t->tm_year *= 100;
		yearset = 1;
		/* FALLTHROUGH */
	case 10:			/* YYMMDDhhmm */
		if (yearset) {
			yearset = ATOI2(arg);
			t->tm_year += yearset;
		} else {
			yearset = ATOI2(arg);
			if (yearset < 69)
				t->tm_year = yearset + 2000;
			else
				t->tm_year = yearset + 1900;
		}
		t->tm_year -= 1900;	/* Convert to UNIX time. */
		/* FALLTHROUGH */
	case 8:				/* MMDDhhmm */
		t->tm_mon = ATOI2(arg);
		--t->tm_mon;		/* Convert from 01-12 to 00-11 */
		t->tm_mday = ATOI2(arg);
		t->tm_hour = ATOI2(arg);
		t->tm_min = ATOI2(arg);
		break;
	default:
		goto terr;
	}

	t->tm_isdst = -1;		/* Figure out DST. */

	*timep = mktime(t);
	if (*timep == -1) {
terr:		fprintf(stderr,
	"%s: out of range or illegal time specification: [[CC]YY]MMDDhhmm[.SS]",
		    progname);
		return (EXIT_FAILURE);
	}
	return (0);
}

int
db_recover_usage()
{
	(void)fprintf(stderr,
	    "usage: db_recover [-ceVv] [-h home] [-t [[CC]YY]MMDDhhmm[.SS]]\n");
	return (EXIT_FAILURE);
}

int
db_recover_version_check(progname)
	const char *progname;
{
	int v_major, v_minor, v_patch;

	/* Make sure we're loaded with the right version of the DB library. */
	(void)db_version(&v_major, &v_minor, &v_patch);
	if (v_major != DB_VERSION_MAJOR ||
	    v_minor != DB_VERSION_MINOR || v_patch != DB_VERSION_PATCH) {
		fprintf(stderr,
	"%s: version %d.%d.%d doesn't match library version %d.%d.%d\n",
		    progname, DB_VERSION_MAJOR, DB_VERSION_MINOR,
		    DB_VERSION_PATCH, v_major, v_minor, v_patch);
		return (EXIT_FAILURE);
	}
	return (0);
}