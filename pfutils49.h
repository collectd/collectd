void	 print_rule(struct pf_rule *, const char *, int);
void	 print_pool(struct pf_pool *, u_int16_t, u_int16_t, sa_family_t, int, int);
int	 pfctl_get_pool(int, struct pf_pool *, u_int32_t, u_int32_t, int,
	    char *);
void	 pfctl_clear_pool(struct pf_pool *);


#define PF_NAT_PROXY_PORT_LOW	50001
#define PF_NAT_PROXY_PORT_HIGH	65535

