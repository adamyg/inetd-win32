//
//  MMDB lookup/test tool.
//

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <ctime>
#include <fstream>
#include <vector>

#if defined(HAVE_LIBMAXMINDDB)
#if defined(ssize_t)
#undef ssize_t
#endif
#if defined(_DEBUG)
#pragma comment(lib, "libmaxminddbd.lib")
#else
#pragma comment(lib, "libmaxminddb.lib")
#endif
#pragma comment(lib, "ws2_32.lib")

#include <maxminddb/maxminddb.h>

#endif

#include "../libinetd/ServiceGetOpt.h"

#if defined(HAVE_LIBMAXMINDDB)
static bool         mmdbopen(const char *filename, MMDB_s *mmdb);
static void         mmdbmeta(MMDB_s *mmdb);
static int          mmdblookup(MMDB_s *mmdb, const char *ip_address);
#endif
static void         usage(const inetd::Getopt &options, const char *fmt, ...); /*no-return*/

static const char *short_options = "f:i:v";
static struct inetd::Getopt::Option long_options[] = {
    { "db",         inetd::Getopt::argument_required,   NULL,   'd'  },
    { "ip",         inetd::Getopt::argument_required,   NULL,   'i'  },
    { "file",       inetd::Getopt::argument_required,   NULL,   'f'  },
    { "verbose",    inetd::Getopt::argument_none,       NULL,   'v'  },
    { "usage",      inetd::Getopt::argument_none,       NULL,   1100 },
    { NULL }
    };

namespace {
// left trim
static inline std::string&
ltrim(std::string &s, const char *c = " \t\n\r")
{
	s.erase(0, s.find_first_not_of(c));
	return s;
}

// right trim
static inline std::string&
rtrim(std::string &s, const char *c = " \t\n\r")
{
	s.erase(s.find_last_not_of(c) + 1);
	return s;
}

// trim
static inline std::string&
trim(std::string &s, const char *c = " \t\n\r")
{
	return ltrim(rtrim(s, c), c);
}

// parse input file, allowing simple comment structure.
template<typename Pred>
bool parse_file(const inetd::Getopt &options, const char *filename, Pred &pred)
{
	std::ifstream stream(filename);
	if (stream.fail()) {
		usage(options, "FILE option, unable to open source <%s>", filename);
	}

	std::string line;
	line.reserve(1024);
	while (std::getline(stream, line)) {
		const size_t bang = line.find_first_of('#');
		if (bang != std::string::npos) {
			line.erase(bang);
		}
		trim(line);
		if (! line.empty()) {
			if (! pred(line)) {
				return false;
			}
		}
	}
	return true;
}
};


int
main(int argc, const char **argv)
{
    inetd::Getopt options(short_options, long_options);
    std::vector<const char *> ips, files;
    std::string errmsg;
    const char *database = nullptr;
    bool verbose = false;

    while (-1 != options.shift(argc, argv, errmsg)) {
        switch (options.optret()) {
        case 'd': {
                const char *t_database = options.optarg();
                if (!*t_database) {
                    usage(options, "empty database");
                } else if (database) {
                    usage(options, "multiple database's specified <%s> and <%s>", database, t_database);
                }
                database = t_database;
            }
            break;
        case 'f':
            files.push_back(options.optarg());
            break;
        case 'i':
            ips.push_back(options.optarg());
            break;
        case 'v':
            verbose = true;
            break;
        case 1100:          // usage
        case '?':
            usage(options, nullptr);
        default:            // error
            usage(options, "%s", errmsg.c_str());
        }
    }

    if (nullptr == database) {
        usage(options, "database missing");
    }

    argv += options.optind();
    if (0 != (argc -= options.optind())) {
        usage(options, "unexpected arguments %s ...", argv[0]);
    }

#if defined(HAVE_LIBMAXMINDDB)
    MMDB_s mmdb;
    if (! mmdbopen(database, &mmdb))
        return 3;
    if (verbose)
        mmdbmeta(&mmdb);
    for (auto ip : ips)
        mmdblookup(&mmdb, ip);    
    for (auto file : files) {
        parse_file<>(options, file, [&](std::string &line) {
		        const size_t comma = line.find_first_of(',');
                        if (comma != std::string::npos) { // first element
			        std::string ip(line, 0, comma);
                                mmdblookup(&mmdb, ip.c_str());   
                        } else { // whole element
                                mmdblookup(&mmdb, line.c_str());                      
                        }
                        return true;
                        });
    }
    MMDB_close(&mmdb);

#else
    usage("libmaxminddb support not enabled");
#endif
    return 0;
}


static void
usage(const inetd::Getopt &options, const char *fmt, ...)
{
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap), fputs("\n\n", stderr);
        va_end(ap);
    }

    fprintf(stderr,
        "Usage: %s [options] --db <mmdb> --file <file> | --ip <addr> ..\n\n", options.progname());
    fprintf(stderr,
        "options:\n"
        "   -v,--verbose                Verbose.\n"
        "\n"
        "arguments:\n"
        "   -d,--db <database>          MMDB file path, required.\n"
        "   -i,--ip <path>              Address to resolve, none or more.\n"
        "   -f,--file <file>            Address list, none or more.\n"
        );
    exit(3);
}


#if defined(HAVE_LIBMAXMINDDB)
static bool
mmdbopen(const char *filename, MMDB_s *mmdb)
{
    int status = MMDB_open(filename, MMDB_MODE_MMAP, mmdb);
    if (MMDB_SUCCESS != status) {
        fprintf(stderr, "\n  Can't open %s - %s\n", filename, MMDB_strerror(status));
        if (MMDB_IO_ERROR == status) {
            fprintf(stderr, "    IO error: %s\n", strerror(errno));
        }
        return false;
    }
    return true;
}


static void 
mmdbmeta(MMDB_s *mmdb)
{
    const char *meta_dump = "\n"
                            "  Database metadata\n"
                            "    Node count:    %i\n"
                            "    Record size:   %i bits\n"
                            "    IP version:    IPv%i\n"
                            "    Binary format: %i.%i\n"
                            "    Build epoch:   %llu (%s)\n"
                            "    Type:          %s\n"
                            "    Languages:     ";

    char date[40];
    const time_t epoch = (const time_t)mmdb->metadata.build_epoch;
    strftime(date, 40, "%F %T UTC", gmtime(&epoch));

    fprintf(stdout,
            meta_dump,
            mmdb->metadata.node_count,
            mmdb->metadata.record_size,
            mmdb->metadata.ip_version,
            mmdb->metadata.binary_format_major_version,
            mmdb->metadata.binary_format_minor_version,
            mmdb->metadata.build_epoch,
            date,
            mmdb->metadata.database_type);

    for (size_t i = 0; i < mmdb->metadata.languages.count; i++) {
        fprintf(stdout, "%s", mmdb->metadata.languages.names[i]);
        if (i < mmdb->metadata.languages.count - 1) {
            fprintf(stdout, " ");
        }
    }
    fprintf(stdout, "\n");

    fprintf(stdout, "    Description:\n");
    for (size_t i = 0; i < mmdb->metadata.description.count; i++) {
        fprintf(stdout,
                "      %s:   %s\n",
                mmdb->metadata.description.descriptions[i]->language,
                mmdb->metadata.description.descriptions[i]->description);
    }
    fprintf(stdout, "\n");
}


static int
mmdblookup(MMDB_s *mmdb, const char *ip_address)
{
    int gai_error, mmdb_error;
    MMDB_lookup_result_s result =
        MMDB_lookup_string(mmdb, ip_address, &gai_error, &mmdb_error);
    if (0 != gai_error) {
        fprintf(stderr, "\n  Error from getaddrinfo for %s - %s\n\n", ip_address, gai_strerror(gai_error));
        return -2;
    }

    if (MMDB_SUCCESS != mmdb_error) {
        fprintf(stderr, "\n  Got an error from libmaxminddb: %s\n\n", MMDB_strerror(mmdb_error));
        return -2;
    }

    MMDB_entry_data_list_s *entry_data_list = NULL;

    int exit_code = 0;
    printf("IP: %s\n", ip_address);
    if (result.found_entry) {
        const int status = MMDB_get_entry_data_list(&result.entry, &entry_data_list);
        if (MMDB_SUCCESS != status) {
            fprintf(stderr, "Got an error looking up the entry data - %s\n", MMDB_strerror(status));
            exit_code = 4;
            goto end;
        }

        if (NULL != entry_data_list) {
            MMDB_dump_entry_data_list(stdout, entry_data_list, 2);
        }

    } else {
        fprintf(stderr, "\n  No entry for this IP address (%s) was found\n\n", ip_address);
        exit_code = 5;
    }

end:
    printf("\n");
    MMDB_free_entry_data_list(entry_data_list);
    return exit_code;
}
#endif //HAVE_LIBMAXMINDDB

//end