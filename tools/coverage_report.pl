#!/usr/bin/env perl
# Turns OpenCppCoverage's Cobertura XML into a terminal summary and one
# readable HTML page. The tool's own HTML is a frame-per-file dump; this is
# the view you actually want: worst first, grouped by module.
use strict;
use warnings;

my $xml_path = shift @ARGV or die "usage: coverage_report.pl <coverage.xml> [out.html]\n";
my $html_path = shift @ARGV;

open(my $in, '<', $xml_path) or die "open $xml_path: $!";
local $/;
my $xml = <$in>;
close $in;

my $bs = chr(92);

# One record per source file: covered and total executable lines.
my @files;
while ($xml =~ m{<class\b[^>]*\bfilename="([^"]*)"[^>]*>(.*?)</class>}gs) {
    my ($path, $body) = ($1, $2);
    my ($total, $covered) = (0, 0);
    while ($body =~ m{<line\b[^>]*\bhits="(\d+)"}g) {
        $total++;
        $covered++ if $1 > 0;
    }
    next unless $total;

    my @parts = split(/\Q$bs\E|\//, $path);
    my $base = $parts[-1];
    # The module is the directory under libs/ or apps/, which is how the
    # codebase is actually organised.
    my $module = 'other';
    for my $i (0 .. $#parts - 1) {
        if ($parts[$i] eq 'libs' or $parts[$i] eq 'apps') {
            $module = $parts[$i] . '/' . $parts[$i + 1];
            last;
        }
    }
    push @files, { name => $base, module => $module, total => $total, covered => $covered,
                   rate => $covered / $total };
}

die "no coverage data in $xml_path\n" unless @files;

my ($all_total, $all_covered) = (0, 0);
my %modules;
for my $f (@files) {
    $all_total += $f->{total};
    $all_covered += $f->{covered};
    $modules{$f->{module}}{total} += $f->{total};
    $modules{$f->{module}}{covered} += $f->{covered};
}
my $all_rate = $all_covered / $all_total;

sub pct { return sprintf('%.1f%%', $_[0] * 100); }

# ---- terminal ----
printf "\n  coverage %s   %d of %d lines   %d files\n\n",
    pct($all_rate), $all_covered, $all_total, scalar(@files);

printf "  %-28s %7s %14s\n", 'module', 'covered', 'lines';
printf "  %-28s %7s %14s\n", '-' x 28, '-' x 7, '-' x 14;
for my $m (sort { $modules{$a}{covered} / $modules{$a}{total}
                  <=> $modules{$b}{covered} / $modules{$b}{total} } keys %modules) {
    printf "  %-28s %7s %7d/%-6d\n", $m,
        pct($modules{$m}{covered} / $modules{$m}{total}),
        $modules{$m}{covered}, $modules{$m}{total};
}

my @worst = (sort { $a->{rate} <=> $b->{rate} } @files)[0 .. ($#files < 9 ? $#files : 9)];
print "\n  least covered files\n\n";
for my $f (@worst) {
    printf "  %7s  %-34s %d/%d\n", pct($f->{rate}), $f->{name}, $f->{covered}, $f->{total};
}
print "\n";

exit 0 unless defined $html_path;

# ---- html ----
sub esc {
    my $s = shift;
    $s =~ s/&/&amp;/g; $s =~ s/</&lt;/g; $s =~ s/>/&gt;/g;
    return $s;
}

# Red below 60, amber below 80, green above: the thresholds worth acting on.
sub tone {
    my $r = shift;
    return 'low' if $r < 0.60;
    return 'mid' if $r < 0.80;
    return 'high';
}

my $rows = '';
for my $m (sort { $modules{$a}{covered} / $modules{$a}{total}
                  <=> $modules{$b}{covered} / $modules{$b}{total} } keys %modules) {
    my $r = $modules{$m}{covered} / $modules{$m}{total};
    $rows .= sprintf(
        '<tr class="mod"><td>%s</td><td class="num">%s</td>'
        . '<td class="bar"><span class="%s" style="width:%.1f%%"></span></td>'
        . '<td class="num dim">%d/%d</td></tr>' . "\n",
        esc($m), pct($r), tone($r), $r * 100, $modules{$m}{covered}, $modules{$m}{total});
    for my $f (sort { $a->{rate} <=> $b->{rate} }
               grep { $_->{module} eq $m } @files) {
        $rows .= sprintf(
            '<tr><td class="file">%s</td><td class="num">%s</td>'
            . '<td class="bar"><span class="%s" style="width:%.1f%%"></span></td>'
            . '<td class="num dim">%d/%d</td></tr>' . "\n",
            esc($f->{name}), pct($f->{rate}), tone($f->{rate}), $f->{rate} * 100,
            $f->{covered}, $f->{total});
    }
}

my $html = <<"HTML";
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BeamAI coverage</title>
<style>
  :root {
    --bg: #f7fafb; --panel: #ffffff; --ink: #17313f; --dim: #6b828e;
    --line: #dde6ea; --low: #d4564f; --mid: #d8a12a; --high: #2e9b7a;
    --accent: #167e91;
  }
  \@media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg: #10242c; --panel: #16303a; --ink: #e8f1f4; --dim: #8fa8b3;
      --line: #23424e; --low: #e0736c; --mid: #e3b556; --high: #49b994;
      --accent: #5ccde5;
    }
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 32px 20px 64px; background: var(--bg); color: var(--ink);
    font: 15px/1.5 "Segoe UI", system-ui, -apple-system, sans-serif;
  }
  main { max-width: 860px; margin: 0 auto; }
  h1 { font-size: 15px; font-weight: 600; letter-spacing: .08em;
       text-transform: uppercase; color: var(--dim); margin: 0 0 20px; }
  .headline {
    background: var(--panel); border: 1px solid var(--line); border-radius: 10px;
    padding: 22px 24px; margin-bottom: 24px; display: flex; align-items: baseline;
    gap: 16px; flex-wrap: wrap;
  }
  .big { font-size: 44px; font-weight: 700; color: var(--accent);
         font-variant-numeric: tabular-nums; line-height: 1; }
  .meta { color: var(--dim); font-size: 14px; }
  .wrap { background: var(--panel); border: 1px solid var(--line);
          border-radius: 10px; overflow-x: auto; }
  table { width: 100%; border-collapse: collapse; font-size: 14px; }
  th { text-align: left; font-size: 11px; letter-spacing: .07em; text-transform: uppercase;
       color: var(--dim); font-weight: 600; padding: 12px 14px;
       border-bottom: 1px solid var(--line); }
  td { padding: 7px 14px; border-bottom: 1px solid var(--line); }
  tr:last-child td { border-bottom: none; }
  .mod td { font-weight: 650; background: color-mix(in srgb, var(--accent) 7%, transparent); }
  .file { padding-left: 32px; color: var(--dim); }
  .num { text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }
  .dim { color: var(--dim); font-size: 13px; }
  .bar { width: 42%; min-width: 120px; }
  .bar span { display: block; height: 8px; border-radius: 4px; }
  .bar .low { background: var(--low); } .bar .mid { background: var(--mid); }
  .bar .high { background: var(--high); }
  footer { color: var(--dim); font-size: 13px; margin-top: 18px; }
</style>
</head>
<body>
<main>
  <h1>BeamAI line coverage</h1>
  <div class="headline">
    <span class="big">@{[ pct($all_rate) ]}</span>
    <span class="meta">$all_covered of $all_total executable lines &middot;
      @{[ scalar(@files) ]} files &middot; @{[ scalar(keys %modules) ]} modules</span>
  </div>
  <div class="wrap">
    <table>
      <thead><tr><th>module / file</th><th class="num">covered</th>
        <th>&nbsp;</th><th class="num">lines</th></tr></thead>
      <tbody>
$rows      </tbody>
    </table>
  </div>
  <footer>Modules worst first. Only libs/ and apps/ sources are measured; the
    test binary links the libraries, so apps/ code is absent rather than at zero.</footer>
</main>
</body>
</html>
HTML

open(my $out, '>', $html_path) or die "open $html_path: $!";
print $out $html;
close $out;
print "  summary: $html_path\n\n";
