package Collectd::Graph::Data;

use strict;
use warnings;
use utf8;

use Carp (qw(cluck confess));

my %valid_keys =
(
  host            => undef,
  plugin          => undef,
  plugin_instance => undef,
  type            => undef,
  type_instance   => undef,
  style           => qr/^(Area|Line)$/i,
  aggregation     => qr/^(Sum|Stack|None)$/i,
  legend          => undef,
  color           => qr/^#[0-9A-Fa-f]{6}$/
);

sub get
{
  my $obj = shift;
  my $key = shift;

  if (exists ($obj->{$key}))
  {
    return ($obj->{$key});
  }

  cluck ("Invalid key: $key");
  return;
} # get

sub set
{
  my $obj = shift;
  my $key = shift;
  my $value = shift;

  if (!exists ($valid_keys{$key}))
  {
    return;
  }
  elsif (!defined ($valid_keys{$key}))
  {
    $obj->{$key} = $value;
  }
  elsif (ref ($valid_keys{$key}) eq 'Regexp')
  {
    if ($value =~ $valid_keys{$key})
    {
      $obj->{$key} = $value;
    }
  }
  else
  {
    cluck ("Unknown key type");
  }
} # set

sub new
{
  my $pkg = shift;
  my %args = @_;
  my $obj = bless {}, $pkg;

  for (keys %args)
  {
    if (!exists ($valid_keys{$_}))
    {
      cluck ("Ignoring invalid field $_");
      next;
    }
    if (exists $args{$_})
    {
      $obj->{$_} = $args{$_};
      delete $args{$_};
    }
  }

  return ($obj);
} # new

sub _check_field
{
  my $obj = shift;
  my $field = shift;
  my $file_obj = shift;

  my $file_field = $file_obj->get ($field);

  if (defined ($obj->{$field}) && ($obj->{$field} eq '*'))
  {
    # Group files by this key
    if (!$file_field)
    {
      # Unsure about this. This shouldn't happen, really, so I'll return false
      # for now.
      return;
    }
    else
    {
      return (1);
    }
  }
  elsif (defined ($obj->{$field}))
  {
    # All files must have `field' set to this value to apply.
    if ($file_field eq $obj->{$field})
    {
      return (1);
    }
    else
    {
      return;
    }
  }
  else # if (!defined ($obj->{$field}))
  {
    # We don't care.
    return (1);
  }
} # _check_field


sub checkAvailable
{
  my $obj = shift;
  my $files = shift;
  my $files_match_num;

  $files_match_num = 0;
  for (@$files)
  {
    my $file = shift;
    my $file_matches = 1;

    for (qw(host plugin plugin_instance type type_instance))
    {
      if (!$obj->_check_field ($_, $file))
      {
        $file_matches = 0;
        last;
      }
    }

    if ($file_matches)
    {
      $files_match_num++;
    }
  }

  if (!$files_match_num)
  {
    return;
  }
  return ($files_match_num);
}

# vim: set sw=2 sts=2 et fdm=marker :
