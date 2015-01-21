/*
 * Coraid disk errors stats collector
 *
 * Suitable for illumos, CorOS, and Solaris 11 derivatives.
 *
 * Copyright 2014 Coraid, Inc.
 *
 * MIT License
 * ===========
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

extern kstat_ctl_t *kc;

/* pass the counters as collectd derive (int64_t) */
void
disk_errors_derive(value_list_t *vl, kstat_t *ksp, char *k, char *s)
{
    value_t v[1];
    long long ll;

    if ((ll = get_kstat_value(ksp, k)) == -1LL) return;
    v[0].derive = (derive_t)ll;
    vl->values = v;
    vl->values_len = 1;
    sstrncpy(vl->type_instance, (s == NULL ? k : s), sizeof(vl->type_instance));
    plugin_dispatch_values(vl);
}

/* 
 * Most of the work is done in the disk_errors_read() callback.
 * For brevity, a simplistic approach is taken to match a reasonable
 * collectd and whisper-compatible namespace. The general form is:
 *   disk_errors-[sd instance].derive-[statistic]
 */
static int
disk_errors_read(void)
{
    kstat_t *ksp = NULL;
    kid_t kid;
    value_list_t vl = VALUE_LIST_INIT;
    char *s;   /* sd instance as string */

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "Disk_Errors", sizeof (vl.plugin));

    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if ((strncmp(ksp->ks_module, "sderr", KSTAT_STRLEN) == 0) &&
            (strncmp(ksp->ks_class, "device_error", KSTAT_STRLEN) == 0)) {

            if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                continue;
            if ((s = strtok(ksp->ks_name, ",")) == NULL)
                continue;
            sstrncpy(vl.plugin_instance, s, sizeof (vl.plugin_instance));
            sstrncpy(vl.type, "derive", sizeof (vl.type));

            /* note: hard and soft errors are counts of other types of errors */
            disk_errors_derive(&vl, ksp, "All Resets", "All_Resets");
            disk_errors_derive(&vl, ksp, "Device Not Ready", "DNR");
            disk_errors_derive(&vl, ksp, "Hard Errors", "Hard");
            disk_errors_derive(&vl, ksp, "Illegal Request", "Illegal_Request");
            disk_errors_derive(&vl, ksp, "LUN Resets", "LUN_Resets");
            disk_errors_derive(&vl, ksp, "Media Error", "Media");
            disk_errors_derive(&vl, ksp, "No Device", "No_Device");
            disk_errors_derive(&vl, ksp, "Predictive Failure Analysis", "PFA");
            disk_errors_derive(&vl, ksp, "Retries", NULL);
            disk_errors_derive(&vl, ksp, "Recoverable", "Recoverable");
            disk_errors_derive(&vl, ksp, "Soft Errors", "Soft");
            disk_errors_derive(&vl, ksp, "Transport Errors", "Transport");
            disk_errors_derive(&vl, ksp, "Target Resets", "Target_Resets");
            /* size isn't really an error, but can be handy to have at hand */
            disk_errors_derive(&vl, ksp, "Size", NULL);
        }
   }
    return (0);
}

static int
disk_errors_init(void)
{
    /* the kstat chain is opened already, if not bail out */
    if (kc == NULL) {
        ERROR ("disk_errors plugin: kstat chain control initialization failed");
        return (-1);
    }
    return (0);
}

void
module_register(void)
{
    plugin_register_init ("disk_errors", disk_errors_init);
    plugin_register_read ("disk_errors", disk_errors_read);
}
