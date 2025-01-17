/*
 * Copyright 1987, 1988, 1989 by Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose is hereby granted, provided that
 * the names of M.I.T. and the M.I.T. S.I.P.B. not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  M.I.T. and the
 * M.I.T. S.I.P.B. make no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#include "config.h"
#ifdef HAS_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include "ss_internal.h"
#include <stdio.h>

static int check_request_table PROTOTYPE((ss_request_table *rqtbl, int argc,
					  char *argv[], int sci_idx));
static int really_execute_command PROTOTYPE((int sci_idx, int argc,
					     char **argv[]));

/*
 * get_request(tbl, idx)
 *
 * Function:
 *      Gets the idx'th request from the request table pointed to
 *      by tbl.
 * Arguments:
 *      tbl (ss_request_table *)
 *              pointer to request table
 *      idx (int)
 *              index into table
 * Returns:
 *      (ss_request_entry *)
 *              pointer to request table entry
 * Notes:
 *      Has been replaced by a macro.
 */

#ifdef __SABER__
/* sigh.  saber won't deal with pointer-to-const-struct */
static struct _ss_request_entry * get_request (tbl, idx)
    ss_request_table * tbl;
    int idx;
{
    struct _ss_request_table *tbl1 = (struct _ss_request_table *) tbl;
    struct _ss_request_entry *e = (struct _ss_request_entry *) tbl1->requests;
    return e + idx;
}
#else
#define get_request(tbl,idx)    ((tbl) -> requests + (idx))
#endif

/*
 * check_request_table(rqtbl, argc, argv, sci_idx)
 *
 * Function:
 *      If the command string in argv[0] is in the request table, execute
 *      the commands and return error code 0.  Otherwise, return error
 *      code ss_et_command_not_found.
 * Arguments:
 *      rqtbl (ss_request_table *)
 *              pointer to request table
 *      argc (int)
 *              number of elements in argv[]
 *      argv (char *[])
 *              argument string array
 *      sci_idx (int)
 *              ss-internal index for subsystem control info structure
 * Returns:
 *      (int)
 *              zero if command found, ss_et_command_not_found otherwise
 * Notes:
 */

static int check_request_table(register ss_request_table *rqtbl, int argc,
			       char *argv[], int sci_idx)
{
#ifdef __SABER__
    struct _ss_request_entry *request;
#else
    register ss_request_entry *request;
#endif
    register ss_data *info;
    register char const * const * name;
    char *string = argv[0];
    int i;

    info = ss_info(sci_idx);
    info->argc = argc;
    info->argv = argv;
    for (i = 0; (request = get_request(rqtbl, i))->command_names; i++) {
	for (name = request->command_names; *name; name++)
	    if (!strcmp(*name, string)) {
		info->current_request = request->command_names[0];
		(request->function)(argc, (const char *const *) argv,
				    sci_idx,info->info_ptr);
		info->current_request = (char *)NULL;
		return(0);
	    }
    }
    return(SS_ET_COMMAND_NOT_FOUND);
}

/*
 * really_execute_command(sci_idx, argc, argv)
 *
 * Function:
 *      Fills in the argc, argv values in the subsystem entry and
 *      call the appropriate routine.
 * Arguments:
 *      sci_idx (int)
 *              ss-internal index for subsystem control info structure
 *      argc (int)
 *              number of arguments in argument list
 *      argv (char **[])
 *              pointer to parsed argument list (may be reallocated
 *              on abbrev expansion)
 *
 * Returns:
 *      (int)
 *              Zero if successful, ss_et_command_not_found otherwise.
 * Notes:
 */

static int really_execute_command(int sci_idx, int argc, char **argv[])
{
    register ss_request_table **rqtbl;
    register ss_data *info;

    info = ss_info(sci_idx);

    for (rqtbl = info->rqt_tables; *rqtbl; rqtbl++) {
        if (check_request_table (*rqtbl, argc, *argv, sci_idx) == 0)
            return(0);
    }
    return(SS_ET_COMMAND_NOT_FOUND);
}

/*
 * ss_execute_command(sci_idx, argv)
 *
 * Function:
 *	Executes a parsed command list within the subsystem.
 * Arguments:
 *	sci_idx (int)
 *		ss-internal index for subsystem control info structure
 *	argv (char *[])
 *		parsed argument list
 * Returns:
 *	(int)
 *		Zero if successful, ss_et_command_not_found otherwise.
 * Notes:
 */

int ss_execute_command(int sci_idx, register char *argv[])
{
	register int i, argc;
	char **argp;

	argc = 0;
	for (argp = argv; *argp; argp++)
		argc++;
	argp = (char **)malloc((argc+1)*sizeof(char *));
	if (!argp)
		return(ENOMEM);
	for (i = 0; i <= argc; i++)
		argp[i] = argv[i];
	i = really_execute_command(sci_idx, argc, &argp);
	free(argp);
	return(i);
}

/*
 * ss_execute_line(sci_idx, line_ptr)
 *
 * Function:
 *      Parses and executes a command line within a subsystem.
 * Arguments:
 *      sci_idx (int)
 *              ss-internal index for subsystem control info structure
 *      line_ptr (char *)
 *              Pointer to command line to be parsed.
 * Returns:
 *      (int)
 *      	Error code.
 * Notes:
 */

int ss_execute_line(int sci_idx, char *line_ptr)
{
    char **argv;
    int argc, ret;

    /* flush leading whitespace */
    while (line_ptr[0] == ' ' || line_ptr[0] == '\t')
        line_ptr++;

    /* check if it should be sent to operating system for execution */
    if (*line_ptr == '!') {
        if (ss_info(sci_idx)->flags.escape_disabled)
            return SS_ET_ESCAPE_DISABLED;
        else {
            line_ptr++;
            return (system(line_ptr) < 0) ? errno : 0;
        }
    }

    /* parse it */
    argv = ss_parse(sci_idx, line_ptr, &argc);
    if (argc == 0) {
	free(argv);
        return 0;
    }

    /* look it up in the request tables, execute if found */
    ret = really_execute_command (sci_idx, argc, &argv);

    free(argv);

    return(ret);
}

void ss_set_exit_status(int sci_idx, int code)
{
	ss_info(sci_idx)->exit_status = code;
}

int ss_get_exit_status(int sci_idx)
{
	return ss_info(sci_idx)->exit_status;
}
