/* mount - mount a file system		Author: Andy Tanenbaum */

#include <errno.h>
#include <sys/types.h>
#include <limits.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>
#include <fcntl.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/minlib.h>
#include <sys/svrctl.h>
#include <stdio.h>
#include "mfs/const.h"
#include <fstab.h>

#define MINIX_FS_TYPE "mfs"

int main(int argc, char **argv);
void list(void);
void usage(void);
void update_mtab(char *dev, char *mountpoint, char *fstype, int mountflags);
int mount_all(void);

static int write_mtab = 1;

int main(argc, argv)
int argc;
char *argv[];
{
  int all = 0, i, v = 0, mountflags;
  char **ap, *opt, *err, *type, *args, *device;

  if (argc == 1) list();	/* just list /etc/mtab */
  mountflags = 0;
  type = NULL;
  args = NULL;
  ap = argv+1;
  for (i = 1; i < argc; i++) {
	if (argv[i][0] == '-') {
		opt = argv[i]+1;
		while (*opt != 0) switch (*opt++) {
		case 'r':	mountflags |= MS_RDONLY;	break;
		case 't':	if (++i == argc) usage();
				type = argv[i];
				break;
		case 'i':	mountflags |= MS_REUSE;		break;
		case 'e':	mountflags |= MS_EXISTING;		break;
		case 'n':	write_mtab = 0;			break;
		case 'o':	if (++i == argc) usage();
				args = argv[i];
				break;
		case 'a':	all = 1; break;
		default:	usage();
		}
	} else {
		*ap++ = argv[i];
	}
  }
  *ap = NULL;
  argc = (ap - argv);

  if (!all && (argc != 3 || *argv[1] == 0)) usage();
  if (all == 1) {
	return mount_all();
  }

  device = argv[1];
  if (!strcmp(device, "none")) device = NULL;

  if ((type == NULL || !strcmp(type, MINIX_FS_TYPE)) && device != NULL) {
	/* auto-detect type and/or version */
	v = fsversion(device, "mount");
	switch (v) {
		case FSVERSION_MFS1:
		case FSVERSION_MFS2: 
		case FSVERSION_MFS3: type = MINIX_FS_TYPE; break;		
		case FSVERSION_EXT2: type = "ext2"; break;
	}
  }
  
  if (mount(device, argv[2], mountflags, type, args) < 0) {
	err = strerror(errno);
	fprintf(stderr, "mount: Can't mount %s on %s: %s\n",
		argv[1], argv[2], err);
	return(EXIT_FAILURE);
  }

  /* The mount has completed successfully. Tell the user. */
  printf("%s is read-%s mounted on %s\n",
	argv[1], mountflags & MS_RDONLY ? "only" : "write", argv[2]);
  
  /* Update /etc/mtab. */
  update_mtab(argv[1], argv[2], type, mountflags);
  return(EXIT_SUCCESS);
}

void
update_mtab(char *dev, char *mountpoint, char *fstype, int mountflags)
{
	int n;
	char *vs;
	char special[PATH_MAX], mounted_on[PATH_MAX], version[10], rw_flag[10];

	if (!write_mtab) return;
	n = load_mtab("mount");
	if (n < 0) exit(1);		/* something is wrong. */

	/* Loop on all the /etc/mtab entries, copying each one to the output
	 * buf. */
	while (1) {
		n = get_mtab_entry(special, mounted_on, version, rw_flag);
		if (n < 0) break;
		n = put_mtab_entry(special, mounted_on, version, rw_flag);
		if (n < 0) {
			std_err("mount: /etc/mtab has grown too large\n");
			exit(1);
		}
	}
	/* For MFS, use a version number. Otherwise, use the FS type name. */
	if (!strcmp(fstype, MINIX_FS_TYPE)) {
		vs = "MFSv3";
	} else if (strlen(fstype) < sizeof(version)) {
		vs = fstype;
	} else {
		vs = "-";
	}
	n = put_mtab_entry(dev, mountpoint, vs,
			     (mountflags & MS_RDONLY ? "ro" : "rw") );
	if (n < 0) {
		std_err("mount: /etc/mtab has grown too large\n");
		exit(1);
	}

	n = rewrite_mtab("mount");
}

void list()
{
  int n;
  char special[PATH_MAX], mounted_on[PATH_MAX], version[10], rw_flag[10];

  /* Read and print /etc/mtab. */
  n = load_mtab("mount");
  if (n < 0) exit(1);

  while (1) {
	n = get_mtab_entry(special, mounted_on, version, rw_flag);
	if  (n < 0) break;
	printf("%s is read-%s mounted on %s (type %s)\n",
		special, strcmp(rw_flag, "rw") == 0 ? "write" : "only",
		mounted_on, version);
  }
  exit(0);
}

int
has_opt(char *mntopts, char *option)
{
	char *optbuf, *opt;
	int found = 0;

	optbuf = strdup(mntopts);
	for (opt = optbuf; (opt = strtok(opt, ",")) != NULL; opt = NULL) {
		if (!strcmp(opt, option)) found = 1;
	}
	free (optbuf);
	return(found);
}


int
mount_all()
{
	struct fstab *fs;
	int ro, mountflags;
	char mountpoint[PATH_MAX];
  	char *device, *err;

	while ((fs = getfsent()) != NULL) {
		ro = 0;
		mountflags = 0;
		device = NULL;
		if (realpath(fs->fs_file, mountpoint) == NULL) {
			fprintf(stderr, "Can't mount on %s\n", fs->fs_file);
			return(EXIT_FAILURE);
		}
		if (has_opt(fs->fs_mntops, "noauto"))
			continue;
		if (!strcmp(mountpoint, "/"))
			continue; /* Not remounting root */
		if (has_opt(fs->fs_mntops, "ro"))
			ro = 1;
		if (ro) {
			mountflags |= MS_RDONLY;
		}

		device = fs->fs_spec;
		/* passing a null string for block special device means don't 
		 * use a device at all and this is what we need to do for 
		 * entries starting with "none"
		 */
		if (!strcmp(device, "none")) 
			device = NULL;

		if (mount(device, mountpoint, mountflags, fs->fs_vfstype,
			fs->fs_mntops) == 0) {
			update_mtab(fs->fs_spec, fs->fs_file, fs->fs_vfstype,
					mountflags);
		} else {
			err = strerror(errno);
			fprintf(stderr, "mount: Can't mount %s on %s: %s\n",
				fs->fs_spec, fs->fs_file, err);
			return(EXIT_FAILURE);
		}
	}
	return(EXIT_SUCCESS);
}

void usage()
{
  std_err("Usage: mount [-a] [-r] [-e] [-t type] [-o options] special name\n");
  exit(1);
}
