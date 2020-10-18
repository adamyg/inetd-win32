/*
 *  libinetd
 */

#include <sys/cdefs.h>

__BEGIN_DECLS

extern int  inetd_main(int argc, const char **argv);
extern void inetd_signal_reconfig(int verbose);
extern void inetd_signal_stop(int verbose);

__END_DECLS

/*end*/
