package Collectd::MockDaemon;
use strict;
use warnings;
use IO::Socket::UNIX;
use IO::Handle;
use File::Temp;
use Carp;
use YAML::Any qw/Load/;

use base 'Exporter';
our @EXPORT = qw/ mockd_start mockd_stop /;

my ($pid, $childfh, $sock_path);
my $data = Load(do{local $/; <DATA>});
my @metrics = sort keys %$data;

sub mockd_start {
    croak "don't call me twice" if defined $pid;
    $sock_path = File::Temp::mktemp('collectd-mocksock.XXXXXX');

    my $sock = IO::Socket::UNIX->new(
        Type => SOCK_STREAM,
        Local => $sock_path,
        Listen => 1
    ) or die "Can't open Unix domain socket `$sock_path': $!";
    $pid = open(my $kid, "-|");
    croak "cannot fork(): $!" unless defined $pid;
    if($pid) {
        $childfh = $kid;
    } else {
        daemon($sock);
        exit 0;
    }
    return $sock_path;
}

sub mockd_stop {
    return unless defined $childfh;
    kill 'TERM', $pid;  # kill it with fire
    unlink $sock_path;
    $childfh = $pid = undef;
}

sub daemon {
    my $sock = shift;
    while(my $csock = $sock->accept) {
        while(<$csock>) {
            chomp;
            /^LISTVAL\s*$/i and $csock->print(listval()), next;
            /^GETVAL\s+(.*)$/i and $csock->print(getval($1), "\n"), next;
            /^PUTVAL|PUTNOTIF|FLUSH\s+(.*)$/i and $csock->print("-1 Unimplemented command `$1'\n"), next;
            $csock->print("-1 Unknown command: $_\n"), next;
        }
    }
}

sub listval {
    my $now = time;
    return print_nvalues(scalar @metrics) .
    join('', map { "$now $_\n" } @metrics);
}

sub getval {
    my ($val) = @_;
    $val =~ s/(?:^\s+|\s+$)//g;
    $val =~ s/(?:^"|"$)//g;
    my $id = _parse_identifier($val) or return "-1 Cannot parse identifier `$val'";
    return "-1 No such value ($val)" unless exists $data->{$val};
    my $result = print_nvalues(scalar keys %{$data->{$val}});
    return $result . join("\n", map { "$_=$data->{$val}{$_}" } keys %{$data->{$val}});
}

sub _parse_identifier
{
	my $s = shift;
	my ($plugin_instance, $type_instance);

	my ($host, $plugin, $type) = split ('/', $s);
    return unless length $host and length $plugin and length $type;

	($plugin, $plugin_instance) = split ('-', $plugin, 2);
	($type, $type_instance) = split ('-', $type, 2);

	my $ident =
	{
		host => $host,
		plugin => $plugin,
		type => $type
	};
	$ident->{'plugin_instance'} = $plugin_instance if (defined ($plugin_instance));
	$ident->{'type_instance'} = $type_instance if (defined ($type_instance));

	return $ident;
}

sub print_nvalues {
    my $nvals = shift;
    return sprintf("%d Value%s found\n", $nvals, $nvals > 1 ? 's' : '');
}

1;

__DATA__
---
a1d8f6310/cpu-0/cpu-idle:
  value: 9.999973e+01
a1d8f6310/cpu-0/cpu-interrupt:
  value: 0.000000e+00
a1d8f6310/cpu-0/cpu-nice:
  value: 0.000000e+00
a1d8f6310/cpu-0/cpu-softirq:
  value: 0.000000e+00
a1d8f6310/cpu-0/cpu-steal:
  value: 0.000000e+00
a1d8f6310/cpu-0/cpu-system:
  value: 0.000000e+00
a1d8f6310/cpu-0/cpu-user:
  value: 9.999996e-02
a1d8f6310/cpu-0/cpu-wait:
  value: 0.000000e+00
a1d8f6310/cpu-1/cpu-idle:
  value: 9.989979e+01
a1d8f6310/cpu-1/cpu-interrupt:
  value: 0.000000e+00
a1d8f6310/cpu-1/cpu-nice:
  value: 0.000000e+00
a1d8f6310/cpu-1/cpu-softirq:
  value: 0.000000e+00
a1d8f6310/cpu-1/cpu-steal:
  value: 0.000000e+00
a1d8f6310/cpu-1/cpu-system:
  value: 0.000000e+00
a1d8f6310/cpu-1/cpu-user:
  value: 0.000000e+00
a1d8f6310/cpu-1/cpu-wait:
  value: 0.000000e+00
a1d8f6310/df-boot/df_complex-free:
  value: 4.368712e+08
a1d8f6310/df-boot/df_complex-reserved:
  value: 2.684109e+07
a1d8f6310/df-boot/df_complex-used:
  value: 6.471270e+07
a1d8f6310/df-boot/df_inodes-free:
  value: 3.271800e+04
a1d8f6310/df-boot/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-boot/df_inodes-used:
  value: 5.000000e+01
a1d8f6310/df-boot/percent_bytes-free:
  value: 8.267421e+01
a1d8f6310/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
a1d8f6310/df-boot/percent_bytes-used:
  value: 1.224634e+01
a1d8f6310/df-boot/percent_inodes-free:
  value: 9.984741e+01
a1d8f6310/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-boot/percent_inodes-used:
  value: 1.525879e-01
a1d8f6310/df-data1/df_complex-free:
  value: 2.636943e+10
a1d8f6310/df-data1/df_complex-reserved:
  value: 1.476235e+09
a1d8f6310/df-data1/df_complex-used:
  value: 1.215783e+09
a1d8f6310/df-data1/df_inodes-free:
  value: 1.797470e+06
a1d8f6310/df-data1/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-data1/df_inodes-used:
  value: 4.770000e+03
a1d8f6310/df-data1/percent_bytes-free:
  value: 9.073681e+01
a1d8f6310/df-data1/percent_bytes-reserved:
  value: 5.079704e+00
a1d8f6310/df-data1/percent_bytes-used:
  value: 4.183491e+00
a1d8f6310/df-data1/percent_inodes-free:
  value: 9.973533e+01
a1d8f6310/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-data1/percent_inodes-used:
  value: 2.646706e-01
a1d8f6310/df-dev-shm/df_complex-free:
  value: 9.842483e+08
a1d8f6310/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
a1d8f6310/df-dev-shm/df_complex-used:
  value: 0.000000e+00
a1d8f6310/df-dev-shm/df_inodes-free:
  value: 2.402940e+05
a1d8f6310/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
a1d8f6310/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
a1d8f6310/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
a1d8f6310/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
a1d8f6310/df-dev-shm/percent_inodes-free:
  value: 9.999958e+01
a1d8f6310/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-dev-shm/percent_inodes-used:
  value: 4.161551e-04
a1d8f6310/df-root/df_complex-free:
  value: 1.072081e+10
a1d8f6310/df-root/df_complex-reserved:
  value: 6.442435e+08
a1d8f6310/df-root/df_complex-used:
  value: 1.317655e+09
a1d8f6310/df-root/df_inodes-free:
  value: 7.423750e+05
a1d8f6310/df-root/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-root/df_inodes-used:
  value: 4.405700e+04
a1d8f6310/df-root/percent_bytes-free:
  value: 8.453092e+01
a1d8f6310/df-root/percent_bytes-reserved:
  value: 5.079700e+00
a1d8f6310/df-root/percent_bytes-used:
  value: 1.038938e+01
a1d8f6310/df-root/percent_inodes-free:
  value: 9.439786e+01
a1d8f6310/df-root/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-root/percent_inodes-used:
  value: 5.602138e+00
a1d8f6310/df-var/df_complex-free:
  value: 7.454015e+09
a1d8f6310/df-var/df_complex-reserved:
  value: 4.294943e+08
a1d8f6310/df-var/df_complex-used:
  value: 5.716091e+08
a1d8f6310/df-var/df_inodes-free:
  value: 5.222690e+05
a1d8f6310/df-var/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-var/df_inodes-used:
  value: 2.019000e+03
a1d8f6310/df-var/percent_bytes-free:
  value: 8.815979e+01
a1d8f6310/df-var/percent_bytes-reserved:
  value: 5.079695e+00
a1d8f6310/df-var/percent_bytes-used:
  value: 6.760509e+00
a1d8f6310/df-var/percent_inodes-free:
  value: 9.961491e+01
a1d8f6310/df-var/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6310/df-var/percent_inodes-used:
  value: 3.850937e-01
a1d8f6310/disk-vda/disk_merged:
  read: 0.000000e+00
  write: 9.999910e-02
a1d8f6310/disk-vda/disk_octets:
  read: 0.000000e+00
  write: 1.228789e+03
a1d8f6310/disk-vda/disk_ops:
  read: 0.000000e+00
  write: 1.999982e-01
a1d8f6310/disk-vda/disk_time:
  read: 0.000000e+00
  write: 4.999955e-01
a1d8f6310/disk-vda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6310/disk-vda6/disk_merged:
  read: 0.000000e+00
  write: 9.999878e-02
a1d8f6310/disk-vda6/disk_octets:
  read: 0.000000e+00
  write: 1.228786e+03
a1d8f6310/disk-vda6/disk_ops:
  read: 0.000000e+00
  write: 1.999976e-01
a1d8f6310/disk-vda6/disk_time:
  read: 0.000000e+00
  write: 4.999941e-01
a1d8f6310/load/load:
  longterm: 7.000000e-02
  midterm: 6.000000e-02
  shortterm: 0.000000e+00
a1d8f6310/memory/memory-buffered:
  value: 2.348646e+08
a1d8f6310/memory/memory-cached:
  value: 5.790310e+08
a1d8f6310/memory/memory-free:
  value: 2.351022e+08
a1d8f6310/memory/memory-used:
  value: 9.194988e+08
a1d8f6310/network/if_octets:
  rx: 0.000000e+00
  tx: 6.586002e+02
a1d8f6310/network/if_packets:
  rx: 0.000000e+00
  tx: 4.999991e-01
a1d8f6310/network/queue_length:
  value: 0.000000e+00
a1d8f6310/network/total_values-dispatch-accepted:
  value: 0.000000e+00
a1d8f6310/network/total_values-dispatch-rejected:
  value: 0.000000e+00
a1d8f6310/network/total_values-send-accepted:
  value: 1.480003e+01
a1d8f6310/network/total_values-send-rejected:
  value: 0.000000e+00
a1d8f6310/swap/swap-cached:
  value: 4.173824e+06
a1d8f6310/swap/swap-free:
  value: 2.118148e+09
a1d8f6310/swap/swap-used:
  value: 2.515354e+07
a1d8f6310/swap/swap_io-in:
  value: 0.000000e+00
a1d8f6310/swap/swap_io-out:
  value: 0.000000e+00
a1d8f6310/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 6.999952e-01
a1d8f6310/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
a1d8f6310/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
a1d8f6310/vmem/vmpage_number-active_anon:
  value: 5.748900e+04
a1d8f6310/vmem/vmpage_number-active_file:
  value: 1.023940e+05
a1d8f6310/vmem/vmpage_number-anon_pages:
  value: 5.851400e+04
a1d8f6310/vmem/vmpage_number-anon_transparent_hugepages:
  value: 1.920000e+02
a1d8f6310/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
a1d8f6310/vmem/vmpage_number-dirty:
  value: 2.000000e+00
a1d8f6310/vmem/vmpage_number-file_pages:
  value: 1.997240e+05
a1d8f6310/vmem/vmpage_number-free_pages:
  value: 5.739800e+04
a1d8f6310/vmem/vmpage_number-inactive_anon:
  value: 1.004470e+05
a1d8f6310/vmem/vmpage_number-inactive_file:
  value: 9.618100e+04
a1d8f6310/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
a1d8f6310/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
a1d8f6310/vmem/vmpage_number-kernel_stack:
  value: 1.280000e+02
a1d8f6310/vmem/vmpage_number-mapped:
  value: 4.414000e+03
a1d8f6310/vmem/vmpage_number-mlock:
  value: 0.000000e+00
a1d8f6310/vmem/vmpage_number-page_table_pages:
  value: 2.171000e+03
a1d8f6310/vmem/vmpage_number-shmem:
  value: 1.290000e+02
a1d8f6310/vmem/vmpage_number-slab_reclaimable:
  value: 5.264400e+04
a1d8f6310/vmem/vmpage_number-slab_unreclaimable:
  value: 5.743000e+03
a1d8f6310/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
a1d8f6310/vmem/vmpage_number-unstable:
  value: 0.000000e+00
a1d8f6310/vmem/vmpage_number-vmscan_write:
  value: 7.366000e+03
a1d8f6310/vmem/vmpage_number-writeback:
  value: 0.000000e+00
a1d8f6310/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
a1d8f6410/cpu-0/cpu-idle:
  value: 9.920010e+01
a1d8f6410/cpu-0/cpu-interrupt:
  value: 0.000000e+00
a1d8f6410/cpu-0/cpu-nice:
  value: 0.000000e+00
a1d8f6410/cpu-0/cpu-softirq:
  value: 0.000000e+00
a1d8f6410/cpu-0/cpu-steal:
  value: 0.000000e+00
a1d8f6410/cpu-0/cpu-system:
  value: 1.000001e-01
a1d8f6410/cpu-0/cpu-user:
  value: 3.000003e-01
a1d8f6410/cpu-0/cpu-wait:
  value: 0.000000e+00
a1d8f6410/cpu-1/cpu-idle:
  value: 9.900000e+01
a1d8f6410/cpu-1/cpu-interrupt:
  value: 0.000000e+00
a1d8f6410/cpu-1/cpu-nice:
  value: 0.000000e+00
a1d8f6410/cpu-1/cpu-softirq:
  value: 0.000000e+00
a1d8f6410/cpu-1/cpu-steal:
  value: 0.000000e+00
a1d8f6410/cpu-1/cpu-system:
  value: 2.000000e-01
a1d8f6410/cpu-1/cpu-user:
  value: 2.000002e-01
a1d8f6410/cpu-1/cpu-wait:
  value: 2.000000e-01
a1d8f6410/df-boot/df_complex-free:
  value: 4.369080e+08
a1d8f6410/df-boot/df_complex-reserved:
  value: 2.684109e+07
a1d8f6410/df-boot/df_complex-used:
  value: 6.467584e+07
a1d8f6410/df-boot/df_inodes-free:
  value: 3.271800e+04
a1d8f6410/df-boot/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-boot/df_inodes-used:
  value: 5.000000e+01
a1d8f6410/df-boot/percent_bytes-free:
  value: 8.268118e+01
a1d8f6410/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
a1d8f6410/df-boot/percent_bytes-used:
  value: 1.223936e+01
a1d8f6410/df-boot/percent_inodes-free:
  value: 9.984741e+01
a1d8f6410/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-boot/percent_inodes-used:
  value: 1.525879e-01
a1d8f6410/df-data1/df_complex-free:
  value: 2.740489e+10
a1d8f6410/df-data1/df_complex-reserved:
  value: 1.476235e+09
a1d8f6410/df-data1/df_complex-used:
  value: 1.803182e+08
a1d8f6410/df-data1/df_inodes-free:
  value: 1.802223e+06
a1d8f6410/df-data1/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-data1/df_inodes-used:
  value: 1.700000e+01
a1d8f6410/df-data1/percent_bytes-free:
  value: 9.429982e+01
a1d8f6410/df-data1/percent_bytes-reserved:
  value: 5.079704e+00
a1d8f6410/df-data1/percent_bytes-used:
  value: 6.204723e-01
a1d8f6410/df-data1/percent_inodes-free:
  value: 9.999906e+01
a1d8f6410/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-data1/percent_inodes-used:
  value: 9.432706e-04
a1d8f6410/df-dev-shm/df_complex-free:
  value: 2.008437e+09
a1d8f6410/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
a1d8f6410/df-dev-shm/df_complex-used:
  value: 0.000000e+00
a1d8f6410/df-dev-shm/df_inodes-free:
  value: 4.903400e+05
a1d8f6410/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
a1d8f6410/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
a1d8f6410/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
a1d8f6410/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
a1d8f6410/df-dev-shm/percent_inodes-free:
  value: 9.999979e+01
a1d8f6410/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-dev-shm/percent_inodes-used:
  value: 2.039397e-04
a1d8f6410/df-root/df_complex-free:
  value: 1.030750e+10
a1d8f6410/df-root/df_complex-reserved:
  value: 6.442435e+08
a1d8f6410/df-root/df_complex-used:
  value: 1.730961e+09
a1d8f6410/df-root/df_inodes-free:
  value: 7.294150e+05
a1d8f6410/df-root/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-root/df_inodes-used:
  value: 5.701700e+04
a1d8f6410/df-root/percent_bytes-free:
  value: 8.127210e+01
a1d8f6410/df-root/percent_bytes-reserved:
  value: 5.079700e+00
a1d8f6410/df-root/percent_bytes-used:
  value: 1.364820e+01
a1d8f6410/df-root/percent_inodes-free:
  value: 9.274991e+01
a1d8f6410/df-root/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-root/percent_inodes-used:
  value: 7.250086e+00
a1d8f6410/df-var/df_complex-free:
  value: 6.861853e+09
a1d8f6410/df-var/df_complex-reserved:
  value: 4.294943e+08
a1d8f6410/df-var/df_complex-used:
  value: 1.163772e+09
a1d8f6410/df-var/df_inodes-free:
  value: 5.104480e+05
a1d8f6410/df-var/df_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-var/df_inodes-used:
  value: 1.384000e+04
a1d8f6410/df-var/percent_bytes-free:
  value: 8.115620e+01
a1d8f6410/df-var/percent_bytes-reserved:
  value: 5.079695e+00
a1d8f6410/df-var/percent_bytes-used:
  value: 1.376411e+01
a1d8f6410/df-var/percent_inodes-free:
  value: 9.736023e+01
a1d8f6410/df-var/percent_inodes-reserved:
  value: 0.000000e+00
a1d8f6410/df-var/percent_inodes-used:
  value: 2.639771e+00
a1d8f6410/disk-vda/disk_merged:
  read: 0.000000e+00
  write: 3.569994e+01
a1d8f6410/disk-vda/disk_octets:
  read: 0.000000e+00
  write: 1.724413e+05
a1d8f6410/disk-vda/disk_ops:
  read: 0.000000e+00
  write: 6.399989e+00
a1d8f6410/disk-vda/disk_time:
  read: 0.000000e+00
  write: 3.099995e+01
a1d8f6410/disk-vda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda2/disk_merged:
  read: 0.000000e+00
  write: 3.999993e-01
a1d8f6410/disk-vda2/disk_octets:
  read: 0.000000e+00
  write: 7.372786e+03
a1d8f6410/disk-vda2/disk_ops:
  read: 0.000000e+00
  write: 1.399997e+00
a1d8f6410/disk-vda2/disk_time:
  read: 0.000000e+00
  write: 9.999982e-01
a1d8f6410/disk-vda3/disk_merged:
  read: 0.000000e+00
  write: 3.529993e+01
a1d8f6410/disk-vda3/disk_octets:
  read: 0.000000e+00
  write: 1.650685e+05
a1d8f6410/disk-vda3/disk_ops:
  read: 0.000000e+00
  write: 4.999991e+00
a1d8f6410/disk-vda3/disk_time:
  read: 0.000000e+00
  write: 3.939993e+01
a1d8f6410/disk-vda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/disk-vda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1d8f6410/load/load:
  longterm: 0.000000e+00
  midterm: 3.000000e-02
  shortterm: 5.000000e-02
a1d8f6410/memory/memory-buffered:
  value: 3.095921e+08
a1d8f6410/memory/memory-cached:
  value: 1.241870e+09
a1d8f6410/memory/memory-free:
  value: 2.926060e+08
a1d8f6410/memory/memory-used:
  value: 2.172809e+09
a1d8f6410/network/if_octets:
  rx: 0.000000e+00
  tx: 6.586998e+02
a1d8f6410/network/if_packets:
  rx: 0.000000e+00
  tx: 4.999999e-01
a1d8f6410/network/queue_length:
  value: 0.000000e+00
a1d8f6410/network/total_values-dispatch-accepted:
  value: 0.000000e+00
a1d8f6410/network/total_values-dispatch-rejected:
  value: 0.000000e+00
a1d8f6410/network/total_values-send-accepted:
  value: 1.479998e+01
a1d8f6410/network/total_values-send-rejected:
  value: 0.000000e+00
a1d8f6410/swap/swap-cached:
  value: 2.048000e+05
a1d8f6410/swap/swap-free:
  value: 2.147271e+09
a1d8f6410/swap/swap-used:
  value: 0.000000e+00
a1d8f6410/swap/swap_io-in:
  value: 0.000000e+00
a1d8f6410/swap/swap_io-out:
  value: 0.000000e+00
a1d8f6410/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 6.400115e+00
a1d8f6410/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 1.684030e+02
a1d8f6410/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
a1d8f6410/vmem/vmpage_number-active_anon:
  value: 2.143920e+05
a1d8f6410/vmem/vmpage_number-active_file:
  value: 1.979880e+05
a1d8f6410/vmem/vmpage_number-anon_pages:
  value: 2.734200e+04
a1d8f6410/vmem/vmpage_number-anon_transparent_hugepages:
  value: 3.880000e+02
a1d8f6410/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
a1d8f6410/vmem/vmpage_number-dirty:
  value: 2.700000e+01
a1d8f6410/vmem/vmpage_number-file_pages:
  value: 3.788250e+05
a1d8f6410/vmem/vmpage_number-free_pages:
  value: 7.143700e+04
a1d8f6410/vmem/vmpage_number-inactive_anon:
  value: 1.176200e+04
a1d8f6410/vmem/vmpage_number-inactive_file:
  value: 1.806700e+05
a1d8f6410/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
a1d8f6410/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
a1d8f6410/vmem/vmpage_number-kernel_stack:
  value: 3.000000e+02
a1d8f6410/vmem/vmpage_number-mapped:
  value: 1.012100e+04
a1d8f6410/vmem/vmpage_number-mlock:
  value: 0.000000e+00
a1d8f6410/vmem/vmpage_number-page_table_pages:
  value: 1.602000e+03
a1d8f6410/vmem/vmpage_number-shmem:
  value: 1.170000e+02
a1d8f6410/vmem/vmpage_number-slab_reclaimable:
  value: 2.856060e+05
a1d8f6410/vmem/vmpage_number-slab_unreclaimable:
  value: 6.201000e+03
a1d8f6410/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
a1d8f6410/vmem/vmpage_number-unstable:
  value: 0.000000e+00
a1d8f6410/vmem/vmpage_number-vmscan_write:
  value: 7.900000e+01
a1d8f6410/vmem/vmpage_number-writeback:
  value: 0.000000e+00
a1d8f6410/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
a1ddf6210/cpu-0/cpu-idle:
  value: 9.020090e+01
a1ddf6210/cpu-0/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-0/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-0/cpu-softirq:
  value: 4.000034e-01
a1ddf6210/cpu-0/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-0/cpu-system:
  value: 3.000031e-01
a1ddf6210/cpu-0/cpu-user:
  value: 4.200047e+00
a1ddf6210/cpu-0/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-1/cpu-idle:
  value: 9.969991e+01
a1ddf6210/cpu-1/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-1/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-1/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-1/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-1/cpu-system:
  value: 0.000000e+00
a1ddf6210/cpu-1/cpu-user:
  value: 2.000011e-01
a1ddf6210/cpu-1/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-10/cpu-idle:
  value: 9.979942e+01
a1ddf6210/cpu-10/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-10/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-10/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-10/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-10/cpu-system:
  value: 0.000000e+00
a1ddf6210/cpu-10/cpu-user:
  value: 9.999943e-02
a1ddf6210/cpu-10/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-11/cpu-idle:
  value: 9.989938e+01
a1ddf6210/cpu-11/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-11/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-11/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-11/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-11/cpu-system:
  value: 9.999938e-02
a1ddf6210/cpu-11/cpu-user:
  value: 0.000000e+00
a1ddf6210/cpu-11/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-2/cpu-idle:
  value: 9.959987e+01
a1ddf6210/cpu-2/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-2/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-2/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-2/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-2/cpu-system:
  value: 1.999997e-01
a1ddf6210/cpu-2/cpu-user:
  value: 9.999988e-02
a1ddf6210/cpu-2/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-3/cpu-idle:
  value: 9.989985e+01
a1ddf6210/cpu-3/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-3/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-3/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-3/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-3/cpu-system:
  value: 0.000000e+00
a1ddf6210/cpu-3/cpu-user:
  value: 0.000000e+00
a1ddf6210/cpu-3/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-4/cpu-idle:
  value: 9.989964e+01
a1ddf6210/cpu-4/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-4/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-4/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-4/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-4/cpu-system:
  value: 0.000000e+00
a1ddf6210/cpu-4/cpu-user:
  value: 0.000000e+00
a1ddf6210/cpu-4/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-5/cpu-idle:
  value: 9.989956e+01
a1ddf6210/cpu-5/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-5/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-5/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-5/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-5/cpu-system:
  value: 0.000000e+00
a1ddf6210/cpu-5/cpu-user:
  value: 0.000000e+00
a1ddf6210/cpu-5/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-6/cpu-idle:
  value: 8.899957e+01
a1ddf6210/cpu-6/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-6/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-6/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-6/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-6/cpu-system:
  value: 1.599993e+00
a1ddf6210/cpu-6/cpu-user:
  value: 9.499956e+00
a1ddf6210/cpu-6/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-7/cpu-idle:
  value: 9.969946e+01
a1ddf6210/cpu-7/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-7/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-7/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-7/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-7/cpu-system:
  value: 1.999989e-01
a1ddf6210/cpu-7/cpu-user:
  value: 9.999947e-02
a1ddf6210/cpu-7/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-8/cpu-idle:
  value: 9.079948e+01
a1ddf6210/cpu-8/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-8/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-8/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-8/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-8/cpu-system:
  value: 8.999950e-01
a1ddf6210/cpu-8/cpu-user:
  value: 8.099954e+00
a1ddf6210/cpu-8/cpu-wait:
  value: 0.000000e+00
a1ddf6210/cpu-9/cpu-idle:
  value: 9.989942e+01
a1ddf6210/cpu-9/cpu-interrupt:
  value: 0.000000e+00
a1ddf6210/cpu-9/cpu-nice:
  value: 0.000000e+00
a1ddf6210/cpu-9/cpu-softirq:
  value: 0.000000e+00
a1ddf6210/cpu-9/cpu-steal:
  value: 0.000000e+00
a1ddf6210/cpu-9/cpu-system:
  value: 9.999942e-02
a1ddf6210/cpu-9/cpu-user:
  value: 0.000000e+00
a1ddf6210/cpu-9/cpu-wait:
  value: 0.000000e+00
a1ddf6210/df-boot/df_complex-free:
  value: 3.880919e+08
a1ddf6210/df-boot/df_complex-reserved:
  value: 2.684109e+07
a1ddf6210/df-boot/df_complex-used:
  value: 1.134920e+08
a1ddf6210/df-boot/df_inodes-free:
  value: 3.270600e+04
a1ddf6210/df-boot/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-boot/df_inodes-used:
  value: 6.200000e+01
a1ddf6210/df-boot/percent_bytes-free:
  value: 7.344315e+01
a1ddf6210/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
a1ddf6210/df-boot/percent_bytes-used:
  value: 2.147740e+01
a1ddf6210/df-boot/percent_inodes-free:
  value: 9.981079e+01
a1ddf6210/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-boot/percent_inodes-used:
  value: 1.892090e-01
a1ddf6210/df-data1/df_complex-free:
  value: 5.390209e+11
a1ddf6210/df-data1/df_complex-reserved:
  value: 2.933339e+10
a1ddf6210/df-data1/df_complex-used:
  value: 9.088414e+09
a1ddf6210/df-data1/df_inodes-free:
  value: 3.587710e+07
a1ddf6210/df-data1/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-data1/df_inodes-used:
  value: 7.000000e+01
a1ddf6210/df-data1/percent_bytes-free:
  value: 9.334621e+01
a1ddf6210/df-data1/percent_bytes-reserved:
  value: 5.079879e+00
a1ddf6210/df-data1/percent_bytes-used:
  value: 1.573908e+00
a1ddf6210/df-data1/percent_inodes-free:
  value: 9.999979e+01
a1ddf6210/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-data1/percent_inodes-used:
  value: 1.951102e-04
a1ddf6210/df-data2/df_complex-free:
  value: 1.042212e+13
a1ddf6210/df-data2/df_complex-reserved:
  value: 1.200219e+11
a1ddf6210/df-data2/df_complex-used:
  value: 1.271712e+12
a1ddf6210/df-data2/df_inodes-free:
  value: 7.319628e+08
a1ddf6210/df-data2/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-data2/df_inodes-used:
  value: 5.985960e+05
a1ddf6210/df-data2/percent_bytes-free:
  value: 8.821947e+01
a1ddf6210/df-data2/percent_bytes-reserved:
  value: 1.015942e+00
a1ddf6210/df-data2/percent_bytes-used:
  value: 1.076458e+01
a1ddf6210/df-data2/percent_inodes-free:
  value: 9.991829e+01
a1ddf6210/df-data2/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-data2/percent_inodes-used:
  value: 8.171274e-02
a1ddf6210/df-data3/df_complex-free:
  value: 1.143610e+13
a1ddf6210/df-data3/df_complex-reserved:
  value: 1.200219e+11
a1ddf6210/df-data3/df_complex-used:
  value: 2.577290e+11
a1ddf6210/df-data3/df_inodes-free:
  value: 7.323576e+08
a1ddf6210/df-data3/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-data3/df_inodes-used:
  value: 2.037680e+05
a1ddf6210/df-data3/percent_bytes-free:
  value: 9.680247e+01
a1ddf6210/df-data3/percent_bytes-reserved:
  value: 1.015942e+00
a1ddf6210/df-data3/percent_bytes-used:
  value: 2.181582e+00
a1ddf6210/df-data3/percent_inodes-free:
  value: 9.997218e+01
a1ddf6210/df-data3/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-data3/percent_inodes-used:
  value: 2.781583e-02
a1ddf6210/df-dev-shm/df_complex-free:
  value: 3.375733e+10
a1ddf6210/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
a1ddf6210/df-dev-shm/df_complex-used:
  value: 0.000000e+00
a1ddf6210/df-dev-shm/df_inodes-free:
  value: 8.241535e+06
a1ddf6210/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
a1ddf6210/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
a1ddf6210/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
a1ddf6210/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
a1ddf6210/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
a1ddf6210/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-dev-shm/percent_inodes-used:
  value: 1.213366e-05
a1ddf6210/df-root/df_complex-free:
  value: 2.006712e+08
a1ddf6210/df-root/df_complex-reserved:
  value: 1.073725e+08
a1ddf6210/df-root/df_complex-used:
  value: 1.805705e+09
a1ddf6210/df-root/df_inodes-free:
  value: 7.542600e+04
a1ddf6210/df-root/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-root/df_inodes-used:
  value: 5.564600e+04
a1ddf6210/df-root/percent_bytes-free:
  value: 9.493617e+00
a1ddf6210/df-root/percent_bytes-reserved:
  value: 5.079720e+00
a1ddf6210/df-root/percent_bytes-used:
  value: 8.542667e+01
a1ddf6210/df-root/percent_inodes-free:
  value: 5.754547e+01
a1ddf6210/df-root/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-root/percent_inodes-used:
  value: 4.245453e+01
a1ddf6210/df-tmp/df_complex-free:
  value: 1.102356e+08
a1ddf6210/df-tmp/df_complex-reserved:
  value: 0.000000e+00
a1ddf6210/df-tmp/df_complex-used:
  value: 2.398208e+07
a1ddf6210/df-tmp/df_inodes-free:
  value: 8.241515e+06
a1ddf6210/df-tmp/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-tmp/df_inodes-used:
  value: 2.100000e+01
a1ddf6210/df-tmp/percent_bytes-free:
  value: 8.213196e+01
a1ddf6210/df-tmp/percent_bytes-reserved:
  value: 0.000000e+00
a1ddf6210/df-tmp/percent_bytes-used:
  value: 1.786804e+01
a1ddf6210/df-tmp/percent_inodes-free:
  value: 9.999974e+01
a1ddf6210/df-tmp/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-tmp/percent_inodes-used:
  value: 2.548069e-04
a1ddf6210/df-var/df_complex-free:
  value: 6.870528e+09
a1ddf6210/df-var/df_complex-reserved:
  value: 4.294943e+08
a1ddf6210/df-var/df_complex-used:
  value: 1.155097e+09
a1ddf6210/df-var/df_inodes-free:
  value: 5.196230e+05
a1ddf6210/df-var/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-var/df_inodes-used:
  value: 4.665000e+03
a1ddf6210/df-var/percent_bytes-free:
  value: 8.125880e+01
a1ddf6210/df-var/percent_bytes-reserved:
  value: 5.079695e+00
a1ddf6210/df-var/percent_bytes-used:
  value: 1.366151e+01
a1ddf6210/df-var/percent_inodes-free:
  value: 9.911022e+01
a1ddf6210/df-var/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6210/df-var/percent_inodes-used:
  value: 8.897781e-01
a1ddf6210/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 2.080016e+01
a1ddf6210/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 1.212425e+05
a1ddf6210/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 8.700063e+00
a1ddf6210/disk-sda/disk_time:
  read: 0.000000e+00
  write: 1.000007e-01
a1ddf6210/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 1.960018e+01
a1ddf6210/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 1.138698e+05
a1ddf6210/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 8.200073e+00
a1ddf6210/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 1.000009e-01
a1ddf6210/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 1.200011e+00
a1ddf6210/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 6.144060e+03
a1ddf6210/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 3.000029e-01
a1ddf6210/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 1.228812e+03
a1ddf6210/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 2.000019e-01
a1ddf6210/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sdb/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sdb/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sdb/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sdb/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sdc/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sdc/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sdc/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/disk-sdc/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6210/load/load:
  longterm: 9.000000e-02
  midterm: 1.400000e-01
  shortterm: 1.400000e-01
a1ddf6210/memory/memory-buffered:
  value: 7.775150e+08
a1ddf6210/memory/memory-cached:
  value: 2.349433e+10
a1ddf6210/memory/memory-free:
  value: 2.759657e+10
a1ddf6210/memory/memory-used:
  value: 1.564624e+10
a1ddf6210/network/if_octets:
  rx: 0.000000e+00
  tx: 1.455100e+03
a1ddf6210/network/if_packets:
  rx: 0.000000e+00
  tx: 1.100000e+00
a1ddf6210/network/queue_length:
  value: 0.000000e+00
a1ddf6210/network/total_values-dispatch-accepted:
  value: 0.000000e+00
a1ddf6210/network/total_values-dispatch-rejected:
  value: 0.000000e+00
a1ddf6210/network/total_values-send-accepted:
  value: 2.720003e+01
a1ddf6210/network/total_values-send-rejected:
  value: 0.000000e+00
a1ddf6210/swap/swap-cached:
  value: 0.000000e+00
a1ddf6210/swap/swap-free:
  value: 2.147475e+09
a1ddf6210/swap/swap-used:
  value: 0.000000e+00
a1ddf6210/swap/swap_io-in:
  value: 0.000000e+00
a1ddf6210/swap/swap_io-out:
  value: 0.000000e+00
a1ddf6210/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 1.770240e+04
a1ddf6210/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 1.100000e+02
a1ddf6210/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
a1ddf6210/vmem/vmpage_number-active_anon:
  value: 7.455600e+05
a1ddf6210/vmem/vmpage_number-active_file:
  value: 3.175860e+05
a1ddf6210/vmem/vmpage_number-anon_pages:
  value: 5.005000e+04
a1ddf6210/vmem/vmpage_number-anon_transparent_hugepages:
  value: 1.356000e+03
a1ddf6210/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
a1ddf6210/vmem/vmpage_number-dirty:
  value: 1.550000e+02
a1ddf6210/vmem/vmpage_number-file_pages:
  value: 5.925744e+06
a1ddf6210/vmem/vmpage_number-free_pages:
  value: 6.737445e+06
a1ddf6210/vmem/vmpage_number-inactive_anon:
  value: 4.923000e+03
a1ddf6210/vmem/vmpage_number-inactive_file:
  value: 5.601994e+06
a1ddf6210/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
a1ddf6210/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
a1ddf6210/vmem/vmpage_number-kernel_stack:
  value: 3.480000e+02
a1ddf6210/vmem/vmpage_number-mapped:
  value: 6.515000e+03
a1ddf6210/vmem/vmpage_number-mlock:
  value: 0.000000e+00
a1ddf6210/vmem/vmpage_number-page_table_pages:
  value: 4.479000e+03
a1ddf6210/vmem/vmpage_number-shmem:
  value: 6.170000e+03
a1ddf6210/vmem/vmpage_number-slab_reclaimable:
  value: 2.916436e+06
a1ddf6210/vmem/vmpage_number-slab_unreclaimable:
  value: 4.737700e+04
a1ddf6210/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
a1ddf6210/vmem/vmpage_number-unstable:
  value: 0.000000e+00
a1ddf6210/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
a1ddf6210/vmem/vmpage_number-writeback:
  value: 0.000000e+00
a1ddf6210/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
a1ddf6510/cpu-0/cpu-idle:
  value: 9.159977e+01
a1ddf6510/cpu-0/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-0/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-0/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-0/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-0/cpu-system:
  value: 2.999997e+00
a1ddf6510/cpu-0/cpu-user:
  value: 5.200008e+00
a1ddf6510/cpu-0/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-1/cpu-idle:
  value: 9.989949e+01
a1ddf6510/cpu-1/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-1/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-1/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-1/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-1/cpu-system:
  value: 9.999952e-02
a1ddf6510/cpu-1/cpu-user:
  value: 0.000000e+00
a1ddf6510/cpu-1/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-10/cpu-idle:
  value: 9.799903e+01
a1ddf6510/cpu-10/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-10/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-10/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-10/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-10/cpu-system:
  value: 1.199988e+00
a1ddf6510/cpu-10/cpu-user:
  value: 5.999943e-01
a1ddf6510/cpu-10/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-11/cpu-idle:
  value: 9.999894e+01
a1ddf6510/cpu-11/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-11/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-11/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-11/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-11/cpu-system:
  value: 0.000000e+00
a1ddf6510/cpu-11/cpu-user:
  value: 0.000000e+00
a1ddf6510/cpu-11/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-2/cpu-idle:
  value: 9.989933e+01
a1ddf6510/cpu-2/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-2/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-2/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-2/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-2/cpu-system:
  value: 0.000000e+00
a1ddf6510/cpu-2/cpu-user:
  value: 0.000000e+00
a1ddf6510/cpu-2/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-3/cpu-idle:
  value: 9.989927e+01
a1ddf6510/cpu-3/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-3/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-3/cpu-softirq:
  value: 9.999927e-02
a1ddf6510/cpu-3/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-3/cpu-system:
  value: 0.000000e+00
a1ddf6510/cpu-3/cpu-user:
  value: 0.000000e+00
a1ddf6510/cpu-3/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-4/cpu-idle:
  value: 9.999933e+01
a1ddf6510/cpu-4/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-4/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-4/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-4/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-4/cpu-system:
  value: 0.000000e+00
a1ddf6510/cpu-4/cpu-user:
  value: 9.999930e-02
a1ddf6510/cpu-4/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-5/cpu-idle:
  value: 9.999932e+01
a1ddf6510/cpu-5/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-5/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-5/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-5/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-5/cpu-system:
  value: 0.000000e+00
a1ddf6510/cpu-5/cpu-user:
  value: 0.000000e+00
a1ddf6510/cpu-5/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-6/cpu-idle:
  value: 9.589926e+01
a1ddf6510/cpu-6/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-6/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-6/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-6/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-6/cpu-system:
  value: 2.099986e+00
a1ddf6510/cpu-6/cpu-user:
  value: 1.999987e+00
a1ddf6510/cpu-6/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-7/cpu-idle:
  value: 9.679919e+01
a1ddf6510/cpu-7/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-7/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-7/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-7/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-7/cpu-system:
  value: 2.199982e+00
a1ddf6510/cpu-7/cpu-user:
  value: 1.099991e+00
a1ddf6510/cpu-7/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-8/cpu-idle:
  value: 9.439919e+01
a1ddf6510/cpu-8/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-8/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-8/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-8/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-8/cpu-system:
  value: 4.399963e+00
a1ddf6510/cpu-8/cpu-user:
  value: 1.099991e+00
a1ddf6510/cpu-8/cpu-wait:
  value: 0.000000e+00
a1ddf6510/cpu-9/cpu-idle:
  value: 9.949910e+01
a1ddf6510/cpu-9/cpu-interrupt:
  value: 0.000000e+00
a1ddf6510/cpu-9/cpu-nice:
  value: 0.000000e+00
a1ddf6510/cpu-9/cpu-softirq:
  value: 0.000000e+00
a1ddf6510/cpu-9/cpu-steal:
  value: 0.000000e+00
a1ddf6510/cpu-9/cpu-system:
  value: 1.999982e-01
a1ddf6510/cpu-9/cpu-user:
  value: 9.999913e-02
a1ddf6510/cpu-9/cpu-wait:
  value: 0.000000e+00
a1ddf6510/df-boot/df_complex-free:
  value: 4.369039e+08
a1ddf6510/df-boot/df_complex-reserved:
  value: 2.684109e+07
a1ddf6510/df-boot/df_complex-used:
  value: 6.467994e+07
a1ddf6510/df-boot/df_inodes-free:
  value: 3.271800e+04
a1ddf6510/df-boot/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-boot/df_inodes-used:
  value: 5.000000e+01
a1ddf6510/df-boot/percent_bytes-free:
  value: 8.268041e+01
a1ddf6510/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
a1ddf6510/df-boot/percent_bytes-used:
  value: 1.224014e+01
a1ddf6510/df-boot/percent_inodes-free:
  value: 9.984741e+01
a1ddf6510/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-boot/percent_inodes-used:
  value: 1.525879e-01
a1ddf6510/df-data1/df_complex-free:
  value: 5.378703e+11
a1ddf6510/df-data1/df_complex-reserved:
  value: 2.879652e+10
a1ddf6510/df-data1/df_complex-used:
  value: 2.072412e+08
a1ddf6510/df-data1/df_inodes-free:
  value: 3.522052e+07
a1ddf6510/df-data1/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-data1/df_inodes-used:
  value: 1.300000e+01
a1ddf6510/df-data1/percent_bytes-free:
  value: 9.488357e+01
a1ddf6510/df-data1/percent_bytes-reserved:
  value: 5.079879e+00
a1ddf6510/df-data1/percent_bytes-used:
  value: 3.655860e-02
a1ddf6510/df-data1/percent_inodes-free:
  value: 9.999996e+01
a1ddf6510/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-data1/percent_inodes-used:
  value: 3.691029e-05
a1ddf6510/df-dev-shm/df_complex-free:
  value: 3.375725e+10
a1ddf6510/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
a1ddf6510/df-dev-shm/df_complex-used:
  value: 0.000000e+00
a1ddf6510/df-dev-shm/df_inodes-free:
  value: 8.241515e+06
a1ddf6510/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
a1ddf6510/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
a1ddf6510/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
a1ddf6510/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
a1ddf6510/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
a1ddf6510/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-dev-shm/percent_inodes-used:
  value: 1.213369e-05
a1ddf6510/df-root/df_complex-free:
  value: 1.057486e+10
a1ddf6510/df-root/df_complex-reserved:
  value: 6.442435e+08
a1ddf6510/df-root/df_complex-used:
  value: 1.463607e+09
a1ddf6510/df-root/df_inodes-free:
  value: 7.435400e+05
a1ddf6510/df-root/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-root/df_inodes-used:
  value: 4.289200e+04
a1ddf6510/df-root/percent_bytes-free:
  value: 8.338012e+01
a1ddf6510/df-root/percent_bytes-reserved:
  value: 5.079700e+00
a1ddf6510/df-root/percent_bytes-used:
  value: 1.154018e+01
a1ddf6510/df-root/percent_inodes-free:
  value: 9.454601e+01
a1ddf6510/df-root/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-root/percent_inodes-used:
  value: 5.454000e+00
a1ddf6510/df-var/df_complex-free:
  value: 6.990479e+09
a1ddf6510/df-var/df_complex-reserved:
  value: 4.294943e+08
a1ddf6510/df-var/df_complex-used:
  value: 1.035145e+09
a1ddf6510/df-var/df_inodes-free:
  value: 5.169860e+05
a1ddf6510/df-var/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-var/df_inodes-used:
  value: 7.302000e+03
a1ddf6510/df-var/percent_bytes-free:
  value: 8.267748e+01
a1ddf6510/df-var/percent_bytes-reserved:
  value: 5.079695e+00
a1ddf6510/df-var/percent_bytes-used:
  value: 1.224282e+01
a1ddf6510/df-var/percent_inodes-free:
  value: 9.860725e+01
a1ddf6510/df-var/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6510/df-var/percent_inodes-used:
  value: 1.392746e+00
a1ddf6510/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 2.457548e+03
a1ddf6510/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 5.999873e-01
a1ddf6510/disk-sda/disk_time:
  read: 0.000000e+00
  write: 9.999788e-01
a1ddf6510/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 2.457548e+03
a1ddf6510/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 5.999872e-01
a1ddf6510/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 9.999786e-01
a1ddf6510/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6510/load/load:
  longterm: 0.000000e+00
  midterm: 0.000000e+00
  shortterm: 0.000000e+00
a1ddf6510/memory/memory-buffered:
  value: 2.371871e+08
a1ddf6510/memory/memory-cached:
  value: 1.447993e+09
a1ddf6510/memory/memory-free:
  value: 6.351406e+10
a1ddf6510/memory/memory-used:
  value: 2.315260e+09
a1ddf6510/network/if_octets:
  rx: 0.000000e+00
  tx: 1.052800e+03
a1ddf6510/network/if_packets:
  rx: 0.000000e+00
  tx: 8.000000e-01
a1ddf6510/network/queue_length:
  value: 0.000000e+00
a1ddf6510/network/total_values-dispatch-accepted:
  value: 0.000000e+00
a1ddf6510/network/total_values-dispatch-rejected:
  value: 0.000000e+00
a1ddf6510/network/total_values-send-accepted:
  value: 2.280001e+01
a1ddf6510/network/total_values-send-rejected:
  value: 0.000000e+00
a1ddf6510/swap/swap-cached:
  value: 0.000000e+00
a1ddf6510/swap/swap-free:
  value: 2.147475e+09
a1ddf6510/swap/swap-used:
  value: 0.000000e+00
a1ddf6510/swap/swap_io-in:
  value: 0.000000e+00
a1ddf6510/swap/swap_io-out:
  value: 0.000000e+00
a1ddf6510/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 3.700034e+00
a1ddf6510/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
a1ddf6510/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
a1ddf6510/vmem/vmpage_number-active_anon:
  value: 8.369400e+04
a1ddf6510/vmem/vmpage_number-active_file:
  value: 2.449340e+05
a1ddf6510/vmem/vmpage_number-anon_pages:
  value: 4.576400e+04
a1ddf6510/vmem/vmpage_number-anon_transparent_hugepages:
  value: 7.500000e+01
a1ddf6510/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
a1ddf6510/vmem/vmpage_number-dirty:
  value: 1.000000e+00
a1ddf6510/vmem/vmpage_number-file_pages:
  value: 4.114210e+05
a1ddf6510/vmem/vmpage_number-free_pages:
  value: 1.550636e+07
a1ddf6510/vmem/vmpage_number-inactive_anon:
  value: 2.130000e+02
a1ddf6510/vmem/vmpage_number-inactive_file:
  value: 1.662360e+05
a1ddf6510/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
a1ddf6510/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
a1ddf6510/vmem/vmpage_number-kernel_stack:
  value: 3.110000e+02
a1ddf6510/vmem/vmpage_number-mapped:
  value: 6.615000e+03
a1ddf6510/vmem/vmpage_number-mlock:
  value: 0.000000e+00
a1ddf6510/vmem/vmpage_number-page_table_pages:
  value: 2.683000e+03
a1ddf6510/vmem/vmpage_number-shmem:
  value: 2.550000e+02
a1ddf6510/vmem/vmpage_number-slab_reclaimable:
  value: 3.633060e+05
a1ddf6510/vmem/vmpage_number-slab_unreclaimable:
  value: 1.422200e+04
a1ddf6510/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
a1ddf6510/vmem/vmpage_number-unstable:
  value: 0.000000e+00
a1ddf6510/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
a1ddf6510/vmem/vmpage_number-writeback:
  value: 0.000000e+00
a1ddf6510/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
a1ddf6610/cpu-0/cpu-idle:
  value: 8.969821e+01
a1ddf6610/cpu-0/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-0/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-0/cpu-softirq:
  value: 1.099984e+00
a1ddf6610/cpu-0/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-0/cpu-system:
  value: 3.499955e+00
a1ddf6610/cpu-0/cpu-user:
  value: 2.199975e+00
a1ddf6610/cpu-0/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-1/cpu-idle:
  value: 9.719322e+01
a1ddf6610/cpu-1/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-1/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-1/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-1/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-1/cpu-system:
  value: 1.899957e+00
a1ddf6610/cpu-1/cpu-user:
  value: 5.999838e-01
a1ddf6610/cpu-1/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-10/cpu-idle:
  value: 9.979814e+01
a1ddf6610/cpu-10/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-10/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-10/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-10/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-10/cpu-system:
  value: 9.999814e-02
a1ddf6610/cpu-10/cpu-user:
  value: 0.000000e+00
a1ddf6610/cpu-10/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-11/cpu-idle:
  value: 9.979789e+01
a1ddf6610/cpu-11/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-11/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-11/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-11/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-11/cpu-system:
  value: 9.999790e-02
a1ddf6610/cpu-11/cpu-user:
  value: 0.000000e+00
a1ddf6610/cpu-11/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-12/cpu-idle:
  value: 9.709848e+01
a1ddf6610/cpu-12/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-12/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-12/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-12/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-12/cpu-system:
  value: 1.599972e+00
a1ddf6610/cpu-12/cpu-user:
  value: 1.199978e+00
a1ddf6610/cpu-12/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-13/cpu-idle:
  value: 7.879859e+01
a1ddf6610/cpu-13/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-13/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-13/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-13/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-13/cpu-system:
  value: 1.309976e+01
a1ddf6610/cpu-13/cpu-user:
  value: 8.099863e+00
a1ddf6610/cpu-13/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-14/cpu-idle:
  value: 9.309875e+01
a1ddf6610/cpu-14/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-14/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-14/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-14/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-14/cpu-system:
  value: 4.699934e+00
a1ddf6610/cpu-14/cpu-user:
  value: 2.099971e+00
a1ddf6610/cpu-14/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-15/cpu-idle:
  value: 8.789892e+01
a1ddf6610/cpu-15/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-15/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-15/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-15/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-15/cpu-system:
  value: 7.999894e+00
a1ddf6610/cpu-15/cpu-user:
  value: 3.599950e+00
a1ddf6610/cpu-15/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-16/cpu-idle:
  value: 9.959880e+01
a1ddf6610/cpu-16/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-16/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-16/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-16/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-16/cpu-system:
  value: 2.999964e-01
a1ddf6610/cpu-16/cpu-user:
  value: 9.999874e-02
a1ddf6610/cpu-16/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-17/cpu-idle:
  value: 9.959911e+01
a1ddf6610/cpu-17/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-17/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-17/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-17/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-17/cpu-system:
  value: 1.999981e-01
a1ddf6610/cpu-17/cpu-user:
  value: 9.999911e-02
a1ddf6610/cpu-17/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-18/cpu-idle:
  value: 9.519927e+01
a1ddf6610/cpu-18/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-18/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-18/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-18/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-18/cpu-system:
  value: 2.499979e+00
a1ddf6610/cpu-18/cpu-user:
  value: 1.899982e+00
a1ddf6610/cpu-18/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-19/cpu-idle:
  value: 8.319942e+01
a1ddf6610/cpu-19/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-19/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-19/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-19/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-19/cpu-system:
  value: 1.189992e+01
a1ddf6610/cpu-19/cpu-user:
  value: 3.999985e+00
a1ddf6610/cpu-19/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-2/cpu-idle:
  value: 9.899200e+01
a1ddf6610/cpu-2/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-2/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-2/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-2/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-2/cpu-system:
  value: 3.999674e-01
a1ddf6610/cpu-2/cpu-user:
  value: 2.999799e-01
a1ddf6610/cpu-2/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-20/cpu-idle:
  value: 9.389955e+01
a1ddf6610/cpu-20/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-20/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-20/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-20/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-20/cpu-system:
  value: 4.199980e+00
a1ddf6610/cpu-20/cpu-user:
  value: 1.399991e+00
a1ddf6610/cpu-20/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-21/cpu-idle:
  value: 8.779956e+01
a1ddf6610/cpu-21/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-21/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-21/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-21/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-21/cpu-system:
  value: 8.199940e+00
a1ddf6610/cpu-21/cpu-user:
  value: 3.599972e+00
a1ddf6610/cpu-21/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-22/cpu-idle:
  value: 9.959932e+01
a1ddf6610/cpu-22/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-22/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-22/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-22/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-22/cpu-system:
  value: 2.999981e-01
a1ddf6610/cpu-22/cpu-user:
  value: 9.999956e-02
a1ddf6610/cpu-22/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-23/cpu-idle:
  value: 9.929916e+01
a1ddf6610/cpu-23/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-23/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-23/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-23/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-23/cpu-system:
  value: 4.999943e-01
a1ddf6610/cpu-23/cpu-user:
  value: 0.000000e+00
a1ddf6610/cpu-23/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-3/cpu-idle:
  value: 9.949283e+01
a1ddf6610/cpu-3/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-3/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-3/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-3/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-3/cpu-system:
  value: 9.999309e-02
a1ddf6610/cpu-3/cpu-user:
  value: 1.999852e-01
a1ddf6610/cpu-3/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-4/cpu-idle:
  value: 9.989305e+01
a1ddf6610/cpu-4/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-4/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-4/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-4/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-4/cpu-system:
  value: 0.000000e+00
a1ddf6610/cpu-4/cpu-user:
  value: 0.000000e+00
a1ddf6610/cpu-4/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-5/cpu-idle:
  value: 9.989362e+01
a1ddf6610/cpu-5/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-5/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-5/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-5/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-5/cpu-system:
  value: 9.999406e-02
a1ddf6610/cpu-5/cpu-user:
  value: 0.000000e+00
a1ddf6610/cpu-5/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-6/cpu-idle:
  value: 9.469571e+01
a1ddf6610/cpu-6/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-6/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-6/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-6/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-6/cpu-system:
  value: 3.499803e+00
a1ddf6610/cpu-6/cpu-user:
  value: 1.199947e+00
a1ddf6610/cpu-6/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-7/cpu-idle:
  value: 9.819573e+01
a1ddf6610/cpu-7/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-7/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-7/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-7/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-7/cpu-system:
  value: 1.299941e+00
a1ddf6610/cpu-7/cpu-user:
  value: 2.999861e-01
a1ddf6610/cpu-7/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-8/cpu-idle:
  value: 9.929729e+01
a1ddf6610/cpu-8/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-8/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-8/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-8/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-8/cpu-system:
  value: 1.999943e-01
a1ddf6610/cpu-8/cpu-user:
  value: 9.999644e-02
a1ddf6610/cpu-8/cpu-wait:
  value: 0.000000e+00
a1ddf6610/cpu-9/cpu-idle:
  value: 9.989737e+01
a1ddf6610/cpu-9/cpu-interrupt:
  value: 0.000000e+00
a1ddf6610/cpu-9/cpu-nice:
  value: 0.000000e+00
a1ddf6610/cpu-9/cpu-softirq:
  value: 0.000000e+00
a1ddf6610/cpu-9/cpu-steal:
  value: 0.000000e+00
a1ddf6610/cpu-9/cpu-system:
  value: 9.999736e-02
a1ddf6610/cpu-9/cpu-user:
  value: 0.000000e+00
a1ddf6610/cpu-9/cpu-wait:
  value: 0.000000e+00
a1ddf6610/df-boot/df_complex-free:
  value: 4.793754e+08
a1ddf6610/df-boot/df_complex-reserved:
  value: 2.684109e+07
a1ddf6610/df-boot/df_complex-used:
  value: 2.220851e+07
a1ddf6610/df-boot/df_inodes-free:
  value: 3.273000e+04
a1ddf6610/df-boot/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-boot/df_inodes-used:
  value: 3.800000e+01
a1ddf6610/df-boot/percent_bytes-free:
  value: 9.071777e+01
a1ddf6610/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
a1ddf6610/df-boot/percent_bytes-used:
  value: 4.202775e+00
a1ddf6610/df-boot/percent_inodes-free:
  value: 9.988403e+01
a1ddf6610/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-boot/percent_inodes-used:
  value: 1.159668e-01
a1ddf6610/df-data1/df_complex-free:
  value: 2.574899e+11
a1ddf6610/df-data1/df_complex-reserved:
  value: 1.379019e+10
a1ddf6610/df-data1/df_complex-used:
  value: 1.961411e+08
a1ddf6610/df-data1/df_inodes-free:
  value: 1.683454e+07
a1ddf6610/df-data1/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-data1/df_inodes-used:
  value: 1.800000e+01
a1ddf6610/df-data1/percent_bytes-free:
  value: 9.484805e+01
a1ddf6610/df-data1/percent_bytes-reserved:
  value: 5.079704e+00
a1ddf6610/df-data1/percent_bytes-used:
  value: 7.224981e-02
a1ddf6610/df-data1/percent_inodes-free:
  value: 9.999989e+01
a1ddf6610/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-data1/percent_inodes-used:
  value: 1.069229e-04
a1ddf6610/df-dev-shm/df_complex-free:
  value: 3.375709e+10
a1ddf6610/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
a1ddf6610/df-dev-shm/df_complex-used:
  value: 0.000000e+00
a1ddf6610/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
a1ddf6610/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
a1ddf6610/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
a1ddf6610/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
a1ddf6610/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
a1ddf6610/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
a1ddf6610/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
a1ddf6610/df-root/df_complex-free:
  value: 1.030392e+10
a1ddf6610/df-root/df_complex-reserved:
  value: 6.442435e+08
a1ddf6610/df-root/df_complex-used:
  value: 1.734545e+09
a1ddf6610/df-root/df_inodes-free:
  value: 7.387960e+05
a1ddf6610/df-root/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-root/df_inodes-used:
  value: 4.763600e+04
a1ddf6610/df-root/percent_bytes-free:
  value: 8.124384e+01
a1ddf6610/df-root/percent_bytes-reserved:
  value: 5.079700e+00
a1ddf6610/df-root/percent_bytes-used:
  value: 1.367646e+01
a1ddf6610/df-root/percent_inodes-free:
  value: 9.394276e+01
a1ddf6610/df-root/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-root/percent_inodes-used:
  value: 6.057231e+00
a1ddf6610/df-var/df_complex-free:
  value: 5.412876e+09
a1ddf6610/df-var/df_complex-reserved:
  value: 4.294943e+08
a1ddf6610/df-var/df_complex-used:
  value: 2.612748e+09
a1ddf6610/df-var/df_inodes-free:
  value: 5.218510e+05
a1ddf6610/df-var/df_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-var/df_inodes-used:
  value: 2.437000e+03
a1ddf6610/df-var/percent_bytes-free:
  value: 6.401893e+01
a1ddf6610/df-var/percent_bytes-reserved:
  value: 5.079695e+00
a1ddf6610/df-var/percent_bytes-used:
  value: 3.090138e+01
a1ddf6610/df-var/percent_inodes-free:
  value: 9.953518e+01
a1ddf6610/df-var/percent_inodes-reserved:
  value: 0.000000e+00
a1ddf6610/df-var/percent_inodes-used:
  value: 4.648209e-01
a1ddf6610/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 1.224974e+02
a1ddf6610/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 5.377975e+05
a1ddf6610/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 8.799739e+00
a1ddf6610/disk-sda/disk_time:
  read: 0.000000e+00
  write: 1.599958e+00
a1ddf6610/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 1.148967e+02
a1ddf6610/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 4.829033e+05
a1ddf6610/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 2.999895e+00
a1ddf6610/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 3.999861e-01
a1ddf6610/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 7.599447e+00
a1ddf6610/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 5.488478e+04
a1ddf6610/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 5.799747e+00
a1ddf6610/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 2.299833e+00
a1ddf6610/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
a1ddf6610/load/load:
  longterm: 2.600000e-01
  midterm: 2.400000e-01
  shortterm: 3.000000e-02
a1ddf6610/memory/memory-buffered:
  value: 2.945802e+08
a1ddf6610/memory/memory-cached:
  value: 2.390438e+09
a1ddf6610/memory/memory-free:
  value: 6.154120e+10
a1ddf6610/memory/memory-used:
  value: 3.287953e+09
a1ddf6610/network/if_octets:
  rx: 2.326949e+04
  tx: 0.000000e+00
a1ddf6610/network/if_packets:
  rx: 1.759976e+01
  tx: 0.000000e+00
a1ddf6610/network/queue_length:
  value: 0.000000e+00
a1ddf6610/network/total_values-dispatch-accepted:
  value: 4.607941e+02
a1ddf6610/network/total_values-dispatch-rejected:
  value: 0.000000e+00
a1ddf6610/network/total_values-send-accepted:
  value: 0.000000e+00
a1ddf6610/network/total_values-send-rejected:
  value: 0.000000e+00
a1ddf6610/swap/swap-cached:
  value: 0.000000e+00
a1ddf6610/swap/swap-free:
  value: 2.147475e+09
a1ddf6610/swap/swap-used:
  value: 0.000000e+00
a1ddf6610/swap/swap_io-in:
  value: 0.000000e+00
a1ddf6610/swap/swap_io-out:
  value: 0.000000e+00
a1ddf6610/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 1.460435e+05
a1ddf6610/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 5.252066e+02
a1ddf6610/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
a1ddf6610/vmem/vmpage_number-active_anon:
  value: 7.743400e+04
a1ddf6610/vmem/vmpage_number-active_file:
  value: 5.886950e+05
a1ddf6610/vmem/vmpage_number-anon_pages:
  value: 3.168900e+04
a1ddf6610/vmem/vmpage_number-anon_transparent_hugepages:
  value: 8.900000e+01
a1ddf6610/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
a1ddf6610/vmem/vmpage_number-dirty:
  value: 1.217000e+03
a1ddf6610/vmem/vmpage_number-file_pages:
  value: 6.555230e+05
a1ddf6610/vmem/vmpage_number-free_pages:
  value: 1.502482e+07
a1ddf6610/vmem/vmpage_number-inactive_anon:
  value: 1.440000e+02
a1ddf6610/vmem/vmpage_number-inactive_file:
  value: 6.663400e+04
a1ddf6610/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
a1ddf6610/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
a1ddf6610/vmem/vmpage_number-kernel_stack:
  value: 5.050000e+02
a1ddf6610/vmem/vmpage_number-mapped:
  value: 7.061000e+03
a1ddf6610/vmem/vmpage_number-mlock:
  value: 0.000000e+00
a1ddf6610/vmem/vmpage_number-page_table_pages:
  value: 2.714000e+03
a1ddf6610/vmem/vmpage_number-shmem:
  value: 2.010000e+02
a1ddf6610/vmem/vmpage_number-slab_reclaimable:
  value: 5.981700e+05
a1ddf6610/vmem/vmpage_number-slab_unreclaimable:
  value: 1.751300e+04
a1ddf6610/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
a1ddf6610/vmem/vmpage_number-unstable:
  value: 0.000000e+00
a1ddf6610/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
a1ddf6610/vmem/vmpage_number-writeback:
  value: 0.000000e+00
a1ddf6610/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
h2gdf6120/cpu-0/cpu-idle:
  value: 9.830055e+01
h2gdf6120/cpu-0/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-0/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-0/cpu-softirq:
  value: 1.000006e-01
h2gdf6120/cpu-0/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-0/cpu-system:
  value: 2.000012e-01
h2gdf6120/cpu-0/cpu-user:
  value: 7.000045e-01
h2gdf6120/cpu-0/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-1/cpu-idle:
  value: 9.630047e+01
h2gdf6120/cpu-1/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-1/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-1/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-1/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-1/cpu-system:
  value: 5.000024e-01
h2gdf6120/cpu-1/cpu-user:
  value: 3.300017e+00
h2gdf6120/cpu-1/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-10/cpu-idle:
  value: 9.990053e+01
h2gdf6120/cpu-10/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-10/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-10/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-10/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-10/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-10/cpu-user:
  value: 1.000005e-01
h2gdf6120/cpu-10/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-11/cpu-idle:
  value: 9.990010e+01
h2gdf6120/cpu-11/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-11/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-11/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-11/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-11/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-11/cpu-user:
  value: 0.000000e+00
h2gdf6120/cpu-11/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-12/cpu-idle:
  value: 9.600001e+01
h2gdf6120/cpu-12/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-12/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-12/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-12/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-12/cpu-system:
  value: 7.000001e-01
h2gdf6120/cpu-12/cpu-user:
  value: 2.900000e+00
h2gdf6120/cpu-12/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-13/cpu-idle:
  value: 9.740004e+01
h2gdf6120/cpu-13/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-13/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-13/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-13/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-13/cpu-system:
  value: 2.000001e-01
h2gdf6120/cpu-13/cpu-user:
  value: 2.100001e+00
h2gdf6120/cpu-13/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-14/cpu-idle:
  value: 9.920005e+01
h2gdf6120/cpu-14/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-14/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-14/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-14/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-14/cpu-system:
  value: 1.000000e-01
h2gdf6120/cpu-14/cpu-user:
  value: 6.000003e-01
h2gdf6120/cpu-14/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-15/cpu-idle:
  value: 9.990025e+01
h2gdf6120/cpu-15/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-15/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-15/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-15/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-15/cpu-system:
  value: 1.000002e-01
h2gdf6120/cpu-15/cpu-user:
  value: 0.000000e+00
h2gdf6120/cpu-15/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-16/cpu-idle:
  value: 9.989996e+01
h2gdf6120/cpu-16/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-16/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-16/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-16/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-16/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-16/cpu-user:
  value: 1.000000e-01
h2gdf6120/cpu-16/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-17/cpu-idle:
  value: 9.999995e+01
h2gdf6120/cpu-17/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-17/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-17/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-17/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-17/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-17/cpu-user:
  value: 0.000000e+00
h2gdf6120/cpu-17/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-18/cpu-idle:
  value: 9.989994e+01
h2gdf6120/cpu-18/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-18/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-18/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-18/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-18/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-18/cpu-user:
  value: 9.999997e-02
h2gdf6120/cpu-18/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-19/cpu-idle:
  value: 9.930003e+01
h2gdf6120/cpu-19/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-19/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-19/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-19/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-19/cpu-system:
  value: 9.999996e-02
h2gdf6120/cpu-19/cpu-user:
  value: 4.999998e-01
h2gdf6120/cpu-19/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-2/cpu-idle:
  value: 9.800028e+01
h2gdf6120/cpu-2/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-2/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-2/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-2/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-2/cpu-system:
  value: 3.000009e-01
h2gdf6120/cpu-2/cpu-user:
  value: 2.000007e+00
h2gdf6120/cpu-2/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-20/cpu-idle:
  value: 9.980000e+01
h2gdf6120/cpu-20/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-20/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-20/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-20/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-20/cpu-system:
  value: 9.999990e-02
h2gdf6120/cpu-20/cpu-user:
  value: 0.000000e+00
h2gdf6120/cpu-20/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-21/cpu-idle:
  value: 9.990021e+01
h2gdf6120/cpu-21/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-21/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-21/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-21/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-21/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-21/cpu-user:
  value: 0.000000e+00
h2gdf6120/cpu-21/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-22/cpu-idle:
  value: 9.660017e+01
h2gdf6120/cpu-22/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-22/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-22/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-22/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-22/cpu-system:
  value: 1.000002e-01
h2gdf6120/cpu-22/cpu-user:
  value: 3.000008e+00
h2gdf6120/cpu-22/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-23/cpu-idle:
  value: 1.000001e+02
h2gdf6120/cpu-23/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-23/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-23/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-23/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-23/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-23/cpu-user:
  value: 0.000000e+00
h2gdf6120/cpu-23/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-3/cpu-idle:
  value: 9.840034e+01
h2gdf6120/cpu-3/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-3/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-3/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-3/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-3/cpu-system:
  value: 3.000008e-01
h2gdf6120/cpu-3/cpu-user:
  value: 1.700004e+00
h2gdf6120/cpu-3/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-4/cpu-idle:
  value: 9.930038e+01
h2gdf6120/cpu-4/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-4/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-4/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-4/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-4/cpu-system:
  value: 1.000004e-01
h2gdf6120/cpu-4/cpu-user:
  value: 5.000020e-01
h2gdf6120/cpu-4/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-5/cpu-idle:
  value: 1.000003e+02
h2gdf6120/cpu-5/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-5/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-5/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-5/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-5/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-5/cpu-user:
  value: 0.000000e+00
h2gdf6120/cpu-5/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-6/cpu-idle:
  value: 9.840033e+01
h2gdf6120/cpu-6/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-6/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-6/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-6/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-6/cpu-system:
  value: 3.000009e-01
h2gdf6120/cpu-6/cpu-user:
  value: 1.200004e+00
h2gdf6120/cpu-6/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-7/cpu-idle:
  value: 9.700041e+01
h2gdf6120/cpu-7/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-7/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-7/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-7/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-7/cpu-system:
  value: 2.000008e-01
h2gdf6120/cpu-7/cpu-user:
  value: 2.500009e+00
h2gdf6120/cpu-7/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-8/cpu-idle:
  value: 1.000004e+02
h2gdf6120/cpu-8/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-8/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-8/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-8/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-8/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-8/cpu-user:
  value: 0.000000e+00
h2gdf6120/cpu-8/cpu-wait:
  value: 0.000000e+00
h2gdf6120/cpu-9/cpu-idle:
  value: 9.990057e+01
h2gdf6120/cpu-9/cpu-interrupt:
  value: 0.000000e+00
h2gdf6120/cpu-9/cpu-nice:
  value: 0.000000e+00
h2gdf6120/cpu-9/cpu-softirq:
  value: 0.000000e+00
h2gdf6120/cpu-9/cpu-steal:
  value: 0.000000e+00
h2gdf6120/cpu-9/cpu-system:
  value: 0.000000e+00
h2gdf6120/cpu-9/cpu-user:
  value: 0.000000e+00
h2gdf6120/cpu-9/cpu-wait:
  value: 0.000000e+00
h2gdf6120/df-boot/df_complex-free:
  value: 4.325089e+08
h2gdf6120/df-boot/df_complex-reserved:
  value: 2.684109e+07
h2gdf6120/df-boot/df_complex-used:
  value: 6.907494e+07
h2gdf6120/df-boot/df_inodes-free:
  value: 3.271800e+04
h2gdf6120/df-boot/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-boot/df_inodes-used:
  value: 5.000000e+01
h2gdf6120/df-boot/percent_bytes-free:
  value: 8.184869e+01
h2gdf6120/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
h2gdf6120/df-boot/percent_bytes-used:
  value: 1.307185e+01
h2gdf6120/df-boot/percent_inodes-free:
  value: 9.984741e+01
h2gdf6120/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-boot/percent_inodes-used:
  value: 1.525879e-01
h2gdf6120/df-data1/df_complex-free:
  value: 2.503070e+11
h2gdf6120/df-data1/df_complex-reserved:
  value: 1.378982e+10
h2gdf6120/df-data1/df_complex-used:
  value: 7.372034e+09
h2gdf6120/df-data1/df_inodes-free:
  value: 1.683450e+07
h2gdf6120/df-data1/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-data1/df_inodes-used:
  value: 5.900000e+01
h2gdf6120/df-data1/percent_bytes-free:
  value: 9.220468e+01
h2gdf6120/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
h2gdf6120/df-data1/percent_bytes-used:
  value: 2.715609e+00
h2gdf6120/df-data1/percent_inodes-free:
  value: 9.999964e+01
h2gdf6120/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-data1/percent_inodes-used:
  value: 3.504695e-04
h2gdf6120/df-dev-shm/df_complex-free:
  value: 3.375709e+10
h2gdf6120/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
h2gdf6120/df-dev-shm/df_complex-used:
  value: 0.000000e+00
h2gdf6120/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
h2gdf6120/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
h2gdf6120/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
h2gdf6120/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
h2gdf6120/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
h2gdf6120/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
h2gdf6120/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
h2gdf6120/df-root/df_complex-free:
  value: 1.058675e+10
h2gdf6120/df-root/df_complex-reserved:
  value: 6.442435e+08
h2gdf6120/df-root/df_complex-used:
  value: 1.451717e+09
h2gdf6120/df-root/df_inodes-free:
  value: 7.435350e+05
h2gdf6120/df-root/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-root/df_inodes-used:
  value: 4.289700e+04
h2gdf6120/df-root/percent_bytes-free:
  value: 8.347388e+01
h2gdf6120/df-root/percent_bytes-reserved:
  value: 5.079700e+00
h2gdf6120/df-root/percent_bytes-used:
  value: 1.144643e+01
h2gdf6120/df-root/percent_inodes-free:
  value: 9.454536e+01
h2gdf6120/df-root/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-root/percent_inodes-used:
  value: 5.454636e+00
h2gdf6120/df-var/df_complex-free:
  value: 7.503532e+09
h2gdf6120/df-var/df_complex-reserved:
  value: 4.294943e+08
h2gdf6120/df-var/df_complex-used:
  value: 5.220925e+08
h2gdf6120/df-var/df_inodes-free:
  value: 5.225340e+05
h2gdf6120/df-var/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-var/df_inodes-used:
  value: 1.754000e+03
h2gdf6120/df-var/percent_bytes-free:
  value: 8.874543e+01
h2gdf6120/df-var/percent_bytes-reserved:
  value: 5.079695e+00
h2gdf6120/df-var/percent_bytes-used:
  value: 6.174870e+00
h2gdf6120/df-var/percent_inodes-free:
  value: 9.966545e+01
h2gdf6120/df-var/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6120/df-var/percent_inodes-used:
  value: 3.345490e-01
h2gdf6120/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 1.000000e-01
h2gdf6120/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 4.915201e+03
h2gdf6120/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 1.000000e+00
h2gdf6120/disk-sda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 1.638401e+03
h2gdf6120/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 4.000002e-01
h2gdf6120/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 9.999979e-02
h2gdf6120/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 3.276793e+03
h2gdf6120/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 5.999987e-01
h2gdf6120/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6120/load/load:
  longterm: 3.000000e-02
  midterm: 3.000000e-02
  shortterm: 0.000000e+00
h2gdf6120/memory/memory-buffered:
  value: 3.471032e+08
h2gdf6120/memory/memory-cached:
  value: 6.819758e+08
h2gdf6120/memory/memory-free:
  value: 5.914347e+10
h2gdf6120/memory/memory-used:
  value: 7.341625e+09
h2gdf6120/network/if_octets:
  rx: 0.000000e+00
  tx: 1.587800e+03
h2gdf6120/network/if_packets:
  rx: 0.000000e+00
  tx: 1.200000e+00
h2gdf6120/network/queue_length:
  value: 0.000000e+00
h2gdf6120/network/total_values-dispatch-accepted:
  value: 0.000000e+00
h2gdf6120/network/total_values-dispatch-rejected:
  value: 0.000000e+00
h2gdf6120/network/total_values-send-accepted:
  value: 3.189999e+01
h2gdf6120/network/total_values-send-rejected:
  value: 0.000000e+00
h2gdf6120/swap/swap-cached:
  value: 0.000000e+00
h2gdf6120/swap/swap-free:
  value: 2.147475e+09
h2gdf6120/swap/swap-used:
  value: 0.000000e+00
h2gdf6120/swap/swap_io-in:
  value: 0.000000e+00
h2gdf6120/swap/swap_io-out:
  value: 0.000000e+00
h2gdf6120/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 4.499975e+00
h2gdf6120/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 8.799950e+00
h2gdf6120/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
h2gdf6120/vmem/vmpage_number-active_anon:
  value: 1.425964e+06
h2gdf6120/vmem/vmpage_number-active_file:
  value: 1.796950e+05
h2gdf6120/vmem/vmpage_number-anon_pages:
  value: 1.638300e+04
h2gdf6120/vmem/vmpage_number-anon_transparent_hugepages:
  value: 2.753000e+03
h2gdf6120/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
h2gdf6120/vmem/vmpage_number-dirty:
  value: 8.000000e+00
h2gdf6120/vmem/vmpage_number-file_pages:
  value: 2.512400e+05
h2gdf6120/vmem/vmpage_number-free_pages:
  value: 1.443932e+07
h2gdf6120/vmem/vmpage_number-inactive_anon:
  value: 1.100000e+01
h2gdf6120/vmem/vmpage_number-inactive_file:
  value: 7.149200e+04
h2gdf6120/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
h2gdf6120/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
h2gdf6120/vmem/vmpage_number-kernel_stack:
  value: 4.730000e+02
h2gdf6120/vmem/vmpage_number-mapped:
  value: 4.319000e+03
h2gdf6120/vmem/vmpage_number-mlock:
  value: 0.000000e+00
h2gdf6120/vmem/vmpage_number-page_table_pages:
  value: 3.607000e+03
h2gdf6120/vmem/vmpage_number-shmem:
  value: 5.700000e+01
h2gdf6120/vmem/vmpage_number-slab_reclaimable:
  value: 2.365580e+05
h2gdf6120/vmem/vmpage_number-slab_unreclaimable:
  value: 1.603500e+04
h2gdf6120/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
h2gdf6120/vmem/vmpage_number-unstable:
  value: 0.000000e+00
h2gdf6120/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
h2gdf6120/vmem/vmpage_number-writeback:
  value: 0.000000e+00
h2gdf6120/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
h2gdf6220/cpu-0/cpu-idle:
  value: 9.760209e+01
h2gdf6220/cpu-0/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-0/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-0/cpu-softirq:
  value: 1.000040e-01
h2gdf6220/cpu-0/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-0/cpu-system:
  value: 4.000084e-01
h2gdf6220/cpu-0/cpu-user:
  value: 9.999957e-01
h2gdf6220/cpu-0/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-1/cpu-idle:
  value: 9.970561e+01
h2gdf6220/cpu-1/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-1/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-1/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-1/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-1/cpu-system:
  value: 1.000053e-01
h2gdf6220/cpu-1/cpu-user:
  value: 1.000047e-01
h2gdf6220/cpu-1/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-10/cpu-idle:
  value: 9.990600e+01
h2gdf6220/cpu-10/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-10/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-10/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-10/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-10/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-10/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-10/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-11/cpu-idle:
  value: 1.000060e+02
h2gdf6220/cpu-11/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-11/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-11/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-11/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-11/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-11/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-11/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-12/cpu-idle:
  value: 9.780591e+01
h2gdf6220/cpu-12/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-12/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-12/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-12/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-12/cpu-system:
  value: 5.000302e-01
h2gdf6220/cpu-12/cpu-user:
  value: 8.000479e-01
h2gdf6220/cpu-12/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-13/cpu-idle:
  value: 9.980613e+01
h2gdf6220/cpu-13/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-13/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-13/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-13/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-13/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-13/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-13/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-14/cpu-idle:
  value: 1.000061e+02
h2gdf6220/cpu-14/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-14/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-14/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-14/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-14/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-14/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-14/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-15/cpu-idle:
  value: 9.990596e+01
h2gdf6220/cpu-15/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-15/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-15/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-15/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-15/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-15/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-15/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-16/cpu-idle:
  value: 9.990572e+01
h2gdf6220/cpu-16/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-16/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-16/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-16/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-16/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-16/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-16/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-17/cpu-idle:
  value: 1.000057e+02
h2gdf6220/cpu-17/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-17/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-17/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-17/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-17/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-17/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-17/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-18/cpu-idle:
  value: 1.000058e+02
h2gdf6220/cpu-18/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-18/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-18/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-18/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-18/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-18/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-18/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-19/cpu-idle:
  value: 9.990559e+01
h2gdf6220/cpu-19/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-19/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-19/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-19/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-19/cpu-system:
  value: 2.000113e-01
h2gdf6220/cpu-19/cpu-user:
  value: 1.000057e-01
h2gdf6220/cpu-19/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-2/cpu-idle:
  value: 9.990540e+01
h2gdf6220/cpu-2/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-2/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-2/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-2/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-2/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-2/cpu-user:
  value: 1.000054e-01
h2gdf6220/cpu-2/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-20/cpu-idle:
  value: 1.000055e+02
h2gdf6220/cpu-20/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-20/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-20/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-20/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-20/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-20/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-20/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-21/cpu-idle:
  value: 1.000054e+02
h2gdf6220/cpu-21/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-21/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-21/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-21/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-21/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-21/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-21/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-22/cpu-idle:
  value: 9.990534e+01
h2gdf6220/cpu-22/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-22/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-22/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-22/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-22/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-22/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-22/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-23/cpu-idle:
  value: 1.000054e+02
h2gdf6220/cpu-23/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-23/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-23/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-23/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-23/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-23/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-23/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-3/cpu-idle:
  value: 1.000058e+02
h2gdf6220/cpu-3/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-3/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-3/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-3/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-3/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-3/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-3/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-4/cpu-idle:
  value: 9.990588e+01
h2gdf6220/cpu-4/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-4/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-4/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-4/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-4/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-4/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-4/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-5/cpu-idle:
  value: 1.000061e+02
h2gdf6220/cpu-5/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-5/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-5/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-5/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-5/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-5/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-5/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-6/cpu-idle:
  value: 9.980614e+01
h2gdf6220/cpu-6/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-6/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-6/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-6/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-6/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-6/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-6/cpu-wait:
  value: 1.000062e-01
h2gdf6220/cpu-7/cpu-idle:
  value: 9.990617e+01
h2gdf6220/cpu-7/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-7/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-7/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-7/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-7/cpu-system:
  value: 1.000062e-01
h2gdf6220/cpu-7/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-7/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-8/cpu-idle:
  value: 1.000060e+02
h2gdf6220/cpu-8/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-8/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-8/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-8/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-8/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-8/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-8/cpu-wait:
  value: 0.000000e+00
h2gdf6220/cpu-9/cpu-idle:
  value: 9.990601e+01
h2gdf6220/cpu-9/cpu-interrupt:
  value: 0.000000e+00
h2gdf6220/cpu-9/cpu-nice:
  value: 0.000000e+00
h2gdf6220/cpu-9/cpu-softirq:
  value: 0.000000e+00
h2gdf6220/cpu-9/cpu-steal:
  value: 0.000000e+00
h2gdf6220/cpu-9/cpu-system:
  value: 0.000000e+00
h2gdf6220/cpu-9/cpu-user:
  value: 0.000000e+00
h2gdf6220/cpu-9/cpu-wait:
  value: 0.000000e+00
h2gdf6220/df-boot/df_complex-free:
  value: 4.324721e+08
h2gdf6220/df-boot/df_complex-reserved:
  value: 2.684109e+07
h2gdf6220/df-boot/df_complex-used:
  value: 6.911181e+07
h2gdf6220/df-boot/df_inodes-free:
  value: 3.271800e+04
h2gdf6220/df-boot/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-boot/df_inodes-used:
  value: 5.000000e+01
h2gdf6220/df-boot/percent_bytes-free:
  value: 8.184172e+01
h2gdf6220/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
h2gdf6220/df-boot/percent_bytes-used:
  value: 1.307883e+01
h2gdf6220/df-boot/percent_inodes-free:
  value: 9.984741e+01
h2gdf6220/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-boot/percent_inodes-used:
  value: 1.525879e-01
h2gdf6220/df-data1/df_complex-free:
  value: 2.459593e+11
h2gdf6220/df-data1/df_complex-reserved:
  value: 1.378982e+10
h2gdf6220/df-data1/df_complex-used:
  value: 1.171974e+10
h2gdf6220/df-data1/df_inodes-free:
  value: 1.683450e+07
h2gdf6220/df-data1/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-data1/df_inodes-used:
  value: 6.100000e+01
h2gdf6220/df-data1/percent_bytes-free:
  value: 9.060313e+01
h2gdf6220/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
h2gdf6220/df-data1/percent_bytes-used:
  value: 4.317159e+00
h2gdf6220/df-data1/percent_inodes-free:
  value: 9.999964e+01
h2gdf6220/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-data1/percent_inodes-used:
  value: 3.623498e-04
h2gdf6220/df-dev-shm/df_complex-free:
  value: 3.375709e+10
h2gdf6220/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
h2gdf6220/df-dev-shm/df_complex-used:
  value: 0.000000e+00
h2gdf6220/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
h2gdf6220/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
h2gdf6220/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
h2gdf6220/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
h2gdf6220/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
h2gdf6220/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
h2gdf6220/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
h2gdf6220/df-root/df_complex-free:
  value: 1.058283e+10
h2gdf6220/df-root/df_complex-reserved:
  value: 6.442435e+08
h2gdf6220/df-root/df_complex-used:
  value: 1.455632e+09
h2gdf6220/df-root/df_inodes-free:
  value: 7.435320e+05
h2gdf6220/df-root/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-root/df_inodes-used:
  value: 4.290000e+04
h2gdf6220/df-root/percent_bytes-free:
  value: 8.344300e+01
h2gdf6220/df-root/percent_bytes-reserved:
  value: 5.079700e+00
h2gdf6220/df-root/percent_bytes-used:
  value: 1.147730e+01
h2gdf6220/df-root/percent_inodes-free:
  value: 9.454498e+01
h2gdf6220/df-root/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-root/percent_inodes-used:
  value: 5.455017e+00
h2gdf6220/df-var/df_complex-free:
  value: 7.501562e+09
h2gdf6220/df-var/df_complex-reserved:
  value: 4.294943e+08
h2gdf6220/df-var/df_complex-used:
  value: 5.240627e+08
h2gdf6220/df-var/df_inodes-free:
  value: 5.225360e+05
h2gdf6220/df-var/df_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-var/df_inodes-used:
  value: 1.752000e+03
h2gdf6220/df-var/percent_bytes-free:
  value: 8.872214e+01
h2gdf6220/df-var/percent_bytes-reserved:
  value: 5.079695e+00
h2gdf6220/df-var/percent_bytes-used:
  value: 6.198171e+00
h2gdf6220/df-var/percent_inodes-free:
  value: 9.966583e+01
h2gdf6220/df-var/percent_inodes-reserved:
  value: 0.000000e+00
h2gdf6220/df-var/percent_inodes-used:
  value: 3.341675e-01
h2gdf6220/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 5.000078e-01
h2gdf6220/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 1.761274e+05
h2gdf6220/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 6.599997e+00
h2gdf6220/disk-sda/disk_time:
  read: 0.000000e+00
  write: 1.200000e+00
h2gdf6220/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
h2gdf6220/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 5.000119e-01
h2gdf6220/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 1.761322e+05
h2gdf6220/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 6.600157e+00
h2gdf6220/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 1.200029e+00
h2gdf6220/load/load:
  longterm: 3.000000e-02
  midterm: 1.300000e-01
  shortterm: 1.800000e-01
h2gdf6220/memory/memory-buffered:
  value: 3.538944e+08
h2gdf6220/memory/memory-cached:
  value: 6.890127e+08
h2gdf6220/memory/memory-free:
  value: 5.744062e+10
h2gdf6220/memory/memory-used:
  value: 9.030652e+09
h2gdf6220/network/if_octets:
  rx: 0.000000e+00
  tx: 1.449803e+03
h2gdf6220/network/if_packets:
  rx: 0.000000e+00
  tx: 1.100003e+00
h2gdf6220/network/queue_length:
  value: 0.000000e+00
h2gdf6220/network/total_values-dispatch-accepted:
  value: 0.000000e+00
h2gdf6220/network/total_values-dispatch-rejected:
  value: 0.000000e+00
h2gdf6220/network/total_values-send-accepted:
  value: 2.630008e+01
h2gdf6220/network/total_values-send-rejected:
  value: 0.000000e+00
h2gdf6220/swap/swap-cached:
  value: 0.000000e+00
h2gdf6220/swap/swap-free:
  value: 2.147475e+09
h2gdf6220/swap/swap-used:
  value: 0.000000e+00
h2gdf6220/swap/swap_io-in:
  value: 0.000000e+00
h2gdf6220/swap/swap_io-out:
  value: 0.000000e+00
h2gdf6220/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 7.000005e-01
h2gdf6220/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 2.800002e+00
h2gdf6220/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
h2gdf6220/vmem/vmpage_number-active_anon:
  value: 1.830919e+06
h2gdf6220/vmem/vmpage_number-active_file:
  value: 1.812510e+05
h2gdf6220/vmem/vmpage_number-anon_pages:
  value: 1.841200e+04
h2gdf6220/vmem/vmpage_number-anon_transparent_hugepages:
  value: 3.540000e+03
h2gdf6220/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
h2gdf6220/vmem/vmpage_number-dirty:
  value: 4.000000e+00
h2gdf6220/vmem/vmpage_number-file_pages:
  value: 2.546160e+05
h2gdf6220/vmem/vmpage_number-free_pages:
  value: 1.402359e+07
h2gdf6220/vmem/vmpage_number-inactive_anon:
  value: 1.600000e+01
h2gdf6220/vmem/vmpage_number-inactive_file:
  value: 7.330700e+04
h2gdf6220/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
h2gdf6220/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
h2gdf6220/vmem/vmpage_number-kernel_stack:
  value: 4.900000e+02
h2gdf6220/vmem/vmpage_number-mapped:
  value: 4.792000e+03
h2gdf6220/vmem/vmpage_number-mlock:
  value: 0.000000e+00
h2gdf6220/vmem/vmpage_number-page_table_pages:
  value: 4.540000e+03
h2gdf6220/vmem/vmpage_number-shmem:
  value: 6.200000e+01
h2gdf6220/vmem/vmpage_number-slab_reclaimable:
  value: 2.343820e+05
h2gdf6220/vmem/vmpage_number-slab_unreclaimable:
  value: 2.127200e+04
h2gdf6220/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
h2gdf6220/vmem/vmpage_number-unstable:
  value: 0.000000e+00
h2gdf6220/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
h2gdf6220/vmem/vmpage_number-writeback:
  value: 0.000000e+00
h2gdf6220/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
m01df6100/cpu-0/cpu-idle:
  value: 9.980233e+01
m01df6100/cpu-0/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-0/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-0/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-0/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-0/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-0/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-0/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-1/cpu-idle:
  value: 1.000105e+02
m01df6100/cpu-1/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-1/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-1/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-1/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-1/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-1/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-1/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-10/cpu-idle:
  value: 1.000294e+02
m01df6100/cpu-10/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-10/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-10/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-10/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-10/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-10/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-10/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-11/cpu-idle:
  value: 9.993001e+01
m01df6100/cpu-11/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-11/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-11/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-11/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-11/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-11/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-11/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-12/cpu-idle:
  value: 1.000310e+02
m01df6100/cpu-12/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-12/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-12/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-12/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-12/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-12/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-12/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-13/cpu-idle:
  value: 9.993154e+01
m01df6100/cpu-13/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-13/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-13/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-13/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-13/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-13/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-13/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-14/cpu-idle:
  value: 1.000329e+02
m01df6100/cpu-14/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-14/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-14/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-14/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-14/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-14/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-14/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-15/cpu-idle:
  value: 1.000341e+02
m01df6100/cpu-15/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-15/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-15/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-15/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-15/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-15/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-15/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-16/cpu-idle:
  value: 1.000347e+02
m01df6100/cpu-16/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-16/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-16/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-16/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-16/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-16/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-16/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-17/cpu-idle:
  value: 1.000354e+02
m01df6100/cpu-17/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-17/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-17/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-17/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-17/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-17/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-17/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-18/cpu-idle:
  value: 9.993624e+01
m01df6100/cpu-18/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-18/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-18/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-18/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-18/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-18/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-18/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-19/cpu-idle:
  value: 9.993717e+01
m01df6100/cpu-19/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-19/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-19/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-19/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-19/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-19/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-19/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-2/cpu-idle:
  value: 1.000135e+02
m01df6100/cpu-2/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-2/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-2/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-2/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-2/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-2/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-2/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-20/cpu-idle:
  value: 9.993831e+01
m01df6100/cpu-20/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-20/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-20/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-20/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-20/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-20/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-20/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-21/cpu-idle:
  value: 1.000397e+02
m01df6100/cpu-21/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-21/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-21/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-21/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-21/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-21/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-21/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-22/cpu-idle:
  value: 9.994092e+01
m01df6100/cpu-22/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-22/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-22/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-22/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-22/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-22/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-22/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-23/cpu-idle:
  value: 1.000420e+02
m01df6100/cpu-23/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-23/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-23/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-23/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-23/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-23/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-23/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-3/cpu-idle:
  value: 1.000161e+02
m01df6100/cpu-3/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-3/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-3/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-3/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-3/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-3/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-3/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-4/cpu-idle:
  value: 1.000191e+02
m01df6100/cpu-4/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-4/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-4/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-4/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-4/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-4/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-4/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-5/cpu-idle:
  value: 1.000243e+02
m01df6100/cpu-5/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-5/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-5/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-5/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-5/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-5/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-5/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-6/cpu-idle:
  value: 1.000268e+02
m01df6100/cpu-6/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-6/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-6/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-6/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-6/cpu-system:
  value: 1.000266e-01
m01df6100/cpu-6/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-6/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-7/cpu-idle:
  value: 1.000274e+02
m01df6100/cpu-7/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-7/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-7/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-7/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-7/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-7/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-7/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-8/cpu-idle:
  value: 1.000283e+02
m01df6100/cpu-8/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-8/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-8/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-8/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-8/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-8/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-8/cpu-wait:
  value: 0.000000e+00
m01df6100/cpu-9/cpu-idle:
  value: 1.000291e+02
m01df6100/cpu-9/cpu-interrupt:
  value: 0.000000e+00
m01df6100/cpu-9/cpu-nice:
  value: 0.000000e+00
m01df6100/cpu-9/cpu-softirq:
  value: 0.000000e+00
m01df6100/cpu-9/cpu-steal:
  value: 0.000000e+00
m01df6100/cpu-9/cpu-system:
  value: 0.000000e+00
m01df6100/cpu-9/cpu-user:
  value: 0.000000e+00
m01df6100/cpu-9/cpu-wait:
  value: 0.000000e+00
m01df6100/df-boot/df_complex-free:
  value: 4.369408e+08
m01df6100/df-boot/df_complex-reserved:
  value: 2.684109e+07
m01df6100/df-boot/df_complex-used:
  value: 6.464307e+07
m01df6100/df-boot/df_inodes-free:
  value: 3.271800e+04
m01df6100/df-boot/df_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-boot/df_inodes-used:
  value: 5.000000e+01
m01df6100/df-boot/percent_bytes-free:
  value: 8.268739e+01
m01df6100/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
m01df6100/df-boot/percent_bytes-used:
  value: 1.223316e+01
m01df6100/df-boot/percent_inodes-free:
  value: 9.984741e+01
m01df6100/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-boot/percent_inodes-used:
  value: 1.525879e-01
m01df6100/df-data1/df_complex-free:
  value: 2.574032e+11
m01df6100/df-data1/df_complex-reserved:
  value: 1.378982e+10
m01df6100/df-data1/df_complex-used:
  value: 2.758984e+08
m01df6100/df-data1/df_inodes-free:
  value: 1.683426e+07
m01df6100/df-data1/df_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-data1/df_inodes-used:
  value: 2.960000e+02
m01df6100/df-data1/percent_bytes-free:
  value: 9.481866e+01
m01df6100/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
m01df6100/df-data1/percent_bytes-used:
  value: 1.016317e-01
m01df6100/df-data1/percent_inodes-free:
  value: 9.999825e+01
m01df6100/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-data1/percent_inodes-used:
  value: 1.758288e-03
m01df6100/df-dev-shm/df_complex-free:
  value: 3.375709e+10
m01df6100/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
m01df6100/df-dev-shm/df_complex-used:
  value: 0.000000e+00
m01df6100/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
m01df6100/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
m01df6100/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
m01df6100/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
m01df6100/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
m01df6100/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
m01df6100/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
m01df6100/df-root/df_complex-free:
  value: 1.072688e+10
m01df6100/df-root/df_complex-reserved:
  value: 6.442435e+08
m01df6100/df-root/df_complex-used:
  value: 1.311580e+09
m01df6100/df-root/df_inodes-free:
  value: 7.454950e+05
m01df6100/df-root/df_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-root/df_inodes-used:
  value: 4.093700e+04
m01df6100/df-root/percent_bytes-free:
  value: 8.457882e+01
m01df6100/df-root/percent_bytes-reserved:
  value: 5.079700e+00
m01df6100/df-root/percent_bytes-used:
  value: 1.034148e+01
m01df6100/df-root/percent_inodes-free:
  value: 9.479459e+01
m01df6100/df-root/percent_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-root/percent_inodes-used:
  value: 5.205409e+00
m01df6100/df-var/df_complex-free:
  value: 7.566787e+09
m01df6100/df-var/df_complex-reserved:
  value: 4.294943e+08
m01df6100/df-var/df_complex-used:
  value: 4.588380e+08
m01df6100/df-var/df_inodes-free:
  value: 5.228090e+05
m01df6100/df-var/df_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-var/df_inodes-used:
  value: 1.479000e+03
m01df6100/df-var/percent_bytes-free:
  value: 8.949355e+01
m01df6100/df-var/percent_bytes-reserved:
  value: 5.079695e+00
m01df6100/df-var/percent_bytes-used:
  value: 5.426748e+00
m01df6100/df-var/percent_inodes-free:
  value: 9.971790e+01
m01df6100/df-var/percent_inodes-reserved:
  value: 0.000000e+00
m01df6100/df-var/percent_inodes-used:
  value: 2.820969e-01
m01df6100/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6100/load/load:
  longterm: 0.000000e+00
  midterm: 2.000000e-02
  shortterm: 3.000000e-02
m01df6100/memory/memory-buffered:
  value: 3.348275e+08
m01df6100/memory/memory-cached:
  value: 5.892219e+08
m01df6100/memory/memory-free:
  value: 6.425088e+10
m01df6100/memory/memory-used:
  value: 2.339246e+09
m01df6100/network/if_octets:
  rx: 0.000000e+00
  tx: 1.057501e+03
m01df6100/network/if_packets:
  rx: 0.000000e+00
  tx: 8.000004e-01
m01df6100/network/queue_length:
  value: 0.000000e+00
m01df6100/network/total_values-dispatch-accepted:
  value: 0.000000e+00
m01df6100/network/total_values-dispatch-rejected:
  value: 0.000000e+00
m01df6100/network/total_values-send-accepted:
  value: 1.929969e+01
m01df6100/network/total_values-send-rejected:
  value: 0.000000e+00
m01df6100/swap/swap-cached:
  value: 0.000000e+00
m01df6100/swap/swap-free:
  value: 2.147475e+09
m01df6100/swap/swap-used:
  value: 0.000000e+00
m01df6100/swap/swap_io-in:
  value: 0.000000e+00
m01df6100/swap/swap_io-out:
  value: 0.000000e+00
m01df6100/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 3.199662e+00
m01df6100/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
m01df6100/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
m01df6100/vmem/vmpage_number-active_anon:
  value: 2.152660e+05
m01df6100/vmem/vmpage_number-active_file:
  value: 1.507500e+05
m01df6100/vmem/vmpage_number-anon_pages:
  value: 1.456100e+04
m01df6100/vmem/vmpage_number-anon_transparent_hugepages:
  value: 3.920000e+02
m01df6100/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
m01df6100/vmem/vmpage_number-dirty:
  value: 0.000000e+00
m01df6100/vmem/vmpage_number-file_pages:
  value: 2.255980e+05
m01df6100/vmem/vmpage_number-free_pages:
  value: 1.568625e+07
m01df6100/vmem/vmpage_number-inactive_anon:
  value: 4.600000e+01
m01df6100/vmem/vmpage_number-inactive_file:
  value: 7.480400e+04
m01df6100/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
m01df6100/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
m01df6100/vmem/vmpage_number-kernel_stack:
  value: 4.500000e+02
m01df6100/vmem/vmpage_number-mapped:
  value: 3.560000e+03
m01df6100/vmem/vmpage_number-mlock:
  value: 0.000000e+00
m01df6100/vmem/vmpage_number-page_table_pages:
  value: 1.060000e+03
m01df6100/vmem/vmpage_number-shmem:
  value: 4.800000e+01
m01df6100/vmem/vmpage_number-slab_reclaimable:
  value: 2.369210e+05
m01df6100/vmem/vmpage_number-slab_unreclaimable:
  value: 1.479000e+04
m01df6100/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
m01df6100/vmem/vmpage_number-unstable:
  value: 0.000000e+00
m01df6100/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
m01df6100/vmem/vmpage_number-writeback:
  value: 0.000000e+00
m01df6100/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
m01df6200/cpu-0/cpu-idle:
  value: 9.970372e+01
m01df6200/cpu-0/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-0/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-0/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-0/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-0/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-0/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-0/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-1/cpu-idle:
  value: 1.000038e+02
m01df6200/cpu-1/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-1/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-1/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-1/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-1/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-1/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-1/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-10/cpu-idle:
  value: 1.000043e+02
m01df6200/cpu-10/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-10/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-10/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-10/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-10/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-10/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-10/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-11/cpu-idle:
  value: 1.000045e+02
m01df6200/cpu-11/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-11/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-11/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-11/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-11/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-11/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-11/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-12/cpu-idle:
  value: 1.000044e+02
m01df6200/cpu-12/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-12/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-12/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-12/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-12/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-12/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-12/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-13/cpu-idle:
  value: 1.000045e+02
m01df6200/cpu-13/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-13/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-13/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-13/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-13/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-13/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-13/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-14/cpu-idle:
  value: 1.000044e+02
m01df6200/cpu-14/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-14/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-14/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-14/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-14/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-14/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-14/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-15/cpu-idle:
  value: 9.990450e+01
m01df6200/cpu-15/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-15/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-15/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-15/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-15/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-15/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-15/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-16/cpu-idle:
  value: 1.000046e+02
m01df6200/cpu-16/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-16/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-16/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-16/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-16/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-16/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-16/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-17/cpu-idle:
  value: 1.000047e+02
m01df6200/cpu-17/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-17/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-17/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-17/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-17/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-17/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-17/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-18/cpu-idle:
  value: 1.000047e+02
m01df6200/cpu-18/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-18/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-18/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-18/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-18/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-18/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-18/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-19/cpu-idle:
  value: 9.990492e+01
m01df6200/cpu-19/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-19/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-19/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-19/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-19/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-19/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-19/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-2/cpu-idle:
  value: 1.000038e+02
m01df6200/cpu-2/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-2/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-2/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-2/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-2/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-2/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-2/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-20/cpu-idle:
  value: 9.990475e+01
m01df6200/cpu-20/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-20/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-20/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-20/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-20/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-20/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-20/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-21/cpu-idle:
  value: 1.000048e+02
m01df6200/cpu-21/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-21/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-21/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-21/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-21/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-21/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-21/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-22/cpu-idle:
  value: 1.000049e+02
m01df6200/cpu-22/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-22/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-22/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-22/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-22/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-22/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-22/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-23/cpu-idle:
  value: 9.990458e+01
m01df6200/cpu-23/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-23/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-23/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-23/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-23/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-23/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-23/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-3/cpu-idle:
  value: 9.990383e+01
m01df6200/cpu-3/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-3/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-3/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-3/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-3/cpu-system:
  value: 1.000038e-01
m01df6200/cpu-3/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-3/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-4/cpu-idle:
  value: 1.000039e+02
m01df6200/cpu-4/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-4/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-4/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-4/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-4/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-4/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-4/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-5/cpu-idle:
  value: 1.000039e+02
m01df6200/cpu-5/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-5/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-5/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-5/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-5/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-5/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-5/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-6/cpu-idle:
  value: 1.000039e+02
m01df6200/cpu-6/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-6/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-6/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-6/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-6/cpu-system:
  value: 1.000039e-01
m01df6200/cpu-6/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-6/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-7/cpu-idle:
  value: 9.990388e+01
m01df6200/cpu-7/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-7/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-7/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-7/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-7/cpu-system:
  value: 1.000039e-01
m01df6200/cpu-7/cpu-user:
  value: 0.000000e+00
m01df6200/cpu-7/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-8/cpu-idle:
  value: 9.990408e+01
m01df6200/cpu-8/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-8/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-8/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-8/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-8/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-8/cpu-user:
  value: 1.000040e-01
m01df6200/cpu-8/cpu-wait:
  value: 0.000000e+00
m01df6200/cpu-9/cpu-idle:
  value: 1.000043e+02
m01df6200/cpu-9/cpu-interrupt:
  value: 0.000000e+00
m01df6200/cpu-9/cpu-nice:
  value: 0.000000e+00
m01df6200/cpu-9/cpu-softirq:
  value: 0.000000e+00
m01df6200/cpu-9/cpu-steal:
  value: 0.000000e+00
m01df6200/cpu-9/cpu-system:
  value: 0.000000e+00
m01df6200/cpu-9/cpu-user:
  value: 1.000043e-01
m01df6200/cpu-9/cpu-wait:
  value: 0.000000e+00
m01df6200/df-boot/df_complex-free:
  value: 4.368957e+08
m01df6200/df-boot/df_complex-reserved:
  value: 2.684109e+07
m01df6200/df-boot/df_complex-used:
  value: 6.468813e+07
m01df6200/df-boot/df_inodes-free:
  value: 3.271800e+04
m01df6200/df-boot/df_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-boot/df_inodes-used:
  value: 5.000000e+01
m01df6200/df-boot/percent_bytes-free:
  value: 8.267886e+01
m01df6200/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
m01df6200/df-boot/percent_bytes-used:
  value: 1.224169e+01
m01df6200/df-boot/percent_inodes-free:
  value: 9.984741e+01
m01df6200/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-boot/percent_inodes-used:
  value: 1.525879e-01
m01df6200/df-data1/df_complex-free:
  value: 2.574203e+11
m01df6200/df-data1/df_complex-reserved:
  value: 1.378982e+10
m01df6200/df-data1/df_complex-used:
  value: 2.587075e+08
m01df6200/df-data1/df_inodes-free:
  value: 1.683427e+07
m01df6200/df-data1/df_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-data1/df_inodes-used:
  value: 2.940000e+02
m01df6200/df-data1/percent_bytes-free:
  value: 9.482500e+01
m01df6200/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
m01df6200/df-data1/percent_bytes-used:
  value: 9.529912e-02
m01df6200/df-data1/percent_inodes-free:
  value: 9.999825e+01
m01df6200/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-data1/percent_inodes-used:
  value: 1.746407e-03
m01df6200/df-dev-shm/df_complex-free:
  value: 3.375709e+10
m01df6200/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
m01df6200/df-dev-shm/df_complex-used:
  value: 0.000000e+00
m01df6200/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
m01df6200/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
m01df6200/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
m01df6200/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
m01df6200/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
m01df6200/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
m01df6200/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
m01df6200/df-root/df_complex-free:
  value: 1.073381e+10
m01df6200/df-root/df_complex-reserved:
  value: 6.442435e+08
m01df6200/df-root/df_complex-used:
  value: 1.304654e+09
m01df6200/df-root/df_inodes-free:
  value: 7.455060e+05
m01df6200/df-root/df_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-root/df_inodes-used:
  value: 4.092600e+04
m01df6200/df-root/percent_bytes-free:
  value: 8.463343e+01
m01df6200/df-root/percent_bytes-reserved:
  value: 5.079700e+00
m01df6200/df-root/percent_bytes-used:
  value: 1.028687e+01
m01df6200/df-root/percent_inodes-free:
  value: 9.479599e+01
m01df6200/df-root/percent_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-root/percent_inodes-used:
  value: 5.204010e+00
m01df6200/df-var/df_complex-free:
  value: 7.567528e+09
m01df6200/df-var/df_complex-reserved:
  value: 4.294943e+08
m01df6200/df-var/df_complex-used:
  value: 4.580966e+08
m01df6200/df-var/df_inodes-free:
  value: 5.228110e+05
m01df6200/df-var/df_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-var/df_inodes-used:
  value: 1.477000e+03
m01df6200/df-var/percent_bytes-free:
  value: 8.950232e+01
m01df6200/df-var/percent_bytes-reserved:
  value: 5.079695e+00
m01df6200/df-var/percent_bytes-used:
  value: 5.417980e+00
m01df6200/df-var/percent_inodes-free:
  value: 9.971828e+01
m01df6200/df-var/percent_inodes-reserved:
  value: 0.000000e+00
m01df6200/df-var/percent_inodes-used:
  value: 2.817154e-01
m01df6200/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
m01df6200/load/load:
  longterm: 0.000000e+00
  midterm: 0.000000e+00
  shortterm: 0.000000e+00
m01df6200/memory/memory-buffered:
  value: 3.312067e+08
m01df6200/memory/memory-cached:
  value: 5.615206e+08
m01df6200/memory/memory-free:
  value: 6.428290e+10
m01df6200/memory/memory-used:
  value: 2.338546e+09
m01df6200/network/if_octets:
  rx: 0.000000e+00
  tx: 1.583300e+03
m01df6200/network/if_packets:
  rx: 0.000000e+00
  tx: 1.200000e+00
m01df6200/network/queue_length:
  value: 0.000000e+00
m01df6200/network/total_values-dispatch-accepted:
  value: 0.000000e+00
m01df6200/network/total_values-dispatch-rejected:
  value: 0.000000e+00
m01df6200/network/total_values-send-accepted:
  value: 3.150000e+01
m01df6200/network/total_values-send-rejected:
  value: 0.000000e+00
m01df6200/swap/swap-cached:
  value: 0.000000e+00
m01df6200/swap/swap-free:
  value: 2.147475e+09
m01df6200/swap/swap-used:
  value: 0.000000e+00
m01df6200/swap/swap_io-in:
  value: 0.000000e+00
m01df6200/swap/swap_io-out:
  value: 0.000000e+00
m01df6200/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 3.600012e+00
m01df6200/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
m01df6200/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
m01df6200/vmem/vmpage_number-active_anon:
  value: 2.124650e+05
m01df6200/vmem/vmpage_number-active_file:
  value: 1.424970e+05
m01df6200/vmem/vmpage_number-anon_pages:
  value: 1.227400e+04
m01df6200/vmem/vmpage_number-anon_transparent_hugepages:
  value: 3.910000e+02
m01df6200/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
m01df6200/vmem/vmpage_number-dirty:
  value: 0.000000e+00
m01df6200/vmem/vmpage_number-file_pages:
  value: 2.179510e+05
m01df6200/vmem/vmpage_number-free_pages:
  value: 1.569406e+07
m01df6200/vmem/vmpage_number-inactive_anon:
  value: 4.800000e+01
m01df6200/vmem/vmpage_number-inactive_file:
  value: 7.540800e+04
m01df6200/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
m01df6200/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
m01df6200/vmem/vmpage_number-kernel_stack:
  value: 4.460000e+02
m01df6200/vmem/vmpage_number-mapped:
  value: 3.063000e+03
m01df6200/vmem/vmpage_number-mlock:
  value: 0.000000e+00
m01df6200/vmem/vmpage_number-page_table_pages:
  value: 8.920000e+02
m01df6200/vmem/vmpage_number-shmem:
  value: 5.000000e+01
m01df6200/vmem/vmpage_number-slab_reclaimable:
  value: 2.398380e+05
m01df6200/vmem/vmpage_number-slab_unreclaimable:
  value: 1.473600e+04
m01df6200/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
m01df6200/vmem/vmpage_number-unstable:
  value: 0.000000e+00
m01df6200/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
m01df6200/vmem/vmpage_number-writeback:
  value: 0.000000e+00
m01df6200/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w838f6010/cpu-0/cpu-idle:
  value: 9.990008e+01
w838f6010/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w838f6010/cpu-0/cpu-nice:
  value: 0.000000e+00
w838f6010/cpu-0/cpu-softirq:
  value: 0.000000e+00
w838f6010/cpu-0/cpu-steal:
  value: 0.000000e+00
w838f6010/cpu-0/cpu-system:
  value: 0.000000e+00
w838f6010/cpu-0/cpu-user:
  value: 0.000000e+00
w838f6010/cpu-0/cpu-wait:
  value: 0.000000e+00
w838f6010/cpu-1/cpu-idle:
  value: 9.980017e+01
w838f6010/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w838f6010/cpu-1/cpu-nice:
  value: 0.000000e+00
w838f6010/cpu-1/cpu-softirq:
  value: 0.000000e+00
w838f6010/cpu-1/cpu-steal:
  value: 0.000000e+00
w838f6010/cpu-1/cpu-system:
  value: 0.000000e+00
w838f6010/cpu-1/cpu-user:
  value: 0.000000e+00
w838f6010/cpu-1/cpu-wait:
  value: 0.000000e+00
w838f6010/df-boot/df_complex-free:
  value: 4.793754e+08
w838f6010/df-boot/df_complex-reserved:
  value: 2.684109e+07
w838f6010/df-boot/df_complex-used:
  value: 2.220851e+07
w838f6010/df-boot/df_inodes-free:
  value: 3.273000e+04
w838f6010/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-boot/df_inodes-used:
  value: 3.800000e+01
w838f6010/df-boot/percent_bytes-free:
  value: 9.071777e+01
w838f6010/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w838f6010/df-boot/percent_bytes-used:
  value: 4.202775e+00
w838f6010/df-boot/percent_inodes-free:
  value: 9.988403e+01
w838f6010/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-boot/percent_inodes-used:
  value: 1.159668e-01
w838f6010/df-data1/df_complex-free:
  value: 2.740491e+10
w838f6010/df-data1/df_complex-reserved:
  value: 1.476235e+09
w838f6010/df-data1/df_complex-used:
  value: 1.802977e+08
w838f6010/df-data1/df_inodes-free:
  value: 1.802227e+06
w838f6010/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-data1/df_inodes-used:
  value: 1.300000e+01
w838f6010/df-data1/percent_bytes-free:
  value: 9.429990e+01
w838f6010/df-data1/percent_bytes-reserved:
  value: 5.079704e+00
w838f6010/df-data1/percent_bytes-used:
  value: 6.204019e-01
w838f6010/df-data1/percent_inodes-free:
  value: 9.999928e+01
w838f6010/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-data1/percent_inodes-used:
  value: 7.213246e-04
w838f6010/df-dev-shm/df_complex-free:
  value: 9.842483e+08
w838f6010/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w838f6010/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w838f6010/df-dev-shm/df_inodes-free:
  value: 2.402940e+05
w838f6010/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w838f6010/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w838f6010/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w838f6010/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w838f6010/df-dev-shm/percent_inodes-free:
  value: 9.999958e+01
w838f6010/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-dev-shm/percent_inodes-used:
  value: 4.161551e-04
w838f6010/df-root/df_complex-free:
  value: 1.099070e+10
w838f6010/df-root/df_complex-reserved:
  value: 6.442435e+08
w838f6010/df-root/df_complex-used:
  value: 1.047761e+09
w838f6010/df-root/df_inodes-free:
  value: 7.509290e+05
w838f6010/df-root/df_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-root/df_inodes-used:
  value: 3.550300e+04
w838f6010/df-root/percent_bytes-free:
  value: 8.665897e+01
w838f6010/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w838f6010/df-root/percent_bytes-used:
  value: 8.261335e+00
w838f6010/df-root/percent_inodes-free:
  value: 9.548557e+01
w838f6010/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-root/percent_inodes-used:
  value: 4.514440e+00
w838f6010/df-var/df_complex-free:
  value: 7.638827e+09
w838f6010/df-var/df_complex-reserved:
  value: 4.294943e+08
w838f6010/df-var/df_complex-used:
  value: 3.867976e+08
w838f6010/df-var/df_inodes-free:
  value: 5.230900e+05
w838f6010/df-var/df_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-var/df_inodes-used:
  value: 1.198000e+03
w838f6010/df-var/percent_bytes-free:
  value: 9.034559e+01
w838f6010/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w838f6010/df-var/percent_bytes-used:
  value: 4.574715e+00
w838f6010/df-var/percent_inodes-free:
  value: 9.977150e+01
w838f6010/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w838f6010/df-var/percent_inodes-used:
  value: 2.285004e-01
w838f6010/disk-vda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/disk-vda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6010/load/load:
  longterm: 0.000000e+00
  midterm: 2.000000e-02
  shortterm: 2.000000e-02
w838f6010/memory/memory-buffered:
  value: 1.263739e+08
w838f6010/memory/memory-cached:
  value: 4.350566e+08
w838f6010/memory/memory-free:
  value: 1.024844e+09
w838f6010/memory/memory-used:
  value: 3.822223e+08
w838f6010/network/if_octets:
  rx: 0.000000e+00
  tx: 6.632999e+02
w838f6010/network/if_packets:
  rx: 0.000000e+00
  tx: 5.000010e-01
w838f6010/network/queue_length:
  value: 0.000000e+00
w838f6010/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w838f6010/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w838f6010/network/total_values-send-accepted:
  value: 1.480000e+01
w838f6010/network/total_values-send-rejected:
  value: 0.000000e+00
w838f6010/swap/swap-cached:
  value: 0.000000e+00
w838f6010/swap/swap-free:
  value: 2.147475e+09
w838f6010/swap/swap-used:
  value: 0.000000e+00
w838f6010/swap/swap_io-in:
  value: 0.000000e+00
w838f6010/swap/swap_io-out:
  value: 0.000000e+00
w838f6010/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 4.999995e+00
w838f6010/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
w838f6010/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w838f6010/vmem/vmpage_number-active_anon:
  value: 1.249400e+04
w838f6010/vmem/vmpage_number-active_file:
  value: 7.016800e+04
w838f6010/vmem/vmpage_number-anon_pages:
  value: 1.095500e+04
w838f6010/vmem/vmpage_number-anon_transparent_hugepages:
  value: 3.000000e+00
w838f6010/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w838f6010/vmem/vmpage_number-dirty:
  value: 0.000000e+00
w838f6010/vmem/vmpage_number-file_pages:
  value: 1.370680e+05
w838f6010/vmem/vmpage_number-free_pages:
  value: 2.502060e+05
w838f6010/vmem/vmpage_number-inactive_anon:
  value: 4.200000e+01
w838f6010/vmem/vmpage_number-inactive_file:
  value: 6.685900e+04
w838f6010/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w838f6010/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w838f6010/vmem/vmpage_number-kernel_stack:
  value: 1.130000e+02
w838f6010/vmem/vmpage_number-mapped:
  value: 1.480000e+03
w838f6010/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w838f6010/vmem/vmpage_number-page_table_pages:
  value: 4.220000e+02
w838f6010/vmem/vmpage_number-shmem:
  value: 4.500000e+01
w838f6010/vmem/vmpage_number-slab_reclaimable:
  value: 6.896900e+04
w838f6010/vmem/vmpage_number-slab_unreclaimable:
  value: 5.190000e+03
w838f6010/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w838f6010/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w838f6010/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w838f6010/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w838f6010/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w838f6700/cpu-0/cpu-idle:
  value: 9.619931e+01
w838f6700/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w838f6700/cpu-0/cpu-nice:
  value: 0.000000e+00
w838f6700/cpu-0/cpu-softirq:
  value: 0.000000e+00
w838f6700/cpu-0/cpu-steal:
  value: 0.000000e+00
w838f6700/cpu-0/cpu-system:
  value: 1.899992e+00
w838f6700/cpu-0/cpu-user:
  value: 1.600000e+00
w838f6700/cpu-0/cpu-wait:
  value: 0.000000e+00
w838f6700/cpu-1/cpu-idle:
  value: 9.599589e+01
w838f6700/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w838f6700/cpu-1/cpu-nice:
  value: 0.000000e+00
w838f6700/cpu-1/cpu-softirq:
  value: 0.000000e+00
w838f6700/cpu-1/cpu-steal:
  value: 0.000000e+00
w838f6700/cpu-1/cpu-system:
  value: 1.799923e+00
w838f6700/cpu-1/cpu-user:
  value: 1.799919e+00
w838f6700/cpu-1/cpu-wait:
  value: 0.000000e+00
w838f6700/cpu-2/cpu-idle:
  value: 9.539595e+01
w838f6700/cpu-2/cpu-interrupt:
  value: 0.000000e+00
w838f6700/cpu-2/cpu-nice:
  value: 0.000000e+00
w838f6700/cpu-2/cpu-softirq:
  value: 0.000000e+00
w838f6700/cpu-2/cpu-steal:
  value: 9.999593e-02
w838f6700/cpu-2/cpu-system:
  value: 2.299903e+00
w838f6700/cpu-2/cpu-user:
  value: 1.899919e+00
w838f6700/cpu-2/cpu-wait:
  value: 0.000000e+00
w838f6700/cpu-3/cpu-idle:
  value: 9.439610e+01
w838f6700/cpu-3/cpu-interrupt:
  value: 0.000000e+00
w838f6700/cpu-3/cpu-nice:
  value: 0.000000e+00
w838f6700/cpu-3/cpu-softirq:
  value: 0.000000e+00
w838f6700/cpu-3/cpu-steal:
  value: 0.000000e+00
w838f6700/cpu-3/cpu-system:
  value: 2.699890e+00
w838f6700/cpu-3/cpu-user:
  value: 2.599893e+00
w838f6700/cpu-3/cpu-wait:
  value: 0.000000e+00
w838f6700/df-boot/df_complex-free:
  value: 4.793713e+08
w838f6700/df-boot/df_complex-reserved:
  value: 2.684109e+07
w838f6700/df-boot/df_complex-used:
  value: 2.221261e+07
w838f6700/df-boot/df_inodes-free:
  value: 3.273000e+04
w838f6700/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-boot/df_inodes-used:
  value: 3.800000e+01
w838f6700/df-boot/percent_bytes-free:
  value: 9.071700e+01
w838f6700/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w838f6700/df-boot/percent_bytes-used:
  value: 4.203550e+00
w838f6700/df-boot/percent_inodes-free:
  value: 9.988403e+01
w838f6700/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-boot/percent_inodes-used:
  value: 1.159668e-01
w838f6700/df-data1/df_complex-free:
  value: 2.740491e+10
w838f6700/df-data1/df_complex-reserved:
  value: 1.476235e+09
w838f6700/df-data1/df_complex-used:
  value: 1.802977e+08
w838f6700/df-data1/df_inodes-free:
  value: 1.802227e+06
w838f6700/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-data1/df_inodes-used:
  value: 1.300000e+01
w838f6700/df-data1/percent_bytes-free:
  value: 9.429990e+01
w838f6700/df-data1/percent_bytes-reserved:
  value: 5.079704e+00
w838f6700/df-data1/percent_bytes-used:
  value: 6.204019e-01
w838f6700/df-data1/percent_inodes-free:
  value: 9.999928e+01
w838f6700/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-data1/percent_inodes-used:
  value: 7.213246e-04
w838f6700/df-dev-shm/df_complex-free:
  value: 4.126425e+09
w838f6700/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w838f6700/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w838f6700/df-dev-shm/df_inodes-free:
  value: 1.007427e+06
w838f6700/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w838f6700/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w838f6700/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w838f6700/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w838f6700/df-dev-shm/percent_inodes-free:
  value: 9.999990e+01
w838f6700/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-dev-shm/percent_inodes-used:
  value: 9.926267e-05
w838f6700/df-root/df_complex-free:
  value: 1.069915e+10
w838f6700/df-root/df_complex-reserved:
  value: 6.442435e+08
w838f6700/df-root/df_complex-used:
  value: 1.339314e+09
w838f6700/df-root/df_inodes-free:
  value: 7.466750e+05
w838f6700/df-root/df_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-root/df_inodes-used:
  value: 3.975700e+04
w838f6700/df-root/percent_bytes-free:
  value: 8.436014e+01
w838f6700/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w838f6700/df-root/percent_bytes-used:
  value: 1.056016e+01
w838f6700/df-root/percent_inodes-free:
  value: 9.494464e+01
w838f6700/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-root/percent_inodes-used:
  value: 5.055364e+00
w838f6700/df-var/df_complex-free:
  value: 6.687375e+09
w838f6700/df-var/df_complex-reserved:
  value: 4.294943e+08
w838f6700/df-var/df_complex-used:
  value: 1.338249e+09
w838f6700/df-var/df_inodes-free:
  value: 5.212840e+05
w838f6700/df-var/df_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-var/df_inodes-used:
  value: 3.004000e+03
w838f6700/df-var/percent_bytes-free:
  value: 7.909262e+01
w838f6700/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w838f6700/df-var/percent_bytes-used:
  value: 1.582768e+01
w838f6700/df-var/percent_inodes-free:
  value: 9.942703e+01
w838f6700/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w838f6700/df-var/percent_inodes-used:
  value: 5.729675e-01
w838f6700/disk-vda/disk_merged:
  read: 0.000000e+00
  write: 1.000002e-01
w838f6700/disk-vda/disk_octets:
  read: 0.000000e+00
  write: 2.457603e+03
w838f6700/disk-vda/disk_ops:
  read: 0.000000e+00
  write: 5.000007e-01
w838f6700/disk-vda/disk_time:
  read: 0.000000e+00
  write: 1.200001e+00
w838f6700/disk-vda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda3/disk_merged:
  read: 0.000000e+00
  write: 1.000001e-01
w838f6700/disk-vda3/disk_octets:
  read: 0.000000e+00
  write: 2.457604e+03
w838f6700/disk-vda3/disk_ops:
  read: 0.000000e+00
  write: 5.000007e-01
w838f6700/disk-vda3/disk_time:
  read: 0.000000e+00
  write: 1.200002e+00
w838f6700/disk-vda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/disk-vda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6700/load/load:
  longterm: 2.000000e-02
  midterm: 4.000000e-02
  shortterm: 0.000000e+00
w838f6700/memory/memory-buffered:
  value: 1.980662e+08
w838f6700/memory/memory-cached:
  value: 1.702576e+09
w838f6700/memory/memory-free:
  value: 4.717773e+09
w838f6700/memory/memory-used:
  value: 1.634439e+09
w838f6700/network/if_octets:
  rx: 0.000000e+00
  tx: 9.293005e+02
w838f6700/network/if_packets:
  rx: 0.000000e+00
  tx: 7.000008e-01
w838f6700/network/queue_length:
  value: 0.000000e+00
w838f6700/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w838f6700/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w838f6700/network/total_values-send-accepted:
  value: 1.640000e+01
w838f6700/network/total_values-send-rejected:
  value: 0.000000e+00
w838f6700/swap/swap-cached:
  value: 0.000000e+00
w838f6700/swap/swap-free:
  value: 2.147475e+09
w838f6700/swap/swap-used:
  value: 0.000000e+00
w838f6700/swap/swap_io-in:
  value: 0.000000e+00
w838f6700/swap/swap_io-out:
  value: 0.000000e+00
w838f6700/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 2.519992e+01
w838f6700/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 1.999993e+00
w838f6700/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w838f6700/vmem/vmpage_number-active_anon:
  value: 2.680560e+05
w838f6700/vmem/vmpage_number-active_file:
  value: 1.344860e+05
w838f6700/vmem/vmpage_number-anon_pages:
  value: 1.661620e+05
w838f6700/vmem/vmpage_number-anon_transparent_hugepages:
  value: 1.990000e+02
w838f6700/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w838f6700/vmem/vmpage_number-dirty:
  value: 6.000000e+00
w838f6700/vmem/vmpage_number-file_pages:
  value: 4.640230e+05
w838f6700/vmem/vmpage_number-free_pages:
  value: 1.151800e+06
w838f6700/vmem/vmpage_number-inactive_anon:
  value: 6.300000e+01
w838f6700/vmem/vmpage_number-inactive_file:
  value: 3.294730e+05
w838f6700/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w838f6700/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w838f6700/vmem/vmpage_number-kernel_stack:
  value: 2.060000e+02
w838f6700/vmem/vmpage_number-mapped:
  value: 7.423000e+03
w838f6700/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w838f6700/vmem/vmpage_number-page_table_pages:
  value: 2.612000e+03
w838f6700/vmem/vmpage_number-shmem:
  value: 6.900000e+01
w838f6700/vmem/vmpage_number-slab_reclaimable:
  value: 1.048400e+05
w838f6700/vmem/vmpage_number-slab_unreclaimable:
  value: 6.180000e+03
w838f6700/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w838f6700/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w838f6700/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w838f6700/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w838f6700/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w838f6800/cpu-0/cpu-idle:
  value: 1.000001e+02
w838f6800/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w838f6800/cpu-0/cpu-nice:
  value: 0.000000e+00
w838f6800/cpu-0/cpu-softirq:
  value: 0.000000e+00
w838f6800/cpu-0/cpu-steal:
  value: 0.000000e+00
w838f6800/cpu-0/cpu-system:
  value: 0.000000e+00
w838f6800/cpu-0/cpu-user:
  value: 0.000000e+00
w838f6800/cpu-0/cpu-wait:
  value: 0.000000e+00
w838f6800/cpu-1/cpu-idle:
  value: 9.989984e+01
w838f6800/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w838f6800/cpu-1/cpu-nice:
  value: 0.000000e+00
w838f6800/cpu-1/cpu-softirq:
  value: 0.000000e+00
w838f6800/cpu-1/cpu-steal:
  value: 0.000000e+00
w838f6800/cpu-1/cpu-system:
  value: 0.000000e+00
w838f6800/cpu-1/cpu-user:
  value: 0.000000e+00
w838f6800/cpu-1/cpu-wait:
  value: 0.000000e+00
w838f6800/cpu-2/cpu-idle:
  value: 9.989981e+01
w838f6800/cpu-2/cpu-interrupt:
  value: 0.000000e+00
w838f6800/cpu-2/cpu-nice:
  value: 0.000000e+00
w838f6800/cpu-2/cpu-softirq:
  value: 0.000000e+00
w838f6800/cpu-2/cpu-steal:
  value: 0.000000e+00
w838f6800/cpu-2/cpu-system:
  value: 0.000000e+00
w838f6800/cpu-2/cpu-user:
  value: 0.000000e+00
w838f6800/cpu-2/cpu-wait:
  value: 0.000000e+00
w838f6800/cpu-3/cpu-idle:
  value: 9.989982e+01
w838f6800/cpu-3/cpu-interrupt:
  value: 0.000000e+00
w838f6800/cpu-3/cpu-nice:
  value: 0.000000e+00
w838f6800/cpu-3/cpu-softirq:
  value: 0.000000e+00
w838f6800/cpu-3/cpu-steal:
  value: 0.000000e+00
w838f6800/cpu-3/cpu-system:
  value: 0.000000e+00
w838f6800/cpu-3/cpu-user:
  value: 0.000000e+00
w838f6800/cpu-3/cpu-wait:
  value: 0.000000e+00
w838f6800/df-boot/df_complex-free:
  value: 4.793917e+08
w838f6800/df-boot/df_complex-reserved:
  value: 2.684109e+07
w838f6800/df-boot/df_complex-used:
  value: 2.219213e+07
w838f6800/df-boot/df_inodes-free:
  value: 3.273000e+04
w838f6800/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-boot/df_inodes-used:
  value: 3.800000e+01
w838f6800/df-boot/percent_bytes-free:
  value: 9.072087e+01
w838f6800/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w838f6800/df-boot/percent_bytes-used:
  value: 4.199674e+00
w838f6800/df-boot/percent_inodes-free:
  value: 9.988403e+01
w838f6800/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-boot/percent_inodes-used:
  value: 1.159668e-01
w838f6800/df-data1/df_complex-free:
  value: 2.740491e+10
w838f6800/df-data1/df_complex-reserved:
  value: 1.476235e+09
w838f6800/df-data1/df_complex-used:
  value: 1.802977e+08
w838f6800/df-data1/df_inodes-free:
  value: 1.802227e+06
w838f6800/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-data1/df_inodes-used:
  value: 1.300000e+01
w838f6800/df-data1/percent_bytes-free:
  value: 9.429990e+01
w838f6800/df-data1/percent_bytes-reserved:
  value: 5.079704e+00
w838f6800/df-data1/percent_bytes-used:
  value: 6.204019e-01
w838f6800/df-data1/percent_inodes-free:
  value: 9.999928e+01
w838f6800/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-data1/percent_inodes-used:
  value: 7.213246e-04
w838f6800/df-dev-shm/df_complex-free:
  value: 4.126425e+09
w838f6800/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w838f6800/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w838f6800/df-dev-shm/df_inodes-free:
  value: 1.007427e+06
w838f6800/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w838f6800/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w838f6800/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w838f6800/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w838f6800/df-dev-shm/percent_inodes-free:
  value: 9.999990e+01
w838f6800/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-dev-shm/percent_inodes-used:
  value: 9.926267e-05
w838f6800/df-root/df_complex-free:
  value: 1.099071e+10
w838f6800/df-root/df_complex-reserved:
  value: 6.442435e+08
w838f6800/df-root/df_complex-used:
  value: 1.047757e+09
w838f6800/df-root/df_inodes-free:
  value: 7.509280e+05
w838f6800/df-root/df_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-root/df_inodes-used:
  value: 3.550400e+04
w838f6800/df-root/percent_bytes-free:
  value: 8.665900e+01
w838f6800/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w838f6800/df-root/percent_bytes-used:
  value: 8.261303e+00
w838f6800/df-root/percent_inodes-free:
  value: 9.548543e+01
w838f6800/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-root/percent_inodes-used:
  value: 4.514567e+00
w838f6800/df-var/df_complex-free:
  value: 7.635927e+09
w838f6800/df-var/df_complex-reserved:
  value: 4.294943e+08
w838f6800/df-var/df_complex-used:
  value: 3.896975e+08
w838f6800/df-var/df_inodes-free:
  value: 5.230400e+05
w838f6800/df-var/df_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-var/df_inodes-used:
  value: 1.248000e+03
w838f6800/df-var/percent_bytes-free:
  value: 9.031129e+01
w838f6800/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w838f6800/df-var/percent_bytes-used:
  value: 4.609013e+00
w838f6800/df-var/percent_inodes-free:
  value: 9.976196e+01
w838f6800/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w838f6800/df-var/percent_inodes-used:
  value: 2.380371e-01
w838f6800/disk-vda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/disk-vda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6800/load/load:
  longterm: 0.000000e+00
  midterm: 0.000000e+00
  shortterm: 0.000000e+00
w838f6800/memory/memory-buffered:
  value: 1.443430e+08
w838f6800/memory/memory-cached:
  value: 4.507607e+08
w838f6800/memory/memory-free:
  value: 7.122756e+09
w838f6800/memory/memory-used:
  value: 5.349949e+08
w838f6800/network/if_octets:
  rx: 0.000000e+00
  tx: 7.896001e+02
w838f6800/network/if_packets:
  rx: 0.000000e+00
  tx: 6.000121e-01
w838f6800/network/queue_length:
  value: 0.000000e+00
w838f6800/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w838f6800/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w838f6800/network/total_values-send-accepted:
  value: 1.530036e+01
w838f6800/network/total_values-send-rejected:
  value: 0.000000e+00
w838f6800/swap/swap-cached:
  value: 0.000000e+00
w838f6800/swap/swap-free:
  value: 2.147475e+09
w838f6800/swap/swap-used:
  value: 0.000000e+00
w838f6800/swap/swap_io-in:
  value: 0.000000e+00
w838f6800/swap/swap_io-out:
  value: 0.000000e+00
w838f6800/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 4.500056e+00
w838f6800/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
w838f6800/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w838f6800/vmem/vmpage_number-active_anon:
  value: 1.298400e+04
w838f6800/vmem/vmpage_number-active_file:
  value: 7.437200e+04
w838f6800/vmem/vmpage_number-anon_pages:
  value: 1.042100e+04
w838f6800/vmem/vmpage_number-anon_transparent_hugepages:
  value: 5.000000e+00
w838f6800/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w838f6800/vmem/vmpage_number-dirty:
  value: 0.000000e+00
w838f6800/vmem/vmpage_number-file_pages:
  value: 1.452890e+05
w838f6800/vmem/vmpage_number-free_pages:
  value: 1.738954e+06
w838f6800/vmem/vmpage_number-inactive_anon:
  value: 4.200000e+01
w838f6800/vmem/vmpage_number-inactive_file:
  value: 7.087600e+04
w838f6800/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w838f6800/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w838f6800/vmem/vmpage_number-kernel_stack:
  value: 1.390000e+02
w838f6800/vmem/vmpage_number-mapped:
  value: 1.478000e+03
w838f6800/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w838f6800/vmem/vmpage_number-page_table_pages:
  value: 4.100000e+02
w838f6800/vmem/vmpage_number-shmem:
  value: 4.500000e+01
w838f6800/vmem/vmpage_number-slab_reclaimable:
  value: 9.488500e+04
w838f6800/vmem/vmpage_number-slab_unreclaimable:
  value: 5.513000e+03
w838f6800/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w838f6800/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w838f6800/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w838f6800/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w838f6800/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w838f6900/cpu-0/cpu-idle:
  value: 9.700088e+01
w838f6900/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w838f6900/cpu-0/cpu-nice:
  value: 0.000000e+00
w838f6900/cpu-0/cpu-softirq:
  value: 0.000000e+00
w838f6900/cpu-0/cpu-steal:
  value: 0.000000e+00
w838f6900/cpu-0/cpu-system:
  value: 1.000009e-01
w838f6900/cpu-0/cpu-user:
  value: 2.900025e+00
w838f6900/cpu-0/cpu-wait:
  value: 0.000000e+00
w838f6900/cpu-1/cpu-idle:
  value: 9.960079e+01
w838f6900/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w838f6900/cpu-1/cpu-nice:
  value: 0.000000e+00
w838f6900/cpu-1/cpu-softirq:
  value: 0.000000e+00
w838f6900/cpu-1/cpu-steal:
  value: 0.000000e+00
w838f6900/cpu-1/cpu-system:
  value: 0.000000e+00
w838f6900/cpu-1/cpu-user:
  value: 0.000000e+00
w838f6900/cpu-1/cpu-wait:
  value: 0.000000e+00
w838f6900/df-boot/df_complex-free:
  value: 4.793836e+08
w838f6900/df-boot/df_complex-reserved:
  value: 2.684109e+07
w838f6900/df-boot/df_complex-used:
  value: 2.220032e+07
w838f6900/df-boot/df_inodes-free:
  value: 3.273000e+04
w838f6900/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-boot/df_inodes-used:
  value: 3.800000e+01
w838f6900/df-boot/percent_bytes-free:
  value: 9.071932e+01
w838f6900/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w838f6900/df-boot/percent_bytes-used:
  value: 4.201225e+00
w838f6900/df-boot/percent_inodes-free:
  value: 9.988403e+01
w838f6900/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-boot/percent_inodes-used:
  value: 1.159668e-01
w838f6900/df-data1/df_complex-free:
  value: 2.740491e+10
w838f6900/df-data1/df_complex-reserved:
  value: 1.476235e+09
w838f6900/df-data1/df_complex-used:
  value: 1.802977e+08
w838f6900/df-data1/df_inodes-free:
  value: 1.802227e+06
w838f6900/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-data1/df_inodes-used:
  value: 1.300000e+01
w838f6900/df-data1/percent_bytes-free:
  value: 9.429990e+01
w838f6900/df-data1/percent_bytes-reserved:
  value: 5.079704e+00
w838f6900/df-data1/percent_bytes-used:
  value: 6.204019e-01
w838f6900/df-data1/percent_inodes-free:
  value: 9.999928e+01
w838f6900/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-data1/percent_inodes-used:
  value: 7.213246e-04
w838f6900/df-dev-shm/df_complex-free:
  value: 9.842483e+08
w838f6900/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w838f6900/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w838f6900/df-dev-shm/df_inodes-free:
  value: 2.402940e+05
w838f6900/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w838f6900/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w838f6900/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w838f6900/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w838f6900/df-dev-shm/percent_inodes-free:
  value: 9.999958e+01
w838f6900/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-dev-shm/percent_inodes-used:
  value: 4.161551e-04
w838f6900/df-root/df_complex-free:
  value: 1.097382e+10
w838f6900/df-root/df_complex-reserved:
  value: 6.442435e+08
w838f6900/df-root/df_complex-used:
  value: 1.064645e+09
w838f6900/df-root/df_inodes-free:
  value: 7.501330e+05
w838f6900/df-root/df_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-root/df_inodes-used:
  value: 3.629900e+04
w838f6900/df-root/percent_bytes-free:
  value: 8.652584e+01
w838f6900/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w838f6900/df-root/percent_bytes-used:
  value: 8.394459e+00
w838f6900/df-root/percent_inodes-free:
  value: 9.538434e+01
w838f6900/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-root/percent_inodes-used:
  value: 4.615657e+00
w838f6900/df-var/df_complex-free:
  value: 7.637877e+09
w838f6900/df-var/df_complex-reserved:
  value: 4.294943e+08
w838f6900/df-var/df_complex-used:
  value: 3.877478e+08
w838f6900/df-var/df_inodes-free:
  value: 5.229840e+05
w838f6900/df-var/df_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-var/df_inodes-used:
  value: 1.304000e+03
w838f6900/df-var/percent_bytes-free:
  value: 9.033435e+01
w838f6900/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w838f6900/df-var/percent_bytes-used:
  value: 4.585954e+00
w838f6900/df-var/percent_inodes-free:
  value: 9.975128e+01
w838f6900/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w838f6900/df-var/percent_inodes-used:
  value: 2.487183e-01
w838f6900/disk-vda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/disk-vda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w838f6900/load/load:
  longterm: 0.000000e+00
  midterm: 0.000000e+00
  shortterm: 0.000000e+00
w838f6900/memory/memory-buffered:
  value: 1.040671e+08
w838f6900/memory/memory-cached:
  value: 4.379361e+08
w838f6900/memory/memory-free:
  value: 9.871852e+08
w838f6900/memory/memory-used:
  value: 4.393083e+08
w838f6900/network/if_octets:
  rx: 0.000000e+00
  tx: 6.614006e+02
w838f6900/network/if_packets:
  rx: 0.000000e+00
  tx: 4.999994e-01
w838f6900/network/queue_length:
  value: 0.000000e+00
w838f6900/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w838f6900/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w838f6900/network/total_values-send-accepted:
  value: 1.479996e+01
w838f6900/network/total_values-send-rejected:
  value: 0.000000e+00
w838f6900/swap/swap-cached:
  value: 0.000000e+00
w838f6900/swap/swap-free:
  value: 2.147475e+09
w838f6900/swap/swap-used:
  value: 0.000000e+00
w838f6900/swap/swap_io-in:
  value: 0.000000e+00
w838f6900/swap/swap_io-out:
  value: 0.000000e+00
w838f6900/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 4.300042e+00
w838f6900/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
w838f6900/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w838f6900/vmem/vmpage_number-active_anon:
  value: 3.653600e+04
w838f6900/vmem/vmpage_number-active_file:
  value: 7.163800e+04
w838f6900/vmem/vmpage_number-anon_pages:
  value: 2.014900e+04
w838f6900/vmem/vmpage_number-anon_transparent_hugepages:
  value: 3.200000e+01
w838f6900/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w838f6900/vmem/vmpage_number-dirty:
  value: 0.000000e+00
w838f6900/vmem/vmpage_number-file_pages:
  value: 1.323250e+05
w838f6900/vmem/vmpage_number-free_pages:
  value: 2.410120e+05
w838f6900/vmem/vmpage_number-inactive_anon:
  value: 4.300000e+01
w838f6900/vmem/vmpage_number-inactive_file:
  value: 6.064500e+04
w838f6900/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w838f6900/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w838f6900/vmem/vmpage_number-kernel_stack:
  value: 1.280000e+02
w838f6900/vmem/vmpage_number-mapped:
  value: 3.447000e+03
w838f6900/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w838f6900/vmem/vmpage_number-page_table_pages:
  value: 6.660000e+02
w838f6900/vmem/vmpage_number-shmem:
  value: 4.600000e+01
w838f6900/vmem/vmpage_number-slab_reclaimable:
  value: 5.838000e+04
w838f6900/vmem/vmpage_number-slab_unreclaimable:
  value: 5.330000e+03
w838f6900/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w838f6900/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w838f6900/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w838f6900/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w838f6900/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w83df6100/cpu-0/cpu-idle:
  value: 9.979989e+01
w83df6100/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-0/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-0/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-0/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-0/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-0/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-0/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-1/cpu-idle:
  value: 9.999984e+01
w83df6100/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-1/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-1/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-1/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-1/cpu-system:
  value: 9.999985e-02
w83df6100/cpu-1/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-1/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-10/cpu-idle:
  value: 9.989993e+01
w83df6100/cpu-10/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-10/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-10/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-10/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-10/cpu-system:
  value: 9.999993e-02
w83df6100/cpu-10/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-10/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-11/cpu-idle:
  value: 9.999994e+01
w83df6100/cpu-11/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-11/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-11/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-11/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-11/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-11/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-11/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-12/cpu-idle:
  value: 9.999992e+01
w83df6100/cpu-12/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-12/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-12/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-12/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-12/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-12/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-12/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-13/cpu-idle:
  value: 9.989989e+01
w83df6100/cpu-13/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-13/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-13/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-13/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-13/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-13/cpu-user:
  value: 9.999990e-02
w83df6100/cpu-13/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-14/cpu-idle:
  value: 9.999994e+01
w83df6100/cpu-14/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-14/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-14/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-14/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-14/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-14/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-14/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-15/cpu-idle:
  value: 9.990003e+01
w83df6100/cpu-15/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-15/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-15/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-15/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-15/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-15/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-15/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-16/cpu-idle:
  value: 1.000001e+02
w83df6100/cpu-16/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-16/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-16/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-16/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-16/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-16/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-16/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-17/cpu-idle:
  value: 9.999995e+01
w83df6100/cpu-17/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-17/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-17/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-17/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-17/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-17/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-17/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-18/cpu-idle:
  value: 9.999993e+01
w83df6100/cpu-18/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-18/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-18/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-18/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-18/cpu-system:
  value: 9.999993e-02
w83df6100/cpu-18/cpu-user:
  value: 9.999991e-02
w83df6100/cpu-18/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-19/cpu-idle:
  value: 9.989974e+01
w83df6100/cpu-19/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-19/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-19/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-19/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-19/cpu-system:
  value: 9.999975e-02
w83df6100/cpu-19/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-19/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-2/cpu-idle:
  value: 9.999995e+01
w83df6100/cpu-2/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-2/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-2/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-2/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-2/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-2/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-2/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-20/cpu-idle:
  value: 9.999968e+01
w83df6100/cpu-20/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-20/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-20/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-20/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-20/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-20/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-20/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-21/cpu-idle:
  value: 9.999958e+01
w83df6100/cpu-21/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-21/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-21/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-21/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-21/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-21/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-21/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-22/cpu-idle:
  value: 9.999951e+01
w83df6100/cpu-22/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-22/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-22/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-22/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-22/cpu-system:
  value: 9.999951e-02
w83df6100/cpu-22/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-22/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-23/cpu-idle:
  value: 9.989947e+01
w83df6100/cpu-23/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-23/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-23/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-23/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-23/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-23/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-23/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-3/cpu-idle:
  value: 1.000002e+02
w83df6100/cpu-3/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-3/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-3/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-3/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-3/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-3/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-3/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-4/cpu-idle:
  value: 1.000004e+02
w83df6100/cpu-4/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-4/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-4/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-4/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-4/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-4/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-4/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-5/cpu-idle:
  value: 1.000004e+02
w83df6100/cpu-5/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-5/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-5/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-5/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-5/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-5/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-5/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-6/cpu-idle:
  value: 9.990037e+01
w83df6100/cpu-6/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-6/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-6/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-6/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-6/cpu-system:
  value: 1.000004e-01
w83df6100/cpu-6/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-6/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-7/cpu-idle:
  value: 9.990034e+01
w83df6100/cpu-7/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-7/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-7/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-7/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-7/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-7/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-7/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-8/cpu-idle:
  value: 9.990024e+01
w83df6100/cpu-8/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-8/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-8/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-8/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-8/cpu-system:
  value: 3.000007e-01
w83df6100/cpu-8/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-8/cpu-wait:
  value: 0.000000e+00
w83df6100/cpu-9/cpu-idle:
  value: 9.999998e+01
w83df6100/cpu-9/cpu-interrupt:
  value: 0.000000e+00
w83df6100/cpu-9/cpu-nice:
  value: 0.000000e+00
w83df6100/cpu-9/cpu-softirq:
  value: 0.000000e+00
w83df6100/cpu-9/cpu-steal:
  value: 0.000000e+00
w83df6100/cpu-9/cpu-system:
  value: 0.000000e+00
w83df6100/cpu-9/cpu-user:
  value: 0.000000e+00
w83df6100/cpu-9/cpu-wait:
  value: 0.000000e+00
w83df6100/df-boot/df_complex-free:
  value: 4.369900e+08
w83df6100/df-boot/df_complex-reserved:
  value: 2.684109e+07
w83df6100/df-boot/df_complex-used:
  value: 6.459392e+07
w83df6100/df-boot/df_inodes-free:
  value: 3.271800e+04
w83df6100/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-boot/df_inodes-used:
  value: 5.000000e+01
w83df6100/df-boot/percent_bytes-free:
  value: 8.269669e+01
w83df6100/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w83df6100/df-boot/percent_bytes-used:
  value: 1.222386e+01
w83df6100/df-boot/percent_inodes-free:
  value: 9.984741e+01
w83df6100/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-boot/percent_inodes-used:
  value: 1.525879e-01
w83df6100/df-data1/df_complex-free:
  value: 2.574625e+11
w83df6100/df-data1/df_complex-reserved:
  value: 1.378982e+10
w83df6100/df-data1/df_complex-used:
  value: 2.165924e+08
w83df6100/df-data1/df_inodes-free:
  value: 1.683452e+07
w83df6100/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-data1/df_inodes-used:
  value: 4.400000e+01
w83df6100/df-data1/percent_bytes-free:
  value: 9.484051e+01
w83df6100/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
w83df6100/df-data1/percent_bytes-used:
  value: 7.978535e-02
w83df6100/df-data1/percent_inodes-free:
  value: 9.999974e+01
w83df6100/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-data1/percent_inodes-used:
  value: 2.613671e-04
w83df6100/df-dev-shm/df_complex-free:
  value: 3.375709e+10
w83df6100/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w83df6100/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w83df6100/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
w83df6100/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w83df6100/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w83df6100/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w83df6100/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w83df6100/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
w83df6100/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
w83df6100/df-root/df_complex-free:
  value: 1.076427e+10
w83df6100/df-root/df_complex-reserved:
  value: 6.442435e+08
w83df6100/df-root/df_complex-used:
  value: 1.274192e+09
w83df6100/df-root/df_inodes-free:
  value: 7.458150e+05
w83df6100/df-root/df_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-root/df_inodes-used:
  value: 4.061700e+04
w83df6100/df-root/percent_bytes-free:
  value: 8.487361e+01
w83df6100/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w83df6100/df-root/percent_bytes-used:
  value: 1.004669e+01
w83df6100/df-root/percent_inodes-free:
  value: 9.483528e+01
w83df6100/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-root/percent_inodes-used:
  value: 5.164719e+00
w83df6100/df-var/df_complex-free:
  value: 7.566651e+09
w83df6100/df-var/df_complex-reserved:
  value: 4.294943e+08
w83df6100/df-var/df_complex-used:
  value: 4.589732e+08
w83df6100/df-var/df_inodes-free:
  value: 5.227920e+05
w83df6100/df-var/df_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-var/df_inodes-used:
  value: 1.496000e+03
w83df6100/df-var/percent_bytes-free:
  value: 8.949196e+01
w83df6100/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w83df6100/df-var/percent_bytes-used:
  value: 5.428347e+00
w83df6100/df-var/percent_inodes-free:
  value: 9.971466e+01
w83df6100/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w83df6100/df-var/percent_inodes-used:
  value: 2.853394e-01
w83df6100/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6100/load/load:
  longterm: 0.000000e+00
  midterm: 0.000000e+00
  shortterm: 0.000000e+00
w83df6100/memory/memory-buffered:
  value: 2.127012e+08
w83df6100/memory/memory-cached:
  value: 5.203763e+08
w83df6100/memory/memory-free:
  value: 6.526190e+10
w83df6100/memory/memory-used:
  value: 1.519194e+09
w83df6100/network/if_octets:
  rx: 0.000000e+00
  tx: 1.580500e+03
w83df6100/network/if_packets:
  rx: 0.000000e+00
  tx: 1.200000e+00
w83df6100/network/queue_length:
  value: 0.000000e+00
w83df6100/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w83df6100/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w83df6100/network/total_values-send-accepted:
  value: 3.239997e+01
w83df6100/network/total_values-send-rejected:
  value: 0.000000e+00
w83df6100/swap/swap-cached:
  value: 0.000000e+00
w83df6100/swap/swap-free:
  value: 2.147475e+09
w83df6100/swap/swap-used:
  value: 0.000000e+00
w83df6100/swap/swap_io-in:
  value: 0.000000e+00
w83df6100/swap/swap_io-out:
  value: 0.000000e+00
w83df6100/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 3.899998e+00
w83df6100/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6100/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6100/vmem/vmpage_number-active_anon:
  value: 1.617900e+04
w83df6100/vmem/vmpage_number-active_file:
  value: 1.166460e+05
w83df6100/vmem/vmpage_number-anon_pages:
  value: 1.208200e+04
w83df6100/vmem/vmpage_number-anon_transparent_hugepages:
  value: 8.000000e+00
w83df6100/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w83df6100/vmem/vmpage_number-dirty:
  value: 0.000000e+00
w83df6100/vmem/vmpage_number-file_pages:
  value: 1.789740e+05
w83df6100/vmem/vmpage_number-free_pages:
  value: 1.593308e+07
w83df6100/vmem/vmpage_number-inactive_anon:
  value: 4.500000e+01
w83df6100/vmem/vmpage_number-inactive_file:
  value: 6.228400e+04
w83df6100/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w83df6100/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w83df6100/vmem/vmpage_number-kernel_stack:
  value: 4.390000e+02
w83df6100/vmem/vmpage_number-mapped:
  value: 1.670000e+03
w83df6100/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w83df6100/vmem/vmpage_number-page_table_pages:
  value: 5.870000e+02
w83df6100/vmem/vmpage_number-shmem:
  value: 4.700000e+01
w83df6100/vmem/vmpage_number-slab_reclaimable:
  value: 2.364110e+05
w83df6100/vmem/vmpage_number-slab_unreclaimable:
  value: 1.472600e+04
w83df6100/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w83df6100/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w83df6100/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w83df6100/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w83df6100/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w83df6200/cpu-0/cpu-idle:
  value: 9.970004e+01
w83df6200/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-0/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-0/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-0/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-0/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-0/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-0/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-1/cpu-idle:
  value: 1.000000e+02
w83df6200/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-1/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-1/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-1/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-1/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-1/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-1/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-10/cpu-idle:
  value: 9.990010e+01
w83df6200/cpu-10/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-10/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-10/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-10/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-10/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-10/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-10/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-11/cpu-idle:
  value: 1.000000e+02
w83df6200/cpu-11/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-11/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-11/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-11/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-11/cpu-system:
  value: 1.000001e-01
w83df6200/cpu-11/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-11/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-12/cpu-idle:
  value: 9.990011e+01
w83df6200/cpu-12/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-12/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-12/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-12/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-12/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-12/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-12/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-13/cpu-idle:
  value: 9.990010e+01
w83df6200/cpu-13/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-13/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-13/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-13/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-13/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-13/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-13/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-14/cpu-idle:
  value: 1.000000e+02
w83df6200/cpu-14/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-14/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-14/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-14/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-14/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-14/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-14/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-15/cpu-idle:
  value: 1.000000e+02
w83df6200/cpu-15/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-15/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-15/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-15/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-15/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-15/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-15/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-16/cpu-idle:
  value: 9.999999e+01
w83df6200/cpu-16/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-16/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-16/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-16/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-16/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-16/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-16/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-17/cpu-idle:
  value: 1.000000e+02
w83df6200/cpu-17/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-17/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-17/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-17/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-17/cpu-system:
  value: 1.000000e-01
w83df6200/cpu-17/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-17/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-18/cpu-idle:
  value: 9.990002e+01
w83df6200/cpu-18/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-18/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-18/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-18/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-18/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-18/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-18/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-19/cpu-idle:
  value: 9.989997e+01
w83df6200/cpu-19/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-19/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-19/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-19/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-19/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-19/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-19/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-2/cpu-idle:
  value: 9.980002e+01
w83df6200/cpu-2/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-2/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-2/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-2/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-2/cpu-system:
  value: 1.000000e-01
w83df6200/cpu-2/cpu-user:
  value: 1.000000e-01
w83df6200/cpu-2/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-20/cpu-idle:
  value: 9.999997e+01
w83df6200/cpu-20/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-20/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-20/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-20/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-20/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-20/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-20/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-21/cpu-idle:
  value: 9.989997e+01
w83df6200/cpu-21/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-21/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-21/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-21/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-21/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-21/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-21/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-22/cpu-idle:
  value: 9.990010e+01
w83df6200/cpu-22/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-22/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-22/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-22/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-22/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-22/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-22/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-23/cpu-idle:
  value: 1.000000e+02
w83df6200/cpu-23/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-23/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-23/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-23/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-23/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-23/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-23/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-3/cpu-idle:
  value: 9.990001e+01
w83df6200/cpu-3/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-3/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-3/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-3/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-3/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-3/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-3/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-4/cpu-idle:
  value: 9.989997e+01
w83df6200/cpu-4/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-4/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-4/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-4/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-4/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-4/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-4/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-5/cpu-idle:
  value: 9.989993e+01
w83df6200/cpu-5/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-5/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-5/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-5/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-5/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-5/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-5/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-6/cpu-idle:
  value: 9.989989e+01
w83df6200/cpu-6/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-6/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-6/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-6/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-6/cpu-system:
  value: 9.999989e-02
w83df6200/cpu-6/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-6/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-7/cpu-idle:
  value: 9.990003e+01
w83df6200/cpu-7/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-7/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-7/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-7/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-7/cpu-system:
  value: 2.000001e-01
w83df6200/cpu-7/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-7/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-8/cpu-idle:
  value: 9.990009e+01
w83df6200/cpu-8/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-8/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-8/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-8/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-8/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-8/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-8/cpu-wait:
  value: 0.000000e+00
w83df6200/cpu-9/cpu-idle:
  value: 1.000001e+02
w83df6200/cpu-9/cpu-interrupt:
  value: 0.000000e+00
w83df6200/cpu-9/cpu-nice:
  value: 0.000000e+00
w83df6200/cpu-9/cpu-softirq:
  value: 0.000000e+00
w83df6200/cpu-9/cpu-steal:
  value: 0.000000e+00
w83df6200/cpu-9/cpu-system:
  value: 0.000000e+00
w83df6200/cpu-9/cpu-user:
  value: 0.000000e+00
w83df6200/cpu-9/cpu-wait:
  value: 0.000000e+00
w83df6200/df-boot/df_complex-free:
  value: 4.369490e+08
w83df6200/df-boot/df_complex-reserved:
  value: 2.684109e+07
w83df6200/df-boot/df_complex-used:
  value: 6.463488e+07
w83df6200/df-boot/df_inodes-free:
  value: 3.271800e+04
w83df6200/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-boot/df_inodes-used:
  value: 5.000000e+01
w83df6200/df-boot/percent_bytes-free:
  value: 8.268894e+01
w83df6200/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w83df6200/df-boot/percent_bytes-used:
  value: 1.223161e+01
w83df6200/df-boot/percent_inodes-free:
  value: 9.984741e+01
w83df6200/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-boot/percent_inodes-used:
  value: 1.525879e-01
w83df6200/df-data1/df_complex-free:
  value: 2.574625e+11
w83df6200/df-data1/df_complex-reserved:
  value: 1.378982e+10
w83df6200/df-data1/df_complex-used:
  value: 2.165924e+08
w83df6200/df-data1/df_inodes-free:
  value: 1.683452e+07
w83df6200/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-data1/df_inodes-used:
  value: 4.400000e+01
w83df6200/df-data1/percent_bytes-free:
  value: 9.484051e+01
w83df6200/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
w83df6200/df-data1/percent_bytes-used:
  value: 7.978535e-02
w83df6200/df-data1/percent_inodes-free:
  value: 9.999974e+01
w83df6200/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-data1/percent_inodes-used:
  value: 2.613671e-04
w83df6200/df-dev-shm/df_complex-free:
  value: 3.375709e+10
w83df6200/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w83df6200/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w83df6200/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
w83df6200/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w83df6200/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w83df6200/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w83df6200/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w83df6200/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
w83df6200/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
w83df6200/df-root/df_complex-free:
  value: 1.076426e+10
w83df6200/df-root/df_complex-reserved:
  value: 6.442435e+08
w83df6200/df-root/df_complex-used:
  value: 1.274200e+09
w83df6200/df-root/df_inodes-free:
  value: 7.458150e+05
w83df6200/df-root/df_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-root/df_inodes-used:
  value: 4.061700e+04
w83df6200/df-root/percent_bytes-free:
  value: 8.487355e+01
w83df6200/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w83df6200/df-root/percent_bytes-used:
  value: 1.004675e+01
w83df6200/df-root/percent_inodes-free:
  value: 9.483528e+01
w83df6200/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-root/percent_inodes-used:
  value: 5.164719e+00
w83df6200/df-var/df_complex-free:
  value: 7.567688e+09
w83df6200/df-var/df_complex-reserved:
  value: 4.294943e+08
w83df6200/df-var/df_complex-used:
  value: 4.579369e+08
w83df6200/df-var/df_inodes-free:
  value: 5.227970e+05
w83df6200/df-var/df_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-var/df_inodes-used:
  value: 1.491000e+03
w83df6200/df-var/percent_bytes-free:
  value: 8.950421e+01
w83df6200/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w83df6200/df-var/percent_bytes-used:
  value: 5.416090e+00
w83df6200/df-var/percent_inodes-free:
  value: 9.971561e+01
w83df6200/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w83df6200/df-var/percent_inodes-used:
  value: 2.843857e-01
w83df6200/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6200/load/load:
  longterm: 0.000000e+00
  midterm: 2.000000e-02
  shortterm: 1.000000e-02
w83df6200/memory/memory-buffered:
  value: 2.156175e+08
w83df6200/memory/memory-cached:
  value: 5.203517e+08
w83df6200/memory/memory-free:
  value: 6.524863e+10
w83df6200/memory/memory-used:
  value: 1.529577e+09
w83df6200/network/if_octets:
  rx: 0.000000e+00
  tx: 2.117499e+03
w83df6200/network/if_packets:
  rx: 0.000000e+00
  tx: 1.600001e+00
w83df6200/network/queue_length:
  value: 0.000000e+00
w83df6200/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w83df6200/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w83df6200/network/total_values-send-accepted:
  value: 4.570050e+01
w83df6200/network/total_values-send-rejected:
  value: 0.000000e+00
w83df6200/swap/swap-cached:
  value: 0.000000e+00
w83df6200/swap/swap-free:
  value: 2.147475e+09
w83df6200/swap/swap-used:
  value: 0.000000e+00
w83df6200/swap/swap_io-in:
  value: 0.000000e+00
w83df6200/swap/swap_io-out:
  value: 0.000000e+00
w83df6200/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 8.000883e-01
w83df6200/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6200/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6200/vmem/vmpage_number-active_anon:
  value: 1.666900e+04
w83df6200/vmem/vmpage_number-active_file:
  value: 1.167210e+05
w83df6200/vmem/vmpage_number-anon_pages:
  value: 1.154700e+04
w83df6200/vmem/vmpage_number-anon_transparent_hugepages:
  value: 1.000000e+01
w83df6200/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w83df6200/vmem/vmpage_number-dirty:
  value: 2.000000e+00
w83df6200/vmem/vmpage_number-file_pages:
  value: 1.796800e+05
w83df6200/vmem/vmpage_number-free_pages:
  value: 1.592984e+07
w83df6200/vmem/vmpage_number-inactive_anon:
  value: 4.500000e+01
w83df6200/vmem/vmpage_number-inactive_file:
  value: 6.291600e+04
w83df6200/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w83df6200/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w83df6200/vmem/vmpage_number-kernel_stack:
  value: 4.390000e+02
w83df6200/vmem/vmpage_number-mapped:
  value: 1.669000e+03
w83df6200/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w83df6200/vmem/vmpage_number-page_table_pages:
  value: 5.770000e+02
w83df6200/vmem/vmpage_number-shmem:
  value: 4.700000e+01
w83df6200/vmem/vmpage_number-slab_reclaimable:
  value: 2.386770e+05
w83df6200/vmem/vmpage_number-slab_unreclaimable:
  value: 1.476200e+04
w83df6200/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w83df6200/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w83df6200/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w83df6200/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w83df6200/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w83df6300/cpu-0/cpu-idle:
  value: 9.980074e+01
w83df6300/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-0/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-0/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-0/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-0/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-0/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-0/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-1/cpu-idle:
  value: 9.990070e+01
w83df6300/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-1/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-1/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-1/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-1/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-1/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-1/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-10/cpu-idle:
  value: 1.000010e+02
w83df6300/cpu-10/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-10/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-10/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-10/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-10/cpu-system:
  value: 1.000011e-01
w83df6300/cpu-10/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-10/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-11/cpu-idle:
  value: 1.000011e+02
w83df6300/cpu-11/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-11/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-11/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-11/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-11/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-11/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-11/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-12/cpu-idle:
  value: 9.980111e+01
w83df6300/cpu-12/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-12/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-12/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-12/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-12/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-12/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-12/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-13/cpu-idle:
  value: 9.990102e+01
w83df6300/cpu-13/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-13/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-13/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-13/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-13/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-13/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-13/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-14/cpu-idle:
  value: 9.990101e+01
w83df6300/cpu-14/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-14/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-14/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-14/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-14/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-14/cpu-user:
  value: 1.000010e-01
w83df6300/cpu-14/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-15/cpu-idle:
  value: 1.000010e+02
w83df6300/cpu-15/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-15/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-15/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-15/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-15/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-15/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-15/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-16/cpu-idle:
  value: 1.000010e+02
w83df6300/cpu-16/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-16/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-16/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-16/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-16/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-16/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-16/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-17/cpu-idle:
  value: 1.000010e+02
w83df6300/cpu-17/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-17/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-17/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-17/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-17/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-17/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-17/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-18/cpu-idle:
  value: 1.000009e+02
w83df6300/cpu-18/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-18/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-18/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-18/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-18/cpu-system:
  value: 1.000009e-01
w83df6300/cpu-18/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-18/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-19/cpu-idle:
  value: 9.990092e+01
w83df6300/cpu-19/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-19/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-19/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-19/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-19/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-19/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-19/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-2/cpu-idle:
  value: 1.000009e+02
w83df6300/cpu-2/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-2/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-2/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-2/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-2/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-2/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-2/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-20/cpu-idle:
  value: 1.000010e+02
w83df6300/cpu-20/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-20/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-20/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-20/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-20/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-20/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-20/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-21/cpu-idle:
  value: 1.000011e+02
w83df6300/cpu-21/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-21/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-21/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-21/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-21/cpu-system:
  value: 1.000011e-01
w83df6300/cpu-21/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-21/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-22/cpu-idle:
  value: 1.000012e+02
w83df6300/cpu-22/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-22/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-22/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-22/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-22/cpu-system:
  value: 1.000012e-01
w83df6300/cpu-22/cpu-user:
  value: 1.000012e-01
w83df6300/cpu-22/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-23/cpu-idle:
  value: 9.990117e+01
w83df6300/cpu-23/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-23/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-23/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-23/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-23/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-23/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-23/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-3/cpu-idle:
  value: 1.000009e+02
w83df6300/cpu-3/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-3/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-3/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-3/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-3/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-3/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-3/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-4/cpu-idle:
  value: 9.990089e+01
w83df6300/cpu-4/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-4/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-4/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-4/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-4/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-4/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-4/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-5/cpu-idle:
  value: 1.000010e+02
w83df6300/cpu-5/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-5/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-5/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-5/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-5/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-5/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-5/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-6/cpu-idle:
  value: 1.000010e+02
w83df6300/cpu-6/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-6/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-6/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-6/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-6/cpu-system:
  value: 1.000010e-01
w83df6300/cpu-6/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-6/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-7/cpu-idle:
  value: 1.000011e+02
w83df6300/cpu-7/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-7/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-7/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-7/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-7/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-7/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-7/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-8/cpu-idle:
  value: 9.990103e+01
w83df6300/cpu-8/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-8/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-8/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-8/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-8/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-8/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-8/cpu-wait:
  value: 0.000000e+00
w83df6300/cpu-9/cpu-idle:
  value: 1.000010e+02
w83df6300/cpu-9/cpu-interrupt:
  value: 0.000000e+00
w83df6300/cpu-9/cpu-nice:
  value: 0.000000e+00
w83df6300/cpu-9/cpu-softirq:
  value: 0.000000e+00
w83df6300/cpu-9/cpu-steal:
  value: 0.000000e+00
w83df6300/cpu-9/cpu-system:
  value: 0.000000e+00
w83df6300/cpu-9/cpu-user:
  value: 0.000000e+00
w83df6300/cpu-9/cpu-wait:
  value: 0.000000e+00
w83df6300/df-boot/df_complex-free:
  value: 4.369449e+08
w83df6300/df-boot/df_complex-reserved:
  value: 2.684109e+07
w83df6300/df-boot/df_complex-used:
  value: 6.463898e+07
w83df6300/df-boot/df_inodes-free:
  value: 3.271800e+04
w83df6300/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-boot/df_inodes-used:
  value: 5.000000e+01
w83df6300/df-boot/percent_bytes-free:
  value: 8.268816e+01
w83df6300/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w83df6300/df-boot/percent_bytes-used:
  value: 1.223238e+01
w83df6300/df-boot/percent_inodes-free:
  value: 9.984741e+01
w83df6300/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-boot/percent_inodes-used:
  value: 1.525879e-01
w83df6300/df-data1/df_complex-free:
  value: 2.574625e+11
w83df6300/df-data1/df_complex-reserved:
  value: 1.378982e+10
w83df6300/df-data1/df_complex-used:
  value: 2.165924e+08
w83df6300/df-data1/df_inodes-free:
  value: 1.683452e+07
w83df6300/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-data1/df_inodes-used:
  value: 4.400000e+01
w83df6300/df-data1/percent_bytes-free:
  value: 9.484051e+01
w83df6300/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
w83df6300/df-data1/percent_bytes-used:
  value: 7.978535e-02
w83df6300/df-data1/percent_inodes-free:
  value: 9.999974e+01
w83df6300/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-data1/percent_inodes-used:
  value: 2.613671e-04
w83df6300/df-dev-shm/df_complex-free:
  value: 3.375709e+10
w83df6300/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w83df6300/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w83df6300/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
w83df6300/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w83df6300/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w83df6300/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w83df6300/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w83df6300/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
w83df6300/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
w83df6300/df-root/df_complex-free:
  value: 1.022576e+10
w83df6300/df-root/df_complex-reserved:
  value: 6.442435e+08
w83df6300/df-root/df_complex-used:
  value: 1.812701e+09
w83df6300/df-root/df_inodes-free:
  value: 7.415980e+05
w83df6300/df-root/df_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-root/df_inodes-used:
  value: 4.483400e+04
w83df6300/df-root/percent_bytes-free:
  value: 8.062760e+01
w83df6300/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w83df6300/df-root/percent_bytes-used:
  value: 1.429270e+01
w83df6300/df-root/percent_inodes-free:
  value: 9.429906e+01
w83df6300/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-root/percent_inodes-used:
  value: 5.700938e+00
w83df6300/df-var/df_complex-free:
  value: 7.485731e+09
w83df6300/df-var/df_complex-reserved:
  value: 4.294943e+08
w83df6300/df-var/df_complex-used:
  value: 5.398938e+08
w83df6300/df-var/df_inodes-free:
  value: 5.224290e+05
w83df6300/df-var/df_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-var/df_inodes-used:
  value: 1.859000e+03
w83df6300/df-var/percent_bytes-free:
  value: 8.853490e+01
w83df6300/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w83df6300/df-var/percent_bytes-used:
  value: 6.385407e+00
w83df6300/df-var/percent_inodes-free:
  value: 9.964542e+01
w83df6300/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w83df6300/df-var/percent_inodes-used:
  value: 3.545761e-01
w83df6300/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6300/load/load:
  longterm: 0.000000e+00
  midterm: 2.000000e-02
  shortterm: 1.000000e-02
w83df6300/memory/memory-buffered:
  value: 2.686280e+08
w83df6300/memory/memory-cached:
  value: 1.142759e+09
w83df6300/memory/memory-free:
  value: 6.452790e+10
w83df6300/memory/memory-used:
  value: 1.574883e+09
w83df6300/network/if_octets:
  rx: 0.000000e+00
  tx: 1.575799e+03
w83df6300/network/if_packets:
  rx: 0.000000e+00
  tx: 1.199999e+00
w83df6300/network/queue_length:
  value: 0.000000e+00
w83df6300/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w83df6300/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w83df6300/network/total_values-send-accepted:
  value: 3.240001e+01
w83df6300/network/total_values-send-rejected:
  value: 0.000000e+00
w83df6300/swap/swap-cached:
  value: 0.000000e+00
w83df6300/swap/swap-free:
  value: 2.147475e+09
w83df6300/swap/swap-used:
  value: 0.000000e+00
w83df6300/swap/swap_io-in:
  value: 0.000000e+00
w83df6300/swap/swap_io-out:
  value: 0.000000e+00
w83df6300/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 3.800015e+00
w83df6300/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6300/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6300/vmem/vmpage_number-active_anon:
  value: 1.835300e+04
w83df6300/vmem/vmpage_number-active_file:
  value: 1.867190e+05
w83df6300/vmem/vmpage_number-anon_pages:
  value: 1.272000e+04
w83df6300/vmem/vmpage_number-anon_transparent_hugepages:
  value: 1.100000e+01
w83df6300/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w83df6300/vmem/vmpage_number-dirty:
  value: 4.000000e+00
w83df6300/vmem/vmpage_number-file_pages:
  value: 3.445770e+05
w83df6300/vmem/vmpage_number-free_pages:
  value: 1.575388e+07
w83df6300/vmem/vmpage_number-inactive_anon:
  value: 4.900000e+01
w83df6300/vmem/vmpage_number-inactive_file:
  value: 1.578110e+05
w83df6300/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w83df6300/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w83df6300/vmem/vmpage_number-kernel_stack:
  value: 4.460000e+02
w83df6300/vmem/vmpage_number-mapped:
  value: 2.404000e+03
w83df6300/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w83df6300/vmem/vmpage_number-page_table_pages:
  value: 8.250000e+02
w83df6300/vmem/vmpage_number-shmem:
  value: 5.100000e+01
w83df6300/vmem/vmpage_number-slab_reclaimable:
  value: 2.469330e+05
w83df6300/vmem/vmpage_number-slab_unreclaimable:
  value: 1.504900e+04
w83df6300/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w83df6300/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w83df6300/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w83df6300/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w83df6300/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w83df6400/cpu-0/cpu-idle:
  value: 9.979917e+01
w83df6400/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-0/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-0/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-0/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-0/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-0/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-0/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-1/cpu-idle:
  value: 9.989810e+01
w83df6400/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-1/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-1/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-1/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-1/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-1/cpu-user:
  value: 9.999843e-02
w83df6400/cpu-1/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-10/cpu-idle:
  value: 9.999853e+01
w83df6400/cpu-10/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-10/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-10/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-10/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-10/cpu-system:
  value: 9.999853e-02
w83df6400/cpu-10/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-10/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-11/cpu-idle:
  value: 9.989854e+01
w83df6400/cpu-11/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-11/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-11/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-11/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-11/cpu-system:
  value: 9.999853e-02
w83df6400/cpu-11/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-11/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-12/cpu-idle:
  value: 9.989849e+01
w83df6400/cpu-12/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-12/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-12/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-12/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-12/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-12/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-12/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-13/cpu-idle:
  value: 9.989843e+01
w83df6400/cpu-13/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-13/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-13/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-13/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-13/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-13/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-13/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-14/cpu-idle:
  value: 9.999838e+01
w83df6400/cpu-14/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-14/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-14/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-14/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-14/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-14/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-14/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-15/cpu-idle:
  value: 9.989833e+01
w83df6400/cpu-15/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-15/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-15/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-15/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-15/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-15/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-15/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-16/cpu-idle:
  value: 9.999832e+01
w83df6400/cpu-16/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-16/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-16/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-16/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-16/cpu-system:
  value: 9.999834e-02
w83df6400/cpu-16/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-16/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-17/cpu-idle:
  value: 9.989833e+01
w83df6400/cpu-17/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-17/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-17/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-17/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-17/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-17/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-17/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-18/cpu-idle:
  value: 9.999827e+01
w83df6400/cpu-18/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-18/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-18/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-18/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-18/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-18/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-18/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-19/cpu-idle:
  value: 9.999815e+01
w83df6400/cpu-19/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-19/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-19/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-19/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-19/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-19/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-19/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-2/cpu-idle:
  value: 9.979807e+01
w83df6400/cpu-2/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-2/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-2/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-2/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-2/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-2/cpu-user:
  value: 9.999811e-02
w83df6400/cpu-2/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-20/cpu-idle:
  value: 9.999811e+01
w83df6400/cpu-20/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-20/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-20/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-20/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-20/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-20/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-20/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-21/cpu-idle:
  value: 9.999773e+01
w83df6400/cpu-21/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-21/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-21/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-21/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-21/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-21/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-21/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-22/cpu-idle:
  value: 9.999773e+01
w83df6400/cpu-22/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-22/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-22/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-22/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-22/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-22/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-22/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-23/cpu-idle:
  value: 9.989763e+01
w83df6400/cpu-23/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-23/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-23/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-23/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-23/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-23/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-23/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-3/cpu-idle:
  value: 9.999817e+01
w83df6400/cpu-3/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-3/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-3/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-3/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-3/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-3/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-3/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-4/cpu-idle:
  value: 9.989839e+01
w83df6400/cpu-4/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-4/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-4/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-4/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-4/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-4/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-4/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-5/cpu-idle:
  value: 9.999820e+01
w83df6400/cpu-5/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-5/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-5/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-5/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-5/cpu-system:
  value: 9.999815e-02
w83df6400/cpu-5/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-5/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-6/cpu-idle:
  value: 9.989865e+01
w83df6400/cpu-6/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-6/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-6/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-6/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-6/cpu-system:
  value: 0.000000e+00
w83df6400/cpu-6/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-6/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-7/cpu-idle:
  value: 9.999866e+01
w83df6400/cpu-7/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-7/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-7/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-7/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-7/cpu-system:
  value: 9.999866e-02
w83df6400/cpu-7/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-7/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-8/cpu-idle:
  value: 9.999857e+01
w83df6400/cpu-8/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-8/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-8/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-8/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-8/cpu-system:
  value: 9.999860e-02
w83df6400/cpu-8/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-8/cpu-wait:
  value: 0.000000e+00
w83df6400/cpu-9/cpu-idle:
  value: 9.989853e+01
w83df6400/cpu-9/cpu-interrupt:
  value: 0.000000e+00
w83df6400/cpu-9/cpu-nice:
  value: 0.000000e+00
w83df6400/cpu-9/cpu-softirq:
  value: 0.000000e+00
w83df6400/cpu-9/cpu-steal:
  value: 0.000000e+00
w83df6400/cpu-9/cpu-system:
  value: 9.999854e-02
w83df6400/cpu-9/cpu-user:
  value: 0.000000e+00
w83df6400/cpu-9/cpu-wait:
  value: 0.000000e+00
w83df6400/df-boot/df_complex-free:
  value: 4.369449e+08
w83df6400/df-boot/df_complex-reserved:
  value: 2.684109e+07
w83df6400/df-boot/df_complex-used:
  value: 6.463898e+07
w83df6400/df-boot/df_inodes-free:
  value: 3.271800e+04
w83df6400/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-boot/df_inodes-used:
  value: 5.000000e+01
w83df6400/df-boot/percent_bytes-free:
  value: 8.268816e+01
w83df6400/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w83df6400/df-boot/percent_bytes-used:
  value: 1.223238e+01
w83df6400/df-boot/percent_inodes-free:
  value: 9.984741e+01
w83df6400/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-boot/percent_inodes-used:
  value: 1.525879e-01
w83df6400/df-data1/df_complex-free:
  value: 2.574625e+11
w83df6400/df-data1/df_complex-reserved:
  value: 1.378982e+10
w83df6400/df-data1/df_complex-used:
  value: 2.165924e+08
w83df6400/df-data1/df_inodes-free:
  value: 1.683452e+07
w83df6400/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-data1/df_inodes-used:
  value: 4.400000e+01
w83df6400/df-data1/percent_bytes-free:
  value: 9.484051e+01
w83df6400/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
w83df6400/df-data1/percent_bytes-used:
  value: 7.978535e-02
w83df6400/df-data1/percent_inodes-free:
  value: 9.999974e+01
w83df6400/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-data1/percent_inodes-used:
  value: 2.613671e-04
w83df6400/df-dev-shm/df_complex-free:
  value: 3.375709e+10
w83df6400/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w83df6400/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w83df6400/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
w83df6400/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w83df6400/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w83df6400/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w83df6400/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w83df6400/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
w83df6400/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
w83df6400/df-root/df_complex-free:
  value: 1.062062e+10
w83df6400/df-root/df_complex-reserved:
  value: 6.442435e+08
w83df6400/df-root/df_complex-used:
  value: 1.417847e+09
w83df6400/df-root/df_inodes-free:
  value: 7.449690e+05
w83df6400/df-root/df_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-root/df_inodes-used:
  value: 4.146300e+04
w83df6400/df-root/percent_bytes-free:
  value: 8.374093e+01
w83df6400/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w83df6400/df-root/percent_bytes-used:
  value: 1.117937e+01
w83df6400/df-root/percent_inodes-free:
  value: 9.472771e+01
w83df6400/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-root/percent_inodes-used:
  value: 5.272293e+00
w83df6400/df-var/df_complex-free:
  value: 7.567413e+09
w83df6400/df-var/df_complex-reserved:
  value: 4.294943e+08
w83df6400/df-var/df_complex-used:
  value: 4.582113e+08
w83df6400/df-var/df_inodes-free:
  value: 5.227820e+05
w83df6400/df-var/df_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-var/df_inodes-used:
  value: 1.506000e+03
w83df6400/df-var/percent_bytes-free:
  value: 8.950097e+01
w83df6400/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w83df6400/df-var/percent_bytes-used:
  value: 5.419336e+00
w83df6400/df-var/percent_inodes-free:
  value: 9.971275e+01
w83df6400/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w83df6400/df-var/percent_inodes-used:
  value: 2.872467e-01
w83df6400/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6400/load/load:
  longterm: 0.000000e+00
  midterm: 2.000000e-02
  shortterm: 3.000000e-02
w83df6400/memory/memory-buffered:
  value: 2.072453e+08
w83df6400/memory/memory-cached:
  value: 6.651863e+08
w83df6400/memory/memory-free:
  value: 6.511982e+10
w83df6400/memory/memory-used:
  value: 1.521922e+09
w83df6400/network/if_octets:
  rx: 0.000000e+00
  tx: 1.849000e+03
w83df6400/network/if_packets:
  rx: 0.000000e+00
  tx: 1.400000e+00
w83df6400/network/queue_length:
  value: 0.000000e+00
w83df6400/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w83df6400/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w83df6400/network/total_values-send-accepted:
  value: 3.370000e+01
w83df6400/network/total_values-send-rejected:
  value: 0.000000e+00
w83df6400/swap/swap-cached:
  value: 0.000000e+00
w83df6400/swap/swap-free:
  value: 2.147475e+09
w83df6400/swap/swap-used:
  value: 0.000000e+00
w83df6400/swap/swap_io-in:
  value: 0.000000e+00
w83df6400/swap/swap_io-out:
  value: 0.000000e+00
w83df6400/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 4.700028e+00
w83df6400/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6400/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6400/vmem/vmpage_number-active_anon:
  value: 1.704400e+04
w83df6400/vmem/vmpage_number-active_file:
  value: 1.158240e+05
w83df6400/vmem/vmpage_number-anon_pages:
  value: 1.090600e+04
w83df6400/vmem/vmpage_number-anon_transparent_hugepages:
  value: 1.200000e+01
w83df6400/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w83df6400/vmem/vmpage_number-dirty:
  value: 0.000000e+00
w83df6400/vmem/vmpage_number-file_pages:
  value: 2.129960e+05
w83df6400/vmem/vmpage_number-free_pages:
  value: 1.589839e+07
w83df6400/vmem/vmpage_number-inactive_anon:
  value: 4.400000e+01
w83df6400/vmem/vmpage_number-inactive_file:
  value: 9.713000e+04
w83df6400/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w83df6400/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w83df6400/vmem/vmpage_number-kernel_stack:
  value: 4.350000e+02
w83df6400/vmem/vmpage_number-mapped:
  value: 1.621000e+03
w83df6400/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w83df6400/vmem/vmpage_number-page_table_pages:
  value: 4.680000e+02
w83df6400/vmem/vmpage_number-shmem:
  value: 4.600000e+01
w83df6400/vmem/vmpage_number-slab_reclaimable:
  value: 2.361780e+05
w83df6400/vmem/vmpage_number-slab_unreclaimable:
  value: 1.468100e+04
w83df6400/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w83df6400/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w83df6400/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w83df6400/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w83df6400/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w83df6500/cpu-0/cpu-idle:
  value: 8.689957e+01
w83df6500/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-0/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-0/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-0/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-0/cpu-system:
  value: 2.299989e+00
w83df6500/cpu-0/cpu-user:
  value: 1.059998e+01
w83df6500/cpu-0/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-1/cpu-idle:
  value: 9.869419e+01
w83df6500/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-1/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-1/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-1/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-1/cpu-system:
  value: 3.999803e-01
w83df6500/cpu-1/cpu-user:
  value: 6.999674e-01
w83df6500/cpu-1/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-10/cpu-idle:
  value: 9.999000e+01
w83df6500/cpu-10/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-10/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-10/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-10/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-10/cpu-system:
  value: 9.999001e-02
w83df6500/cpu-10/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-10/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-11/cpu-idle:
  value: 9.988985e+01
w83df6500/cpu-11/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-11/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-11/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-11/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-11/cpu-system:
  value: 9.998982e-02
w83df6500/cpu-11/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-11/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-12/cpu-idle:
  value: 9.988970e+01
w83df6500/cpu-12/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-12/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-12/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-12/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-12/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-12/cpu-user:
  value: 9.998982e-02
w83df6500/cpu-12/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-13/cpu-idle:
  value: 9.239045e+01
w83df6500/cpu-13/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-13/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-13/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-13/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-13/cpu-system:
  value: 2.999690e+00
w83df6500/cpu-13/cpu-user:
  value: 4.399545e+00
w83df6500/cpu-13/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-14/cpu-idle:
  value: 9.639002e+01
w83df6500/cpu-14/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-14/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-14/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-14/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-14/cpu-system:
  value: 2.999689e-01
w83df6500/cpu-14/cpu-user:
  value: 3.299658e+00
w83df6500/cpu-14/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-15/cpu-idle:
  value: 9.998962e+01
w83df6500/cpu-15/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-15/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-15/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-15/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-15/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-15/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-15/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-16/cpu-idle:
  value: 9.998964e+01
w83df6500/cpu-16/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-16/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-16/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-16/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-16/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-16/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-16/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-17/cpu-idle:
  value: 9.998943e+01
w83df6500/cpu-17/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-17/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-17/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-17/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-17/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-17/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-17/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-18/cpu-idle:
  value: 9.978947e+01
w83df6500/cpu-18/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-18/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-18/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-18/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-18/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-18/cpu-user:
  value: 9.998947e-02
w83df6500/cpu-18/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-19/cpu-idle:
  value: 9.149020e+01
w83df6500/cpu-19/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-19/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-19/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-19/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-19/cpu-system:
  value: 3.299646e+00
w83df6500/cpu-19/cpu-user:
  value: 5.099453e+00
w83df6500/cpu-19/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-2/cpu-idle:
  value: 9.999229e+01
w83df6500/cpu-2/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-2/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-2/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-2/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-2/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-2/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-2/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-20/cpu-idle:
  value: 9.988926e+01
w83df6500/cpu-20/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-20/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-20/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-20/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-20/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-20/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-20/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-21/cpu-idle:
  value: 9.888932e+01
w83df6500/cpu-21/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-21/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-21/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-21/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-21/cpu-system:
  value: 9.998920e-02
w83df6500/cpu-21/cpu-user:
  value: 7.999136e-01
w83df6500/cpu-21/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-22/cpu-idle:
  value: 9.998916e+01
w83df6500/cpu-22/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-22/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-22/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-22/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-22/cpu-system:
  value: 9.998916e-02
w83df6500/cpu-22/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-22/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-23/cpu-idle:
  value: 9.988925e+01
w83df6500/cpu-23/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-23/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-23/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-23/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-23/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-23/cpu-user:
  value: 9.998923e-02
w83df6500/cpu-23/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-3/cpu-idle:
  value: 9.989136e+01
w83df6500/cpu-3/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-3/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-3/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-3/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-3/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-3/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-3/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-4/cpu-idle:
  value: 9.989094e+01
w83df6500/cpu-4/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-4/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-4/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-4/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-4/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-4/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-4/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-5/cpu-idle:
  value: 9.999060e+01
w83df6500/cpu-5/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-5/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-5/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-5/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-5/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-5/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-5/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-6/cpu-idle:
  value: 9.959022e+01
w83df6500/cpu-6/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-6/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-6/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-6/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-6/cpu-system:
  value: 9.999033e-02
w83df6500/cpu-6/cpu-user:
  value: 2.999711e-01
w83df6500/cpu-6/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-7/cpu-idle:
  value: 9.809048e+01
w83df6500/cpu-7/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-7/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-7/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-7/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-7/cpu-system:
  value: 4.999515e-01
w83df6500/cpu-7/cpu-user:
  value: 1.299874e+00
w83df6500/cpu-7/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-8/cpu-idle:
  value: 9.999017e+01
w83df6500/cpu-8/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-8/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-8/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-8/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-8/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-8/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-8/cpu-wait:
  value: 0.000000e+00
w83df6500/cpu-9/cpu-idle:
  value: 9.989011e+01
w83df6500/cpu-9/cpu-interrupt:
  value: 0.000000e+00
w83df6500/cpu-9/cpu-nice:
  value: 0.000000e+00
w83df6500/cpu-9/cpu-softirq:
  value: 0.000000e+00
w83df6500/cpu-9/cpu-steal:
  value: 0.000000e+00
w83df6500/cpu-9/cpu-system:
  value: 0.000000e+00
w83df6500/cpu-9/cpu-user:
  value: 0.000000e+00
w83df6500/cpu-9/cpu-wait:
  value: 0.000000e+00
w83df6500/df-boot/df_complex-free:
  value: 4.793876e+08
w83df6500/df-boot/df_complex-reserved:
  value: 2.684109e+07
w83df6500/df-boot/df_complex-used:
  value: 2.219622e+07
w83df6500/df-boot/df_inodes-free:
  value: 3.273000e+04
w83df6500/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-boot/df_inodes-used:
  value: 3.800000e+01
w83df6500/df-boot/percent_bytes-free:
  value: 9.072010e+01
w83df6500/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w83df6500/df-boot/percent_bytes-used:
  value: 4.200449e+00
w83df6500/df-boot/percent_inodes-free:
  value: 9.988403e+01
w83df6500/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-boot/percent_inodes-used:
  value: 1.159668e-01
w83df6500/df-data1/df_complex-free:
  value: 2.574625e+11
w83df6500/df-data1/df_complex-reserved:
  value: 1.378982e+10
w83df6500/df-data1/df_complex-used:
  value: 2.165883e+08
w83df6500/df-data1/df_inodes-free:
  value: 1.683452e+07
w83df6500/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-data1/df_inodes-used:
  value: 4.300000e+01
w83df6500/df-data1/percent_bytes-free:
  value: 9.484052e+01
w83df6500/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
w83df6500/df-data1/percent_bytes-used:
  value: 7.978383e-02
w83df6500/df-data1/percent_inodes-free:
  value: 9.999974e+01
w83df6500/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-data1/percent_inodes-used:
  value: 2.554269e-04
w83df6500/df-dev-shm/df_complex-free:
  value: 3.375709e+10
w83df6500/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w83df6500/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w83df6500/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
w83df6500/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w83df6500/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w83df6500/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w83df6500/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w83df6500/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
w83df6500/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
w83df6500/df-root/df_complex-free:
  value: 1.055831e+10
w83df6500/df-root/df_complex-reserved:
  value: 6.442435e+08
w83df6500/df-root/df_complex-used:
  value: 1.480151e+09
w83df6500/df-root/df_inodes-free:
  value: 7.465750e+05
w83df6500/df-root/df_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-root/df_inodes-used:
  value: 3.985700e+04
w83df6500/df-root/percent_bytes-free:
  value: 8.324968e+01
w83df6500/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w83df6500/df-root/percent_bytes-used:
  value: 1.167062e+01
w83df6500/df-root/percent_inodes-free:
  value: 9.493192e+01
w83df6500/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-root/percent_inodes-used:
  value: 5.068080e+00
w83df6500/df-var/df_complex-free:
  value: 7.641543e+09
w83df6500/df-var/df_complex-reserved:
  value: 4.294943e+08
w83df6500/df-var/df_complex-used:
  value: 3.840819e+08
w83df6500/df-var/df_inodes-free:
  value: 5.228880e+05
w83df6500/df-var/df_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-var/df_inodes-used:
  value: 1.400000e+03
w83df6500/df-var/percent_bytes-free:
  value: 9.037771e+01
w83df6500/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w83df6500/df-var/percent_bytes-used:
  value: 4.542596e+00
w83df6500/df-var/percent_inodes-free:
  value: 9.973297e+01
w83df6500/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w83df6500/df-var/percent_inodes-used:
  value: 2.670288e-01
w83df6500/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 7.199968e+00
w83df6500/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 3.850286e+04
w83df6500/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 2.200014e+00
w83df6500/disk-sda/disk_time:
  read: 0.000000e+00
  write: 4.000026e-01
w83df6500/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 2.299968e+00
w83df6500/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 1.146866e+04
w83df6500/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 4.999937e-01
w83df6500/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 1.999972e-01
w83df6500/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 4.899926e+00
w83df6500/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 2.703321e+04
w83df6500/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 1.699975e+00
w83df6500/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 4.999926e-01
w83df6500/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6500/load/load:
  longterm: 2.000000e-02
  midterm: 9.000000e-02
  shortterm: 1.600000e-01
w83df6500/memory/memory-buffered:
  value: 4.858675e+07
w83df6500/memory/memory-cached:
  value: 7.746028e+08
w83df6500/memory/memory-free:
  value: 6.579641e+10
w83df6500/memory/memory-used:
  value: 8.945746e+08
w83df6500/network/if_octets:
  rx: 0.000000e+00
  tx: 1.591300e+03
w83df6500/network/if_packets:
  rx: 0.000000e+00
  tx: 1.199999e+00
w83df6500/network/queue_length:
  value: 0.000000e+00
w83df6500/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w83df6500/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w83df6500/network/total_values-send-accepted:
  value: 3.239995e+01
w83df6500/network/total_values-send-rejected:
  value: 0.000000e+00
w83df6500/swap/swap-cached:
  value: 0.000000e+00
w83df6500/swap/swap-free:
  value: 2.147475e+09
w83df6500/swap/swap-used:
  value: 0.000000e+00
w83df6500/swap/swap_io-in:
  value: 0.000000e+00
w83df6500/swap/swap_io-out:
  value: 0.000000e+00
w83df6500/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 1.747727e+04
w83df6500/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 3.759999e+01
w83df6500/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6500/vmem/vmpage_number-active_anon:
  value: 7.795900e+04
w83df6500/vmem/vmpage_number-active_file:
  value: 8.036800e+04
w83df6500/vmem/vmpage_number-anon_pages:
  value: 2.727800e+04
w83df6500/vmem/vmpage_number-anon_transparent_hugepages:
  value: 1.000000e+02
w83df6500/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w83df6500/vmem/vmpage_number-dirty:
  value: 3.920000e+02
w83df6500/vmem/vmpage_number-file_pages:
  value: 2.009740e+05
w83df6500/vmem/vmpage_number-free_pages:
  value: 1.606358e+07
w83df6500/vmem/vmpage_number-inactive_anon:
  value: 4.600000e+01
w83df6500/vmem/vmpage_number-inactive_file:
  value: 1.205540e+05
w83df6500/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w83df6500/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w83df6500/vmem/vmpage_number-kernel_stack:
  value: 5.970000e+02
w83df6500/vmem/vmpage_number-mapped:
  value: 6.373000e+03
w83df6500/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w83df6500/vmem/vmpage_number-page_table_pages:
  value: 1.091000e+03
w83df6500/vmem/vmpage_number-shmem:
  value: 4.800000e+01
w83df6500/vmem/vmpage_number-slab_reclaimable:
  value: 1.991400e+04
w83df6500/vmem/vmpage_number-slab_unreclaimable:
  value: 1.512500e+04
w83df6500/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w83df6500/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w83df6500/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w83df6500/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w83df6500/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
w83df6600/cpu-0/cpu-idle:
  value: 9.980063e+01
w83df6600/cpu-0/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-0/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-0/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-0/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-0/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-0/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-0/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-1/cpu-idle:
  value: 9.990608e+01
w83df6600/cpu-1/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-1/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-1/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-1/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-1/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-1/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-1/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-10/cpu-idle:
  value: 9.991430e+01
w83df6600/cpu-10/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-10/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-10/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-10/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-10/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-10/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-10/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-11/cpu-idle:
  value: 1.000143e+02
w83df6600/cpu-11/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-11/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-11/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-11/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-11/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-11/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-11/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-12/cpu-idle:
  value: 1.000143e+02
w83df6600/cpu-12/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-12/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-12/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-12/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-12/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-12/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-12/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-13/cpu-idle:
  value: 9.991410e+01
w83df6600/cpu-13/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-13/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-13/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-13/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-13/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-13/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-13/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-14/cpu-idle:
  value: 1.000141e+02
w83df6600/cpu-14/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-14/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-14/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-14/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-14/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-14/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-14/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-15/cpu-idle:
  value: 9.998509e+01
w83df6600/cpu-15/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-15/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-15/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-15/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-15/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-15/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-15/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-16/cpu-idle:
  value: 9.998503e+01
w83df6600/cpu-16/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-16/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-16/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-16/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-16/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-16/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-16/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-17/cpu-idle:
  value: 9.988509e+01
w83df6600/cpu-17/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-17/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-17/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-17/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-17/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-17/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-17/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-18/cpu-idle:
  value: 9.988507e+01
w83df6600/cpu-18/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-18/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-18/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-18/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-18/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-18/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-18/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-19/cpu-idle:
  value: 9.978521e+01
w83df6600/cpu-19/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-19/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-19/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-19/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-19/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-19/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-19/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-2/cpu-idle:
  value: 9.999391e+01
w83df6600/cpu-2/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-2/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-2/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-2/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-2/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-2/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-2/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-20/cpu-idle:
  value: 1.000140e+02
w83df6600/cpu-20/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-20/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-20/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-20/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-20/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-20/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-20/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-21/cpu-idle:
  value: 1.000139e+02
w83df6600/cpu-21/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-21/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-21/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-21/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-21/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-21/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-21/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-22/cpu-idle:
  value: 1.000138e+02
w83df6600/cpu-22/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-22/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-22/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-22/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-22/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-22/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-22/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-23/cpu-idle:
  value: 1.000137e+02
w83df6600/cpu-23/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-23/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-23/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-23/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-23/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-23/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-23/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-3/cpu-idle:
  value: 9.989001e+01
w83df6600/cpu-3/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-3/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-3/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-3/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-3/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-3/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-3/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-4/cpu-idle:
  value: 9.998791e+01
w83df6600/cpu-4/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-4/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-4/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-4/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-4/cpu-system:
  value: 9.998773e-02
w83df6600/cpu-4/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-4/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-5/cpu-idle:
  value: 9.988613e+01
w83df6600/cpu-5/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-5/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-5/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-5/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-5/cpu-system:
  value: 9.998665e-02
w83df6600/cpu-5/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-5/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-6/cpu-idle:
  value: 9.988577e+01
w83df6600/cpu-6/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-6/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-6/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-6/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-6/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-6/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-6/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-7/cpu-idle:
  value: 9.988529e+01
w83df6600/cpu-7/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-7/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-7/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-7/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-7/cpu-system:
  value: 3.999412e-01
w83df6600/cpu-7/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-7/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-8/cpu-idle:
  value: 9.998504e+01
w83df6600/cpu-8/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-8/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-8/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-8/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-8/cpu-system:
  value: 0.000000e+00
w83df6600/cpu-8/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-8/cpu-wait:
  value: 0.000000e+00
w83df6600/cpu-9/cpu-idle:
  value: 9.988501e+01
w83df6600/cpu-9/cpu-interrupt:
  value: 0.000000e+00
w83df6600/cpu-9/cpu-nice:
  value: 0.000000e+00
w83df6600/cpu-9/cpu-softirq:
  value: 0.000000e+00
w83df6600/cpu-9/cpu-steal:
  value: 0.000000e+00
w83df6600/cpu-9/cpu-system:
  value: 9.998501e-02
w83df6600/cpu-9/cpu-user:
  value: 0.000000e+00
w83df6600/cpu-9/cpu-wait:
  value: 0.000000e+00
w83df6600/df-boot/df_complex-free:
  value: 4.793631e+08
w83df6600/df-boot/df_complex-reserved:
  value: 2.684109e+07
w83df6600/df-boot/df_complex-used:
  value: 2.222080e+07
w83df6600/df-boot/df_inodes-free:
  value: 3.273000e+04
w83df6600/df-boot/df_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-boot/df_inodes-used:
  value: 3.800000e+01
w83df6600/df-boot/percent_bytes-free:
  value: 9.071545e+01
w83df6600/df-boot/percent_bytes-reserved:
  value: 5.079451e+00
w83df6600/df-boot/percent_bytes-used:
  value: 4.205100e+00
w83df6600/df-boot/percent_inodes-free:
  value: 9.988403e+01
w83df6600/df-boot/percent_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-boot/percent_inodes-used:
  value: 1.159668e-01
w83df6600/df-data1/df_complex-free:
  value: 2.574625e+11
w83df6600/df-data1/df_complex-reserved:
  value: 1.378982e+10
w83df6600/df-data1/df_complex-used:
  value: 2.165883e+08
w83df6600/df-data1/df_inodes-free:
  value: 1.683452e+07
w83df6600/df-data1/df_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-data1/df_inodes-used:
  value: 4.300000e+01
w83df6600/df-data1/percent_bytes-free:
  value: 9.484052e+01
w83df6600/df-data1/percent_bytes-reserved:
  value: 5.079707e+00
w83df6600/df-data1/percent_bytes-used:
  value: 7.978383e-02
w83df6600/df-data1/percent_inodes-free:
  value: 9.999974e+01
w83df6600/df-data1/percent_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-data1/percent_inodes-used:
  value: 2.554269e-04
w83df6600/df-dev-shm/df_complex-free:
  value: 3.375709e+10
w83df6600/df-dev-shm/df_complex-reserved:
  value: 0.000000e+00
w83df6600/df-dev-shm/df_complex-used:
  value: 0.000000e+00
w83df6600/df-dev-shm/df_inodes-free:
  value: 8.241475e+06
w83df6600/df-dev-shm/df_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-dev-shm/df_inodes-used:
  value: 1.000000e+00
w83df6600/df-dev-shm/percent_bytes-free:
  value: 1.000000e+02
w83df6600/df-dev-shm/percent_bytes-reserved:
  value: 0.000000e+00
w83df6600/df-dev-shm/percent_bytes-used:
  value: 0.000000e+00
w83df6600/df-dev-shm/percent_inodes-free:
  value: 9.999998e+01
w83df6600/df-dev-shm/percent_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-dev-shm/percent_inodes-used:
  value: 1.213375e-05
w83df6600/df-root/df_complex-free:
  value: 1.055849e+10
w83df6600/df-root/df_complex-reserved:
  value: 6.442435e+08
w83df6600/df-root/df_complex-used:
  value: 1.479975e+09
w83df6600/df-root/df_inodes-free:
  value: 7.466050e+05
w83df6600/df-root/df_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-root/df_inodes-used:
  value: 3.982700e+04
w83df6600/df-root/percent_bytes-free:
  value: 8.325107e+01
w83df6600/df-root/percent_bytes-reserved:
  value: 5.079700e+00
w83df6600/df-root/percent_bytes-used:
  value: 1.166924e+01
w83df6600/df-root/percent_inodes-free:
  value: 9.493573e+01
w83df6600/df-root/percent_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-root/percent_inodes-used:
  value: 5.064265e+00
w83df6600/df-var/df_complex-free:
  value: 7.642092e+09
w83df6600/df-var/df_complex-reserved:
  value: 4.294943e+08
w83df6600/df-var/df_complex-used:
  value: 3.835331e+08
w83df6600/df-var/df_inodes-free:
  value: 5.229150e+05
w83df6600/df-var/df_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-var/df_inodes-used:
  value: 1.373000e+03
w83df6600/df-var/percent_bytes-free:
  value: 9.038420e+01
w83df6600/df-var/percent_bytes-reserved:
  value: 5.079695e+00
w83df6600/df-var/percent_bytes-used:
  value: 4.536105e+00
w83df6600/df-var/percent_inodes-free:
  value: 9.973812e+01
w83df6600/df-var/percent_inodes-reserved:
  value: 0.000000e+00
w83df6600/df-var/percent_inodes-used:
  value: 2.618790e-01
w83df6600/disk-sda/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda1/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda1/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda1/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda1/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda2/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda2/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda2/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda2/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda3/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda3/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda3/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda3/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda4/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda4/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda4/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda4/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda5/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda5/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda5/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda5/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda6/disk_merged:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda6/disk_octets:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda6/disk_ops:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/disk-sda6/disk_time:
  read: 0.000000e+00
  write: 0.000000e+00
w83df6600/load/load:
  longterm: 0.000000e+00
  midterm: 0.000000e+00
  shortterm: 0.000000e+00
w83df6600/memory/memory-buffered:
  value: 4.378624e+07
w83df6600/memory/memory-cached:
  value: 7.730299e+08
w83df6600/memory/memory-free:
  value: 6.607691e+10
w83df6600/memory/memory-used:
  value: 6.204498e+08
w83df6600/network/if_octets:
  rx: 0.000000e+00
  tx: 1.043100e+03
w83df6600/network/if_packets:
  rx: 0.000000e+00
  tx: 7.999960e-01
w83df6600/network/queue_length:
  value: 0.000000e+00
w83df6600/network/total_values-dispatch-accepted:
  value: 0.000000e+00
w83df6600/network/total_values-dispatch-rejected:
  value: 0.000000e+00
w83df6600/network/total_values-send-accepted:
  value: 2.019973e+01
w83df6600/network/total_values-send-rejected:
  value: 0.000000e+00
w83df6600/swap/swap-cached:
  value: 0.000000e+00
w83df6600/swap/swap-free:
  value: 2.147475e+09
w83df6600/swap/swap-used:
  value: 0.000000e+00
w83df6600/swap/swap_io-in:
  value: 0.000000e+00
w83df6600/swap/swap_io-out:
  value: 0.000000e+00
w83df6600/vmem/vmpage_faults:
  majflt: 0.000000e+00
  minflt: 8.999036e-01
w83df6600/vmem/vmpage_io-memory:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6600/vmem/vmpage_io-swap:
  in: 0.000000e+00
  out: 0.000000e+00
w83df6600/vmem/vmpage_number-active_anon:
  value: 1.469200e+04
w83df6600/vmem/vmpage_number-active_file:
  value: 6.927600e+04
w83df6600/vmem/vmpage_number-anon_pages:
  value: 1.110500e+04
w83df6600/vmem/vmpage_number-anon_transparent_hugepages:
  value: 7.000000e+00
w83df6600/vmem/vmpage_number-boudfe:
  value: 0.000000e+00
w83df6600/vmem/vmpage_number-dirty:
  value: 9.000000e+00
w83df6600/vmem/vmpage_number-file_pages:
  value: 1.994180e+05
w83df6600/vmem/vmpage_number-free_pages:
  value: 1.613205e+07
w83df6600/vmem/vmpage_number-inactive_anon:
  value: 4.300000e+01
w83df6600/vmem/vmpage_number-inactive_file:
  value: 1.300980e+05
w83df6600/vmem/vmpage_number-isolated_anon:
  value: 0.000000e+00
w83df6600/vmem/vmpage_number-isolated_file:
  value: 0.000000e+00
w83df6600/vmem/vmpage_number-kernel_stack:
  value: 4.330000e+02
w83df6600/vmem/vmpage_number-mapped:
  value: 1.253000e+03
w83df6600/vmem/vmpage_number-mlock:
  value: 0.000000e+00
w83df6600/vmem/vmpage_number-page_table_pages:
  value: 4.520000e+02
w83df6600/vmem/vmpage_number-shmem:
  value: 4.600000e+01
w83df6600/vmem/vmpage_number-slab_reclaimable:
  value: 1.905300e+04
w83df6600/vmem/vmpage_number-slab_unreclaimable:
  value: 1.455200e+04
w83df6600/vmem/vmpage_number-unevictable:
  value: 0.000000e+00
w83df6600/vmem/vmpage_number-unstable:
  value: 0.000000e+00
w83df6600/vmem/vmpage_number-vmscan_write:
  value: 0.000000e+00
w83df6600/vmem/vmpage_number-writeback:
  value: 0.000000e+00
w83df6600/vmem/vmpage_number-writeback_temp:
  value: 0.000000e+00
