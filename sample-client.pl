#!/usr/bin/env perl

use strict;
use warnings;
use utf8;

use IPC::Open3;
use IO::Socket::UNIX;

# Reconnect if we don’t hear from the server in this many seconds (we
# ping-pong constantly, so that’ll only happen when real network
# issues arise). Also, wait this many seconds between reconnecting.
use constant TIMEOUT => 4; # [s]

# If notifications with the same title arrive within this time span,
# they’ll be merged and shown as one. This is here because
# bitlbee-twitter sends multi-line tweets as several messages (with 0
# delay in between).
use constant MERGE_TIMESPAN => 0.1; # [s]

my @command = ('ssh', '-o', 'ConnectTimeout=' . TIMEOUT,
               '-o', 'IdentitiesOnly=yes', '-F', '/dev/null',
               '-i', $ENV{'HOME'} . '/.ssh/tcp-broadcast.pem', 'm@michalrus.com',
               'socat', '-', 'UNIX-CONNECT:.weechat/notify.sock');

my $dbus_socket;
$dbus_socket = $1 if $ENV{'DBUS_SESSION_BUS_ADDRESS'} =~ /^.*?=(.*?),.*?$/;

binmode(STDOUT, ":utf8"); binmode(STDERR, ":utf8"); binmode(STDIN,  ":utf8");

for (;;) {
  my $dbus_ended = 0;

  print "connecting...\n";
  my $pid = open3(*CIN, *COUT, *CERR, @command);
  binmode(COUT, ":utf8"); binmode(CERR, ":utf8"); binmode(CIN,  ":utf8");

  my $rin; my $rout;
  my $awaiting_pong = 0; my $connected_printed = 0;
  my $waiting_for_next_parts = 0;
  my $parts_so_far = "";
  vec($rin, fileno(COUT), 1) = 1;
  for (;;) {
    if ($dbus_socket && !$waiting_for_next_parts) {
      # check if the dbus session is still valid; exit otherwise
      my $socket = IO::Socket::UNIX->new(Type => SOCK_STREAM, Peer => "\0" . $dbus_socket);
      unless ($socket) {
        $dbus_ended = 1;
        last;
      }
      $socket->close();
    }

    my ($found) = select($rout = $rin, undef, undef, ($waiting_for_next_parts ? MERGE_TIMESPAN : TIMEOUT));
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
      } elsif ($waiting_for_next_parts) {
        # timeout when waiting for next parts → cool, display what we’ve got
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
  last if $dbus_ended;
  sleep TIMEOUT;
}
