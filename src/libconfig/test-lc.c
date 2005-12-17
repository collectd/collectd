#include "compat.h"
#include "libconfig.h"

int help_cmd(const char *partarg, const char *arg, const char *argarg, const char *val, lc_flags_t flags, void *extra) {
	printf("Usage info goes here\n");
	printf("\n");

	exit(EXIT_FAILURE);
}
int sally_cmd(const char *partarg, const char *arg, const char *argarg, const char *val, lc_flags_t flags, void *extra) {
	fprintf(stderr, "%s sets value: \"%s\" (flags=%i)\n", arg, val, flags);
	return(0);
}

int cmd_ifmodule(const char *partarg, const char *arg, const char *argarg, const char *val, lc_flags_t flags, void *extra) {
	if (flags == LC_FLAGS_SECTIONEND) {
		return(LC_CBRET_OKAY);
	}
	if (flags != LC_FLAGS_SECTIONSTART) {
		fprintf(stderr, "IfModule can only be used as a section.\n");
		return(LC_CBRET_ERROR);
	}
	if (argarg == NULL) {
		fprintf(stderr, "You must specify an argument to IfModule.\n");
		return(LC_CBRET_ERROR);
	}

	fprintf(stderr, "IfModule (%s)\n", argarg);
	return(LC_CBRET_IGNORESECTION);
}

int main(int argc, char **argv) {
	char *joeval = NULL;
	size_t xval = -1;
	int onoff = -1;
	int lcpret = -1;
	int i = 0;
	int onoff2 = 0;
	uint32_t ipaddr = 0;

	lc_register_var("Section", LC_VAR_SECTION, NULL, 0);
	lc_register_var("Somesection", LC_VAR_SECTION, NULL, 0);
	lc_register_var("Section.Test", LC_VAR_STRING, &joeval, 'j');
	lc_register_var("bob", LC_VAR_SIZE_SIZE_T, &xval, 's');
	lc_register_var("Somesection.Free", LC_VAR_BOOL, &onoff, 0);
	lc_register_var("long", LC_VAR_BOOL_BY_EXISTANCE, &onoff2, 'l');
	lc_register_var("ipaddr", LC_VAR_IP, &ipaddr, 'i');
	lc_register_callback("sally", 0, LC_VAR_STRING, sally_cmd, NULL);
	lc_register_callback("HELP", 'h', LC_VAR_NONE, help_cmd, NULL);
	lc_register_callback("*.ifmodule", 0, LC_VAR_NONE, cmd_ifmodule, NULL);
	lcpret = lc_process_file("testapp", "build/test.conf", LC_CONF_APACHE);
	if (lcpret < 0) {
		fprintf(stderr, "Error processing config file: %s\n", lc_geterrstr());
		return(EXIT_FAILURE);
	}

	lcpret = lc_process(argc, argv, "testapp", LC_CONF_APACHE, "test.cfg");
	if (lcpret < 0) {
		fprintf(stderr, "Error processing config file: %s\n", lc_geterrstr());
		return(EXIT_FAILURE);
	}

	lc_cleanup();

	if (joeval != NULL) {
		fprintf(stderr, "joeval = \"%s\"\n", joeval);
	} else {
		fprintf(stderr, "joeval = \"(null)\"\n");
	}
	fprintf(stderr, "xval = %llu\n", (unsigned long long) xval);
	fprintf(stderr, "onoff = %i\n", onoff);
	fprintf(stderr, "long = %i\n", onoff2);
	fprintf(stderr, "ip = %08lx\n", (unsigned long) ipaddr);
	for (i = lc_optind; i < argc; i++) {
		fprintf(stderr, "argv[%i] = \"%s\"\n", i, argv[i]);
	}

	return(0);
}
