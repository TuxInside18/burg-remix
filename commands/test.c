/* test.c -- The test command..  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2007  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/fs.h>
#include <grub/device.h>
#include <grub/file.h>
#include <grub/command.h>

/* A simple implementation for signed numbers. */
static int
grub_strtosl (char *arg, char **end, int base)
{
  if (arg[0] == '-')
    return -grub_strtoul (arg + 1, end, base);
  return grub_strtoul (arg, end, base);
}

struct test_parse_closure
{
  int ret;
  int discard;
  int invert;
  int file_exists;
  struct grub_dirhook_info file_info;
};

/* Take care of discarding and inverting. */
static void
update_val (int val, struct test_parse_closure *c)
{
  if (! c->discard)
    c->ret = c->invert ? ! val : val;
  c->invert = c->discard = 0;
}

struct get_fileinfo_closure
{
  char *filename;
  struct test_parse_closure *c;
};

/* A hook for iterating directories. */
static int
find_file (const char *cur_filename,
	   const struct grub_dirhook_info *info,
	   void *closure)
{
  struct get_fileinfo_closure *c = closure;

  if ((info->case_insensitive ? grub_strcasecmp (cur_filename, c->filename)
       : grub_strcmp (cur_filename, c->filename)) == 0)
    {
      c->c->file_info = *info;
      c->c->file_exists = 1;
      return 1;
    }
  return 0;
}

/* Check if file exists and fetch its information. */
static void
get_fileinfo (char *path, struct test_parse_closure *c)
{
  char *filename, *pathname;
  char *device_name;
  grub_fs_t fs;
  grub_device_t dev;

  c->file_exists = 0;
  device_name = grub_file_get_device_name (path);
  dev = grub_device_open (device_name);
  if (! dev)
    {
      grub_free (device_name);
      return;
    }

  fs = grub_fs_probe (dev);
  if (! fs)
    {
      grub_free (device_name);
      grub_device_close (dev);
      return;
    }

  pathname = grub_strchr (path, ')');
  if (! pathname)
    pathname = path;
  else
    pathname++;

  /* Remove trailing '/'. */
  while (*pathname && pathname[grub_strlen (pathname) - 1] == '/')
    pathname[grub_strlen (pathname) - 1] = 0;

  /* Split into path and filename. */
  filename = grub_strrchr (pathname, '/');
  if (! filename)
    {
      path = grub_strdup ("/");
      filename = pathname;
    }
  else
    {
      filename++;
      path = grub_strdup (pathname);
      path[filename - pathname] = 0;
    }

  /* It's the whole device. */
  if (! *pathname)
    {
      c->file_exists = 1;
      grub_memset (&c->file_info, 0, sizeof (c->file_info));
      /* Root is always a directory. */
      c->file_info.dir = 1;

      /* Fetch writing time. */
      c->file_info.mtimeset = 0;
      if (fs->mtime)
	{
	  if (! fs->mtime (dev, &c->file_info.mtime))
	    c->file_info.mtimeset = 1;
	  grub_errno = GRUB_ERR_NONE;
	}
    }
  else
    {
      struct get_fileinfo_closure cc;

      cc.filename = filename;
      cc.c = c;
      (fs->dir) (dev, path, find_file, &cc);
    }

  grub_device_close (dev);
  grub_free (path);
  grub_free (device_name);
}

/* Parse a test expression starting from *argn. */
static int
test_parse (char **args, int *argn, int argc)
{
  struct test_parse_closure c;

  c.ret = 0;
  c.discard = 0;
  c.invert = 0;

  /* Here we have the real parsing. */
  while (*argn < argc)
    {
      /* First try 3 argument tests. */
      if (*argn + 2 < argc)
	{
	  /* String tests. */
	  if (grub_strcmp (args[*argn + 1], "=") == 0
	      || grub_strcmp (args[*argn + 1], "==") == 0)
	    {
	      update_val (grub_strcmp (args[*argn], args[*argn + 2]) == 0, &c);
	      (*argn) += 3;
	      continue;
	    }

	  if (grub_strcmp (args[*argn + 1], "!=") == 0)
	    {
	      update_val (grub_strcmp (args[*argn], args[*argn + 2]) != 0, &c);
	      (*argn) += 3;
	      continue;
	    }

	  /* GRUB extension: lexicographical sorting. */
	  if (grub_strcmp (args[*argn + 1], "<") == 0)
	    {
	      update_val (grub_strcmp (args[*argn], args[*argn + 2]) < 0, &c);
	      (*argn) += 3;
	      continue;
	    }

	  if (grub_strcmp (args[*argn + 1], "<=") == 0)
	    {
	      update_val (grub_strcmp (args[*argn], args[*argn + 2]) <= 0, &c);
	      (*argn) += 3;
	      continue;
	    }

	  if (grub_strcmp (args[*argn + 1], ">") == 0)
	    {
	      update_val (grub_strcmp (args[*argn], args[*argn + 2]) > 0, &c);
	      (*argn) += 3;
	      continue;
	    }

	  if (grub_strcmp (args[*argn + 1], ">=") == 0)
	    {
	      update_val (grub_strcmp (args[*argn], args[*argn + 2]) >= 0, &c);
	      (*argn) += 3;
	      continue;
	    }

	  /* Number tests. */
	  if (grub_strcmp (args[*argn + 1], "-eq") == 0)
	    {
	      update_val (grub_strtosl (args[*argn], 0, 0)
			  == grub_strtosl (args[*argn + 2], 0, 0), &c);
	      (*argn) += 3;
	      continue;
	    }

	  if (grub_strcmp (args[*argn + 1], "-ge") == 0)
	    {
	      update_val (grub_strtosl (args[*argn], 0, 0)
			  >= grub_strtosl (args[*argn + 2], 0, 0), &c);
	      (*argn) += 3;
	      continue;
	    }

	  if (grub_strcmp (args[*argn + 1], "-gt") == 0)
	    {
	      update_val (grub_strtosl (args[*argn], 0, 0)
			  > grub_strtosl (args[*argn + 2], 0, 0), &c);
	      (*argn) += 3;
	      continue;
	    }

	  if (grub_strcmp (args[*argn + 1], "-le") == 0)
	    {
	      update_val (grub_strtosl (args[*argn], 0, 0)
		      <= grub_strtosl (args[*argn + 2], 0, 0), &c);
	      (*argn) += 3;
	      continue;
	    }

	  if (grub_strcmp (args[*argn + 1], "-lt") == 0)
	    {
	      update_val (grub_strtosl (args[*argn], 0, 0)
			  < grub_strtosl (args[*argn + 2], 0, 0), &c);
	      (*argn) += 3;
	      continue;
	    }

	  if (grub_strcmp (args[*argn + 1], "-ne") == 0)
	    {
	      update_val (grub_strtosl (args[*argn], 0, 0)
			  != grub_strtosl (args[*argn + 2], 0, 0), &c);
	      (*argn) += 3;
	      continue;
	    }

	  /* GRUB extension: compare numbers skipping prefixes.
	     Useful for comparing versions. E.g. vmlinuz-2 -plt vmlinuz-11. */
	  if (grub_strcmp (args[*argn + 1], "-pgt") == 0
	      || grub_strcmp (args[*argn + 1], "-plt") == 0)
	    {
	      int i;
	      /* Skip common prefix. */
	      for (i = 0; args[*argn][i] == args[*argn + 2][i]
		     && args[*argn][i]; i++);

	      /* Go the digits back. */
	      i--;
	      while (grub_isdigit (args[*argn][i]) && i > 0)
		i--;
	      i++;

	      if (grub_strcmp (args[*argn + 1], "-pgt") == 0)
		update_val (grub_strtoul (args[*argn] + i, 0, 0)
			    > grub_strtoul (args[*argn + 2] + i, 0, 0), &c);
	      else
		update_val (grub_strtoul (args[*argn] + i, 0, 0)
			    < grub_strtoul (args[*argn + 2] + i, 0, 0), &c);
	      (*argn) += 3;
	      continue;
	    }

	  /* -nt and -ot tests. GRUB extension: when doing -?t<bias> bias
	     will be added to the first mtime. */
	  if (grub_memcmp (args[*argn + 1], "-nt", 3) == 0
	      || grub_memcmp (args[*argn + 1], "-ot", 3) == 0)
	    {
	      struct grub_dirhook_info file1;
	      int file1exists;
	      int bias = 0;

	      /* Fetch fileinfo. */
	      get_fileinfo (args[*argn], &c);
	      file1 = c.file_info;
	      file1exists = c.file_exists;
	      get_fileinfo (args[*argn + 2], &c);

	      if (args[*argn + 1][3])
		bias = grub_strtosl (args[*argn + 1] + 3, 0, 0);

	      if (grub_memcmp (args[*argn + 1], "-nt", 3) == 0)
		update_val ((file1exists && ! c.file_exists)
			    || (file1.mtimeset && c.file_info.mtimeset
				&& file1.mtime + bias > c.file_info.mtime),
			    &c);
	      else
		update_val ((! file1exists && c.file_exists)
			    || (file1.mtimeset && c.file_info.mtimeset
				&& file1.mtime + bias < c.file_info.mtime),
			    &c);
	      (*argn) += 3;
	      continue;
	    }
	}

      /* Two-argument tests. */
      if (*argn + 1 < argc)
	{
	  /* File tests. */
	  if (grub_strcmp (args[*argn], "-d") == 0)
	    {
	      get_fileinfo (args[*argn + 1], &c);
	      update_val (c.file_exists && c.file_info.dir, &c);
	      (*argn) += 2;
	      return c.ret;
	    }

	  if (grub_strcmp (args[*argn], "-e") == 0)
	    {
	      get_fileinfo (args[*argn + 1], &c);
	      update_val (c.file_exists, &c);
	      (*argn) += 2;
	      return c.ret;
	    }

	  if (grub_strcmp (args[*argn], "-f") == 0)
	    {
	      get_fileinfo (args[*argn + 1], &c);
	      /* FIXME: check for other types. */
	      update_val (c.file_exists && ! c.file_info.dir, &c);
	      (*argn) += 2;
	      return c.ret;
	    }

	  if (grub_strcmp (args[*argn], "-s") == 0)
	    {
	      grub_file_t file;
	      file = grub_file_open (args[*argn + 1]);
	      update_val (file && (grub_file_size (file) != 0), &c);
	      if (file)
		grub_file_close (file);
	      grub_errno = GRUB_ERR_NONE;
	      (*argn) += 2;
	      return c.ret;
	    }

	  /* String tests. */
	  if (grub_strcmp (args[*argn], "-n") == 0)
	    {
	      update_val (args[*argn + 1][0], &c);

	      (*argn) += 2;
	      continue;
	    }
	  if (grub_strcmp (args[*argn], "-z") == 0)
	    {
	      update_val (! args[*argn + 1][0], &c);
	      (*argn) += 2;
	      continue;
	    }
	}

      /* Special modifiers. */

      /* End of expression. return to parent. */
      if (grub_strcmp (args[*argn], ")") == 0)
	{
	  (*argn)++;
	  return c.ret;
	}
      /* Recursively invoke if parenthesis. */
      if (grub_strcmp (args[*argn], "(") == 0)
	{
	  (*argn)++;
	  update_val (test_parse (args, argn, argc), &c);
	  continue;
	}

      if (grub_strcmp (args[*argn], "!") == 0)
	{
	  c.invert = ! c.invert;
	  (*argn)++;
	  continue;
	}
      if (grub_strcmp (args[*argn], "-a") == 0)
	{
	  /* If current value is 0 second value is to be discarded. */
	  c.discard = ! c.ret;
	  (*argn)++;
	  continue;
	}
      if (grub_strcmp (args[*argn], "-o") == 0)
	{
	  /* If current value is 1 second value is to be discarded. */
	  c.discard = c.ret;
	  (*argn)++;
	  continue;
	}

      /* No test found. Interpret if as just a string. */
      update_val (args[*argn][0], &c);
      (*argn)++;
    }
  return c.ret;
}

static grub_err_t
grub_cmd_test (grub_command_t cmd __attribute__ ((unused)),
	       int argc, char **args)
{
  int argn = 0;

  if (argc >= 1 && grub_strcmp (args[argc - 1], "]") == 0)
    argc--;

  return test_parse (args, &argn, argc) ? GRUB_ERR_NONE
    : grub_error (GRUB_ERR_TEST_FAILURE, "false");
}

static grub_command_t cmd_1, cmd_2;

GRUB_MOD_INIT(test)
{
  cmd_1 = grub_register_command ("[", grub_cmd_test,
				 "[ EXPRESSION ]", "Evaluate an expression");
  cmd_2 = grub_register_command ("test", grub_cmd_test,
				 "test EXPRESSION", "Evaluate an expression");
}

GRUB_MOD_FINI(test)
{
  grub_unregister_command (cmd_1);
  grub_unregister_command (cmd_2);
}
