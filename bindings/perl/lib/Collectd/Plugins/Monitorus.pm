#
# collectd - mon.itor.us collectd plugin
# Copyright (C) 2009  Jeff Green
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; only version 2 of the License is applicable.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
#
# Authors:
#   Jeff Green <jeff at kikisoso.org>
#

package Collectd::Plugins::Monitorus;

use strict;
use warnings;

use Collectd qw( :all );
use LWP;
use threads::shared;

use constant NUM_OF_INTERVALS => 90;

my $intervalcnt :shared;
$intervalcnt=NUM_OF_INTERVALS;
my $prev_value :shared;
$prev_value=0;

plugin_register (TYPE_READ, "monitorus", "monitorus_read");

sub monitorus_read
{
        my $vl = { plugin => 'monitorus', type => 'gauge' };

        # Only retrieve a value occasionally in order to not overload mon.itor.us
        if (++$intervalcnt<NUM_OF_INTERVALS) { # e.g. 180 * 10 secs / 60 seconds/min = 30 minutes
                $vl->{'values'} = [ $prev_value ];
                plugin_dispatch_values ($vl);
                return 1;
        }

        $intervalcnt=0;

        my $site = 'http://mon.itor.us';
        my $username = 'me@example.org';
        my $target = $site.'/user/api/'.$username.'/secretpassword';

        my $ua = LWP::UserAgent->new;
        my $req = HTTP::Request->new(GET => "$target");
        $req->header('Accept' => 'text/html');          #Accept HTML Page

        my $key;
        my $res = $ua->get($target);
        if ($res->is_success) {# Success....all content of page has been received
                $res->content() =~ m/\[CDATA\[(.*)\]\]/;
                $key = $1;
        } else {
                INFO("monitorus: Error in retrieving login page.");
        }

        $target = $site.'/test/api/'.$key.'/testNames';
        my $testid;
        $res = $ua->get($target);
        if ($res->is_success) {# Success....all content of page has been received
                $res->content() =~ m/<test id='(.*)'><!\[CDATA\[sitetest_http\]\]/;
                $testid = $1;
        } else {
                INFO("monitorus: Error in retrieving testNames page.");
        }

        #$target = $site.'/test/api/'.$key.'/testinfo/'.$testid.'/-240';
        #$target = $site.'/test/api/'.$key.'/test/'.$testid.'/27/5/2009/1/3/-240';
        $target = $site.'/test/api/'.$key.'/testsLastValues/1/3';

        my $result;
        my $value;
        $res = $ua->get($target);
        if ($res->is_success) {# Success....all content of page has been received
                $res->content() =~ m/\<\/row\>\s*(\<row\>.*?sitetest_http.*?\<\/row\>)/s;
                $result = $1;
                $result =~ s/\<cell\>.*?CDATA.*?\<\/cell\>//g;
                $result =~ m|\<cell\>([0-9]*)\<\/cell\>|;
                $value = $1;
        } else {
                INFO("monitorus: Error in retrieving testsLastValues page.");
        }

        $prev_value = $value;
        $vl->{'values'} = [ $value ];
        plugin_dispatch_values ($vl);

        return 1;
}

1;
