#!/usr/bin/env perl

use strict;
use warnings;

use IPC::Open3;

my $timeout = 3; # [s]

for (;;) {
  print "connecting...\n";
  my @command = ('ssh', '-o', 'ConnectTimeout=' . $timeout, 'm@michalrus.com',
                 'socat', '-', 'UNIX-CONNECT:./sock');
  my $pid = open3(*CIN, *COUT, *CERR, @command);

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
        system("notify-send", "-h", "int:transient:1", "-a", $1, $1, ($2 ? $2 : ""));
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
