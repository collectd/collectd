#  ifndef _RSK_LIBCONFIG_H
#  define _RSK_LIBCONFIG_H
#  ifdef __cplusplus
extern "C" {
#  endif

#  define LC_VAR_LIST 0x80

typedef enum {
        LC_CONF_SECTION,
        LC_CONF_APACHE,
        LC_CONF_COLON,
        LC_CONF_EQUAL,
        LC_CONF_SPACE,
        LC_CONF_XML
} lc_conf_type_t;

typedef enum {
        LC_VAR_UNKNOWN,
        LC_VAR_NONE,
        LC_VAR_STRING,
        LC_VAR_LONG_LONG,
        LC_VAR_LONG,
        LC_VAR_INT,
        LC_VAR_SHORT,
        LC_VAR_BOOL,
        LC_VAR_FILENAME,
        LC_VAR_DIRECTORY,
        LC_VAR_SIZE_LONG_LONG,
        LC_VAR_SIZE_LONG,
        LC_VAR_SIZE_INT,
        LC_VAR_SIZE_SHORT,
        LC_VAR_TIME,
        LC_VAR_DATE,
        LC_VAR_SECTION,
        LC_VAR_SECTIONSTART,
        LC_VAR_SECTIONEND,
        LC_VAR_BOOL_BY_EXISTANCE,
        LC_VAR_SIZE_SIZE_T,
        LC_VAR_CIDR,
        LC_VAR_IP,
        LC_VAR_IP4,
        LC_VAR_IP6,
        LC_VAR_HOSTNAME4,
        LC_VAR_HOSTNAME6,
} lc_var_type_t;

typedef enum {
        LC_FLAGS_VAR,
        LC_FLAGS_CMDLINE,
        LC_FLAGS_ENVIRON,
        LC_FLAGS_SECTIONSTART,
        LC_FLAGS_SECTIONEND
} lc_flags_t;

typedef enum {
        LC_ERR_NONE,
        LC_ERR_INVCMD,
        LC_ERR_INVSECTION,
        LC_ERR_INVDATA,
        LC_ERR_BADFORMAT,
        LC_ERR_CANTOPEN,
        LC_ERR_CALLBACK,
        LC_ERR_ENOMEM
} lc_err_t;

int lc_process(int argc, char **argv, const char *appname, lc_conf_type_t type, const char *extra);
int lc_register_callback(const char *var, char opt, lc_var_type_t type, int (*callback)(const char *, const char *, const char *, const char *, lc_flags_t, void *), void *extra);
int lc_register_var(const char *var, lc_var_type_t type, void *data, char opt);
lc_err_t lc_geterrno(void);
char *lc_geterrstr(void);
int lc_process_file(const char *appname, const char *pathname, lc_conf_type_t type);
void lc_cleanup(void);

#  define LC_CBRET_IGNORESECTION (255)
#  define LC_CBRET_OKAY (0)
#  define LC_CBRET_ERROR (-1)

extern int lc_optind;

#  ifdef __cplusplus
}
#  endif
#  endif
