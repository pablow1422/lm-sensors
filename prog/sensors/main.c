/*
    main.c - Part of sensors, a user-space program for hardware monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <langinfo.h>

#ifndef __UCLIBC__
#include <iconv.h>
#define HAVE_ICONV
#endif

#include "lib/sensors.h" 
#include "lib/error.h"
#include "chips.h"
#include "version.h"
#include "chips_generic.h"

#define PROGRAM "sensors"
#define VERSION LM_VERSION
#define DEFAULT_CONFIG_FILE_NAME "sensors.conf"

FILE *config_file;

extern int main(int argc, char *arv[]);
static void print_short_help(void);
static void print_long_help(void);
static void print_version(void);
static void do_a_print(const sensors_chip_name *name);
static int do_a_set(const sensors_chip_name *name);
static int do_the_real_work(int *error);
static const char *sprintf_chip_name(const sensors_chip_name *name);

#define CHIPS_MAX 20
sensors_chip_name chips[CHIPS_MAX];
int chips_count=0;
int do_sets, do_unknown, fahrenheit, hide_adapter, hide_unknown;

char degstr[5]; /* store the correct string to print degrees */

void print_short_help(void)
{
  printf("Try `%s -h' for more information\n",PROGRAM);
}

void print_long_help(void)
{
  printf("Usage: %s [OPTION]... [CHIP]...\n",PROGRAM);
  printf("  -c, --config-file     Specify a config file (default: " ETCDIR "/" DEFAULT_CONFIG_FILE_NAME ")\n");
  printf("  -h, --help            Display this help text\n");
  printf("  -s, --set             Execute `set' statements too (root only)\n");
  printf("  -f, --fahrenheit      Show temperatures in degrees fahrenheit\n");
  printf("  -A, --no-adapter      Do not show adapter for each chip\n");
  printf("  -U, --no-unknown      Do not show unknown chips\n");
  printf("  -u, --unknown         Treat chips as unknown ones (testing only)\n");
  printf("  -v, --version         Display the program version\n");
  printf("\n");
  printf("Use `-' after `-c' to read the config file from stdin.\n");
  printf("If no chips are specified, all chip info will be printed.\n");
  printf("Example chip names:\n");
  printf("\tlm78-i2c-0-2d\t*-i2c-0-2d\n");
  printf("\tlm78-i2c-0-*\t*-i2c-0-*\n");
  printf("\tlm78-i2c-*-2d\t*-i2c-*-2d\n");
  printf("\tlm78-i2c-*-*\t*-i2c-*-*\n");
  printf("\tlm78-isa-0290\t*-isa-0290\n");
  printf("\tlm78-isa-*\t*-isa-*\n");
  printf("\tlm78-*\n");
}

void print_version(void)
{
  printf("%s version %s with libsensors version %s\n", PROGRAM, VERSION, libsensors_version);
}

/* This examines global var config_file, and leaves the name there too. 
   It also opens config_file. */
static void open_config_file(const char* config_file_name)
{
  if (!strcmp(config_file_name,"-")) {
    config_file = stdin;
    return;
  }

  config_file = fopen(config_file_name, "r");
  if (!config_file) {
    fprintf(stderr, "Could not open config file\n");
    perror(config_file_name);
    exit(1);
  }
}
    
static void close_config_file(const char* config_file_name)
{
  if (fclose(config_file) == EOF) {
    fprintf(stderr,"Could not close config file\n");
    perror(config_file_name);
  }
}

static void set_degstr(void)
{
  const char *deg_default_text[2] = {" C", " F"};

#ifdef HAVE_ICONV
  /* Size hardcoded for better performance.
     Don't forget to count the trailing \0! */
  size_t deg_latin1_size = 3;
  char *deg_latin1_text[2] = {"\260C", "\260F"};
  size_t nconv;
  size_t degstr_size = sizeof(degstr);
  char *degstr_ptr = degstr;

  iconv_t cd = iconv_open(nl_langinfo(CODESET), "ISO-8859-1");
  if (cd != (iconv_t) -1) {
    nconv = iconv(cd, &(deg_latin1_text[fahrenheit]), &deg_latin1_size,
                  &degstr_ptr, &degstr_size);
    iconv_close(cd);
    
    if (nconv != (size_t) -1)
      return;	   
  }
#endif /* HAVE_ICONV */

  /* There was an error during the conversion, use the default text */
  strcpy(degstr, deg_default_text[fahrenheit]);
}

int main (int argc, char *argv[])
{
  int c,res,i,error;
  const char *config_file_name = ETCDIR "/" DEFAULT_CONFIG_FILE_NAME;

  struct option long_opts[] =  {
    { "help", no_argument, NULL, 'h' },
    { "set", no_argument, NULL, 's' },
    { "version", no_argument, NULL, 'v'},
    { "fahrenheit", no_argument, NULL, 'f' },
    { "no-adapter", no_argument, NULL, 'A' },
    { "no-unknown", no_argument, NULL, 'U' },
    { "config-file", required_argument, NULL, 'c' },
    { "unknown", no_argument, NULL, 'u' },
    { 0,0,0,0 }
  };

  setlocale(LC_CTYPE, "");

  do_unknown = 0;
  do_sets = 0;
  hide_adapter = 0;
  hide_unknown = 0;
  while (1) {
    c = getopt_long(argc, argv, "hsvfAUc:u", long_opts, NULL);
    if (c == EOF)
      break;
    switch(c) {
    case ':':
    case '?':
      print_short_help();
      exit(1);
    case 'h':
      print_long_help();
      exit(0);
    case 'v':
      print_version();
      exit(0);
    case 'c':
      config_file_name = optarg;
      break;
    case 's':
      do_sets = 1;
      break;
    case 'f':
      fahrenheit = 1;
      break;
    case 'A':
      hide_adapter = 1;
      break;
    case 'U':
      hide_unknown = 1;
      break;
    case 'u':
      do_unknown = 1;
      break;
    default:
      fprintf(stderr,"Internal error while parsing options!\n");
      exit(1);
    }
  }

  if (optind == argc) {
    chips[0].prefix = SENSORS_CHIP_NAME_PREFIX_ANY;
    chips[0].bus = SENSORS_CHIP_NAME_BUS_ANY;
    chips[0].addr = SENSORS_CHIP_NAME_ADDR_ANY;
    chips_count = 1;
  } else 
    for(i = optind; i < argc; i++) 
      if ((res = sensors_parse_chip_name(argv[i],chips+chips_count))) {
        fprintf(stderr,"Parse error in chip name `%s'\n",argv[i]);
        print_short_help();
        exit(1);
      } else if (++chips_count == CHIPS_MAX) {
        fprintf(stderr,"Too many chips on command line!\n");
        exit(1);
      }

  open_config_file(config_file_name);
  if ((res = sensors_init(config_file))) {
    fprintf(stderr, "sensors_init: %s\n", sensors_strerror(res));
    exit(1);
  }
  close_config_file(config_file_name);

  /* build the degrees string */
  set_degstr();

  if(do_the_real_work(&error)) {
    sensors_cleanup();
    exit(error);
  } else {
    if(chips[0].prefix == SENSORS_CHIP_NAME_PREFIX_ANY)
	    fprintf(stderr,
	            "No sensors found!\n"
	            "Make sure you loaded all the kernel drivers you need.\n"
	            "Try sensors-detect to find out which these are.\n");
    else
	    fprintf(stderr,"Specified sensor(s) not found!\n");
    sensors_cleanup();
    exit(1);
  }
}

/* returns number of chips found */
int do_the_real_work(int *error)
{
  const sensors_chip_name *chip;
  int chip_nr,i;
  int cnt = 0;

  *error = 0;
  for (chip_nr = 0; (chip = sensors_get_detected_chips(&chip_nr));)
    for(i = 0; i < chips_count; i++)
      if (sensors_match_chip(chip, &chips[i])) {
        if(do_sets) {
          if (do_a_set(chip))
            *error = 1;
        } else
          do_a_print(chip);
        i = chips_count;
	cnt++;
      }
   return(cnt);
}

/* returns 1 on error */
int do_a_set(const sensors_chip_name *name)
{
  int res;

  if ((res = sensors_do_chip_sets(name))) {
    if (res == -SENSORS_ERR_PROC) {
      fprintf(stderr,"%s: %s for writing;\n",sprintf_chip_name(name),
              sensors_strerror(res));
      fprintf(stderr,"Run as root?\n");
      return 1;
    } else if (res == -SENSORS_ERR_ACCESS_W) {
      fprintf(stderr, "%s: At least one \"set\" statement failed\n",
              sprintf_chip_name(name));
    } else {
      fprintf(stderr,"%s: %s\n",sprintf_chip_name(name),
              sensors_strerror(res));
    }
  }
  return 0;
}

const char *sprintf_chip_name(const sensors_chip_name *name)
{
  #define BUF_SIZE 200
  static char buf[BUF_SIZE];

  if (name->bus == SENSORS_CHIP_NAME_BUS_ISA)
    snprintf(buf, BUF_SIZE, "%s-isa-%04x", name->prefix, name->addr);
  else if (name->bus == SENSORS_CHIP_NAME_BUS_PCI)
    snprintf(buf, BUF_SIZE, "%s-pci-%04x", name->prefix, name->addr);
  else if (name->bus == SENSORS_CHIP_NAME_BUS_DUMMY)
    snprintf(buf, BUF_SIZE, "%s-%s-%04x", name->prefix, name->busname,
             name->addr);
  else
    snprintf(buf, BUF_SIZE, "%s-i2c-%d-%02x", name->prefix, name->bus,
             name->addr);
  return buf;
}

void do_a_print(const sensors_chip_name *name)
{
  if (hide_unknown)
    return;

  printf("%s\n",sprintf_chip_name(name));
  if (!hide_adapter) {
    const char *adap = sensors_get_adapter_name(name->bus);
    if (adap)
      printf("Adapter: %s\n", adap);
    else
      fprintf(stderr, "Can't get adapter name for bus %d\n", name->bus);
  }
  if (do_unknown)
    print_unknown_chip(name);
  else
    print_generic_chip(name);
  printf("\n");
}
