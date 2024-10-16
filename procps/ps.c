/*
 * Mini ps implementation for busybox
 *
 *
 * Copyright (C) 1999 by Lineo, inc.
 * Written by Erik Andersen <andersen@lineo.com>, <andersee@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "internal.h"
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>


typedef struct proc_s {
    char
    	cmd[16];	/* basename of executable file in call to exec(2) */
    int
        ruid, rgid,     /* real only (sorry) */
    	pid,		/* process id */
    	ppid;		/* pid of parent process */
    char
    	state;		/* single-char code for process state (S=sleeping) */
} proc_t;



static int file2str(char *filename, char *ret, int cap) 
{
    int fd, num_read;

    if ( (fd       = open(filename, O_RDONLY, 0)) == -1 ) return -1;
    if ( (num_read = read(fd, ret, cap - 1))      <= 0 ) return -1;
    ret[num_read] = 0;
    close(fd);
    return num_read;
}


static void parse_proc_status(char* S, proc_t* P) 
{
    char* tmp;
    memset(P->cmd, 0, sizeof P->cmd);
    sscanf (S, "Name:\t%15c", P->cmd);
    tmp = strchr(P->cmd,'\n');
    if (tmp)
	*tmp='\0';
    tmp = strstr (S,"State");
    sscanf (tmp, "State:\t%c", &P->state);

    tmp = strstr (S,"Pid:");
    if(tmp) sscanf (tmp,
        "Pid:\t%d\n"
        "PPid:\t%d\n",
        &P->pid,
        &P->ppid
    );
    else fprintf(stderr, "Internal error!\n");

    /* For busybox, ignoring effecting, saved, etc */
    tmp = strstr (S,"Uid:");
    if(tmp) sscanf (tmp,
        "Uid:\t%d", &P->ruid);
    else fprintf(stderr, "Internal error!\n");

    tmp = strstr (S,"Gid:");
    if(tmp) sscanf (tmp,
        "Gid:\t%d", &P->rgid);
    else fprintf(stderr, "Internal error!\n");

}


extern int ps_main(int argc, char **argv)
{
    proc_t p;
    DIR *dir;
    FILE *file;
    struct dirent *entry;
    char path[32], sbuf[512];
    char uidName[10]="";
    char groupName[10]="";
    int i, c;

    if ( argc>1 && **(argv+1) == '-' ) {
	usage ("ps - report process status\nThis version of ps accepts no options.\n");
    }
    
    dir = opendir("/proc");
    if (!dir) {
	perror("Can't open /proc");
	exit(FALSE);
    }

    fprintf(stdout, "%5s  %-8s %-3s %5s %s\n", "PID", "Uid", "Gid", "State", "Command");
    while ((entry = readdir(dir)) != NULL) {
	uidName[0]='\0';
	groupName[0]='\0';
		
	if (! isdigit(*entry->d_name))
	    continue;
	sprintf(path, "/proc/%s/status", entry->d_name);
	if ((file2str(path, sbuf, sizeof sbuf)) != -1 ) {
	    parse_proc_status(sbuf, &p);
	}

	/* Make some adjustments as needed */
	my_getpwuid( uidName, p.ruid);
	my_getgrgid( groupName, p.rgid);
	if (*uidName == '\0')
	    sprintf( uidName, "%d", p.ruid);
	if (*groupName == '\0')
	    sprintf( groupName, "%d", p.rgid);

	fprintf(stdout, "%5d %-8s %-8s %c ", p.pid, uidName, groupName, p.state);
	sprintf(path, "/proc/%s/cmdline", entry->d_name);
	file = fopen(path, "r");
	if (file == NULL) {
	    perror(path);
	    exit(FALSE);
	}
	i=0;
	while (((c = getc(file)) != EOF) && (i < 53)) {
	    i++;
	    if (c == '\0')
		c = ' ';
	    putc(c, stdout);
	}
	if (i==0)
	    fprintf(stdout, "%s", p.cmd);
	fprintf(stdout, "\n");
    }
    closedir(dir);
    exit(TRUE);
}
