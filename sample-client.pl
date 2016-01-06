#!/usr/bin/env perl

use strict;
use warnings;
use utf8;

use IPC::Open3;

my $timeout = 4; # [s]
my @command = ('ssh', '-o', 'ConnectTimeout=' . $timeout,
               '-o', 'IdentitiesOnly=yes', '-F', '/dev/null',
               '-i', $ENV{'HOME'} . '/.ssh/tcp-broadcast.pem', 'm@michalrus.com',
               'socat', '-', 'UNIX-CONNECT:.weechat/notify.sock');

binmode(STDOUT, ":utf8"); binmode(STDERR, ":utf8"); binmode(STDIN,  ":utf8");

for (;;) {
  print "connecting...\n";
  my $pid = open3(*CIN, *COUT, *CERR, @command);
  binmode(COUT, ":utf8"); binmode(CERR, ":utf8"); binmode(CIN,  ":utf8");

  my $rin; my $rout;
  my $awaiting_pong = 0; my $connected_printed = 0;
  vec($rin, fileno(COUT), 1) = 1;
  for (;;) {
    my ($found) = select($rout = $rin, undef, undef, $timeout);
    if (($found > 0) && (my $ln = <COUT>)) {
      $awaiting_pong = 0;
      chomp $ln;
      if (!$connected_printed) {
        print "connected\n";
        $connected_printed = 1;
      }
      if ($ln eq 'ping') {
        print CIN "pong\n";
      } elsif ($ln =~ /^broadcast [^ ]+ (.*?)(?:\t(.*))?$/) {
        my $app = $1; my $title = $1; my $body = $2 ? $2 : "";
        $app =~ s/—/-/g; $app =~ s/\s+/ /g; $title =~ s/\s+/ /g; $body =~ s/\\/\\\\/g;
        system("notify-send", "-h", "int:transient:1", "-a", $app, $title, $body);
      }
    } elsif ($found == 0) { # timeout
      if ($awaiting_pong) {
        last;
      } else {
        print CIN "ping\n";
        $awaiting_pong = 1;
      }
    } else {
      last;
    }
  }

  print "failed\n";
  close(COUT); close(CERR); close(CIN);
  kill 'TERM', $pid; waitpid $pid, 0;
  sleep $timeout;
}
